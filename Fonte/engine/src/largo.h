#ifndef SMW_LARGO_H
#define SMW_LARGO_H
#include "types.h"
/* Tela larga 16:9 do port (implementação própria).

   A PPU desenha g_largo colunas a mais de cada lado da tela nativa de 256
   (43 + 256 + 43 = 342, 16:9 com o pixel 8:7 do SNES). O cenário dessas
   colunas já está na VRAM: o jogo atualiza as colunas 128 pixels à esquerda
   e 112 à direita da tela. O motor só precisa desenhar, criar e apagar os
   objetos considerando a área visível maior.

   g_largo_max: 0 (4:3) ou SMW_LARGO_EXTRA, escolhido pelo jogador.
   g_largo: colunas extras ativas no quadro atual (0 nas cenas sem suporte,
   que ficam com faixas pretas). Com 0, todo o motor se comporta como antes. */
enum { SMW_LARGO_EXTRA = 43 };
extern uint8 g_largo_max;
extern uint8 g_largo;

/* Calcula g_largo para a cena atual. Chamado no início de cada quadro do
   jogo e de novo antes de desenhar. */
void LargoAtualiza(void);

/* x relativo à câmera (camada 1) visível na área larga, contando a largura
   do objeto à esquerda. Com g_largo = 0 equivale a 0 <= dx < 256. */
static inline bool LargoNaTela(int dx, int largura) {
  return dx > -largura - (int)g_largo && dx < 256 + (int)g_largo;
}
#endif
