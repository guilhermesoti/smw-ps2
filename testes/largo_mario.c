/* Tela larga: distância entre o Mario e a borda da imagem quando ele para
   no limite da tela (v0.13.5). Carrega cada sala pedida, anda para a
   esquerda e depois para a direita (correndo e pulando) e, em cada quadro
   em que o Mario está encostado no limite do jogo (8 px da câmera à
   esquerda, 232 px à direita), mede quantos pixels sobram entre ele e a
   borda da parte desenhada da imagem. No 4:3 sobram 8 px de cada lado; na
   tela larga, nas pontas da fase, as colunas fora da fase ficam pretas e
   também devem sobrar 8 px.

   Na tela larga, em cada sala, o teste também desenha o mesmo quadro duas
   vezes, com 43 colunas extras de cada lado e com 10 à esquerda e 30 à
   direita, e confere que a tela nativa fica no mesmo lugar e que só as
   colunas que sobram ficam pretas (PpuDrawWholeLine). As folgas usam as
   colunas que o jogo passou para a PPU no quadro (game.c).

   Uso: largo_mario <pasta com o jogo> <quadros> <pasta das imagens ou -> <sala>...
   Com SMW_LARGO no ambiente, joga em tela larga 16:9. Com uma pasta de
   imagens, grava um PPM do primeiro quadro em cada limite. Termina com
   erro se uma sala não chegar à fase, se a conferência da PPU não rodar ou
   falhar (16:9), se uma imagem não puder ser gravada ou se sobrar mais de
   8 px entre o Mario e a borda desenhada numa sala em que a câmera não anda
   sozinha (nas que andam, a tela empurra o Mario na borda do 4:3: só
   aparece na lista). */
#include "files.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/snes/snes.h"
#include "src/snes/ppu.h"
#include "src/snes/dma.h"
#include "src/ptbr.h"
#include "src/largo.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Snes *g_snes;
void SmwDrawPpuFrame(bool draw_pixels);
static uint32_t pixels[SMW_MAX_PITCH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t amostras[SMW_MAX_SAMPLES * 2];
static uint8_t ram_base[sizeof(g_ram)];
static Snes snes_base;
static Ppu ppu_base;
static Dma dma_base;
static bool largo;

void Die(const char *msg) {
  fprintf(stderr, "Falha no motor: %s\n", msg);
  exit(EXIT_FAILURE);
}

/* Mesmo roteiro do percurso.c: título, arquivo 1 e a primeira fase. */
static const struct { unsigned inicio, fim; uint16_t botoes; } roteiro[] = {
  {200, 204, 0x008}, {245, 249, 0x008}, {365, 369, 0x008},
  {700, 705, 0x001}, {760, 765, 0x001}, {975, 1000, 0x040},
  {1030, 1038, 0x001}, {1300, 1302, 0x002}, {1450, 1485, 0x040},
  {1580, 1588, 0x001}
};

static uint16_t entrada_no_passo(unsigned passo) {
  uint16_t botoes = 0;
  for (size_t i = 0; i < sizeof(roteiro) / sizeof(roteiro[0]); ++i)
    if (passo >= roteiro[i].inicio && passo <= roteiro[i].fim) botoes |= roteiro[i].botoes;
  return botoes;
}

static bool grava_ppm(const char *pasta, unsigned sala, const char *lado) {
  char nome[512];
  snprintf(nome, sizeof nome, "%s/sala_%03X_%s.ppm", pasta, sala, lado);
  FILE *f = fopen(nome, "wb");
  if (!f) {
    perror(nome);
    return false;
  }
  unsigned w = largo ? SMW_WIDE_WIDTH : SMW_WIDTH, pitch = largo ? SMW_MAX_PITCH : SMW_WIDTH;
  bool ok = fprintf(f, "P6\n%u %u\n255\n", w, SMW_HEIGHT) > 0;
  for (unsigned y = 0; y < SMW_HEIGHT && ok; ++y)
    for (unsigned x = 0; x < w && ok; ++x) {
      uint32_t c = pixels[y * pitch + x];
      uint8_t rgb[3] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16)};  /* PpuPackRgb: R no byte baixo */
      ok = fwrite(rgb, 1, 3, f) == 3;
    }
  if (fclose(f) != 0) ok = false;
  if (!ok) fprintf(stderr, "%s: falha ao gravar\n", nome);
  return ok;
}

