#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "smw_spc_player.h"
#include "snes/snes.h"

uint8 g_ram[0x20000];
uint8 *g_sram;
int g_sram_size;
const uint8 *g_rom;
bool g_is_uploading_apu;
bool g_did_finish_level_hook;
uint8 game_id;
bool g_playback_mode;

void MemCpy(void *dst, const void *src, int size) {
  memcpy(dst, src, size);
}

void mov24(struct LongPtr *a, uint32 d) {
  a->addr = d & 0xffff;
  a->bank = d >> 16;
}

uint32 Load24(const LongPtr *src) {
  return src->addr | ((uint32)src->bank << 16);
}

bool Unreachable(void) {
  printf("Unreachable!\n");
  assert(0);
  g_ram[0x1ffff] = 1;
  return false;
}

uint8 *RomPtr(uint32_t addr) {
  if (!(addr & 0x8000)) {
    printf("RomPtr - Invalid access 0x%x!\n", addr);
    if (!g_fail) {
      g_fail = true;
    }
  }
  return (uint8 *)&g_rom[(((addr >> 16) << 15) | (addr & 0x7fff)) & 0x7ffff];
}

void WriteReg(uint16 reg, uint8 value) {
  snes_write(g_snes, reg, value);
}

uint16 Mult8x8(uint8 a, uint8 b) {
  return a * b;
}

uint16 SnesDivide(uint16 a, uint8 b) {
  return (b == 0) ? 0xffff : a / b;
}

uint16 SnesModulus(uint16 a, uint8 b) {
  return (b == 0) ? a : a % b;
}

OamEnt *get_OamEnt(OamEnt *base, uint16 off) {
  return (OamEnt *)((uint8 *)base + off);
}

PointU16 *get_PointU16(PointU16 *base, uint8 off) {
  return (PointU16 *)((uint8 *)base + off);
}


uint8 ReadReg(uint16 reg) {
  return snes_read(g_snes, reg);
}

uint16 ReadRegWord(uint16 reg) {
  uint16_t rv = ReadReg(reg);
  rv |= ReadReg(reg + 1) << 8;
  return rv;
}

void WriteRegWord(uint16 reg, uint16 value) {
  WriteReg(reg, (uint8)value);
  WriteReg(reg + 1, value >> 8);
}

uint8 *IndirPtr(LongPtr *ptr, uint16 offs) {
  uint32 a = Load24(ptr) + offs;
  if ((a >> 16) >= 0x7e && (a >> 16) <= 0x7f || (a & 0xffff) < 0x2000) {
    return &g_ram[a & 0x1ffff];
  } else {
    return RomPtr(a);
  }
}

void IndirWriteWord(LongPtr *ptr, uint16 offs, uint16 value) {
  uint8 *p = IndirPtr(ptr, offs);
  WORD(*p) = value;
}

void IndirWriteByte(LongPtr *ptr, uint16 offs, uint8 value) {
  uint8 *p = IndirPtr(ptr, offs);
  p[0] = value;
}

void SetHiLo(uint8 *hi, uint8 *lo, uint16 v) {
  *hi = v >> 8;
  *lo = v;
}

void AddHiLo(uint8 *hi, uint8 *lo, uint16 v) {
  SetHiLo(hi, lo, PAIR16(*hi, *lo) + v);
}

// Maintain a queue cause the snes and audio callback are not in sync.
// If an entry is 255, it means unset.
typedef struct ApuWriteEnt {
  uint8 ports[4];
} ApuWriteEnt;

enum {
  kApuMaxQueueSize = 16,
};

static struct ApuWriteEnt g_apu_write_ents[kApuMaxQueueSize], g_apu_write;
static uint8 g_apu_write_ent_pos, g_apu_queue_size, g_apu_time_since_empty;

void RtlSetUploadingApu(bool uploading) {
  RtlApuLock();
  if (g_is_uploading_apu != uploading && !g_use_my_apu_code) {

    if (!uploading) {
      g_snes->apuCatchupCycles = 10000;
      snes_catchupApu(g_snes);
    } else {
      g_apu_queue_size = 0;
    }
    g_is_uploading_apu = uploading;
  }
  RtlApuUnlock();
}

void RtlApuWrite(uint16 adr, uint8 val) {
  assert(adr >= APUI00 && adr <= APUI03);

  if (g_is_uploading_apu) {
    //    g_snes->apuCatchupCycles = 32;
    snes_catchupApu(g_snes); // catch up the apu before writing
    g_snes->apu->inPorts[adr & 0x3] = val;
    return;
  }

  if (g_snes->runningWhichVersion == (g_use_my_apu_code ? 2 : 1)) {
    g_apu_write.ports[adr & 0x3] = val;  // mine
  }
}

static bool IsFrameEmpty(ApuWriteEnt *w) {
  return (w->ports[0] == 0) && (w->ports[1] == 0) && (w->ports[2] == 0) && (w->ports[3] == 0);
}

