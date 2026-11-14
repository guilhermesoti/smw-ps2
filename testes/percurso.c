/* Percurso de regressão da v0.1, executado com o motor e o carregador do port. */
#include "files.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/ptbr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t pixels[SMW_WIDTH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t amostras[SMW_SAMPLES * 2];

struct Entrada {
  unsigned inicio, fim;
  uint16_t botoes;
};

static const struct Entrada roteiro[] = {
  {200, 204, 0x008}, {245, 249, 0x008}, {365, 369, 0x008},
  {700, 705, 0x001}, {760, 765, 0x001}, {975, 1000, 0x040},
  {1030, 1038, 0x001}, {1300, 1302, 0x002}, {1450, 1485, 0x040},
  {1580, 1588, 0x001}, {1800, 1900, 0x082}, {1820, 1844, 0x001}
};

void Die(const char *msg) {
  fprintf(stderr, "Falha no motor: %s\n", msg);
  exit(EXIT_FAILURE);
}

static uint16_t entrada_no_passo(unsigned passo) {
  uint16_t botoes = 0;
  for (size_t i = 0; i < sizeof(roteiro) / sizeof(roteiro[0]); ++i) {
    if (passo >= roteiro[i].inicio && passo <= roteiro[i].fim)
      botoes |= roteiro[i].botoes;
  }
  return botoes;
}

int main(int argc, char **argv) {
  if (argc != 3 || (strcmp(argv[2], "completo") && strcmp(argv[2], "alternado")))
    return EXIT_FAILURE;
  uint8_t *rom;
  uint32_t crc_rom;
  char arquivo[SMW_PATH];
  RomResult resultado = smw_find_rom(argv[1], &rom, arquivo, &crc_rom);
  if (resultado != ROM_GOOD) {
    fprintf(stderr, "O carregador rejeitou a cópia do jogo (resultado %d).\n", resultado);
    return EXIT_FAILURE;
  }

  bool alternado = strcmp(argv[2], "alternado") == 0;
  unsigned passos_com_som = 0;
  unsigned menor_y_no_salto = UINT16_MAX;
  /* The reference image and sound were measured with the original text. */
  g_ptbr = false;
  smw_start(rom, pixels);
  free(rom);

  for (unsigned passo = 0; passo < 1930; ++passo) {
    bool desenhar = !alternado || (passo % 2 == 1);
    smw_frame(entrada_no_passo(passo), 0, 1, amostras, SMW_SAMPLES, desenhar);
    for (size_t i = 0; i < sizeof(amostras) / sizeof(amostras[0]); ++i) {
      if (amostras[i] != 0) {
        ++passos_com_som;
        break;
      }
    }
    if (passo >= 1820 && passo <= 1850 && ow_level_number_lo == 0x29 &&
        player_ypos < menor_y_no_salto)
      menor_y_no_salto = player_ypos;
  }
  unsigned long crc_imagem = crc32(0, (const unsigned char *)pixels, sizeof(pixels));
  printf("%s: fase=0x%02x, x=%u, menor_y_no_salto=%u, passos_com_som=%u, CRC32=%08lx\n",
         argv[2], (unsigned)ow_level_number_lo, (unsigned)player_xpos,
         menor_y_no_salto, passos_com_som, crc_imagem);
  return misc_game_mode == 0x14 && ow_level_number_lo == 0x29 &&
         player_xpos > 16 && menor_y_no_salto < 330 && passos_com_som == 1594 &&
         crc_imagem == 0xd1ac4ce4UL ? EXIT_SUCCESS : EXIT_FAILURE;
}
