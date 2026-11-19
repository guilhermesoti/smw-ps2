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

/* Colunas extras desenhadas à esquerda e à direita neste quadro (v0.13.5).
   Nas pontas da fase as colunas extras mostrariam o que fica fora dela
   (antes do começo ou depois do fim), aonde o Mario não pode ir; ali elas
   ficam pretas. Chamado depois do quadro do jogo, antes de desenhar. */
void LargoColunas(uint8 *esq, uint8 *dir);

/* x relativo à câmera (camada 1) visível na área larga, contando a largura
   do objeto à esquerda. Com g_largo = 0 equivale a 0 <= dx < 256. */
static inline bool LargoNaTela(int dx, int largura) {
  return dx > -largura - (int)g_largo && dx < 256 + (int)g_largo;
}
/* x de um objeto (byte alto/baixo) relativo à câmera da camada 1.
   Só nos arquivos que incluem variables.h. */
#define LARGO_DX(hi, lo) ((int)(int16)(PAIR16((hi), (lo)) - mirror_current_layer1_xpos))
/* Bit 9 de X na OAM para dx (para o byte de tamanho/X alto). */
static inline uint8 LargoBitX(int dx) { return (dx >> 8) & 1; }
/* Posição de criação na borda da tela (geradores): afasta g_largo para
   fora as posições à esquerda (< 0) e à direita (>= 0xF0) da tela. */
static inline int LargoBorda(int dx) {
  return dx < 0 ? dx - g_largo : dx >= 0xF0 ? dx + g_largo : dx;
}

/* ---- Efeitos visuais na tela larga (v0.12) ----
   O jogo decide se desenha alguns efeitos olhando se o objeto está nos 256
   pixels da tela original. Na tela larga esses efeitos passam a valer na
   área visível maior. Só os desenhos mudam: a lógica do jogo (IA, colisão)
   continua com a tela original. Com g_largo = 0 o código original é usado. */

/* Origem do objeto (dx relativo à câmera) na área visível larga. É o mesmo
   teste "na tela" do original (0 <= dx < 256) com as colunas extras. */
static inline bool LargoOrigemNaTela(int dx) {
  return dx >= -(int)g_largo && dx < 256 + (int)g_largo;
}
/* Tela larga: dx fora de [-g_largo, limite + g_largo). Com limite = 256 é o
   contrário de LargoOrigemNaTela; o original usa limites menores (0xF0,
   0xF4) para não desenhar peças cortadas na borda direita. */
static inline bool LargoForaDe(int dx, int limite) {
  return dx < -(int)g_largo || dx >= limite + (int)g_largo;
}
/* dx de um sprite normal. Só nos arquivos que incluem variables.h. */
#define LARGO_SPR_DX(k) LARGO_DX(spr_xpos_hi[k], spr_xpos_lo[k])
/* dx numa das colunas extras (fora dos 256 pixels originais e dentro da
   área visível larga). Com g_largo = 0 nunca. */
static inline bool LargoNaColunaExtra(int dx) {
  return dx < 0 ? dx >= -(int)g_largo : dx >= 256 && dx < 256 + (int)g_largo;
}
/* CheckIfNormalSpriteOffScreen para efeitos visuais. Vale o resultado do
   original (as marcas de "fora da tela" do jogo) e, na tela larga, o sprite
   nas colunas extras também conta como na tela. Só nos arquivos que incluem
   variables.h. */
#define LargoSpriteFora(k) \
  (CheckIfNormalSpriteOffScreen(k) != 0 && \
   !(spr_yoffscreen_flag[k] == 0 && LargoNaColunaExtra(LARGO_SPR_DX(k))))

/* As fumaças (smoke_spr_*, 4 espaços) só guardam o byte baixo de X. Na tela
   larga o port acompanha o x de cada uma relativo à câmera: quem cria a
   fumaça informa o dx (LargoFumacaNova) e, quadro a quadro, vale o valor
   mais perto do anterior. Sem informação, vale o do original (0..255). */
void LargoFumacaNova(uint8 k, int dx);
int LargoFumacaDx(uint8 k);
void LargoFumacaApaga(uint8 k);
#endif
