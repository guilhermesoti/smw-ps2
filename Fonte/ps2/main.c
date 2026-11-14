#include "files.h"
#include "intro.h"
#include "src/ptbr.h"
#include <audsrv.h>
#include <debug.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <libmc.h>
#include <libpad.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timer.h>
#include <unistd.h>

#define DECLARE_DRIVER(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
DECLARE_DRIVER(bdm);
DECLARE_DRIVER(bdmfs_fatfs);
DECLARE_DRIVER(usbd);
DECLARE_DRIVER(usbmass_bd);
DECLARE_DRIVER(sio2man);
DECLARE_DRIVER(padman);
DECLARE_DRIVER(mcman);
DECLARE_DRIVER(mcserv);
DECLARE_DRIVER(audsrv);
DECLARE_DRIVER(freesd);

static unsigned char pad_buf[2][256] __attribute__((aligned(64)));
static uint32_t pixels[SMW_WIDTH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t stereo[SMW_MAX_SAMPLES * 2] __attribute__((aligned(64)));
static GSGLOBAL *gs;
static GSTEXTURE texture;
static bool pad_open[2];
static bool sound;
static int audio_capacity;  /* bytes in audsrv's ring buffer */
static const char *card_drivers = "nenhum";

/* Video options. 240p works on any cable (AV, S-Video, RGB, component and
   most HDMI adapters). 480p needs component or an HDMI adapter; there each
   SNES line becomes two TV lines, and optional scanlines darken every
   second line like a CRT fed with RGB. */
static struct { bool p480, scanlines, smooth; } video = {false, true, false};

static void stop_with(const char *reason) {
  scr_printf("\n%s\n", reason);
  printf("SMW: %s\n", reason);
  for (;;) SleepThread();
}

void Die(const char *error) {
  init_scr();
  stop_with(error);
}

static bool load_driver(void *image, unsigned int size, const char *name) {
  int status = -1;
  int id = SifExecModuleBuffer(image, size, 0, NULL, &status);
  /* A removable resident module is also ready to use (IOP modload >= 1.3). */
  if (id >= 0 && (status == 0 || status == 2)) return true;
  if (SifSearchModuleByName(name) > 0) return true;
  scr_printf("\n%s: id %d, status %d\n", name, id, status);
  return false;
}
#define LOAD(n) load_driver(n##_irx, size_##n##_irx, #n)

static bool start_audio(void) {
  if (SifLoadModule("rom0:LIBSD", 0, NULL) < 0 && !LOAD(freesd)) return false;
  if (!LOAD(audsrv) || audsrv_init() != 0) return false;
  struct audsrv_fmt_t fmt = {.freq = 32000, .bits = 16, .channels = 2};
  if (audsrv_set_format(&fmt) != 0 || audsrv_set_volume(MAX_VOLUME) != 0) return false;
  audio_capacity = audsrv_available();  /* empty ring = its capacity */
  return audio_capacity >= SMW_MAX_SAMPLES * 4 * 2;
}

static void start_drivers(bool from_disc) {
  SifInitRpc(0);
  while (!SifIopReset(NULL, 0)) {}
  while (!SifIopSync()) {}
  SifInitRpc(0);
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();
  /* From a disc image (OPL ISO mode) the USB drive is in use by OPL's own
     driver. Loading a second USB stack fails or disturbs the ISO reads, so
     the progress goes to the memory card instead. */
  if (!from_disc) {
    /* bdmfs_fatfs registers mass: with the stock IOMAN file interface. */
    bool block = LOAD(bdm);
    bool fat = LOAD(bdmfs_fatfs);
    bool usb = LOAD(usbd);
    bool storage = LOAD(usbmass_bd);
    if (!(block && fat && usb && storage))
      scr_printf("USB: tentando os dispositivos ja montados.\n");
  }
  /* Controller and memory card drivers: the console's own BIOS modules
     first, exactly like commercial games load them. OPL's ISO mode is made
     for that setup. The SDK copies built into the ELF are only a fallback
     for consoles whose BIOS lacks the X modules. */
  bool cards;
  if (SifLoadModule("rom0:XSIO2MAN", 0, NULL) >= 0) {
    if (SifLoadModule("rom0:XPADMAN", 0, NULL) < 0 && !LOAD(padman))
      stop_with("Falha ao iniciar o controle.");
    cards = SifLoadModule("rom0:XMCMAN", 0, NULL) >= 0;
    if (!cards) {  /* XMCMAN needs SECRMAN; load it if the reset left it out */
      SifLoadModule("rom0:SECRMAN", 0, NULL);
      cards = SifLoadModule("rom0:XMCMAN", 0, NULL) >= 0;
    }
    cards = cards && SifLoadModule("rom0:XMCSERV", 0, NULL) >= 0;
    card_drivers = "BIOS";
  } else {
    if (!LOAD(sio2man) || !LOAD(padman))
      stop_with("Falha ao iniciar o controle.");
    cards = LOAD(mcman);
    if (!cards) {  /* mcman needs SECRMAN; load it from the BIOS if missing */
      SifLoadModule("rom0:SECRMAN", 0, NULL);
      cards = LOAD(mcman);
    }
    cards = cards && LOAD(mcserv);
    card_drivers = "embutidos";
  }
  if (padInit(0) < 0) stop_with("Falha ao iniciar o controle.");
  for (int i = 0; i < 2; ++i) pad_open[i] = padPortOpen(i, 0, pad_buf[i]) != 0;
  cards = cards && mcInit(MC_TYPE_XMC) >= 0;
  smw_enable_memory_card(cards);

  sound = start_audio();
}

/* The input word uses SNES serial order, bit 0 = B and bit 11 = R. */
static uint16_t read_pad(int port, unsigned *connected, uint16_t *raw) {
  if (!pad_open[port]) return 0;
  int state = padGetState(port, 0);
  if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) return 0;
  struct padButtonStatus pad;
  if (padRead(port, 0, &pad) <= 0) return 0;
  *connected |= 1u << port;
  uint16_t held = (uint16_t)~pad.btns;
  if (raw) *raw = held;
  static const uint16_t mapping[12] = {PAD_CROSS, PAD_SQUARE, PAD_SELECT, PAD_START,
       PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_CIRCLE, PAD_TRIANGLE, PAD_L1, PAD_R1};
  uint16_t result = 0;
  for (int i = 0; i < 12; ++i)
    if (held & mapping[i]) result |= (uint16_t)(1u << i);
  return result;
}

