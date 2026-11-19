#ifndef SMW_PORT_REGISTRO_H
#define SMW_PORT_REGISTRO_H
#include <stdbool.h>

/* Log do console: SMW_LOG.TXT, refeito a cada vez que o jogo abre.
   - Aberto como aplicativo (APPS/SMW no pendrive): ao lado do ELF.
   - Aberto pela ISO (OPL): o pendrive fica com o OPL, entao vai para o
     Memory Card onde fica o save (pasta SMWPS2).
   As linhas ficam na memoria e o arquivo inteiro e gravado so quando
   smw_log_grava() e chamado (poucos momentos: video, saves, erros), porque
   cada gravacao para o jogo por um instante. */

/* Comeca o log. boot: pasta do ELF (usada antes de achar o jogo). */
void smw_log_inicia(const char *boot);
/* Acrescenta uma linha (com o tempo desde o inicio). Tambem vai para o
   printf. Texto com varias linhas vira varias linhas no log. */
void smw_log(const char *formato, ...) __attribute__((format(printf, 1, 2)));
/* Marca o fim do comeco (boot, ajustes, video): essa parte fica sempre no
   arquivo. Depois, com a memoria do log cheia, saem as linhas mais antigas
   do jogo. Antes disso, o que nao cabe nao entra. */
void smw_log_fixa(void);
/* Grava o arquivo agora. false se nao houve onde gravar. */
bool smw_log_grava(void);
/* Chamada antes de cada gravacao (o jogo deixa o som mudo). */
void smw_log_antes_de_gravar(void (*funcao)(void));
/* Milissegundos desde smw_log_inicia (para medir etapas). */
unsigned smw_log_ms(void);
#endif
