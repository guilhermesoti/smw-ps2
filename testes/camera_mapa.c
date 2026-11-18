/* Regressão da volta da câmera no mapa ("olhar em volta" com START).

   Leva o jogo até o mapa, simula o fim do modo de olhar em volta com a
   câmera deslocada de (dx, dy) e confere que ela volta para a posição
   guardada sem se afastar dela. O jogo original lê o sinal do byte alto da
   diferença; o C antigo usava o byte baixo e, para 128-255 pixels, ia para
   o lado errado.

   Uso: camera_mapa <pasta com o jogo> */
#include "files.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/ptbr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pixels[SMW_WIDTH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t amostras[SMW_MAX_SAMPLES * 2];
static uint8_t ram_mapa[sizeof(g_ram)];

void Die(const char *msg) {
  fprintf(stderr, "Falha no motor: %s\n", msg);
  exit(EXIT_FAILURE);
}

/* Título, arquivo 1, 1 jogador e a fase de abertura (mesmo início do
   percurso.c); termina parado no mapa. */
static const struct { unsigned inicio, fim; uint16_t botoes; } roteiro[] = {
  {200, 204, 0x008}, {245, 249, 0x008}, {365, 369, 0x008},
  {700, 705, 0x001}, {760, 765, 0x001}, {975, 1000, 0x040},
  {1030, 1038, 0x001}, {1300, 1302, 0x002}
};

static uint16_t entrada_no_passo(unsigned passo) {
  uint16_t botoes = 0;
  for (size_t i = 0; i < sizeof(roteiro) / sizeof(roteiro[0]); ++i)
    if (passo >= roteiro[i].inicio && passo <= roteiro[i].fim) botoes |= roteiro[i].botoes;
  return botoes;
}

static int distancia(int a, int b) { return a > b ? a - b : b - a; }

/* Devolve quantos quadros levou para voltar, ou -1 se se afastou / não voltou. */
static int volta(int dx, int dy) {
  memcpy(g_ram, ram_mapa, sizeof(g_ram));
  int x0 = (int16)mirror_current_layer1_xpos, y0 = (int16)mirror_current_layer1_ypos;
  ow_scroll_camera_xpos = x0;
  ow_scroll_camera_ypos = y0;
  mirror_current_layer1_xpos = mirror_current_layer2_xpos = (uint16)(x0 + dx);
  mirror_current_layer1_ypos = mirror_current_layer2_ypos = (uint16)(y0 + dy);
  flag_pause = 2;
  int antes = distancia((int16)mirror_current_layer1_xpos, x0) +
              distancia((int16)mirror_current_layer1_ypos, y0);
  for (int f = 1; f <= 600; ++f) {
    smw_frame(0, 0, 1, amostras, SMW_SAMPLES, false);
    int agora = distancia((int16)mirror_current_layer1_xpos, x0) +
                distancia((int16)mirror_current_layer1_ypos, y0);
    if (agora > antes + 1) return -1;
    antes = agora;
    if (!flag_pause)
      return (int16)mirror_current_layer1_xpos == x0 && (int16)mirror_current_layer1_ypos == y0 ? f : -1;
  }
  return -1;
}

int main(int argc, char **argv) {
  if (argc != 2) return EXIT_FAILURE;
  uint8_t *rom;
  uint32_t crc;
  char arquivo[SMW_PATH];
  if (smw_find_rom(argv[1], &rom, arquivo, &crc) != ROM_GOOD) return EXIT_FAILURE;
  g_ptbr = false;
  smw_start(rom, pixels);
  free(rom);
  unsigned passo = 0;
  for (; passo < 3000; ++passo) {
    smw_frame(entrada_no_passo(passo), 0, 1, amostras, SMW_SAMPLES, false);
    if (passo > 1310 && misc_game_mode == 0x0e && pointer_current_overworld_process == 3) break;
  }
  if (misc_game_mode != 0x0e) {
    fprintf(stderr, "Não chegou ao mapa (modo %02x).\n", misc_game_mode);
    return EXIT_FAILURE;
  }
  for (int i = 0; i < 30; ++i) smw_frame(0, 0, 1, amostras, SMW_SAMPLES, false);
  memcpy(ram_mapa, g_ram, sizeof(g_ram));

  static const int casos[][2] = {
    {40, 0}, {-40, 0}, {0, 60}, {0, -60},          /* curtas: já funcionavam */
    {200, 0}, {-200, 0}, {0, 180}, {0, -180},      /* 128-255 pixels */
    {250, 150}, {-150, -250}, {273, 0}, {-17, -41} /* diagonais e limites */
  };
  int falhas = 0;
  for (size_t i = 0; i < sizeof(casos) / sizeof(casos[0]); ++i) {
    int q = volta(casos[i][0], casos[i][1]);
    printf("deslocamento %4d,%4d: %s", casos[i][0], casos[i][1], q > 0 ? "voltou" : "ERRO");
    if (q > 0) printf(" em %d quadros", q);
    printf("\n");
    falhas += q < 0;
  }
  return falhas ? EXIT_FAILURE : EXIT_SUCCESS;
}
