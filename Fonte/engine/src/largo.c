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
