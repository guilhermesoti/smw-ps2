#ifndef SMW_FORMATO_H
#define SMW_FORMATO_H
#include <stdint.h>

/* Telas da abertura desenhadas num quadro de 320 x 224 (formato.c):
   carregando, AJUSTES (video, tela, suavizacao, scanlines, idioma) e o
   teste do modo de video novo. Cada pixel e 0x00RRGGBB. fonte: a fonte 8x8 do PS2SDK. */
enum { FORMATO_W = 320, FORMATO_H = 224 };

/* Linhas da tela AJUSTES, de cima para baixo. */
enum { AJUSTE_VIDEO, AJUSTE_TELA, AJUSTE_SUAVE, AJUSTE_SCAN, AJUSTE_IDIOMA, AJUSTE_JOGAR, AJUSTE_LINHAS };

typedef struct {
  int linha;     /* AJUSTE_* marcada */
  int video;     /* 0 = 240p, 1 = 480p, 2 = 480i (os mesmos numeros de files.h) */
  int tela;      /* 0 = 4:3, 1 = 16:9 */
  int suave;     /* 0 = desligada, 1 = media, 2 = forte */
  int scanlines; /* 0 = desligadas, 1 = ligadas (so no 480p) */
  int idioma;    /* 0 = portugues, 1 = ingles (os textos da tela tambem) */
  int segundos;  /* comeca sozinho em tantos segundos; < 0 sem contagem */
  const char *aviso;     /* linha em laranja no rodape, ou NULL */
  const char *situacao;  /* linha cinza no rodape (onde fica o save), ou NULL */
} Ajustes;

void formato_ajustes(uint32_t *px, const unsigned char *fonte, const Ajustes *a, int frame);

/* Enquanto carrega o jogo: etapa passo de passos. */
void formato_carregando(uint32_t *px, const unsigned char *fonte, const char *etapa, int passo,
                        int passos, int frame);

/* No modo de video novo: "A imagem apareceu?" com a contagem. */
void formato_confirma(uint32_t *px, const unsigned char *fonte, int video, int idioma, int segundos,
                      int frame);

#endif
