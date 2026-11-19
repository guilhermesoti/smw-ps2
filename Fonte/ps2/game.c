#include "game.h"
#include "files.h"
#include "registro.h"
#include "src/common_cpu_infra.h"
#include "src/smw_rtl.h"
#include "src/smw_spc_player.h"
#include "src/snes/snes.h"
#include "src/variables.h"
#include "src/ptbr.h"
#include "src/largo.h"

Snes *g_snes;
SpcPlayer *g_spc_player;
bool g_use_my_apu_code = true;
bool g_new_ppu = true;
bool g_fail;
unsigned smw_connected_pads;
bool smw_save_error;

void SmwRunOneFrameOfGame(void);
void SmwDrawPpuFrame(bool draw_pixels);

/* Logic, audio generation and rendering run on the same EE thread. */
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

/* Each save goes to the log; the file itself is written after the first
   save (and after the next ones until a write works) and after a failed
   one: writing it stops the game for a moment. */
void RtlWriteSram(void) {
  static unsigned saves;
  static bool pending = true;
  unsigned start = smw_log_ms();
  smw_save_error = !smw_save_progress(g_sram);
  unsigned ms = smw_log_ms() - start;
  ++saves;
  if (smw_save_error)
    smw_log("Save %u: ERRO AO SALVAR %s (%u ms)", saves, smw_save_error_text(), ms);
  else
    smw_log("Save %u: ok, %s (%u ms)", saves, smw_save_location(), ms);
  if (pending || smw_save_error) pending = !smw_log_grava();
}

void smw_start(uint8_t *rom, uint32_t *pixels) {
  PtbrInit();
  g_snes = snes_init(g_ram);
  cart_load(g_snes->cart, 1, rom, SMW_ROM_BYTES, SMW_SRAM_BYTES);
  snes_reset(g_snes, true);
  g_rom = g_snes->cart->rom;
  g_sram = g_snes->cart->ram;
  g_sram_size = g_snes->cart->ramSize;
  game_id = kGameID_SMW;
  g_spc_player = SmwSpcPlayer_Create();
  g_spc_player->initialize(g_spc_player);
  PpuBeginDrawing(g_snes->ppu, (uint8_t *)pixels, SMW_WIDTH * 4, 0);
  smw_load_progress(g_sram);
}

void smw_set_widescreen(bool on, uint32_t *pixels, unsigned pitch) {
  g_largo_max = on ? SMW_LARGO_EXTRA : 0;
  g_largo = 0;
  Ppu *ppu = g_snes->ppu;
  ppu->extraLeftRight = g_largo_max;
  ppu->extraLeftCur = ppu->extraRightCur = ppu->largoCena = 0;
  PpuBeginDrawing(ppu, (uint8_t *)pixels, (on ? pitch : SMW_WIDTH) * 4, 0);
}

static uint16_t filter_dpad(uint16_t buttons) {
  if ((buttons & 0x30) == 0x30) buttons &= ~0x30;
  if ((buttons & 0xc0) == 0xc0) buttons &= ~0xc0;
  return buttons;
}

void smw_frame(uint16_t p1, uint16_t p2, unsigned pads, int16_t *stereo, int samples,
               bool draw_pixels) {
  smw_connected_pads = pads;
  /* Use a single connected pad for both players when they share it. */
  io_controllers_plugged_in = pads == 3 ? 0x82 : (pads == 2 ? 1 : 0);
  g_snes->input1->currentState = filter_dpad(p1);
  g_snes->input2->currentState = filter_dpad(p2);
  g_ram[kSmwRam_APUI02] = g_spc_player->port_to_snes[2];
  g_snes->runningWhichVersion = 2;
  LargoAtualiza();
  SmwRunOneFrameOfGame();
  g_snes->runningWhichVersion = 0;
  ++snes_frame_counter;
  RtlPushApuState();
  LargoAtualiza();
  g_snes->ppu->largoCena = g_largo;
  LargoColunas(&g_snes->ppu->extraLeftCur, &g_snes->ppu->extraRightCur);
  SmwDrawPpuFrame(draw_pixels);
  if (samples < 1 || samples > SMW_MAX_SAMPLES) samples = SMW_SAMPLES;
  RtlRenderAudio(stereo, samples, 2);
}
