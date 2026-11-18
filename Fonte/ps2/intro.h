#ifndef SMW_INTRO_H
#define SMW_INTRO_H
#include <gsKit.h>
#include <stdbool.h>

/* Abertura da Retrogames e tela "Apoie o projeto" (intro.c). Chame depois de
   iniciar o video e antes do jogo; volta quando o jogador aperta X ou START
   na tela de apoio (ou depois de 20 s). */
void smw_intro(GSGLOBAL *gs, bool p480, bool audio_ok, const char *version);

#endif
