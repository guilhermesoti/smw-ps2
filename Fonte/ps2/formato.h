#ifndef SMW_FORMATO_H
#define SMW_FORMATO_H
#include <stdbool.h>
#include <stdint.h>

/* Telas antes do jogo desenhadas num quadro de 256 x 224 (formato.c):
   carregando, AJUSTES, o teste do modo de video novo e a lista de
   trapacas. Desde a v0.14 o quadro tem a resolucao do SNES: mostrado em
   512 ou 1280 pontos por linha, cada pixel fica do tamanho de um pixel do
   jogo, e o fundo pode ser uma cena do proprio jogo (smw_capture_scene,
   lida da ROM do jogador) sem pixels desiguais.
   Cada pixel e 0x00RRGGBB. fonte: a fonte 8x8 do PS2SDK. fundo: uma cena
   256 x 224 no mesmo formato, ou NULL (paisagem desenhada aqui). */
enum { FORMATO_W = 256, FORMATO_H = 224 };

/* Linhas da tela AJUSTES, de cima para baixo. */
enum { AJUSTE_VIDEO, AJUSTE_TELA, AJUSTE_TAMANHO, AJUSTE_IDIOMA, AJUSTE_TRAPACAS, AJUSTE_JOGAR, AJUSTE_LINHAS };

typedef struct {
  int linha;     /* AJUSTE_* marcada */
  int video;     /* 0 = 240p, 1 = 480p, 2 = 480i (os mesmos numeros de files.h) */
  int tela;      /* 0 = 4:3, 1 = 16:9 (escolha guardada; o 16:9 so vale no 480p) */
  int largura, altura;  /* tamanho na TV, % (v0.14.1) */
  bool ajustando;       /* no ajuste do tamanho: as setas mudam a imagem */
  int idioma;    /* 0 = portugues, 1 = ingles (os textos da tela tambem) */
  int segundos;  /* comeca sozinho em tantos segundos; < 0 sem contagem */
  int trapacas, trapacas_total;  /* ligadas / no arquivo (0 = sem arquivo) */
  const char *aviso;     /* linha em laranja no rodape, ou NULL */
  const char *situacao;  /* linha cinza no rodape (onde fica o save), ou NULL */
} Ajustes;

void formato_ajustes(uint32_t *px, const unsigned char *fonte, const Ajustes *a, int frame,
                     const uint32_t *fundo);

/* Enquanto carrega o jogo: etapa passo de passos (antes de achar a ROM:
   so desenho nosso). */
void formato_carregando(uint32_t *px, const unsigned char *fonte, const char *etapa, int passo,
                        int passos, int frame);

/* No modo de video novo: "A imagem apareceu?" com a contagem. */
void formato_confirma(uint32_t *px, const unsigned char *fonte, int video, int idioma, int segundos,
                      int frame, const uint32_t *fundo);

/* Lista de trapacas (TRAPACAS.TXT): uma por linha, ligada ou nao. */
typedef struct {
  const char *nome;
  bool ligada, valida;  /* valida = algum codigo foi aceito */
} FormatoTrapaca;

typedef struct {
  const FormatoTrapaca *itens;
  int n, marcada, idioma;
  const char *lugar;  /* de onde veio o arquivo, ou NULL se nao tem */
  bool no_jogo;       /* aberta com L2+R2+SELECT (o jogo esta pausado) */
} ListaTrapacas;

void formato_trapacas(uint32_t *px, const unsigned char *fonte, const ListaTrapacas *l, int frame,
                      const uint32_t *fundo);

/* Abertura (intro.c): o titulo do port e a tela "Apoie o projeto" com o QR
   do Pix (qr: n linhas de n caracteres '0'/'1'). */
void formato_titulo(uint32_t *px, const unsigned char *fonte, const char *versao, const uint32_t *fundo);
void formato_apoio(uint32_t *px, const unsigned char *fonte, int frame, bool pode_seguir,
                   const char (*qr)[46], int n, const uint32_t *fundo);

#endif
