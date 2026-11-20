#include "ritmo.h"

/* PAUSA: mais quadros da TV que isso numa volta e uma pausa longa (save,
   troca de modo de video): o jogo segue sem correr para alcancar.
   ATRASO / ATRASOS_MAX: cada volta atrasada no leve soma ATRASO a um nivel
   que perde 1 por volta; passou de ATRASOS_MAX (3 atrasos em umas 30
   voltas), pesado.
   SAIDA_PCT / VOLTAS_FOLGA: no pesado, custo medio abaixo de SAIDA_PCT% do
   quadro da TV em VOLTAS_FOLGA voltas seguidas: volta ao leve.
   PROVA_*: no pesado com custo medio abaixo de 100% (o quadro desenhado
   pode caber), uma volta de teste so com o passo desenhado a cada
   intervalo; PROVAS_OK seguidas que couberam: volta ao leve. Se o leve nao
   durar LEVE_CURTO voltas, o intervalo dobra (ate PROVA_MAX): no limite, o
   jogo nao fica trocando de modo toda hora. */
enum {
  PAUSA = 6, ATRASO = 30, ATRASOS_MAX = 60, SAIDA_PCT = 93, VOLTAS_FOLGA = 60,
  PROVA_INICIAL = 90, PROVA_MAX = 1800, PROVAS_OK = 2, LEVE_CURTO = 600
};

void ritmo_inicia(Ritmo *r) {
  r->pesado = false;
  r->divida = 0;
  r->media = 0;
  r->atrasos = 0;
  r->folgadas = 0;
  r->desde_prova = 0;
  r->provas_ok = 0;
  r->intervalo = PROVA_INICIAL;
  r->voltas_leve = 0;
}

static void entra_no_pesado(Ritmo *r) {
  /* O leve durou pouco: espera mais antes de testar de novo. */
  if (r->voltas_leve < LEVE_CURTO) r->intervalo = r->intervalo * 2 > PROVA_MAX ? PROVA_MAX : r->intervalo * 2;
  else r->intervalo = PROVA_INICIAL;
  r->pesado = true;
  r->atrasos = r->folgadas = r->desde_prova = r->provas_ok = 0;
}

static void volta_ao_leve(Ritmo *r) {
  r->pesado = false;
  r->atrasos = 0;
  r->voltas_leve = 0;
}

int ritmo_proxima(Ritmo *r, int quadros, int passos, uint32_t custo, uint32_t quadro_tv) {
  if (quadros > PAUSA) {
    r->divida = 0;
    return r->pesado ? 1 : 0;
  }
  const int divida_antes = r->divida;
  r->divida += quadros - passos;
  if (r->divida > PAUSA) r->divida = PAUSA;
  if (r->divida < -2) r->divida = -2;
  /* Uma volta com o maximo de passos (3) que nao diminuiu a divida: pagar
     nao cabe (os passos sem desenho estao pesados). Fica com o atraso, que
     o buffer do som cobre, em vez de rodar 3 passos a cada 3 quadros. */
  const bool compensacao_falhou = passos == 3 && divida_antes > 0 && r->divida >= divida_antes;
  if (compensacao_falhou) r->divida = 0;

  r->media = r->media ? r->media - r->media / 8 + custo / 8 : custo;
  const uint32_t um_por_cento = quadro_tv / 100;
  if (!r->pesado) {
    ++r->voltas_leve;
    /* Atraso: so o passo desenhado e nao coube; ou uma volta que compensava
       (3 passos) e nao diminuiu a divida. */
    bool atrasou = passos == 1 ? quadros > 1 : compensacao_falhou;
    if (atrasou) r->atrasos += ATRASO;
    else if (r->atrasos) --r->atrasos;
    if (r->atrasos > ATRASOS_MAX) entra_no_pesado(r);
  } else {
    if (r->media < SAIDA_PCT * um_por_cento) {
      if (++r->folgadas >= VOLTAS_FOLGA) volta_ao_leve(r);
    } else {
      r->folgadas = 0;
    }
    if (r->pesado && passos == 1) {  /* volta de teste (ou adiantada) */
      if (quadros == 1) {
        if (++r->provas_ok >= PROVAS_OK) volta_ao_leve(r);
      } else {
        r->provas_ok = 0;
      }
    }
  }

  if (r->pesado) {
    if (r->media < 100 * um_por_cento && r->divida <= 0 && ++r->desde_prova >= r->intervalo) {
      r->desde_prova = 0;
      return 0;  /* volta de teste: so o passo desenhado */
    }
    /* 2 passos por volta (a volta leva 2 quadros da TV), mais ou menos o
       que estiver atrasado. */
    int sem_desenho = 1 + r->divida;
    return sem_desenho < 0 ? 0 : sem_desenho > 2 ? 2 : sem_desenho;
  }
  /* leve: 1 passo por quadro; atrasado, uma volta de 3 passos (2 quadros
     da TV) paga um passo. */
  return r->divida > 0 ? 2 : 0;
}
