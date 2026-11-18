#include "largo.h"
#include "common_rtl.h"
#include "variables.h"

uint8 g_largo_max;
uint8 g_largo;

/* Só as fases usam a tela larga. Título, menus, mapa, cenas e créditos
   ficam em 4:3 com faixas pretas. Nas transições (escurecer/clarear e
   carregar) vale a última cena mostrada. */
static bool cena_fase;

void LargoAtualiza(void) {
  uint8 m = misc_game_mode;
  if (m >= 0x13 && m <= 0x15)
    cena_fase = true;
  else if (m != 0x0B && m != 0x0F && !(m >= 0x10 && m <= 0x12))
    cena_fase = false;
  g_largo = (g_largo_max && cena_fase) ? g_largo_max : 0;
}

/* Fumaças na tela larga: por espaço, a fumaça que está lá (tipo e byte
   baixo de X), o dx visto por último e o quadro em que foi visto. */
static struct {
  bool usado;
  uint8 tipo, xlo;
  int16 dx;
  int quadro;
} fumaca[4];

void LargoFumacaNova(uint8 k, int dx) {
  fumaca[k].usado = true;
  fumaca[k].tipo = smoke_spr_spriteid[k];
  fumaca[k].xlo = smoke_spr_xpos_lo[k];
  fumaca[k].dx = (int16)dx;
  fumaca[k].quadro = snes_frame_counter;
}

int LargoFumacaDx(uint8 k) {
  uint8 d8 = smoke_spr_xpos_lo[k] - (uint8)mirror_current_layer1_xpos;
  int dx = d8;  /* sem informação: como no original */
  /* A mesma fumaça (mesmo tipo e X) vista há pouco: o dx que tem esse byte
     baixo e fica mais perto do anterior (a câmera anda poucos pixels). */
  if (fumaca[k].usado && fumaca[k].tipo == smoke_spr_spriteid[k] &&
      fumaca[k].xlo == smoke_spr_xpos_lo[k] &&
      (unsigned)(snes_frame_counter - fumaca[k].quadro) <= 2)
    dx = fumaca[k].dx + (int8)(uint8)(d8 - (uint8)fumaca[k].dx);
  LargoFumacaNova(k, dx);
  return dx;
}

void LargoFumacaApaga(uint8 k) {
  fumaca[k].usado = false;
}