/* Which pads count as plugged in. A pad has to stay in the same state for
   half a second before the game switches players, so a pad that is briefly
   busy (padman state changes) does not hand control to the other one. */
static unsigned stable_pads(unsigned connected) {
  static unsigned stable, last;
  static int same = -1;
  if (same < 0 || connected != last) {
    if (same < 0) stable = connected;
    last = connected;
    same = 0;
  } else if (same < 30 && ++same == 30) {
    stable = connected;
  }
  return stable;
}

/* Progressive 240p output, like the SNES itself. Interlaced 480i makes
   modern TVs comb or tear moving images. 512 pixels = exactly 2x the SNES
   width, so every SNES pixel has the same size on screen. */
static void start_video(void) {
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
              D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);
  gs = gsKit_init_global();
  if (!gs) stop_with("Sem memoria para iniciar o video.");
  gs->Interlace = GS_NONINTERLACED;
  gs->Field = GS_FRAME;
  if (video.p480) {
    gs->Mode = GS_MODE_DTV_480P;
    gs->Width = 640;
    gs->Height = 480;
  } else {
    gs->Mode = GS_MODE_NTSC;
    gs->Width = 512;
    gs->Height = SMW_HEIGHT;
  }
  gs->PSM = GS_PSM_CT32;
  gs->ZBuffering = GS_SETTING_OFF;
  gs->DoubleBuffering = GS_SETTING_ON;
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);
  /* Scanlines: dest + (black - dest) * alpha / 128 */
  gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  texture.Width = SMW_WIDTH;
  texture.Height = SMW_HEIGHT;
  texture.PSM = GS_PSM_CT32;
  texture.Filter = GS_FILTER_NEAREST;
  texture.TBW = SMW_WIDTH / 64;
  texture.Mem = (u32 *)pixels;
  texture.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(texture.Width, texture.Height, texture.PSM),
                                   GSKIT_ALLOC_USERBUFFER);
  if (texture.Vram == GSKIT_ALLOC_ERROR) {
    init_scr();
    stop_with("Sem memoria de video para a textura.");
  }
}

