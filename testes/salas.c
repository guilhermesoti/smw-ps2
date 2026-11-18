/* Varredura de salas: carrega cada sala do jogo pelo carregador do próprio
   motor e joga alguns segundos com entradas fixas. Serve para achar leituras
   e escritas fora das tabelas com AddressSanitizer/UBSan. Não prova que o
   jogo inteiro pode ser terminado.

   Uso: salas <pasta com o jogo> <quadros por sala> <sala> [<sala>...]
   Com SMW_LARGO no ambiente, joga em tela larga 16:9. */
#include "files.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/snes/snes.h"
#include "src/snes/ppu.h"
#include "src/snes/dma.h"
#include "src/ptbr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Snes *g_snes;
static uint32_t pixels[SMW_MAX_PITCH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t amostras[SMW_MAX_SAMPLES * 2];
static uint8_t ram_base[sizeof(g_ram)];
static Snes snes_base;
static Ppu ppu_base;
static Dma dma_base;

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

static void quadro(uint16_t botoes) {
  smw_frame(botoes, 0, 1, amostras, SMW_SAMPLES, true);
}

int main(int argc, char **argv) {
  if (argc < 4) return EXIT_FAILURE;
  setbuf(stdout, NULL);
  uint8_t *rom;
  uint32_t crc;
  char arquivo[SMW_PATH];
  if (smw_find_rom(argv[1], &rom, arquivo, &crc) != ROM_GOOD) {
    fprintf(stderr, "O carregador rejeitou a cópia do jogo.\n");
    return EXIT_FAILURE;
  }
  unsigned quadros = strtoul(argv[2], NULL, 0);
  g_ptbr = getenv("SMW_INGLES") == NULL;
  smw_start(rom, pixels);
  free(rom);
  if (getenv("SMW_LARGO")) smw_set_widescreen(true, pixels, SMW_MAX_PITCH);
  for (unsigned passo = 0; passo < 1800; ++passo) quadro(entrada_no_passo(passo));
  if (misc_game_mode != 0x14) {
    fprintf(stderr, "O roteiro inicial não chegou numa fase (modo %02x).\n", misc_game_mode);
    return EXIT_FAILURE;
  }
  memcpy(ram_base, g_ram, sizeof(g_ram));
  snes_base = *g_snes;
  ppu_base = *g_snes->ppu;
  dma_base = *g_snes->dma;

  for (int a = 3; a < argc; ++a) {
    unsigned sala = strtoul(argv[a], NULL, 0);
    printf("SALA_INICIO %u\n", sala);
    fprintf(stderr, "SALA_INICIO %u\n", sala);
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
    unsigned chegou = 0, f;
    uint32_t aleatorio = 0x19b35a2du ^ (sala * 1013u);
    for (f = 0; f < quadros; ++f) {
      aleatorio ^= aleatorio << 13;
      aleatorio ^= aleatorio >> 17;
      aleatorio ^= aleatorio << 5;
      uint16_t botoes = ((f / (quadros / 4 + 1)) & 1 ? 0x040 : 0x080) | 0x002 |
                        ((f % 90) < 24 ? 0x001 : 0);
      if ((aleatorio & 255) == 0) botoes |= 0x100;
      if ((aleatorio & 511) == 1) botoes |= 0x200;
      quadro(botoes);
      if (misc_game_mode == 0x14) chegou = 1;
      else if (chegou) break;
    }
    printf("SALA_OK %u %u %u %02x\n", sala, chegou, f, misc_game_mode);
  }
  return EXIT_SUCCESS;
}
