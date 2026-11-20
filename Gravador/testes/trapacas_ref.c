/* Leitor do jogo (Fonte/ps2/trapacas.c) para o testar_nucleo.js comparar com
   o do Gravador (conferirTrapacas, em src/nucleo.js).

   Uso: trapacas_ref <arquivo>
   Le como o main.c (trapacas_carrega: ate 16 KB + 1 e trapacas_corte) e escreve uma linha
   por trapaca: "T linha nome_em_hex recusados ram:valor ram:valor ...", e no
   fim "N total ignoradas". */
#include "trapacas.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  static char texto[TRAPACAS_BYTES + 1];
  static Trapacas t;
  if (argc != 2) {
    fprintf(stderr, "uso: trapacas_ref <arquivo>\n");
    return 2;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    perror(argv[1]);
    return 2;
  }
  size_t n = fread(texto, 1, sizeof(texto), f);
  fclose(f);
  trapacas_le(&t, texto, trapacas_corte(texto, n));
  for (int k = 0; k < t.n; ++k) {
    const Trapaca *tr = &t.t[k];
    printf("T %d ", tr->linha);
    for (const unsigned char *c = (const unsigned char *)tr->nome; *c; ++c) printf("%02x", *c);
    printf(" %d", tr->recusados);
    for (int c = 0; c < tr->codigos; ++c) printf(" %x:%x", (unsigned)tr->codigo[c].ram, tr->codigo[c].valor);
    printf("\n");
  }
  printf("N %d %d\n", t.n, t.ignoradas);
  return 0;
}