/* Small on-screen message with a code if a save fails (see LEIA-ME). */
static void draw_text(unsigned x, unsigned y, const char *msg) {
  static const char letters[] = "AELORSV0123456789-MTBUI";
  static const unsigned char glyph[][5] = {
    {2,5,7,5,5},{7,4,6,4,7},{4,4,4,4,7},{7,5,5,5,7},{6,5,6,5,5},{7,4,7,1,7},{5,5,5,5,2},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,7,0,0},{5,7,7,5,5},
    {7,2,2,2,2},{6,5,6,5,6},{5,5,5,5,7},{7,2,2,2,7}
  };
  unsigned width = (unsigned)strlen(msg) * 4 + 2;
  if (x + width > SMW_WIDTH) width = SMW_WIDTH - x;
  for (unsigned row = 0; row < 7; ++row)
    for (unsigned col = 0; col < width; ++col)
      pixels[(y + row) * SMW_WIDTH + x + col] = 0x00000070;
  for (unsigned i = 0; msg[i] && x + 1 + i*4 + 3 <= SMW_WIDTH; ++i) {
    const char *at = msg[i] == ' ' ? NULL : strchr(letters, msg[i]);
    if (!at) continue;  /* unknown characters are left blank */
    const unsigned char *g = glyph[at - letters];
    for (unsigned row = 0; row < 5; ++row)
      for (unsigned col = 0; col < 3; ++col)
        if (g[row] & (4 >> col)) pixels[(y + 1 + row) * SMW_WIDTH + x + 1 + i*4 + col] = 0x00ffffff;
  }
}

static void present(void) {
  if (smw_save_error) {
    char msg[64];
    snprintf(msg, sizeof(msg), "ERRO AO SALVAR %s", smw_save_error_text());
    draw_text(8, 8, msg);
  }
  SyncDCache(pixels, pixels + SMW_WIDTH * SMW_HEIGHT);
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0, 0));
  gsKit_texture_send_inline(gs, texture.Mem, texture.Width, texture.Height,
                            texture.Vram, texture.PSM, texture.TBW, GS_CLUT_NONE);
  texture.Filter = video.smooth ? GS_FILTER_LINEAR : GS_FILTER_NEAREST;
  /* 480p: 224 SNES lines -> 448 TV lines, centred in the 480 visible. */
  float top = video.p480 ? 16.0f : 0.0f;
  float height = video.p480 ? 2.0f * SMW_HEIGHT : (float)gs->Height;
  gsKit_prim_sprite_texture(gs, &texture, 0, top, 0, 0, gs->Width, top + height,
                            SMW_WIDTH, SMW_HEIGHT, 1, GS_SETREG_RGBAQ(128,128,128,128,0));
  if (video.p480 && video.scanlines) {
    gs->PrimAlphaEnable = GS_SETTING_ON;
    for (int line = 0; line < SMW_HEIGHT; ++line)
      gsKit_prim_sprite(gs, 0, top + 2 * line + 1, gs->Width, top + 2 * line + 2, 2,
                        GS_SETREG_RGBAQ(0, 0, 0, 0x50, 0));
    gs->PrimAlphaEnable = GS_SETTING_OFF;
  }
  gsKit_queue_exec(gs);
  gsKit_finish();
  gsKit_sync_flip(gs);
}

/* The TV refresh drives the game. Audio follows it: each frame asks the
   engine for slightly more or fewer than 534 samples to keep audsrv's
   buffer about three frames full, and never waits for audio (waiting on
   two clocks is what caused periodic hitches). */
static int samples_for_next_frame(void) {
  if (!sound) return SMW_SAMPLES;
  int target = 3 * 535 * 4;
  if (target > audio_capacity / 2) target = audio_capacity / 2;
  int queued = audio_capacity - audsrv_available();
  int n = 535 + (target - queued) / 1024;
  if (n < SMW_SAMPLES - 2) n = SMW_SAMPLES - 2;
  if (n > SMW_MAX_SAMPLES - 2) n = SMW_MAX_SAMPLES - 2;
  return n;
}

/* L2+R2 + Square: scanlines on/off (480p). L2+R2 + Circle: smoothing. While
   L2+R2 are held the pad sends nothing to the game. */
static uint16_t video_hotkeys(uint16_t game, uint16_t raw, uint16_t *previous) {
  uint16_t pressed = raw & ~*previous;
  *previous = raw;
  if ((raw & (PAD_L2 | PAD_R2)) != (PAD_L2 | PAD_R2)) return game;
  if (pressed & PAD_SQUARE) video.scanlines = !video.scanlines;
  if (pressed & PAD_CIRCLE) video.smooth = !video.smooth;
  return 0;
}

static void step_game(bool draw) {
  static uint16_t previous[2];
  unsigned connected = 0;
  uint16_t raw1 = 0, raw2 = 0;
  uint16_t p1 = read_pad(0, &connected, &raw1), p2 = read_pad(1, &connected, &raw2);
  p1 = video_hotkeys(p1, raw1, &previous[0]);
  p2 = video_hotkeys(p2, raw2, &previous[1]);
  int samples = samples_for_next_frame();
  smw_frame(p1, p2, stable_pads(connected), stereo, samples, draw);
  if (sound) {
    int bytes = samples * 4;
    if (audsrv_available() >= bytes) audsrv_play_audio((const char *)stereo, bytes);
  }
}

