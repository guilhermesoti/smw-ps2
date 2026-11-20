#ifndef SMW_RITMO_H
#define SMW_RITMO_H

/* Ritmo do jogo (v0.15): 60 passos do jogo por segundo, um desenhado e um
   nao, sempre: 30 imagens por segundo, cada uma 2 quadros da TV (o som e
   feito a cada passo e anda junto com a TV).

   Cada volta do laco principal (main.c) roda os passos sem desenho, o
   desenhado, manda a imagem ao GS e dura pelo menos 2 quadros da TV (se
   tudo coube num quadro, main.c espera o segundo). Uma volta mais longa
   (cena pesada) deixa passos atrasados: a seguinte roda um passo a mais (3
   passos), ate pagar. A divida passa de RITMO_DIVIDA_MAX ou houve uma pausa
   longa (save, troca de video, menu): o resto e esquecido, sem correr. Numa
   cena que passasse sempre de uns 45 ms por imagem o jogo ficaria mais
   lento, como o SNES fica nas cenas pesadas, em vez de pular varios quadros
   seguidos (no console e no PCSX2 o desenho leva uns 13 a 17,5 ms; revisao
   do Codex).

   Da v0.13.7 a v0.14.2 o jogo tentava desenhar todos os quadros quando
   cabiam; no console ficava trocando entre 60 e 30 e saiu (pedido do
   usuario, 20/11/2026). */

typedef struct {
  int divida;  /* passos atrasados */
} Ritmo;

enum { RITMO_PAUSA = 6, RITMO_DIVIDA_MAX = 2 };

void ritmo_inicia(Ritmo *r);

/* Depois de cada volta: quadros da TV que passaram (2 ou mais) e passos
   que ela rodou. Devolve os passos sem desenho da proxima volta (1 ou 2). */
int ritmo_proxima(Ritmo *r, int quadros, int passos);

#endif
