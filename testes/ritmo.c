/* Teste do ritmo do jogo (Fonte/ps2/ritmo.c), simulado no PC.
 *
 *   cc -std=gnu11 -I Fonte/ps2 Fonte/ps2/ritmo.c testes/ritmo.c -lm -o /tmp/ritmo && /tmp/ritmo
 *
 * Cada volta do laco principal comeca numa troca de quadro da TV, roda os
 * passos sem desenho (L ms cada) e o desenhado (C ms: passo + envio ao GS)
 * e termina na proxima troca: leva ceil(tempo / quadro) quadros da TV. Os
 * tempos sao os medidos no console (v0.13.6, medidor L2+R2+TRIANGULO):
 * passo sem desenho 2,9 ms; desenhado 13,2 ms no 4:3 e 16,5 ms no 16:9,
 * mais o envio ao GS (aqui 1,5 ms, ainda sem medida no console).
 *
 * Confere: o jogo anda junto com a TV (o som e feito a cada passo); no 4:3
 * desenha quase todos os quadros, e tambem com 16,0 ms constantes (cabe,
 * mesmo perto do limite) e depois de um trecho pesado que ficou leve; uma
 * compensacao que nao cabe nao prende o jogo em 20; no 16:9 desenha um
 * sim, outro nao, sempre com 2
 * passos por quadro mostrado (sem trancos); depois de uma pausa (save) nao
 * acelera nem conta a pausa como custo; e a regra antiga (ate a v0.13.6)
 * ficava presa em 30. */
#include "ritmo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define QUADRO_MS (1000.0 / 59.82)
#define CICLOS_MS 294912.0
static const uint32_t QUADRO_TV = (uint32_t)(QUADRO_MS * CICLOS_MS);

static unsigned semente = 12345;
static double aleatorio(void) {  /* 0..1, sempre a mesma sequencia */
  semente = semente * 1103515245u + 12345u;
  return ((semente >> 8) & 0xFFFF) / 65535.0;
}

typedef struct {
  long quadros, passos, desenhados, voltas_2passos, voltas;
  int maior_atraso;  /* maior (quadros - passos) acumulado */
  long trocas;       /* trocas entre desenhar todos e 1 em 2 */
  long quadros_fim, desenhados_fim;  /* da volta medir_de em diante */
} Resultado;

static long medir_de;  /* volta a partir da qual contam quadros_fim e desenhados_fim */

/* custo(volta) em ms; pausa_em: volta com uma pausa longa (save). */
static Resultado simula(double (*custo)(long), long voltas, double logica, bool antiga, long pausa_em) {
  Ritmo r;
  ritmo_inicia(&r);
  Resultado s = {0};
  int sem_desenho = 0, divida_antiga = 0;
  for (long v = 0; v < voltas; ++v) {
    double c = custo(v);
    double ms = sem_desenho * logica + c;
    int quadros = (int)ceil(ms / QUADRO_MS - 1e-9);
    if (quadros < 1) quadros = 1;
    if (v == pausa_em) quadros = 140, c = 140 * QUADRO_MS;  /* save de 2,3 s dentro do passo */
    int passos = sem_desenho + 1;
    s.quadros += quadros, s.passos += passos, s.desenhados++, s.voltas++;
    if (v >= medir_de) s.quadros_fim += quadros, s.desenhados_fim++;
    if (passos == 2) s.voltas_2passos++;
    if (v != pausa_em) {
      int atraso = (int)(s.quadros - s.passos);
      if (atraso > s.maior_atraso) s.maior_atraso = atraso;
    } else {
      s.passos += quadros - passos;  /* a pausa nao conta como atraso */
    }
    if (antiga) {  /* main.c ate a v0.13.6 */
      divida_antiga += quadros - passos;
      if (divida_antiga < 0) divida_antiga = 0;
      if (divida_antiga > 6) divida_antiga = 0;
      sem_desenho = divida_antiga > 2 ? 2 : divida_antiga;
    } else {
      bool antes = r.pesado;
      sem_desenho = ritmo_proxima(&r, quadros, passos, (uint32_t)(c * CICLOS_MS), QUADRO_TV);
      if (r.pesado != antes) s.trocas++;
    }
  }
  return s;
}

