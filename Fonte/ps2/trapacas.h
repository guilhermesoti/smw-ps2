#ifndef SMW_TRAPACAS_H
#define SMW_TRAPACAS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Trapacas (v0.14): codigos de memoria no estilo Pro Action Replay, lidos do
   TRAPACAS.TXT. O port e o jogo reescrito em C, mas a RAM do jogo e a mesma
   do SNES (g_ram = $7E0000-$7FFFFF), entao um codigo que escreve um valor na
   RAM a cada quadro funciona igual. Codigos Game Genie mudam a ROM (o
   programa do jogo) e nao funcionam aqui.

   Formato do arquivo (UTF-8, uma trapaca por linha):
     # comentario
     Nome da trapaca = 7E0DBE:62
     Outra = 7E0F31:09+7E0F32:09+7E0F33:09
   Cada codigo: 6 algarismos hexadecimais do endereco e 2 do valor, com um
   ':' ou '=' no meio, ou os 8 juntos (7E0DBE62). Separados por '+', ',' ou
   espaco. Enderecos aceitos: $7E0000-$7FFFFF e o espelho $0000-$1FFF dos
   bancos $00-$3F e $80-$BF. O resto (ROM, SRAM, registros) e recusado. */

/* TRAPACAS_BYTES: o jogo le no maximo isso do TRAPACAS.TXT (main.c). */
enum { TRAPACA_MAX = 64, TRAPACA_NOME = 28, TRAPACA_CODIGOS = 16, TRAPACAS_BYTES = 16384 };

typedef struct {
  uint32_t ram;  /* posicao em g_ram (0 a 0x1FFFF) */
  uint8_t valor;
} TrapacaCodigo;

typedef struct {
  char nome[TRAPACA_NOME * 2 + 1];  /* ate TRAPACA_NOME letras, em UTF-8 */
  TrapacaCodigo codigo[TRAPACA_CODIGOS];
  int codigos;      /* aceitos */
  int recusados;    /* codigos da linha que nao deram */
  int linha;        /* no arquivo, a partir de 1 */
  bool ligada;
} Trapaca;

typedef struct {
  Trapaca t[TRAPACA_MAX];
  int n;
  int ignoradas;  /* linhas a mais que TRAPACA_MAX */
} Trapacas;

/* Quanto do texto lido usar: lido com ate TRAPACAS_BYTES + 1 bytes, se
   passou de TRAPACAS_BYTES o arquivo foi cortado e a linha cortada sai (uma
   trapaca de varios codigos nao pode ficar pela metade). */
size_t trapacas_corte(const char *texto, size_t n);

/* Le o texto do TRAPACAS.TXT (n bytes; nao precisa terminar em 0). Tudo
   desligado. */
void trapacas_le(Trapacas *t, const char *texto, size_t n);

/* Liga as trapacas do texto do TRAPACAS.LIG (que trapacas_texto_ligadas
   escreve): cada linha liga a primeira trapaca ainda desligada com o mesmo
   nome e os mesmos codigos. Volta quantas ligou. */
int trapacas_liga_salvas(Trapacas *t, const char *texto, size_t n);

/* O texto do TRAPACAS.LIG: as ligadas, uma por linha, no formato do
   TRAPACAS.TXT ("Nome = 7E0DBE:62+7E0F31:09"). Com o nome e os codigos,
   duas trapacas com o mesmo nome nao voltam ligadas juntas. */
int trapacas_texto_ligadas(const Trapacas *t, char *texto, size_t tamanho);

int trapacas_ligadas(const Trapacas *t);

/* Escreve na RAM do jogo os valores das ligadas (chamado a cada passo). */
void trapacas_aplica(const Trapacas *t, uint8_t *ram);

#endif
