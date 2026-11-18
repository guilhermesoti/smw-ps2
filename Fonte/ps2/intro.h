#ifndef SMW_INTRO_H
#define SMW_INTRO_H
#include <gsKit.h>
#include <stdbool.h>
#include <stdint.h>

/* Abertura da Retrogames e tela "Apoie o projeto" (intro.c). Chame depois de
   iniciar o video e antes do jogo; volta quando o jogador aperta X ou START
   na tela de apoio (ou depois de 20 s). suave: a suavizacao escolhida
   (0 desligada, 1 media, 2 forte), a mesma do jogo. */
void smw_intro(GSGLOBAL *gs, bool p480, bool audio_ok, const char *version, int suave);

/* Desenha a textura t de (x0, y0) a (x1, y1), usando u1 x v1 texels, com a
   suavizacao suave (0 nitida, 1 media, 2 forte). */
void smw_sprite_suave(GSGLOBAL *gs, GSTEXTURE *t, float x0, float y0, float x1, float y1, float u1,
                      float v1, int suave);

/* Mostra um quadro 0x00RRGGBB do tamanho de tex (que aponta para ele) na tela
   toda, com o brilho 0..128 e a suavizacao, e espera a TV (um quadro). O
   quadro e convertido no lugar para o formato do GS. 480p: 448 linhas. */
void smw_mostra_quadro(GSGLOBAL *gs, GSTEXTURE *tex, uint32_t *quadro, bool p480, int bright, int suave);

/* Um "blip" curto ao trocar de opcao. */
void smw_blip(bool audio_ok);

#endif
