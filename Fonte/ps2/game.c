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

/* v0.16: os efeitos do jogo nos menus (smw_som_menus_*, mais abaixo). */
static int16_t som_menus[SMW_SAMPLES * 2];
static int som_menus_n;            /* amostras prontas em som_menus */
static uint8_t som_menus_pedido;   /* efeito para o proximo quadro do som */
static uint8_t som_menus_porta;    /* o que a porta 3 recebeu por ultimo */
static bool som_menus_ligado;

static uint32_t cor_obj(const Ppu *ppu, int i) {
  uint16_t c = ppu->cgram[i];
  uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  return 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

/* Uma peca 8 x 8 de 4 bits dos OBJ em (x0, y0) de um quadro 16 x 16;
   vira: espelhada na vertical. */
static void peca_obj(const Ppu *ppu, int tile, uint32_t *dest, int x0, int y0, bool vira) {
  const int base = PPU_objTileAdr1(ppu) + tile * 16;
  for (int y = 0; y < 8; y++) {
    int ly = vira ? 7 - y : y;
    uint16_t a = ppu->vram[(base + ly) & 0x7fff], b = ppu->vram[(base + 8 + ly) & 0x7fff];
    for (int x = 0; x < 8; x++) {
      int v = ((a >> (7 - x)) & 1) | ((a >> (15 - x)) & 1) << 1 | ((b >> (7 - x)) & 1) << 2 |
              ((b >> (15 - x)) & 1) << 3;
      dest[(y0 + y) * 16 + x0 + x] = v ? cor_obj(ppu, 0xA0 + v) : 0;
    }
  }
}

static void tira_arte(SmwArteMenu *arte) {
  const Ppu *ppu = g_snes->ppu;
  const int base = PPU_bgTileAdr(ppu, 2) + 0x100 * 8;
  for (int t = 0; t < SMW_ARTE_LETRAS; t++)
    for (int y = 0; y < 8; y++) arte->letras[t][y] = ppu->vram[(base + t * 8 + y) & 0x7fff];
  /* As 24 primeiras letras do ptbr.c: as 12 minusculas acentuadas e depois
     as maiusculas, com o codigo de cada uma tirado da tabela de la. */
  _Static_assert(sizeof(arte->acentos) <= sizeof(g_smw_extra_font), "g_smw_extra_font menor que os acentos");
  _Static_assert(SMW_ARTE_ACENTOS == 2 * sizeof(g_ptbr_acentos), "acentos do ptbr.c mudaram");
  memcpy(arte->acentos, g_smw_extra_font, sizeof(arte->acentos));
  for (int i = 0; i < SMW_ARTE_ACENTOS; i++)
    arte->acento_codigo[i] = (uint8_t)(i < 12 ? g_ptbr_acentos[i] : g_ptbr_acentos[i - 12] - 0x20);
  memset(arte->moeda, 0, sizeof(arte->moeda));
  peca_obj(ppu, 0xE8, arte->moeda[0], 0, 0, false), peca_obj(ppu, 0xE9, arte->moeda[0], 8, 0, false);
  peca_obj(ppu, 0xF8, arte->moeda[0], 0, 8, false), peca_obj(ppu, 0xF9, arte->moeda[0], 8, 8, false);
  peca_obj(ppu, 0xEA, arte->moeda[1], 4, 0, false), peca_obj(ppu, 0xEA, arte->moeda[1], 4, 8, true);
  peca_obj(ppu, 0xFA, arte->moeda[2], 4, 0, false), peca_obj(ppu, 0xFA, arte->moeda[2], 4, 8, true);
}

bool smw_capture_scene(unsigned room, uint32_t *dest, SmwArteMenu *arte) {
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
    if (arte) tira_arte(arte);
  }
  if (arte) arte->ok = ok;
  restart_engine();
  /* O motor novo nao tem o programa de som dos menus (revisao 2). */
  som_menus_ligado = false, som_menus_n = 0, som_menus_pedido = som_menus_porta = 0;
  before_frame = hook;
  return ok;
}

/* ---- v0.16: efeitos do jogo nos menus ---- */


/* Um quadro do reprodutor: as portas como o NMI do jogo escreve ($1DFC na
   porta 3; 0 = nada novo) e SMW_SAMPLES amostras. */
static void som_menus_quadro(int16_t *dest) {
  /* O programa de som so ve um comando quando a porta muda: o mesmo efeito
     no quadro seguinte espera um quadro com 0 (revisao 2). */
  uint8_t porta = som_menus_pedido == som_menus_porta ? 0 : som_menus_pedido;
  if (porta == som_menus_pedido) som_menus_pedido = 0;
  g_snes->runningWhichVersion = 2;  /* o RtlApuWrite so guarda nesse modo, como no smw_frame */
  RtlApuWrite(APUI00, 0);
  RtlApuWrite(APUI01, 0);
  RtlApuWrite(APUI02, 0);
  RtlApuWrite(APUI03, porta);
  g_snes->runningWhichVersion = 0;
  som_menus_porta = porta;
  RtlPushApuState();
  RtlRenderAudio(dest, SMW_SAMPLES, 2);
}

void smw_som_menus_liga(void) {
  if (som_menus_ligado) return;
  void (*hook)(uint8_t *) = before_frame;
  before_frame = NULL;  /* sem trapacas nesse quadro */
  smw_frame(0, 0, 1, som_menus, SMW_SAMPLES, false);
  before_frame = hook;
  som_menus_pedido = som_menus_porta = 0;
  /* O "plim" do primeiro quadro dura uns 25 quadros: 40 sem tocar. */
  for (int i = 0; i < 40; i++) som_menus_quadro(som_menus);
  som_menus_n = 0;
  som_menus_ligado = true;
}

void smw_som_menus_efeito(uint8_t efeito) {
  if (som_menus_ligado) som_menus_pedido = efeito;
}

/* Sempre quadros inteiros de SMW_SAMPLES: um quadro do reprodutor espremido
   em menos amostras tocaria mais rapido. */
void smw_som_menus_gera(int16_t *stereo, int samples) {
  if (!som_menus_ligado) {
    memset(stereo, 0, (size_t)(samples > 0 ? samples : 0) * 4);
    return;
  }
  while (samples > 0) {
    if (som_menus_n == 0) {
      som_menus_quadro(som_menus);
      som_menus_n = SMW_SAMPLES;
    }
    int n = samples < som_menus_n ? samples : som_menus_n;
    memcpy(stereo, som_menus + (SMW_SAMPLES - som_menus_n) * 2, (size_t)n * 4);
    stereo += n * 2, samples -= n, som_menus_n -= n;
  }
}

void smw_som_menus_desliga(void) {
  if (!som_menus_ligado) return;
  restart_engine();
  som_menus_n = 0;
  som_menus_pedido = som_menus_porta = 0;
  som_menus_ligado = false;
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
