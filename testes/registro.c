/* Testes do log do console (SMW_LOG.TXT, Fonte/ps2/registro.c).

   O pendrive e uma pasta temporaria de verdade; a recusa em gravar e
   simulada com --wrap=fopen. Os Memory Cards e o relogio sao simulados
   (mocks/mc_mock.h, mocks/timer.h).

   Compilar a partir da pasta testes/:
     cc -std=gnu11 -D_EE -Imocks -I../Fonte/ps2 $(pkg-config --cflags minizip zlib) \
        registro.c -Wl,--wrap=fopen $(pkg-config --libs minizip zlib) -o /tmp/smw-registro */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "timer.h"
#include "../Fonte/ps2/files.c"
#include "../Fonte/ps2/registro.c"
#include "mc_mock.h"

static char usb_dir[256];
static int usb_denied;
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) {
  if (usb_denied && strchr(mode, 'w') && !strncmp(path, usb_dir, strlen(usb_dir))) {
    errno = 7;
    return NULL;
  }
  return __real_fopen(path, mode);
}

static char lido[LOG_CABECALHO + LOG_BYTES + 2 * LOG_AVISO + 1];
static long le_usb(void) {
  char p[512];
  snprintf(p, sizeof(p), "%sSMW_LOG.TXT", usb_dir);
  lido[0] = '\0';
  FILE *f = __real_fopen(p, "rb");
  if (!f) return -1;
  long n = (long)fread(lido, 1, sizeof(lido) - 1, f);
  lido[n] = '\0';
  fclose(f);
  return n;
}
static void apaga_usb(void) {
  char p[512];
  snprintf(p, sizeof(p), "%sSMW_LOG.TXT", usb_dir);
  unlink(p);
}
static const char *le_cartao(int port) {
  const McFile *f = mc_get(port, "/SMWPS2/SMW_LOG.TXT");
  if (!f || f->size < 0 || f->size >= (int)sizeof(lido)) return NULL;
  memcpy(lido, f->data, (size_t)f->size);
  lido[f->size] = '\0';
  return lido;
}
static int conta(const char *texto, const char *trecho) {
  int n = 0;
  for (const char *p = texto; (p = strstr(p, trecho)); p += strlen(trecho)) ++n;
  return n;
}

/* Linhas na memoria do log (so a parte em uso). */
static int conta_memoria(const char *trecho) {
  static char copia[LOG_BYTES + 1];
  memcpy(copia, texto, (size_t)usado);
  copia[usado] = '\0';
  return conta(copia, trecho);
}

static int mudos;
static void som_mudo(void) { ++mudos; }

/* Liga o PS2: estado zerado. pasta: onde o jogo foi achado ("" = ISO). */
static void liga(const char *pasta, const char *boot) {
  active_store = -1;
  memory_card_ok = true;
  snprintf(folder_dir, sizeof(folder_dir), "%s", pasta);
  teste_relogio = 147456000ull * 30;  /* o console ja estava ligado */
  smw_log_inicia(boot);
  smw_log_antes_de_gravar(som_mudo);
  mudos = 0;
}

static int checks;
#define CHECK(c) do { assert(c); ++checks; } while (0)

