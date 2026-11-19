#include "registro.h"
#include "files.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _EE
#include <timer.h>
#else
#include <time.h>
#endif

/* 16 KB na memoria: a abertura ocupa uns 3 KB e cada save uma linha. O
   comeco (ate smw_log_fixa: boot, ajustes e video; no maximo 8 KB) fica
   sempre; depois, com a memoria cheia, saem as linhas mais antigas do jogo,
   para os erros recentes sempre entrarem. */
enum { LOG_BYTES = 16 * 1024, LOG_FIXO = 8 * 1024, LOG_AVISO = 96, LOG_LINHA = 200,
       LOG_CABECALHO = 160 };

static char texto[LOG_BYTES];
static int usado;
static int fixo = -1;            /* bytes do comeco que nunca saem; -1 = ainda nao */
static unsigned perdidas;        /* linhas que nao entraram (antes de fixar) */
static unsigned tiradas;         /* linhas antigas tiradas depois de fixar */
static char pasta_boot[SMW_PATH];
static unsigned avisou_falha;    /* bit id: falha no lugar id (0 pasta, 1 e 2 cartoes); bit 3: sem lugar */
static bool iniciado;            /* sem smw_log_inicia (testes no PC) nao grava arquivo */
static void (*antes)(void);
static uint64_t inicio_us;

static uint64_t agora_us(void) {
#ifdef _EE
  /* Relogio do sistema do PS2SDK (o mesmo do usleep), em ciclos do barramento. */
  u32 s = 0, us = 0;
  TimerBusClock2USec(GetTimerSystemTime(), &s, &us);
  return (uint64_t)s * 1000000u + us;
#else
  return (uint64_t)clock() * 1000000u / CLOCKS_PER_SEC;
#endif
}

unsigned smw_log_ms(void) { return (unsigned)((agora_us() - inicio_us) / 1000u); }

void smw_log_inicia(const char *boot) {
  usado = 0;
  fixo = -1;
  perdidas = tiradas = 0;
  avisou_falha = 0;
  iniciado = true;
  snprintf(pasta_boot, sizeof(pasta_boot), "%s", boot ? boot : "");
  inicio_us = agora_us();
}

/* No maximo LOG_FIXO bytes ficam fixos (ate o fim de uma linha), para
   sempre sobrar memoria para as linhas do jogo. */
void smw_log_fixa(void) {
  if (fixo >= 0) return;
  fixo = usado;
  if (fixo > LOG_FIXO) {
    const char *p = texto + LOG_FIXO;
    while (p > texto && p[-1] != '\n') --p;
    fixo = (int)(p - texto);
  }
}

void smw_log_antes_de_gravar(void (*funcao)(void)) { antes = funcao; }

/* Abre espaco para k bytes tirando as linhas mais antigas depois do comeco
   fixo. false se nao der. */
static bool abre_espaco(int k) {
  if (fixo < 0 || fixo + k > LOG_BYTES) return false;
  int tirar = 0;
  while (usado - tirar + k > LOG_BYTES && fixo + tirar < usado) {
    const char *fim = memchr(texto + fixo + tirar, '\n', (size_t)(usado - fixo - tirar));
    tirar = fim ? (int)(fim + 1 - (texto + fixo)) : usado - fixo;
    ++tiradas;
  }
  memmove(texto + fixo, texto + fixo + tirar, (size_t)(usado - fixo - tirar));
  usado -= tirar;
  return usado + k <= LOG_BYTES;
}

/* "  12.3 texto": segundos desde o inicio, com um decimo. */
static void acrescenta(unsigned ms, const char *s, size_t n) {
  char linha[LOG_LINHA + 24];
  if (n > LOG_LINHA) n = LOG_LINHA;
  int k = snprintf(linha, sizeof(linha), "%4u.%u %.*s\n", ms / 1000, ms % 1000 / 100, (int)n, s);
  if (k <= 0 || k >= (int)sizeof(linha)) return;
  if (usado + k > LOG_BYTES && !abre_espaco(k)) {
    ++perdidas;
    return;
  }
  memcpy(texto + usado, linha, (size_t)k);
  usado += k;
}

void smw_log(const char *formato, ...) {
  char msg[1024];
  va_list args;
  va_start(args, formato);
  vsnprintf(msg, sizeof(msg), formato, args);
  va_end(args);
  unsigned ms = smw_log_ms();
  for (const char *p = msg; *p;) {
    size_t n = strcspn(p, "\n");
    printf("SMW: %.*s\n", (int)n, p);
    acrescenta(ms, p, n);
    p += n;
    if (*p == '\n') ++p;
  }
}

/* Monta o arquivo (cabecalho com o lugar + linhas) e grava. */
static bool grava_em(const char *caminho) {
  static char arquivo[LOG_CABECALHO + LOG_BYTES + 2 * LOG_AVISO];
  unsigned ms = smw_log_ms();
  const char *cartao = strncmp(caminho, "mc0:", 4) == 0 ? " (Memory Card 1)"
                       : strncmp(caminho, "mc1:", 4) == 0 ? " (Memory Card 2)" : "";
  int n = snprintf(arquivo, LOG_CABECALHO, "SMW_LOG.TXT: %.100s%s, gravado aos %u.%u s\n\n",
                   caminho, cartao, ms / 1000, ms % 1000 / 100);
  if (n <= 0 || n >= LOG_CABECALHO) n = 0;
  int comeco = fixo >= 0 ? fixo : usado;
  memcpy(arquivo + n, texto, (size_t)comeco);
  n += comeco;
  if (tiradas) {
    int k = snprintf(arquivo + n, LOG_AVISO,
                     "       ... %u linhas mais antigas tiradas (memoria do log cheia) ...\n", tiradas);
    if (k > 0 && k < LOG_AVISO) n += k;
  }
  memcpy(arquivo + n, texto + comeco, (size_t)(usado - comeco));
  n += usado - comeco;
  if (perdidas) {
    int k = snprintf(arquivo + n, LOG_AVISO, "       %u linhas nao couberam no log (memoria cheia)\n",
                     perdidas);
    if (k > 0 && k < LOG_AVISO) n += k;
  }
  return smw_write_log(caminho, arquivo, n);
}

/* Tenta o pendrive (ou a pasta do jogo) e, se ele recusar, os Memory Cards,
   como o save. Cada aviso de falha entra uma vez so por lugar. */
bool smw_log_grava(void) {
  char caminho[SMW_PATH];
  int ordem[3], achou = 0;
  if (!iniciado) return false;
  if (antes) antes();
  smw_log_order(ordem);
  for (int i = 0; i < 3; ++i) {
    if (!smw_log_place(pasta_boot, ordem[i], caminho)) continue;
    ++achou;
    if (grava_em(caminho)) return true;
    unsigned bit = 1u << ordem[i];
    if (!(avisou_falha & bit)) {
      avisou_falha |= bit;
      smw_log("Log: nao consegui gravar %s", caminho);
    }
  }
  if (!achou && !(avisou_falha & 8u)) {
    avisou_falha |= 8u;
    smw_log("Log: sem lugar para gravar o SMW_LOG.TXT");
  }
  return false;
}
