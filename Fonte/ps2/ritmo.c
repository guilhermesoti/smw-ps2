#include "ritmo.h"

void ritmo_inicia(Ritmo *r) { r->divida = 0; }

int ritmo_proxima(Ritmo *r, int quadros, int passos) {
  if (quadros > RITMO_PAUSA) {  /* pausa longa: segue sem correr para alcancar */
    r->divida = 0;
    return 1;
  }
  r->divida += quadros - passos;
  if (r->divida < 0) r->divida = 0;
  if (r->divida > RITMO_DIVIDA_MAX) r->divida = RITMO_DIVIDA_MAX;
  return r->divida > 0 ? 2 : 1;
}