void RtlPushApuState(void) {
  RtlApuLock();
  if (!g_is_uploading_apu) {
    // Strive for the queue to be empty.
    if (g_apu_queue_size == 0) {
      g_apu_time_since_empty = 0;
    } else {
      if (g_apu_time_since_empty >= 32 && IsFrameEmpty(&g_apu_write)) {
        g_apu_time_since_empty -= 4;
        RtlApuUnlock();
        return;
      }
      g_apu_time_since_empty++;
    }
    // Merge the two oldest to make space
    ApuWriteEnt *w0 = &g_apu_write_ents[g_apu_write_ent_pos++ & (kApuMaxQueueSize - 1)];
    if (g_apu_queue_size == kApuMaxQueueSize) {
      ApuWriteEnt *w1 = &g_apu_write_ents[g_apu_write_ent_pos & (kApuMaxQueueSize - 1)];
      for (int i = 0; i < 4; i++)
        if (w1->ports[i] == 0)
          w1->ports[i] = w0->ports[i];
    } else {
      g_apu_queue_size++;
    }
    *w0 = g_apu_write;
  } else {
    g_apu_queue_size = 0;
  }
  RtlApuUnlock();
}

static void RtlPopApuState_Locked(void) {
  uint8 *input_ports = g_use_my_apu_code ? g_spc_player->input_ports : g_snes->apu->inPorts;
  if (g_apu_queue_size != 0) {
    ApuWriteEnt *w = &g_apu_write_ents[(g_apu_write_ent_pos - g_apu_queue_size--) & (kApuMaxQueueSize - 1)];
    for (int i = 0; i != 4; i++) {
      input_ports[i] = w->ports[i];
    }
  }
}

static void RtlResetApuQueue_Locked(void) {
  g_apu_write_ent_pos = g_apu_time_since_empty = g_apu_queue_size = 0;
  uint8 *input_ports = g_use_my_apu_code ? g_spc_player->input_ports : g_snes->apu->inPorts;
  memcpy(g_apu_write.ports, input_ports, sizeof(g_apu_write.ports));
}

void RtlApuUpload(const uint8 *p) {
  RtlApuLock();
  g_spc_player->upload(g_spc_player, p);
  RtlResetApuQueue_Locked();
  RtlApuUnlock();
}

void RtlApuReset() {
  RtlApuLock();
  g_spc_player->initialize(g_spc_player);
  apu_reset(g_snes->apu);
  RtlResetApuQueue_Locked();
  RtlApuUnlock();
}

static uint8 RtlApuReadReg(int reg) {
  if (g_use_my_apu_code)
    return g_spc_player->port_to_snes[reg];
  return g_snes->apu->outPorts[reg];
}

void RtlRenderAudio(ram_s16 *audio_buffer, int samples, int channels) {
  assert(channels == 2);
  RtlApuLock();

  RtlPopApuState_Locked();

  if (!g_use_my_apu_code) {
    if (!g_is_uploading_apu) {
      while (g_snes->apu->dsp->sampleOffset < 534)
        apu_cycle(g_snes->apu);
      dsp_getSamples(g_snes->apu->dsp, audio_buffer, samples);
    }
  } else {
    g_spc_player->gen_samples(g_spc_player);
    dsp_getSamples(g_spc_player->dsp, audio_buffer, samples);
  }

  RtlApuUnlock();
}

void SmwCopyToVram(uint16 vram_addr, const uint8 *src, int n) {
  for (size_t i = 0; i < (n >> 1); i++)
    g_snes->ppu->vram[vram_addr + i] = WORD(src[i * 2]);
}

void SmwCopyToVramPitch32(uint16 vram_addr, const uint8 *src, int n) {
  for (size_t i = 0; i < (n >> 1); i++)
    g_snes->ppu->vram[vram_addr + i * 32] = WORD(src[i * 2]);
}

void SmwCopyToVramLow(uint16 vram_addr, const uint8 *src, int n) {
  for (size_t i = 0; i < n; i++)
    g_snes->ppu->vram[vram_addr + i] = (g_snes->ppu->vram[vram_addr + i] & 0xff00) | src[i];
}

void RtlUpdatePalette(const ram_u16 *src, int dst, int n) {
  g_snes->ppu->paletteValid = false;  /* a PPU guarda as cores já convertidas */
  for(int i = 0; i < n; i++)
    g_snes->ppu->cgram[dst + i] = src[i];
}

void SmwClearVram(uint16 vram_addr, uint16 value, int n) {
  for (int i = 0; i < n; i++)
    g_snes->ppu->vram[vram_addr + i] = value;
}

ram_u16 *RtlGetVramAddr() {
  return g_snes->ppu->vram;
}

void RtlPpuWrite(uint16 addr, uint8 value) {
  assert((addr & 0xff00) == 0x2100);
  ppu_write(g_snes->ppu, addr, value);
}

