#ifndef SMW_INTRO_H
#define SMW_INTRO_H
#include <gsKit.h>
#include <stdbool.h>

/* Abertura da Retrogames e tela "Apoie o projeto" (intro.c). Chame depois de
   iniciar o video e antes do jogo; volta quando o jogador aperta X ou START
   na tela de apoio (ou depois de 20 s).
   tela: com *tela < 0, mostra a tela FORMATO DA TELA depois da vinheta e
   devolve nela 0 (4:3) ou 1 (16:9). Retorna true quando o jogador confirmou
   a escolha (para gravar); false se ela nao apareceu ou se o tempo acabou. */
bool smw_intro(GSGLOBAL *gs, bool p480, bool audio_ok, const char *version, int *tela);

#endif