/* EE Count ticks between two vertical blanks, measured on this console. */
static uint32_t measure_field(void) {
  gsKit_vsync_wait();
  uint32_t start = cpu_ticks();
  for (int i = 0; i < 16; ++i) gsKit_vsync_wait();
  uint32_t field = (cpu_ticks() - start) / 16;
  /* 294.912 MHz / 59.83 Hz (NTSC 240p); used if the measure looks wrong. */
  if (field < 4000000 || field > 6500000) field = 4929000;
  return field;
}

int main(int argc, char **argv) {
  char folder[SMW_PATH] = "", actual[SMW_PATH] = "", report[256];
  if (argc && argv && argv[0]) smw_boot_folder(argv[0], folder);
  bool from_disc = smw_path_is_disc(folder);
  init_scr();
  scr_printf("RETROGAMES  |  SUPER MARIO WORLD  |  PS2 v0.7.3\n\n");
  scr_printf(from_disc ? "Iniciando controles e Memory Card...\n"
                       : "Iniciando USB, controles e Memory Card...\n");
  start_drivers(from_disc);
  if (!sound) scr_printf("Audio indisponivel. Continuando.\n");
  scr_printf("Procurando o ZIP ou cartucho americano...\n");
  uint8_t *rom = NULL;
  uint32_t crc = 0;
  RomResult result = ROM_MISSING;
  for (int i = 0; i < 25 && result == ROM_MISSING; ++i) {
    result = smw_find_rom(folder, &rom, actual, &crc);
    if (result == ROM_MISSING) usleep(200000);
  }
  if (result == ROM_MISSING)
    stop_with("Coloque Super Mario World (USA).zip ou SMW.SFC\nem APPS/SMW no pendrive FAT32 e abra o ELF de novo.");
  if (result != ROM_GOOD) {
    scr_printf("%s\n", actual);
    if (result == ROM_BAD_ZIP) stop_with("ZIP invalido ou sem arquivo .sfc/.smc.");
    if (result == ROM_BAD_SIZE) stop_with("Tamanho incorreto. Use o jogo USA de SNES, 512 KiB,\ncom ou sem cabecalho de copiador de 512 bytes.");
    if (result == ROM_BAD_CRC) {
      scr_printf("CRC32 lido: %08lX  |  Esperado: B19ED489\n", (unsigned long)crc);
      stop_with("Esta versao precisa do cartucho USA original.");
    }
    stop_with(result == ROM_NO_MEMORY ? "Memoria insuficiente." : "Erro de leitura da midia.");
  }
  scr_printf("ROM USA verificada.\n\n");
  smw_start(rom, pixels);
  free(rom);
  smw_storage_report(report, sizeof(report));
  scr_printf("Drivers de controle e Memory Card: %s\n%sProgresso: %s\n",
             card_drivers, report, smw_save_location());
  /* Three seconds to read the above and pick the video mode. */
  scr_printf("\nVIDEO: aperte TRIANGULO para 480p (adaptador HDMI ou componente)\n"
             "       ou CIRCULO para 240p (cabo AV). Sem apertar: 240p.\n"
             "IDIOMA: portugues. Segure SELECT para jogar em ingles.\n");
  for (int i = 0; i < 60; ++i) {
    unsigned connected = 0;
    uint16_t a = read_pad(0, &connected, NULL) | read_pad(1, &connected, NULL);
    if (a & (1u << 2)) g_ptbr = false;                  /* Select */
    if (a & (1u << 9)) { video.p480 = true; break; }   /* Triangle = SNES X */
    if (a & (1u << 8)) break;                           /* Circle = SNES A */
    usleep(50000);
  }
  scr_printf("Video: %s | Idioma: %s\n", video.p480 ? "480p (sem imagem? reinicie sem apertar nada)" : "240p",
             g_ptbr ? "portugues" : "ingles");
  sleep(1);

  start_video();
  smw_intro(gs, video.p480, sound, "0.7.3");
  const uint32_t field = measure_field();
  uint32_t last = cpu_ticks();
  int pending = 0, debt = 0;
  for (;;) {
    /* If the previous image took more than one TV refresh, catch the game
       up without drawing (at most 2 frames), so speed and music stay right. */
    for (int i = 0; i < pending; ++i) step_game(false);
    step_game(true);
    present();
    uint32_t now = cpu_ticks();
    int fields = (int)((now - last + field / 2) / field);
    last = now;
    debt += fields - (1 + pending);
    if (debt < 0) debt = 0;
    if (debt > 6) debt = 0;  /* long pause (saving): resume, don't fast-forward */
    pending = debt > 2 ? 2 : debt;
  }
}
