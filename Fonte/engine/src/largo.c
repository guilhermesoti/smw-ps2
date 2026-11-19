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

/* A câmera do original (camada 1) vai do começo da fase (x = 0) até o fim
   menos uma tela (HandleStandardLevelCameraScroll): fase horizontal com
   camera_last_screen_horiz telas de 256 pixels (com 0 telas a câmera vai
   até 128), fase vertical com duas telas de largura (câmera de 0 a 256).
   As salas de chefe em modo 7 (Iggy, Larry, Reznor, Bowser) são uma tela
   só. Cada coluna extra só é desenhada se cair dentro da fase; longe das
   pontas, as duas ficam com g_largo. */
void LargoColunas(uint8 *esq, uint8 *dir) {
  *esq = *dir = g_largo;
  if (!g_largo)
    return;
  int fim;
  if (misc_nmito_use_flag & 0x80)
    fim = 256;
  else if (misc_level_layout_flags & 1)
    fim = 512;
  else if (camera_last_screen_horiz)
    fim = camera_last_screen_horiz << 8;
  else
    fim = 128 + 256;
  int cam = (int16)mirror_current_layer1_xpos;
  int e = cam, d = fim - 256 - cam;
  *esq = e <= 0 ? 0 : e >= g_largo ? g_largo : (uint8)e;
  *dir = d <= 0 ? 0 : d >= g_largo ? g_largo : (uint8)d;
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
