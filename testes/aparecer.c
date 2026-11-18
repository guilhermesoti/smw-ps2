/* Tela larga: confere se os objetos da fase surgem e somem fora da área
   visível. Carrega cada sala pedida, joga com entradas fixas e acusa:
   - objeto da lista da fase criado com a borda a mais de 8 px dentro da
     área visível (o jogador veria ele aparecer do nada);
   - objeto da lista apagado enquanto ainda está dentro da área visível.
   Uso: aparecer <pasta com o jogo> <largo 0/1> <quadros> <sala> [<sala>...] */
#include "files.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/snes/snes.h"
#include "src/snes/ppu.h"
#include "src/snes/dma.h"
#include "src/smw_rtl.h"
#include "src/ptbr.h"
#include "src/largo.h"
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

int main(int argc, char **argv) {
  if (argc < 5) return EXIT_FAILURE;
  setbuf(stdout, NULL);
  uint8_t *rom;
  uint32_t crc;
  char arquivo[SMW_PATH];
  if (smw_find_rom(argv[1], &rom, arquivo, &crc) != ROM_GOOD) return EXIT_FAILURE;
  bool largo = atoi(argv[2]) != 0;
  unsigned quadros = strtoul(argv[3], NULL, 0);
  smw_start(rom, pixels);
  free(rom);
  if (largo) smw_set_widescreen(true, pixels, SMW_MAX_PITCH);
  for (unsigned passo = 0; passo < 1800; ++passo)
    smw_frame(entrada_no_passo(passo), 0, 1, amostras, SMW_SAMPLES, false);
  memcpy(ram_base, g_ram, sizeof(g_ram));
  snes_base = *g_snes;
  ppu_base = *g_snes->ppu;
  dma_base = *g_snes->dma;
  unsigned total_surge = 0, total_some = 0;
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
    uint8 status[12] = {0}, origem[12];
    int16 relx[12] = {0};
    uint32_t aleatorio = 0x19b35a2du ^ (sala * 1013u);
    unsigned surge = 0, some = 0;
    bool em_fase = false;
    for (unsigned f = 0; f < quadros; ++f) {
      aleatorio ^= aleatorio << 13;
      aleatorio ^= aleatorio >> 17;
      aleatorio ^= aleatorio << 5;
      uint16_t botoes = ((f / (quadros / 4 + 1)) & 1 ? 0x040 : 0x080) | 0x002 |
                        ((f % 90) < 24 ? 0x001 : 0);
      if ((aleatorio & 255) == 0) botoes |= 0x100;
      smw_frame(botoes, 0, 1, amostras, SMW_SAMPLES, false);
      bool fase = misc_game_mode == 0x14 && !(misc_level_layout_flags & 1);
      int e = g_largo;
      for (int k = 0; k < 12; ++k) {
        int16 x = (int16)(GetSprXPos(k) - mirror_current_layer1_xpos);
        bool vivo = spr_current_status[k] != 0;
        bool da_lista = vivo && spr_load_status_table_index[k] != 0xFF;
        if (fase && em_fase) {
          /* Criado agora pela lista da fase com a borda visível. */
          if (da_lista && !status[k] && x > -e - 16 + 8 && x < 256 + e - 8) {
            printf("SURGE sala %03X quadro %u objeto %02X x=%d\n", sala, f, spr_spriteid[k], x);
            ++surge;
          }
          /* Apagado (não morto) enquanto ainda aparecia. */
          if (!vivo && status[k] && origem[k] && relx[k] > -e - 16 + 8 && relx[k] < 256 + e - 8 &&
              (relx[k] < -8 || relx[k] > 248)) {
            printf("SOME sala %03X quadro %u x=%d objeto %02X\n", sala, f, relx[k], spr_spriteid[k]);
            ++some;
          }
        }
        status[k] = spr_current_status[k];
        origem[k] = da_lista;
        relx[k] = x;
      }
      em_fase = fase;
    }
    total_surge += surge;
    total_some += some;
  }
  printf("TOTAL surge=%u some=%u\n", total_surge, total_some);
  return EXIT_SUCCESS;
}
