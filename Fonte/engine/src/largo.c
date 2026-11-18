#include "largo.h"
#include "common_rtl.h"
#include "variables.h"

uint8 g_largo_max;
uint8 g_largo;

/* Cenas em que uma fase está na tela: título (a demonstração é uma fase),
   escolha de arquivo e de jogadores, entrada, jogo e saídas da fase. */
static bool LargoCenaDeFase(void) {
  uint8 m = misc_game_mode;
  return (m >= 0x05 && m <= 0x0B) || (m >= 0x13 && m <= 0x15);
}

void LargoAtualiza(void) {
  g_largo = (g_largo_max && LargoCenaDeFase()) ? g_largo_max : 0;
}
