#ifndef SMW_RITMO_H
#define SMW_RITMO_H
#include <stdbool.h>
#include <stdint.h>

/* Ritmo do jogo (v0.13.7): quantos passos do jogo rodar sem desenhar antes
   do proximo quadro desenhado, para o jogo andar junto com a TV (60 passos
   por segundo; o som e feito a cada passo e nao pode ficar para tras).

   Cada volta do laco principal roda os passos sem desenho, um passo
   desenhado e manda a imagem ao GS; termina na troca de quadro da TV.

   - leve: desenha todos (60 por segundo). Se uma volta atrasa, a proxima
     roda 2 passos sem desenho (3 passos em 2 quadros da TV) e o jogo volta
     ao ritmo.
   - pesado: desenha um sim, outro nao: 2 passos por volta, sempre iguais
     (sem trancos), e compensa atrasos.

   Entra no pesado quando o leve de fato atrasa: 3 voltas atrasadas em umas
   30 (so o passo desenhado e levou mais de um quadro da TV, ou uma volta de
   compensacao que nao diminuiu a divida). Volta ao leve pelo custo medido
   do quadro desenhado (passo + envio ao GS, sem a espera da TV): media
   abaixo de 93% de um quadro da TV num intervalo de voltas seguidas; ou,
   com a media abaixo de 100%, por voltas de teste so com o passo desenhado
   (uma por intervalo; 2 seguidas que couberam). O intervalo comeca em 3 s
   e dobra (ate 1 minuto) cada vez que o leve dura pouco, para nao ficar
   trocando (v0.13.8); a troca 4:3 / 16:9 zera o freio.

   Ate a v0.13.6 so havia a compensacao: depois do primeiro atraso, cada
   volta rodava 1 passo sem desenho + 1 desenhado; com 2,9 + 13,2 ms mais o
   envio, isso nunca cabia num quadro da TV e o jogo ficava em 30 quadros
   por segundo para sempre, mesmo no 4:3. */

typedef struct {
  bool pesado;
  int divida;         /* passos atrasados (negativo: adiantados) */
  uint32_t media;     /* custo medio do quadro desenhado, em ciclos do EE */
  unsigned atrasos;   /* nivel de atrasos no leve */
  unsigned folgadas;  /* voltas seguidas com folga, no pesado */
  /* no pesado: voltas de teste; intervalo = voltas de teste e de folga
     seguidas para voltar ao leve (cresce com o freio) */
  unsigned desde_prova, provas_ok, intervalo;
  unsigned voltas_leve;  /* voltas desde que voltou ao leve */
  bool voltou;           /* ja voltou do pesado ao leve alguma vez */
} Ritmo;

void ritmo_inicia(Ritmo *r);

/* Cena nova (troca 4:3 / 16:9): esquece o freio da volta ao leve. */
void ritmo_cena_nova(Ritmo *r);

/* Depois de cada volta: quadros da TV que passaram, passos que a volta
   rodou (sem desenho + 1), custo do quadro desenhado e a duracao de um
   quadro da TV (os dois em ciclos do EE). Devolve os passos sem desenho da
   proxima volta (0 a 2). */
int ritmo_proxima(Ritmo *r, int quadros, int passos, uint32_t custo, uint32_t quadro_tv);

#endif
