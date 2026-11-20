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
  if (argc != 3 || (strcmp(argv[2], "completo") && strcmp(argv[2], "alternado") &&
                    strcmp(argv[2], "cenario")))
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
  /* v0.14: os menus tiram fotos de fases (smw_capture_scene) e o motor
     recomeça; o percurso tem de sair igual, imagem e som. v0.16: como no
     PS2, a primeira tira também a fonte e a moeda, e os menus tocam os
     efeitos do jogo pelo motor (smw_som_menus_*) antes de ele recomeçar. */
  if (strcmp(argv[2], "cenario") == 0) {
    static uint32_t cena[SMW_WIDTH * SMW_HEIGHT];
    static SmwArteMenu arte;
    if (!smw_capture_scene(0x105, cena, &arte) || !arte.ok || !smw_capture_scene(0x0C7, cena, NULL)) {
      fprintf(stderr, "A foto da fase falhou.\n");
      return EXIT_FAILURE;
    }
    long tinta = 0, moeda = 0;
    for (int t = 0; t < SMW_ARTE_LETRAS; ++t)
      for (int r = 0; r < 8; ++r) tinta += (arte.letras[t][r] & (arte.letras[t][r] >> 8)) != 0;
    for (int f = 0; f < 3; ++f)
      for (int i = 0; i < 256; ++i) moeda += arte.moeda[f][i] != 0;
    smw_som_menus_liga();
    /* Faixas: antes do 1º efeito; o cursor (quadros 10-39, com o mesmo
       pedido de novo no 11, que tem de esperar um quadro com 0); a moeda. */
    long faixa[3] = {0, 0, 0};
    for (int q = 0; q < 120; ++q) {
      if (q == 10 || q == 11) smw_som_menus_efeito(SMW_EFEITO_CURSOR);
      if (q == 40) smw_som_menus_efeito(SMW_EFEITO_MOEDA);
      int n = q % 3 ? SMW_SAMPLES : 100;  /* pedaços de tamanhos diferentes */
      smw_som_menus_gera(amostras, n);
      for (int i = 0; i < n * 2; ++i) faixa[q < 10 ? 0 : q < 40 ? 1 : 2] += amostras[i] != 0;
    }
    smw_som_menus_desliga();
    /* Antes do primeiro efeito, silêncio: o "plim" da tela da Nintendo, do
       quadro que smw_som_menus_liga roda, não pode tocar. */
    if (tinta < 100 || moeda < 100 || faixa[0] != 0 || faixa[1] == 0 || faixa[2] == 0) {
      fprintf(stderr, "Arte dos menus: %ld linhas com tinta, %ld pontos de moeda; efeitos: %ld %ld %ld amostras.\n",
              tinta, moeda, faixa[0], faixa[1], faixa[2]);
      return EXIT_FAILURE;
    }
  }

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
