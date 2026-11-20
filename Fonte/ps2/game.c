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
#include <stdlib.h>
#include <string.h>

Snes *g_snes;
SpcPlayer *g_spc_player;
bool g_use_my_apu_code = true;
bool g_new_ppu = true;
bool g_fail;
unsigned smw_connected_pads;
bool smw_save_error;

void SmwRunOneFrameOfGame(void);
void SmwDrawPpuFrame(bool draw_pixels);
void SmwDrawPpuFrameSemHud(void);

static uint32_t *game_pixels;

/* Logic, audio generation and rendering run on the same EE thread. */
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

static void (*before_save)(void);
void smw_before_save(void (*hook)(void)) { before_save = hook; }

static void (*before_frame)(uint8_t *ram);
void smw_before_frame(void (*hook)(uint8_t *ram)) { before_frame = hook; }

/* Each save goes to the log; the file itself is written after the first
   save (and after the next ones until a write works) and after a failed
   one: writing it stops the game for a moment. A save of what the save
   place already has (choosing the file on the title screen) writes nothing
   (v0.13.8). */
void RtlWriteSram(void) {
  static unsigned saves;
  static bool pending = true;
  if (smw_save_unchanged(g_sram)) {
    smw_log("Save %u: sem mudanca, nada gravado", saves + 1);
    ++saves;
    return;
  }
  if (before_save) before_save();
  unsigned start = smw_log_ms();
  smw_save_error = !smw_save_progress(g_sram);
  unsigned ms = smw_log_ms() - start;
  ++saves;
  if (smw_save_error)
    smw_log("Save %u: ERRO AO SALVAR %s (%u ms)", saves, smw_save_error_text(), ms);
  else
    smw_log("Save %u: ok, %s (%u ms: %s)", saves, smw_save_location(), ms, smw_save_steps());
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
  game_pixels = pixels;
  PpuBeginDrawing(g_snes->ppu, (uint8_t *)pixels, SMW_WIDTH * 4, 0);
  smw_load_progress(g_sram);
}

/* O motor de novo como logo depois de smw_start: Snes, APU e reprodutor de
   som novos, a mesma ROM e o mesmo save (a SRAM carregada). */
static void restart_engine(void) {
  Snes *old = g_snes;
  Snes *s = snes_init(g_ram);
  cart_load(s->cart, 1, old->cart->rom, SMW_ROM_BYTES, SMW_SRAM_BYTES);
  memcpy(s->cart->ram, old->cart->ram, SMW_SRAM_BYTES);
  snes_free(old);
  g_snes = s;
  snes_reset(g_snes, true);
  g_rom = s->cart->rom;
  g_sram = s->cart->ram;
  g_sram_size = s->cart->ramSize;
  dsp_free(g_spc_player->dsp);
  free(g_spc_player);
  g_spc_player = SmwSpcPlayer_Create();
  g_spc_player->initialize(g_spc_player);
  RtlReiniciaEstado();
  PpuBeginDrawing(g_snes->ppu, (uint8_t *)game_pixels, SMW_WIDTH * 4, 0);
}

bool smw_capture_scene(unsigned room, uint32_t *dest) {
  static int16_t scratch[SMW_MAX_SAMPLES * 2];
  /* Como o carregador de salas dos testes: logo depois do reset, a sala
     direto no modo 0x11 (carregar fase). */
  void (*hook)(uint8_t *) = before_frame;
  before_frame = NULL;  /* sem trapacas na foto */
  for (int i = 0; i < 2; ++i) smw_frame(0, 0, 1, scratch, SMW_SAMPLES, false);
  player_xpos = player_ypos = 0;
  player_current_state = 0;
  misc_intro_level_flag = 0;
  flag_use_secondary_entrance = 0;
  counter_sublevels_entered = 1;
  ow_players_map[0] = (uint8)(room >> 8);
  misc_subscreen_exit_entrance_number_lo[0] = (uint8)room;
  misc_game_mode = 0x11;
  /* Mais 2 quadros depois de entrar na fase (no PC, o quadro ja sai igual
     ao de 30 quadros depois em 0x105, 0x0C7 e 0x104). */
  int in_level = -1;
  for (int f = 0; f < 300 && (in_level < 0 || f - in_level < 2); ++f) {
    smw_frame(0, 0, 1, scratch, SMW_SAMPLES, false);
    if (in_level < 0 && misc_game_mode == 0x14) in_level = f;
  }
  const bool ok = in_level >= 0;
  if (ok) {
    PpuBeginDrawing(g_snes->ppu, (uint8_t *)dest, SMW_WIDTH * 4, 0);
    SmwDrawPpuFrameSemHud();
  }
  restart_engine();
  before_frame = hook;
  return ok;
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
  /* v0.14: trapacas (codigos de RAM) antes da logica de cada passo, como o
     Pro Action Replay escreve a cada quadro. */
  if (before_frame) before_frame(g_ram);
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