/* Desenha o quadro atual sem mudar o estado do jogo (o desenho roda o IRQ
   da barra de status e o HDMA): guarda RAM, SNES, PPU e DMA e volta depois. */
static uint8_t ram_antes[sizeof(g_ram)];
static void desenha_com(uint8 esq, uint8 dir) {
  static Snes snes;
  static Ppu ppu;
  static Dma dma;
  memcpy(ram_antes, g_ram, sizeof(g_ram));
  snes = *g_snes, ppu = *g_snes->ppu, dma = *g_snes->dma;
  g_snes->ppu->extraLeftCur = esq, g_snes->ppu->extraRightCur = dir;
  SmwDrawPpuFrame(true);
  memcpy(g_ram, ram_antes, sizeof(g_ram));
  *g_snes = snes, *g_snes->ppu = ppu, *g_snes->dma = dma;
}

/* Com colunas extras diferentes de cada lado, a tela nativa e as colunas
   desenhadas ficam iguais às do quadro com 43 de cada lado, e o resto fica
   preto. */
static uint32_t referencia[SMW_MAX_PITCH * SMW_HEIGHT];
static bool confere_ppu(unsigned sala) {
  enum { E = 10, D = 30 };
  const int max = SMW_LARGO_EXTRA;
  const uint32_t preto = PpuPackRgb(0, 0, 0);
  desenha_com(max, max);
  memcpy(referencia, pixels, sizeof(pixels));
  /* Valor que a PPU nunca grava: o que não for desenhado de novo aparece. */
  memset(pixels, 0xAA, sizeof(pixels));
  desenha_com(E, D);
  int diferentes = 0, nao_preto = 0;
  for (int y = 0; y < SMW_HEIGHT; ++y)
    for (int x = 0; x < SMW_WIDE_WIDTH; ++x) {
      uint32_t c = pixels[y * SMW_MAX_PITCH + x];
      if (x < max - E || x >= max + 256 + D) nao_preto += c != preto;
      else diferentes += c != referencia[y * SMW_MAX_PITCH + x];
    }
  if (diferentes || nao_preto)
    fprintf(stderr, "SALA %03X: PPU com colunas %d/%d: %d pontos diferentes, %d fora sem preto\n",
            sala, E, D, diferentes, nao_preto);
  return !diferentes && !nao_preto;
}