int main(void) {
  snprintf(usb_dir, sizeof(usb_dir), "/tmp/smw_log_%d/", (int)getpid());
  mkdir(usb_dir, 0777);

  /* 0. Sem smw_log_inicia (testes do motor no PC, que nao rodam o main.c):
        nada e gravado, nem na pasta do jogo. */
  mc_reset();
  memory_card_ok = true;
  snprintf(folder_dir, sizeof(folder_dir), "%s", usb_dir);
  smw_log("Antes de iniciar");
  CHECK(!smw_log_grava() && le_usb() < 0 && !mc_get(0, "/SMWPS2/SMW_LOG.TXT"));

  /* 1. Aplicativo no pendrive: ao lado do jogo, com o tempo de cada linha. */
  mc_reset();
  liga(usb_dir, usb_dir);
  teste_relogio += 147456000ull * 3 / 2;  /* +1,5 s */
  smw_log("Linha um");
  smw_log("A\nB\n");
  CHECK(smw_log_grava() && mudos == 1);
  CHECK(le_usb() > 0);
  CHECK(!strncmp(lido, "SMW_LOG.TXT: ", 13) && strstr(lido, "SMW_LOG.TXT, gravado aos 1.5 s\n\n"));
  CHECK(strstr(lido, "\n\n   1.5 Linha um\n   1.5 A\n   1.5 B\n") != NULL);
  CHECK(!mc_get(0, "/SMWPS2/SMW_LOG.TXT"));
  /* Gravar de novo refaz o arquivo inteiro (nao repete as linhas). */
  teste_relogio += 147456000ull * 62;
  smw_log("Depois de um minuto");
  CHECK(smw_log_grava() && mudos == 2);
  le_usb();
  CHECK(conta(lido, "Linha um") == 1 && strstr(lido, "  63.5 Depois de um minuto\n"));

  /* 2. Linha comprida: corta em 200 caracteres. */
  char comprida[400];
  memset(comprida, 'x', sizeof(comprida) - 1);
  comprida[sizeof(comprida) - 1] = '\0';
  smw_log("%s", comprida);
  CHECK(smw_log_grava());
  le_usb();
  char esperada[210];
  memset(esperada, 'x', 200);
  memcpy(esperada + 200, "\n", 2);
  CHECK(strstr(lido, esperada) && !strstr(lido, "xxxxxxxxxx\nx"));

  /* 3. Memoria cheia: as linhas que nao cabem sao contadas no fim. */
  liga(usb_dir, usb_dir);
  int linhas = 400;
  for (int i = 0; i < linhas; ++i)
    smw_log("linha %03d %s", i, "0123456789012345678901234567890123456789012345678901234567890123456789");
  CHECK(smw_log_grava());
  long tamanho = le_usb();
  CHECK(tamanho <= LOG_CABECALHO + LOG_BYTES + LOG_AVISO);
  int guardadas = conta(lido, " linha ");
  unsigned perdidas_arquivo = 0;
  const char *aviso = strstr(lido, " linhas nao couberam no log (memoria cheia)\n");
  CHECK(aviso != NULL);
  while (aviso > lido && aviso[-1] != ' ') --aviso;
  sscanf(aviso, "%u", &perdidas_arquivo);
  CHECK(guardadas > 100 && perdidas_arquivo > 0 && guardadas + (int)perdidas_arquivo == linhas);
  CHECK(strstr(lido, "linha 000 ") && !strstr(lido, "linha 399 "));
  CHECK(lido[tamanho - 1] == '\n');

  /* 3b. Memoria cheia durante o jogo: o comeco (boot, video) fica, saem as
         linhas mais antigas do jogo e o erro mais novo sempre entra. */
  liga(usb_dir, usb_dir);
  smw_log("Boot: drivers");
  smw_log("Video em uso: 240p");
  smw_log_fixa();
  for (int i = 0; i < linhas; ++i)
    smw_log("save %03d %s", i, "0123456789012345678901234567890123456789012345678901234567890123456789");
  smw_log("Save 999: ERRO AO SALVAR 24 31");
  CHECK(smw_log_grava());
  tamanho = le_usb();
  CHECK(tamanho <= LOG_CABECALHO + LOG_BYTES + LOG_AVISO);
  const char *boot = strstr(lido, "Boot: drivers\n"), *video = strstr(lido, "Video em uso: 240p\n");
  const char *nota = strstr(lido, " linhas mais antigas tiradas (memoria do log cheia) ...\n");
  const char *erro = strstr(lido, "Save 999: ERRO AO SALVAR 24 31\n");
  CHECK(boot && video && nota && erro && boot < video && video < nota && nota < erro);
  CHECK(erro + strlen("Save 999: ERRO AO SALVAR 24 31\n") == lido + tamanho);
  unsigned saiu = 0;
  const char *num = nota;
  while (num > lido && num[-1] != ' ') --num;
  sscanf(num, "%u", &saiu);
  int ficaram = conta(lido, " save ");
  CHECK(saiu > 100 && ficaram > 100 && ficaram + (int)saiu == linhas);
  CHECK(strstr(lido, "save 399 ") && !strstr(lido, "save 000 ") && !strstr(lido, "nao couberam"));

  /* 3c. Comeco maior que 8 KB (muitos testes de video): so 8 KB ficam fixos,
         o resto do comeco sai primeiro e o erro novo sempre entra. */
  liga(usb_dir, usb_dir);
  for (int i = 0; i < 120; ++i)
    smw_log("boot %03d %s", i, "0123456789012345678901234567890123456789012345678901234567890123456789");
  CHECK(usado > LOG_FIXO);
  smw_log_fixa();
  CHECK(fixo > 0 && fixo <= LOG_FIXO && texto[fixo - 1] == '\n');
  for (int i = 0; i < linhas; ++i)
    smw_log("save %03d %s", i, "0123456789012345678901234567890123456789012345678901234567890123456789");
  smw_log("Save 999: ERRO AO SALVAR 24 31");
  CHECK(smw_log_grava());
  tamanho = le_usb();
  CHECK(strstr(lido, "boot 000 ") && !strstr(lido, "boot 119 ") && strstr(lido, "linhas mais antigas tiradas"));
  const char *ultimo = strstr(lido, "Save 999: ERRO AO SALVAR 24 31\n");
  CHECK(ultimo && ultimo + 31 == lido + tamanho);

  /* 4. Antes de achar o jogo (ROM nao encontrada): pasta do ELF. */
  apaga_usb();
  liga("", usb_dir);
  smw_log("ERRO: Coloque o jogo");
  CHECK(smw_log_grava() && le_usb() > 0 && strstr(lido, "ERRO: Coloque o jogo"));

  /* 5. Pela ISO: o pendrive fica com o OPL; vai para o Memory Card 1, com o
        icone do save (a pasta SMWPS2 nao fica como "dados corrompidos"). */
  apaga_usb();
  mc_reset();
  liga("", "cdrom0:\\");
  smw_log("Pela ISO");
  CHECK(smw_log_grava() && le_usb() < 0);
  CHECK(le_cartao(0) && strstr(lido, "SMW_LOG.TXT: mc0:/SMWPS2/SMW_LOG.TXT (Memory Card 1), gravado"));
  CHECK(strstr(lido, "Pela ISO\n") && mc_get(0, "/SMWPS2/icon.sys") && mc_get(0, "/SMWPS2/" SMW_ICON_NAME));
  /* Gravar de novo no cartao: um arquivo so, com o texto novo. */
  smw_log("Segunda vez");
  CHECK(smw_log_grava() && le_cartao(0) && conta(lido, "Pela ISO") == 1 && strstr(lido, "Segunda vez"));

  /* 6. Save no Memory Card 2: o log vai junto. */
  mc_reset();
  mc_present[1] = 1;
  liga("", "cdrom0:\\");
  active_store = STORE_MC1;
  smw_log("Cartao 2");
  CHECK(smw_log_grava() && !mc_get(0, "/SMWPS2/SMW_LOG.TXT") && le_cartao(1));
  CHECK(strstr(lido, "(Memory Card 2)") && strstr(lido, "Cartao 2"));

  /* 7. Sem save ainda e so o cartao 2 no console: usa o cartao 2. */
  mc_reset();
  mc_present[0] = 0, mc_present[1] = 1;
  liga("", "cdrom0:\\");
  CHECK(smw_log_grava() && le_cartao(1));
  /* O cartao do save (1) foi tirado: usa o outro. */
  mc_reset();
  mc_present[0] = 0, mc_present[1] = 1;
  liga("", "cdrom0:\\");
  active_store = STORE_MC0;
  CHECK(smw_log_grava() && le_cartao(1) && !mc_get(0, "/SMWPS2/SMW_LOG.TXT"));
  /* Cartao 1 sem espaco para o log: tenta o cartao 2. */
  mc_reset();
  mc_present[1] = 1;
  mc_capacity_kb[0] = 2;
  liga("", "cdrom0:\\");
  for (int i = 0; i < 40; ++i) smw_log("linha %02d %s", i, "0123456789012345678901234567890123456789012345678901234567890123");
  CHECK(smw_log_grava() && le_cartao(1) && strstr(lido, "(Memory Card 2)"));
  CHECK(conta(lido, "Log: nao consegui gravar mc0:/SMWPS2/SMW_LOG.TXT") == 1);
  CHECK(!mc_get(0, "/SMWPS2/SMW_LOG.TXT") || mc_get(0, "/SMWPS2/SMW_LOG.TXT")->size == 0);

  /* Reserva do save: com menos de 16 KB sobrando depois do log, o cartao
     nao recebe o log (o save tem prioridade, como no icone). */
  mc_reset();
  mc_capacity_kb[0] = 18;
  liga("", "cdrom0:\\");
  smw_log("Pouco espaco");
  CHECK(!smw_log_grava() && !mc_get(0, "/SMWPS2/SMW_LOG.TXT"));
  mc_reset();
  mc_capacity_kb[0] = 20;
  liga("", "cdrom0:\\");
  smw_log("Espaco certo");
  CHECK(smw_log_grava() && le_cartao(0) && strstr(lido, "Espaco certo"));

  /* ELF numa pasta do Memory Card (mc0:/APPS/SMW/): a reserva do save vale
     ali tambem, e a pasta SMWPS2 nao e criada por causa do log. */
  mc_reset();
  mc_capacity_kb[0] = 18;
  liga("mc0:/APPS/SMW/", "mc0:/APPS/SMW/");
  smw_log("ELF no cartao");
  CHECK(!smw_log_grava() && !mc_get(0, "/APPS/SMW/SMW_LOG.TXT") && !mc_get(0, "/SMWPS2/SMW_LOG.TXT"));
  mc_reset();
  liga("mc0:/APPS/SMW/", "mc0:/APPS/SMW/");
  smw_log("ELF no cartao");
  CHECK(smw_log_grava() && mc_get(0, "/APPS/SMW/SMW_LOG.TXT") && !mc_get(0, "/SMWPS2/icon.sys"));

  /* Aviso por lugar, nao pela posicao na lista: cartao 1 cheio e depois,
     no lugar dele, o cartao 2 cheio: os dois avisos aparecem. */
  mc_reset();
  mc_capacity_kb[0] = 2;
  liga("", "cdrom0:\\");
  CHECK(!smw_log_grava() && conta_memoria("Log: nao consegui gravar mc0:/SMWPS2/SMW_LOG.TXT") == 1);
  mc_reset();
  mc_present[0] = 0, mc_present[1] = 1, mc_capacity_kb[1] = 2;
  CHECK(!smw_log_grava() && conta_memoria("Log: nao consegui gravar mc1:/SMWPS2/SMW_LOG.TXT") == 1);
  CHECK(!smw_log_grava() && conta_memoria("Log: nao consegui gravar") == 2);

  /* Pasta com caminho comprido demais para o nome do log: vai para o cartao. */
  mc_reset();
  char comprido[SMW_PATH];
  memset(comprido, 'p', 505);
  comprido[505] = '\0';
  liga(comprido, comprido);
  CHECK(smw_log_grava() && le_cartao(0));

  /* 8. Pela ISO sem Memory Card: nao grava; avisa uma vez so no log. */
  mc_reset();
  mc_present[0] = 0;
  liga("", "cdrom0:\\");
  CHECK(!smw_log_grava() && !smw_log_grava());
  CHECK(conta_memoria("Log: sem lugar para gravar") == 1);
  /* O cartao entra depois: grava, e o aviso fica no arquivo. */
  mc_present[0] = 1;
  CHECK(smw_log_grava() && le_cartao(0) && strstr(lido, "Log: sem lugar para gravar"));

  /* 9. Pendrive recusando gravar (o "U27" visto no PS2): vai para o Memory
        Card, como o save, sem mexer no ERRO AO SALVAR. */
  apaga_usb();
  mc_reset();
  liga(usb_dir, usb_dir);
  usb_denied = 1;
  usb_step = 0;
  card_recording = false;
  CHECK(smw_log_grava() && smw_log_grava() && conta_memoria("Log: nao consegui gravar") == 1);
  CHECK(le_usb() < 0 && le_cartao(0) && strstr(lido, "(Memory Card 1)"));
  CHECK(strstr(lido, "Log: nao consegui gravar /tmp/smw_log_"));
  CHECK(usb_step == 0 && !card_recording && !error_text[0]);
  /* Sem cartao tambem: nao grava em lugar nenhum. */
  mc_present[0] = 0;
  CHECK(!smw_log_grava());
  usb_denied = 0;
  CHECK(smw_log_grava() && le_usb() > 0 && strstr(lido, "Log: nao consegui gravar"));

  /* 10. Durante um save (card_recording ligado) o log nao apaga o registro. */
  card_recording = true;
  CHECK(smw_log_grava() && card_recording);
  card_recording = false;

  apaga_usb();
  rmdir(usb_dir);
  printf("REGISTRO: OK (%d conferencias)\n", checks);
  return 0;
}