void RtlPpuWriteTwice(uint16 addr, uint16 value) {
  RtlPpuWrite(addr, value);
  RtlPpuWrite(addr, value >> 8);
}

void RtlHdmaSetup(uint8 which, uint8 transfer_unit, uint8 reg, uint32 addr, uint8 indirect_bank) {
  Dma *dma = g_snes->dma;
  dma_write(dma, DMAP0 + which * 16, transfer_unit);
  dma_write(dma, BBAD0 + which * 16, reg);
  dma_write(dma, A1T0L + which * 16, addr);
  dma_write(dma, A1T0H + which * 16, addr >> 8);
  dma_write(dma, A1B0 + which * 16, addr >> 16);
  dma_write(dma, DAS00 + which * 16, indirect_bank);
}

void RtlEnableVirq(int line) {
  g_snes->vIrqEnabled = line >= 0;
  if (line >= 0)
    g_snes->vTimer = line;
}

static const uint8 kSetupHDMAWindowingEffects_DATA_00927C[] = { 0xF0,0xA0,   4,0xF0,0x80,   5,   0 };
static const uint8 *SimpleHdma_GetPtr(uint32 p) {
  if (game_id == kGameID_SMW) {
    switch (p) {
    case 0x927c: return kSetupHDMAWindowingEffects_DATA_00927C;
    }
    if (p < 0x2000)
      return g_ram + p;
  }
  printf("SimpleHdma_GetPtr: bad addr 0x%x\n", p);
  return NULL;
}

void SimpleHdma_Init(SimpleHdma *c, DmaChannel *dc) {
  if (!dc->hdmaActive) {
    c->table = 0;
    return;
  }
  c->table = SimpleHdma_GetPtr(dc->aAdr | dc->aBank << 16);
  c->rep_count = 0;
  c->mode = dc->mode | dc->indirect << 6;
  c->ppu_addr = dc->bAdr;
  c->indir_bank = dc->indBank;
}

void SimpleHdma_DoLine(SimpleHdma *c) {
  static const uint8 bAdrOffsets[8][4] = {
    {0, 0, 0, 0},
    {0, 1, 0, 1},
    {0, 0, 0, 0},
    {0, 0, 1, 1},
    {0, 1, 2, 3},
    {0, 1, 0, 1},
    {0, 0, 0, 0},
    {0, 0, 1, 1}
  };
  static const uint8 transferLength[8] = {
    1, 2, 2, 4, 4, 4, 2, 4
  };

  if (c->table == NULL)
    return;
  bool do_transfer = false;
  if ((c->rep_count & 0x7f) == 0) {
    c->rep_count = *c->table++;
    if (c->rep_count == 0) {
      c->table = NULL;
      return;
    }
    if(c->mode & 0x40) {
      c->indir_ptr = SimpleHdma_GetPtr(c->indir_bank << 16 | c->table[0] | c->table[1] * 256);
      c->table += 2;
    }
    do_transfer = true;
  }
  if(do_transfer || c->rep_count & 0x80) {
    for(int j = 0, j_end = transferLength[c->mode & 7]; j < j_end; j++) {
      uint8 v = c->mode & 0x40 ? *c->indir_ptr++ : *c->table++;
      RtlPpuWrite(0x2100 + c->ppu_addr + bAdrOffsets[c->mode & 7][j], v);
    }
  }
  c->rep_count--;
}


void LoadStripeImage_UploadToVRAM(const uint8 *pp) {  // 00871e
  while (1) {
    if ((*pp & 0x80) != 0)
      break;
    uint16 vram_addr = pp[0] << 8 | pp[1];
    uint8 vmain = __CFSHL__(pp[2], 1);
    uint8 fixed_addr = (uint8)(pp[2] & 0x40) >> 3;
    uint16 num = (swap16(WORD(pp[2])) & 0x3FFF) + 1;
    ram_u16 *dst = g_snes->ppu->vram + vram_addr;
    pp += 4;
    
    if (fixed_addr) {
      uint16 src_data = WORD(*pp);
      int ctr = (num + 1) >> 1;
      if (vmain) {
        for (int i = 0; i < ctr; i++)
          dst[i * 32] = src_data;
      } else {
        // uhm...?
        uint8 *dst_b = (uint8 *)dst;
        for (int i = 0; i < num; i++)
          dst_b[i + ((i & 1) << 1)] = src_data;
        for (int i = 0; i < num; i += 2)
          dst_b[i + 1] = src_data >> 8;
      }
      pp += 2;
    } else {
      ram_u16 *src = (ram_u16 *)pp;
      if (vmain) {
        for (int i = 0; i < (num >> 1); i++)
          dst[i * 32] = src[i];
      } else {
        for (int i = 0; i < (num >> 1); i++)
          dst[i] = src[i];
      }
      pp += num;
    }
  }
}
