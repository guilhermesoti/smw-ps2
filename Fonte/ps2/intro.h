#ifndef SMW_INTRO_H
#define SMW_INTRO_H
#include <gsKit.h>
#include <stdbool.h>
#include <stdint.h>

/* Abertura da Retrogames e tela "Apoie o projeto" (intro.c). Chame depois de
   iniciar o video e antes do jogo; volta quando o jogador aperta X ou START
   na tela de apoio (ou depois de 20 s).
   v0.14: fundo_titulo e fundo_apoio sao cenas do jogo (256 x 224, 0x00RRGGBB,
   smw_capture_scene) para o titulo e a tela de apoio, ou NULL. */
void smw_intro(GSGLOBAL *gs, bool p480, bool audio_ok, const char *version, const uint32_t *fundo_titulo,
               const uint32_t *fundo_apoio);

/* Desenha a textura t de (x0, y0) a (x1, y1), usando u1 x v1 texels, com os
   pixels nitidos (sem filtro). Com o tamanho abaixo de 100% o retangulo
   encolhe em volta do proprio centro, com o filtro bilinear (v0.14.1): feito
   para o quadro da tela inteira (o jogo, os menus, a abertura); um desenho
   menor por cima de outro nao ficaria no lugar certo. u1 e v1 >= 1. */
void smw_desenha_textura(GSGLOBAL *gs, GSTEXTURE *t, float x0, float y0, float x1, float y1, float u1,
                         float v1);

/* v0.14.1: tamanho da imagem na TV, largura e altura em % (SMW_TAMANHO_MIN a
   SMW_TAMANHO_MAX, files.h), para todos os desenhos de smw_desenha_textura:
   o jogo, as telas antes dele e a abertura. Como o ajuste de tela de um
   monitor. As telas de carregamento de antes da leitura dos ajustes (ate
   "Lendo o save...") ficam em 100%. */
void smw_set_tamanho(int largura, int altura);

/* Mostra um quadro 0x00RRGGBB do tamanho de tex (que aponta para ele) com o
   brilho 0..128 e espera a TV (um quadro). O quadro e convertido no lugar
   para o formato do GS. 480p: 448 linhas. colunas: quantas colunas do SNES
   a largura da tela mostra; tex->Width ocupa a tela toda, 342 (jogo em
   16:9) deixa o quadro no meio com faixas pretas. */
void smw_mostra_quadro(GSGLOBAL *gs, GSTEXTURE *tex, uint32_t *quadro, bool p480, int bright, unsigned colunas);

/* Um "blip" curto ao trocar de opcao: so marca; o som sai em smw_som_menu. */
void smw_blip(bool audio_ok);
/* Som de um pedaco dos menus: o resto do bip marcado ou silencio. */
void smw_som_menu(int16_t *stereo, int frames);

#endif
