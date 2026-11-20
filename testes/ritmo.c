/* Teste do ritmo do jogo (Fonte/ps2/ritmo.c, v0.15), simulado no PC.
 *
 *   cc -std=gnu11 -I Fonte/ps2 Fonte/ps2/ritmo.c testes/ritmo.c -lm -o /tmp/ritmo && /tmp/ritmo
 *
 * Cada volta do laco principal (main.c) comeca numa troca de quadro da TV,
 * roda os passos sem desenho (L ms cada) e o desenhado (C ms: passo + envio
 * ao GS) e termina na proxima troca; se tudo coube num quadro, espera o
 * segundo. Tempos medidos no console (v0.13.6): passo sem desenho 2,9 ms,
 * desenhado 13,2 ms no 4:3 e 16,5 ms no 16:9, mais o envio.
 *
 * Confere: o jogo anda junto com a TV (passos = quadros, no fim); desenha um
 * sim, outro nao (30 por segundo) nas cenas que cabem em 2 quadros, sem
 * trancos; um pico isolado e pago na volta seguinte; numa cena pesada que
 * nunca cabe o jogo nao fica para tras mais que RITMO_DIVIDA_MAX passos; e
 * depois de uma pausa longa (save) nao corre para alcancar. */
#include "ritmo.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define QUADRO_MS (1000.0 / 59.82)
#define LOGICA_MS 2.9

static int falhas;
#define CONFERE(c) do { if (!(c)) { printf("FALHOU linha %d: %s\n", __LINE__, #c); ++falhas; } } while (0)

typedef struct {
  long quadros, passos, desenhados, voltas_fora_de_2;
} Resultado;

/* custo(volta): ms do passo desenhado + envio; pausa_em: volta com um save
   de 140 quadros da TV. */
static Resultado simula(double (*custo)(long), long voltas, long pausa_em) {
  Ritmo r;
  ritmo_inicia(&r);
  Resultado s = {0};
  int sem_desenho = 1;
  for (long v = 0; v < voltas; ++v) {
    double ms = sem_desenho * LOGICA_MS + custo(v);
    int quadros = (int)ceil(ms / QUADRO_MS - 1e-9);
    if (quadros < 2) quadros = 2;  /* main.c espera o segundo quadro */
    int passos = sem_desenho + 1;
    if (v == pausa_em) quadros = 140;
    else s.quadros += quadros, s.passos += passos;
    s.desenhados++;
    if (quadros != 2 && v != pausa_em) s.voltas_fora_de_2++;
    sem_desenho = ritmo_proxima(&r, quadros, passos);
    CONFERE(sem_desenho == 1 || sem_desenho == 2);
  }
  return s;
}

static double leve(long v) { (void)v; return 13.2 + 1.5; }
static double larga(long v) { (void)v; return 16.5 + 1.5; }
static double pico(long v) { return v % 100 == 50 ? 40.0 : 13.2 + 1.5; }
static double pesada(long v) { (void)v; return 36.0; }
static double variada(long v) { return 10.0 + 22.0 * fabs(sin(v * 0.37)); }

int main(void) {
  /* 4:3 e 16:9: 2 quadros por volta, 30 imagens por segundo, sem trancos */
  Resultado a = simula(leve, 1800, -1), b = simula(larga, 1800, -1);
  CONFERE(a.quadros == 2 * 1800 && a.passos == a.quadros && a.voltas_fora_de_2 == 0);
  CONFERE(b.quadros == 2 * 1800 && b.passos == b.quadros && b.voltas_fora_de_2 == 0);
  printf("4:3: %ld imagens em %ld quadros (%.1f por segundo)\n", a.desenhados, a.quadros,
         a.desenhados * 59.82 / a.quadros);
  /* pico isolado de 40 ms (3 quadros): a volta seguinte paga o passo */
  Resultado c = simula(pico, 1800, -1);
  CONFERE(c.passos == c.quadros);
  /* cena que nunca cabe em 2 quadros: o jogo nao fica para tras mais que o limite */
  Resultado d = simula(pesada, 600, -1);
  CONFERE(d.quadros - d.passos <= RITMO_DIVIDA_MAX && d.quadros - d.passos >= 0);
  /* custos variados (10 a 32 ms): o jogo anda junto com a TV */
  Resultado e = simula(variada, 3000, -1);
  CONFERE(e.quadros - e.passos <= RITMO_DIVIDA_MAX && e.quadros - e.passos >= 0);
  printf("variada: %ld passos em %ld quadros\n", e.passos, e.quadros);
  /* pausa longa (save): nao corre depois, e o resto segue igual */
  Resultado f = simula(leve, 1800, 900);
  CONFERE(f.passos == f.quadros && f.voltas_fora_de_2 == 0);
  /* a funcao sozinha */
  Ritmo r;
  ritmo_inicia(&r);
  CONFERE(ritmo_proxima(&r, 2, 2) == 1);
  CONFERE(ritmo_proxima(&r, 3, 2) == 2 && r.divida == 1);
  CONFERE(ritmo_proxima(&r, 2, 3) == 1 && r.divida == 0);
  CONFERE(ritmo_proxima(&r, 5, 2) == 2 && r.divida == RITMO_DIVIDA_MAX);
  CONFERE(ritmo_proxima(&r, RITMO_PAUSA + 1, 3) == 1 && r.divida == 0);
  puts(falhas ? "RITMO: FALHOU" : "RITMO: OK");
  return falhas != 0;
}