static double custo_43(long v) {  /* 13,2 + 1,5 ms, com variacao e um tranco a cada 10 s */
  return (v % 600 == 300 ? 21.0 : 14.7) + (aleatorio() - 0.5) * 1.0;
}
static double custo_169(long v) { (void)v; return 18.0 + (aleatorio() - 0.5) * 1.0; }
static double custo_limite(long v) { (void)v; return 16.2 + (aleatorio() - 0.5) * 1.4; }
static double custo_troca(long v) { return v < 1800 ? custo_169(v) : custo_43(v); }
static double custo_16(long v) { (void)v; return 16.0; }  /* cabe, com 0,7 ms de folga (revisao do Codex) */
static double custo_18_16(long v) { return v < 1800 ? 18.0 : 16.0; }  /* pesado e depois cabe (Codex) */
static double custo_tranco(long v) { return v == 100 ? 40.0 : 14.7; }  /* um tranco so */
/* Cena que fica mais leve e mais pesada a cada 20 s, passando do limite nos
   picos (o 60/39 alternando visto no console na v0.13.7). */
static double custo_onda(long v) { return 15.2 + 1.6 * sin(v * 6.2832 / 1200.0) + (aleatorio() - 0.5) * 1.0; }

static int falhas;
static void confere(bool ok, const char *o_que) {
  printf("%s %s\n", ok ? "ok    " : "FALHOU", o_que);
  if (!ok) ++falhas;
}

static void mostra(const char *nome, Resultado s) {
  printf("  %-26s %5.1f quadros desenhados/s, jogo %+ld passos da TV, maior atraso %d\n", nome,
         59.82 * s.desenhados / s.quadros, s.passos - s.quadros, s.maior_atraso);
}