int main(int argc, char **argv) {
  if (argc < 5) return EXIT_FAILURE;
  setbuf(stdout, NULL);
  uint8_t *rom;
  uint32_t crc;
  char arquivo[SMW_PATH];
  if (smw_find_rom(argv[1], &rom, arquivo, &crc) != ROM_GOOD) {
    fprintf(stderr, "O carregador rejeitou a cópia do jogo.\n");
    return EXIT_FAILURE;
  }
  unsigned quadros = strtoul(argv[2], NULL, 0);
  if (quadros < 60) {
    fprintf(stderr, "Use pelo menos 60 quadros por sala.\n");
    return EXIT_FAILURE;
  }
  const char *imagens = strcmp(argv[3], "-") ? argv[3] : NULL;
  g_ptbr = getenv("SMW_INGLES") == NULL;
  largo = getenv("SMW_LARGO") != NULL;
  smw_start(rom, pixels);
  free(rom);
  if (largo) smw_set_widescreen(true, pixels, SMW_MAX_PITCH);
  for (unsigned passo = 0; passo < 1800; ++passo)
    smw_frame(entrada_no_passo(passo), 0, 1, amostras, SMW_SAMPLES, false);
  if (misc_game_mode != 0x14) {
    fprintf(stderr, "O roteiro inicial não chegou numa fase (modo %02x).\n", misc_game_mode);
    return EXIT_FAILURE;
  }
  memcpy(ram_base, g_ram, sizeof(g_ram));
  snes_base = *g_snes;
  ppu_base = *g_snes->ppu;
  dma_base = *g_snes->dma;

  int falhas = 0;
  for (int a = 4; a < argc; ++a) {
    unsigned sala = strtoul(argv[a], NULL, 0);
    memcpy(g_ram, ram_base, sizeof(g_ram));
    *g_snes = snes_base;
    *g_snes->ppu = ppu_base;
    *g_snes->dma = dma_base;
    player_xpos = player_ypos = 0;
    player_current_state = 0;
    misc_intro_level_flag = 0;
    flag_use_secondary_entrance = 0;
    counter_sublevels_entered = 1;
    ow_players_map[0] = sala >> 8;
    misc_subscreen_exit_entrance_number_lo[0] = sala & 0xff;
    misc_game_mode = 0x11;
    /* Maior folga vista em cada lado (-1: nunca encostou) e a câmera. */
    int folga[2] = {-1, -1}, cam_folga[2] = {0, 0}, rolagem = -1, telas = 0, forma = 0, sprite = 0;
    bool gravou[2] = {false, false}, conferiu = false;
    for (unsigned f = 0; f < quadros; ++f) {
      bool direita = f >= quadros / 3;
      uint16_t botoes = (direita ? 0x080 : 0x040) | 0x002 | ((f % 50) < 14 ? 0x001 : 0);
      bool desenha = imagens && misc_game_mode == 0x14 && !(gravou[0] && gravou[1]);
      smw_frame(botoes, 0, 1, amostras, SMW_SAMPLES, desenha);
      /* Colunas que o jogo passou para a PPU neste quadro (game.c). */
      uint8 e = g_snes->ppu->extraLeftCur, d = g_snes->ppu->extraRightCur;
      if (misc_game_mode != 0x14) continue;
      if (largo && !conferiu && g_largo) {
        conferiu = true;
        if (!confere_ppu(sala)) ++falhas;
        desenha = false;  /* o quadro em pixels agora é o da conferência */
      }
      if (player_current_state != 0) continue;
      if (rolagem < 0) {
        rolagem = flag_layer1_horiz_scroll_level_setting;
        telas = camera_last_screen_horiz;
        forma = misc_level_layout_flags;
        sprite = l1_l2_scroll_spr_spriteid[0];
      }
      int cam = (int16)mirror_current_layer1_xpos;
      int sx = (int16)(player_xpos - mirror_current_layer1_xpos);
      int lado = -1, sobra = 0;
      if (sx <= 8) lado = 0, sobra = sx + e;
      else if (sx >= 232) lado = 1, sobra = (256 + d) - (sx + 16);
      if (lado < 0) continue;
      if (sobra > folga[lado]) folga[lado] = sobra, cam_folga[lado] = cam;
      if (desenha && !gravou[lado]) {
        if (!grava_ppm(imagens, sala, lado ? "direita" : "esquerda")) ++falhas;
        gravou[lado] = true;
      }
    }
    printf("SALA %03X rolagem=%d telas=%d forma=%02x sprite=%d esquerda=%d (camera %d) direita=%d (camera %d)\n",
           sala, rolagem, telas, forma, sprite, folga[0], cam_folga[0], folga[1], cam_folga[1]);
    if (rolagem < 0) {
      fprintf(stderr, "SALA %03X: não chegou à fase\n", sala);
      ++falhas;
    } else if (largo && !conferiu) {
      fprintf(stderr, "SALA %03X: a conferência da PPU não rodou\n", sala);
      ++falhas;
    }
    if (sprite == 0 && (folga[0] > 8 || folga[1] > 8)) {
      fprintf(stderr, "SALA %03X: Mario parou a mais de 8 px da borda desenhada\n", sala);
      ++falhas;
    }
  }
  if (falhas) fprintf(stderr, "%d falha(s)\n", falhas);
  return falhas ? EXIT_FAILURE : EXIT_SUCCESS;
}
