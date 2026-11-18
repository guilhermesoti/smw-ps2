#include "game.h"
#include "src/common_cpu_infra.h"
#include "src/smw_rtl.h"
#include "src/smw_spc_player.h"
#include "src/snes/snes.h"
#include "src/variables.h"
#include "src/ptbr.h"

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

void RtlWriteSram(void) {
  smw_save_error = !smw_save_progress(g_sram);
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
  SmwRunOneFrameOfGame();
  g_snes->runningWhichVersion = 0;
  ++snes_frame_counter;
  RtlPushApuState();
  SmwDrawPpuFrame(draw_pixels);
  if (samples < 1 || samples > SMW_MAX_SAMPLES) samples = SMW_SAMPLES;
  RtlRenderAudio(stereo, samples, 2);
}