int main(void) {
  const long DEZ_MIN = 36000;
  Resultado a = simula(custo_43, DEZ_MIN, 2.9, false, -1);
  Resultado b = simula(custo_43, DEZ_MIN, 2.9, true, -1);
  Resultado c = simula(custo_169, DEZ_MIN, 2.9, false, -1);
  Resultado d = simula(custo_limite, DEZ_MIN, 2.9, false, -1);
  Resultado e = simula(custo_43, 3000, 2.9, false, 1000);
  Resultado f = simula(custo_troca, 3600, 2.9, false, -1);
  Resultado g = simula(custo_16, DEZ_MIN, 2.9, false, -1);
  medir_de = 3600;  /* 1 min depois de ficar mais leve */
  Resultado h = simula(custo_18_16, 7200, 2.9, false, -1);
  medir_de = 1000;
  Resultado i = simula(custo_tranco, 3600, 10.0, false, -1);  /* passo sem desenho de 10 ms (Codex) */
  medir_de = 0;
  Resultado o = simula(custo_onda, DEZ_MIN, 2.9, false, -1);
  mostra("4:3", a);
  mostra("4:3, regra antiga", b);
  mostra("16:9", c);
  mostra("no limite (16,2 ms)", d);
  mostra("4:3 com save no meio", e);
  mostra("16:9 e depois 4:3", f);
  mostra("16,0 ms constantes", g);
  mostra("18 ms e depois 16 ms", h);
  mostra("tranco, logica de 10 ms", i);
  mostra("cena que varia (onda)", o);
  printf("  trocas de modo: 4:3 %ld, 16:9 %ld, no limite %ld em %ld s, onda %ld em %ld s\n", a.trocas,
         c.trocas, d.trocas, d.quadros / 60, o.trocas, o.quadros / 60);

  confere(labs(a.passos - a.quadros) <= 2, "4:3: o jogo anda junto com a TV");
  confere(59.82 * a.desenhados / a.quadros > 58.0, "4:3: desenha quase todos os quadros (mais de 58 por segundo)");
  confere(59.82 * b.desenhados / b.quadros < 31.0, "4:3, regra antiga: ficava em 30 (o defeito)");
  confere(labs(c.passos - c.quadros) <= 2, "16:9: o jogo anda junto com a TV");
  printf("  16:9: %ld voltas fora do ritmo de 2 passos (as do comeco, antes de passar a desenhar 1 em 2)\n",
         c.voltas - c.voltas_2passos);
  confere(c.voltas - c.voltas_2passos <= 6, "16:9: 2 passos por quadro mostrado, tirando o comeco (sem trancos)");
  confere(labs(d.passos - d.quadros) <= 2, "no limite: o jogo anda junto com a TV");
  confere(e.passos - e.quadros >= -2 && e.passos - e.quadros <= 2, "save no meio: nao acelera depois da pausa");
  confere(labs(f.passos - f.quadros) <= 2, "16:9 e depois 4:3: o jogo anda junto com a TV");
  confere(59.82 * g.desenhados / g.quadros > 59.5 && labs(g.passos - g.quadros) <= 2,
          "16,0 ms constantes: desenha todos (nao cai para 30 sem precisar)");
  confere(59.82 * h.desenhados_fim / h.quadros_fim > 59.0 && labs(h.passos - h.quadros) <= 2,
          "18 ms e depois 16 ms: volta a desenhar todos (voltas de teste)");
  confere(59.82 * i.desenhados_fim / i.quadros_fim > 59.0 && labs(i.passos - i.quadros) <= 2,
          "tranco com logica de 10 ms: nao fica preso em 20 quadros");
  confere(c.trocas <= 2, "16:9: nao fica trocando de modo (sem voltas de teste quando nao cabe)");
  confere(d.trocas * 1800 <= d.quadros, "no limite: no maximo 1 troca de modo a cada 30 s");
  confere(o.trocas * 600 <= o.quadros && labs(o.passos - o.quadros) <= 2,
          "cena que varia: o jogo anda junto com a TV e troca de modo no maximo a cada 10 s");
  confere(a.maior_atraso <= 2 && c.maior_atraso <= 2 && d.maior_atraso <= 3 && e.maior_atraso <= 2 &&
          f.maior_atraso <= 3 && g.maior_atraso <= 1 && h.maior_atraso <= 3 && i.maior_atraso <= 3 &&
          o.maior_atraso <= 3,
          "atraso nunca passa de 3 passos");

  /* Depois de trocar 16:9 -> 4:3, volta a desenhar todos em poucos segundos. */
  Ritmo r;
  ritmo_inicia(&r);
  int sem = 0, volta_leve = -1;
  for (int v = 0; v < 3600; ++v) {
    double cst = custo_troca(v), ms = sem * 2.9 + cst;
    int q = (int)ceil(ms / QUADRO_MS - 1e-9);
    sem = ritmo_proxima(&r, q < 1 ? 1 : q, sem + 1, (uint32_t)(cst * CICLOS_MS), QUADRO_TV);
    if (v >= 1800 && !r.pesado && volta_leve < 0) volta_leve = v - 1800;
  }
  printf("  troca 16:9 -> 4:3: voltou a desenhar todos depois de %d voltas\n", volta_leve);
  confere(volta_leve >= 0 && volta_leve <= 120, "16:9 -> 4:3: volta a desenhar todos em ate 4 s");

  /* Depois de uma cena no limite o freio aumenta o intervalo da volta ao
     leve; uma cena bem mais leve (12 ms: o passo sem desenho e o desenhado
     cabem num quadro, e a volta adiantada serve de teste) ou a troca de tela
     (4:3, 14,7 ms, com ritmo_cena_nova como no main.c) nao pode deixar o
     jogo em 30 por 1 minuto (revisao do Antigravity). */
  for (int caso = 0; caso < 2; ++caso) {
    ritmo_inicia(&r);
    sem = 0;
    int depois = -1;
    unsigned intervalo = 0;
    for (long v = 0; v < 24000; ++v) {
      if (v == 20000) {
        intervalo = r.pesado ? r.intervalo : 0;
        if (caso == 1) ritmo_cena_nova(&r);
      }
      double cst = v < 20000 ? custo_limite(v) : caso == 0 ? 12.0 : custo_43(v);
      double ms = sem * 2.9 + cst;
      int q = (int)ceil(ms / QUADRO_MS - 1e-9);
      sem = ritmo_proxima(&r, q < 1 ? 1 : q, sem + 1, (uint32_t)(cst * CICLOS_MS), QUADRO_TV);
      if (v >= 20000 && !r.pesado && depois < 0) depois = (int)(v - 20000);
    }
    printf("  %s depois da cena no limite (intervalo %u voltas): desenha todos depois de %d voltas\n",
           caso == 0 ? "cena de 12 ms" : "troca para o 4:3", intervalo, depois);
    confere(intervalo > 90 && depois >= 0 && depois <= 150,
            caso == 0 ? "cena bem mais leve depois do freio: volta a desenhar todos em ate 5 s"
                      : "troca de tela depois do freio: volta a desenhar todos em ate 5 s");
  }

  printf(falhas ? "RITMO: %d FALHA(S)\n" : "RITMO: OK\n", falhas);
  return falhas ? 1 : 0;
}
