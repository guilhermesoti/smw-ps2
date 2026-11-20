/* Testes do salvamento entre pendrive, Memory Card 1 e Memory Card 2,
   incluindo reinicios, arquivos corrompidos ou ilegiveis e cartao cheio.

   O pendrive e uma pasta temporaria de verdade; a recusa do pendrive em
   gravar (o "U27" visto no PS2) e simulada com --wrap=fopen. Os Memory Cards
   sao simulados em mocks/mc_mock.h.

   Compilar a partir da pasta testes/ (ver README):
     cc -std=gnu11 -D_EE -Imocks -I../Fonte/ps2 $(pkg-config --cflags minizip zlib) \
        save_midias.c -Wl,--wrap=fopen $(pkg-config --libs minizip zlib) -o /tmp/smw-save-midias */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- pendrive: pasta temporaria, com recusa de gravacao opcional ---- */
static char usb_dir[256];
/* A pasta do pendrive simulado faz o papel do "mass:" (bdmfs_fatfs). */
#define SMW_TESTE_FATFS(path) (strncmp((path), usb_dir, strlen(usb_dir)) == 0)
#include "../Fonte/ps2/files.c"
#include "mc_mock.h"

static int usb_denied;  /* fopen para gravar devolve EACCES (como o FR_DENIED, errno 7) */
/* Como o bdmfs_fatfs do PS2: arquivo que nao existe da errno 4 (FR_NO_FILE)
   e pasta que nao existe, errno 5 (FR_NO_PATH), em vez de ENOENT. */
static int usb_fatfs;
/* Outra pasta de jogo que nao e o pendrive (por exemplo o HD), com SMW.SRM
   que existe e da erro de leitura (errno 5 = EIO) nas proximas hdd_eio
   leituras (o carregamento e a leitura do save antes de gravar). */
static char hdd_dir[256];
static int hdd_eio;
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) {
  if (hdd_eio > 0 && mode[0] == 'r' && !strncmp(path, hdd_dir, strlen(hdd_dir)) && strstr(path, "SMW.SRM")) {
    --hdd_eio;
    errno = 5;
    return NULL;
  }
  bool usb = !strncmp(path, usb_dir, strlen(usb_dir));
  if (usb_denied && strchr(mode, 'w') && usb) {
    errno = 7;
    return NULL;
  }
  FILE *f = __real_fopen(path, mode);
  if (!f && usb && usb_fatfs && errno == ENOENT) errno = strchr(path + strlen(usb_dir), '/') ? 5 : 4;
  return f;
}
static void usb_put(const char *name, const void *data, size_t size) {
  char p[512];
  snprintf(p, sizeof(p), "%s%s", usb_dir, name);
  FILE *f = __real_fopen(p, "wb");
  assert(f && fwrite(data, 1, size, f) == size);
  fclose(f);
}
static long usb_get(const char *name, void *data, size_t cap) {
  char p[512];
  snprintf(p, sizeof(p), "%s%s", usb_dir, name);
  FILE *f = __real_fopen(p, "rb");
  if (!f) return -1;
  long n = (long)fread(data, 1, cap, f);
  fclose(f);
  return n;
}
static void usb_clear(void) {
  static const char *const names[] = {"SMW.SRM", "SMW.TMP", "SMW.BAK", "SMW_S1.BAD", "SMW_T1.BAD", "SMW_B1.BAD"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    char p[512];
    snprintf(p, sizeof(p), "%s%s", usb_dir, names[i]);
    unlink(p);
  }
}

/* ---- SRAM do jogo: arquivo 1 com N fases concluidas (e o Palacio Amarelo) ---- */
enum { YELLOW = 134 };  /* flag_activated_switches[1] dentro do arquivo salvo */
static void make_sram(uint8_t *sram, int exits, bool yellow) {
  memset(sram, 0, SMW_SRAM_BYTES);
  for (int copy = 0; copy < 2; ++copy) {
    uint8_t *p = sram + copy * 429;
    for (int i = 0; i < 96; ++i) p[i] = (uint8_t)(i * 7);
    p[YELLOW] = yellow;
    p[140] = (uint8_t)exits;
    uint16_t sum = 0;
    for (int i = 0; i < 141; ++i) sum += p[i];
    uint16_t check = (uint16_t)(0x5A5A - sum);
    p[141] = (uint8_t)check, p[142] = (uint8_t)(check >> 8);
  }
}
static int exits_of(const uint8_t *sram) {
  const uint8_t *p = srm_slot(sram, 0);
  return p ? p[140] : -1;
}

/* Liga o PS2: estado do port zerado, SRAM vazia, carrega o progresso. */
static uint8_t g_test_sram[SMW_SRAM_BYTES];
static void reboot(void) {
  active_store = -1;
  max_generation = 0;
  memset(g_test_sram, 0, sizeof(g_test_sram));
  smw_enable_memory_card(true);
  snprintf(folder_dir, sizeof(folder_dir), "%s", usb_dir);
  smw_load_progress(g_test_sram);
}

static void fresh(void) {
  mc_reset();
  usb_clear();
  usb_denied = 0;
  usb_fatfs = 0;
}

static int checks;
#define CHECK(c) do { assert(c); ++checks; } while (0)

int main(void) {
  snprintf(usb_dir, sizeof(usb_dir), "/tmp/smw_usb_%d/", (int)getpid());
  mkdir(usb_dir, 0777);
  uint8_t stale[SMW_SRAM_BYTES], yellow[SMW_SRAM_BYTES], buf[SAVE_BYTES + 16];
  make_sram(stale, 0, false);
  make_sram(yellow, 1, true);

  /* 1. O caso do PS2: pendrive com save antigo e recusando gravar; o save do
        Palacio Amarelo vai para o Memory Card sem erro. Ao reiniciar, o
        carregamento tem de achar o save do cartao, nao o do pendrive.
        Depois a tela de arquivos grava de novo (RtlWriteSram ao escolher o
        arquivo): o progresso tem de continuar. */
  fresh();
  usb_put("SMW.SRM", stale, SMW_SRAM_BYTES);
  reboot();
  CHECK(load_info.store == STORE_FOLDER && exits_of(g_test_sram) == 0);
  usb_denied = 1;
  CHECK(smw_save_progress(yellow) && !error_text[0]);
  CHECK(!strcmp(notice, "SALVO: MEMORY CARD 1"));
  reboot();
  CHECK(load_info.store == STORE_MC0 && load_info.generation == 1);
  CHECK(!memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));
  CHECK(!strcmp(notice, "CARREGADO: MEMORY CARD 1"));
  /* Escolher o arquivo na tela inicial regrava a SRAM sem mudar nada. Logo
     depois do primeiro save o cartao ainda nao tem SMW.BAK: essa vez grava
     (cria o BAK). Dai em diante, desde a v0.13.8, nada vai para o cartao
     (no console isso parava o jogo por ate 5 s, com o som repetindo). */
  CHECK(!mc_get(0, "/SMWPS2/SMW.BAK"));
  CHECK(!smw_save_unchanged(g_test_sram) && smw_save_progress(g_test_sram));
  CHECK(mc_get(0, "/SMWPS2/SMW.BAK") != NULL);
  reboot();
  CHECK(!memcmp(g_test_sram, yellow, SMW_SRAM_BYTES) && load_info.generation == 2);
  int antes = mc_writes;
  CHECK(smw_save_unchanged(g_test_sram) && smw_save_progress(g_test_sram) && mc_writes == antes);
  reboot();
  CHECK(!memcmp(g_test_sram, yellow, SMW_SRAM_BYTES) && load_info.generation == 2);
  CHECK(usb_get("SMW.SRM", buf, sizeof(buf)) == SMW_SRAM_BYTES && !memcmp(buf, stale, SMW_SRAM_BYTES));
  /* O pendrive volta a aceitar gravacao: o proximo save com progresso novo
     vai para o cartao (onde esta o mais recente) e o reinicio continua
     achando o mais novo. */
  usb_denied = 0;
  uint8_t more[SMW_SRAM_BYTES];
  make_sram(more, 2, true);
  CHECK(!smw_save_unchanged(more) && smw_save_progress(more) && mc_writes > antes);
  reboot();
  CHECK(load_info.store == STORE_MC0 && load_info.generation == 3 && exits_of(g_test_sram) == 2);
  /* Depois de um save que deu certo, o mesmo conteudo de novo tambem nao
     grava; um ajuste trocado no jogo (L2+R2+X) com o progresso igual grava
     so o SMW.OPT, sem mexer no save. */
  uint8_t mais[SMW_SRAM_BYTES];
  make_sram(mais, 3, true);
  CHECK(!smw_save_unchanged(mais) && smw_save_progress(mais));
  antes = mc_writes;
  CHECK(smw_save_unchanged(mais) && smw_save_progress(mais) && mc_writes == antes);
  SmwOptions tela = {.tela = 1, .video = SMW_VIDEO_240P, .idioma = 0, .compat = SMW_OPT_ASK};
  smw_options_changed(&tela);
  CHECK(!smw_save_unchanged(mais) && smw_save_progress(mais) && mc_writes > antes);
  CHECK(mc_get(0, "/SMWPS2/SMW.OPT") != NULL && smw_save_unchanged(mais));
  reboot();
  CHECK(load_info.store == STORE_MC0 && load_info.generation == 4 && exits_of(g_test_sram) == 3);
  /* Trocou o Memory Card 1 por um vazio com o jogo ligado: o mesmo progresso
     tem de ir para o cartao novo (o libmc avisa a troca com -1); como no
     primeiro save, a vez seguinte cria o SMW.BAK e so depois nada grava. */
  for (int i = 0; i < MC_MAXF; i++) if (mc_fs[i].port == 0) mc_fs[i].used = 0;
  mc_swapped[0] = 1;
  antes = mc_writes;
  CHECK(!smw_save_unchanged(g_test_sram) && smw_save_progress(g_test_sram) && mc_writes > antes);
  CHECK(mc_get(0, "/SMWPS2/SMW.SRM") != NULL && !mc_get(0, "/SMWPS2/SMW.BAK"));
  CHECK(!smw_save_unchanged(g_test_sram) && smw_save_progress(g_test_sram));
  CHECK(mc_get(0, "/SMWPS2/SMW.BAK") != NULL && smw_save_unchanged(g_test_sram));
  reboot();
  CHECK(load_info.store == STORE_MC0 && exits_of(g_test_sram) == 3);
  /* O libmc pode avisar a troca na segunda consulta (revisao do Codex). */
  mc_swapped[0] = 2;
  CHECK(!smw_save_unchanged(g_test_sram));
  /* Pendrive: o save so e pulado se o SMW.SRM dele ainda tiver o progresso
     (o pendrive pode ter saido ou o arquivo mudado no PC; revisao do Codex). */
  fresh();
  reboot();
  CHECK(smw_save_progress(yellow) && smw_save_progress(yellow));  /* SMW.SRM e SMW.BAK */
  CHECK(!strcmp(notice, "SALVO: PENDRIVE") && smw_save_unchanged(yellow));
  usb_put("SMW.SRM", stale, SMW_SRAM_BYTES);  /* mudou no PC */
  CHECK(!smw_save_unchanged(yellow) && smw_save_progress(yellow));
  CHECK(usb_get("SMW.SRM", buf, sizeof(buf)) == SAVE_BYTES && smw_save_unchanged(yellow));
  /* Trocado por outro pendrive so com o mesmo SMW.SRM (sem o SMW.BAK): grava
     de novo para recriar a reserva (revisao do Codex). */
  {
    char bak[300];
    snprintf(bak, sizeof(bak), "%sSMW.BAK", usb_dir);
    CHECK(unlink(bak) == 0);
  }
  CHECK(!smw_save_unchanged(yellow) && smw_save_progress(yellow));
  CHECK(usb_get("SMW.BAK", buf, sizeof(buf)) == SAVE_BYTES && smw_save_unchanged(yellow));
  /* Cartao trocado durante o save: a consulta do icone avisa e o progresso
     nao fica como gravado no cartao de agora (revisao do Codex). */
  fresh();
  usb_denied = 1;
  reboot();
  CHECK(smw_save_progress(yellow) && smw_save_progress(yellow));  /* SMW.SRM e SMW.BAK no cartao 1 */
  CHECK(!strcmp(notice, "SALVO: MEMORY CARD 1") && smw_save_unchanged(yellow));
  uint8_t nova[SMW_SRAM_BYTES];
  make_sram(nova, 2, true);
  mc_fs[mc_find(0, "/SMWPS2/icon.sys")].used = 0;  /* o icone sera regravado: consulta o cartao */
  mc_swapped[0] = 3;  /* 1a e 2a consulta: antes de gravar; 3a: a do icone */
  CHECK(smw_save_progress(nova) && !smw_save_unchanged(nova));
  /* Arquivo ruim em outro lugar (pendrive) nao impede pular o save do
     cartao: o save nao mexe nele (revisao do Antigravity). */
  usb_put("SMW.SRM", "ruim", 4);
  reboot();
  CHECK(load_info.bad == 1 && load_info.store == STORE_MC0 && smw_save_unchanged(g_test_sram));

  /* 2. Migracao dos saves antigos do jeito que a v0.7.3 deixou o cartao:
        SMW.SRM com o progresso antigo (regravado ao escolher o arquivo) e
        SMW.BAK com o Palacio Amarelo; pendrive com o save antigo. O mais
        avancado e o BAK, e ele tem de ser preservado nos saves seguintes. */
  fresh();
  usb_put("SMW.SRM", stale, SMW_SRAM_BYTES);
  mc_put(0, "/SMWPS2/SMW.SRM", stale, SMW_SRAM_BYTES);
  mc_put(0, "/SMWPS2/SMW.BAK", yellow, SMW_SRAM_BYTES);
  reboot();
  CHECK(load_info.store == STORE_MC0 && load_info.file == FILE_BAK && load_info.legacy);
  CHECK(!memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));
  CHECK(smw_save_progress(g_test_sram));
  const McFile *bak = mc_get(0, "/SMWPS2/SMW.BAK");
  CHECK(bak && bak->size == SMW_SRAM_BYTES && !memcmp(bak->data, yellow, SMW_SRAM_BYTES));
  reboot();
  CHECK(load_info.store == STORE_MC0 && load_info.file == FILE_SRM && !load_info.legacy);
  CHECK(!memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));
  /* Diario antigo (SMWJ) de uma gravacao interrompida tambem e lido. */
  fresh();
  {
    uint8_t j[JOURNAL_BYTES];
    memcpy(j, "SMWJ", 4);
    put_le32(j + 4, (uint32_t)crc32(0, yellow, SMW_SRAM_BYTES));
    memcpy(j + 8, yellow, SMW_SRAM_BYTES);
    mc_put(0, "/SMWPS2/SMW.TMP", j, JOURNAL_BYTES);
    mc_put(0, "/SMWPS2/SMW.SRM", stale, SMW_SRAM_BYTES);
  }
  reboot();
  CHECK(load_info.file == FILE_TMP && !memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));

  /* 3. SMW.SRM corrompido (CRC errado): carrega o BAK, avisa e, ao salvar,
        preserva o arquivo corrompido como SMW_S1.BAD. */
  fresh();
  {
    uint8_t good[SAVE_BYTES], bad[SAVE_BYTES];
    encode_save(stale, 3, good);
    encode_save(yellow, 4, bad);
    bad[100] ^= 0x40;
    mc_put(0, "/SMWPS2/SMW.SRM", bad, SAVE_BYTES);
    mc_put(0, "/SMWPS2/SMW.BAK", good, SAVE_BYTES);
    reboot();
    CHECK(load_info.file == FILE_BAK && load_info.generation == 3 && load_info.bad == 1);
    CHECK(load_info.bad_state[0] == COPY_CORRUPT && load_info.bad_file[0] == FILE_SRM);
    char report[512];
    smw_load_report(report, sizeof(report));
    CHECK(strstr(report, "Memory Card 1, SMW.BAK") && strstr(report, "SMW.SRM corrompido"));
    CHECK(smw_save_progress(g_test_sram));
    const McFile *kept = mc_get(0, "/SMWPS2/SMW_S1.BAD");
    CHECK(kept && kept->size == SAVE_BYTES && !memcmp(kept->data, bad, SAVE_BYTES));
    bak = mc_get(0, "/SMWPS2/SMW.BAK");
    CHECK(bak && !memcmp(bak->data, good, SAVE_BYTES));     /* backup intocado */
    CHECK(!mc_get(0, "/SMWPS2/SMW.TMP"));
    reboot();
    CHECK(load_info.file == FILE_SRM && load_info.generation == 4 && load_info.bad == 0);
  }
  /* SRAM sem nenhum arquivo valido e com lixo (formato antigo): corrompido. */
  fresh();
  {
    uint8_t junk[SMW_SRAM_BYTES];
    for (int i = 0; i < SMW_SRAM_BYTES; ++i) junk[i] = (uint8_t)(i * 37 + 11);
    mc_put(0, "/SMWPS2/SMW.SRM", junk, SMW_SRAM_BYTES);
    reboot();
    CHECK(load_info.store < 0 && load_info.bad == 1 && !strcmp(notice, "SAVE ILEGIVEL: VEJA LEIA-ME"));
  }

  /* 4. Nenhum save: jogo novo, sem confundir com save ilegivel. */
  fresh();
  reboot();
  CHECK(load_info.store < 0 && load_info.bad == 0 && !strcmp(notice, "NENHUM SAVE: JOGO NOVO"));
  {
    char report[512];
    smw_load_report(report, sizeof(report));
    CHECK(strstr(report, "nenhum save encontrado"));
  }

  /* 5. Falha de leitura (-5 em todas as tentativas): save ilegivel. O
        arquivo e preservado como .BAD antes do proximo save; se nem renomear
        for possivel, o cartao nao e tocado e o save vai para outro lugar. */
  fresh();
  {
    uint8_t orig[SAVE_BYTES];
    encode_save(yellow, 9, orig);
    mc_put(0, "/SMWPS2/SMW.SRM", orig, SAVE_BYTES);
    mc_fs[mc_find(0, "/SMWPS2/SMW.SRM")].fail_read = 1;
    usb_denied = 1;
    reboot();
    CHECK(load_info.store < 0 && load_info.bad == 1 && load_info.bad_state[0] == COPY_UNREADABLE);
    CHECK(!strcmp(notice, "SAVE ILEGIVEL: VEJA LEIA-ME"));
    char report[512];
    smw_load_report(report, sizeof(report));
    CHECK(strstr(report, "NENHUMA COPIA PODE SER LIDA") && strstr(report, "nao pode ser lido"));
    mc_fail_rename = 1;
    mc_present[1] = 1;
    CHECK(smw_save_progress(stale));
    CHECK(!strcmp(notice, "SALVO: MEMORY CARD 2"));
    const McFile *srm = mc_get(0, "/SMWPS2/SMW.SRM");
    CHECK(srm && !memcmp(srm->data, orig, SAVE_BYTES));       /* intocado */
    mc_fail_rename = 0;
    mc_present[1] = 0;
    CHECK(smw_save_progress(stale));
    const McFile *kept = mc_get(0, "/SMWPS2/SMW_S1.BAD");
    CHECK(kept && !memcmp(kept->data, orig, SAVE_BYTES));
    CHECK(mc_get(0, "/SMWPS2/SMW.SRM") && !mc_get(0, "/SMWPS2/SMW.SRM")->fail_read);
  }

  /* 6. Cartao cheio: o erro aparece (CHEIO) e nada do que existia e apagado;
        com o cartao 2 livre, o save vai para ele e o reinicio o encontra. */
  fresh();
  {
    uint8_t old[SAVE_BYTES];
    encode_save(stale, 5, old);
    mc_put(0, "/SMWPS2/SMW.SRM", old, SAVE_BYTES);
    mc_put(0, "/SMWPS2/SMW.BAK", old, SAVE_BYTES);
    usb_denied = 1;
    reboot();
    mc_capacity_kb[0] = mc_used_kb(0);       /* nenhum bloco livre */
    CHECK(!smw_save_progress(yellow));
    CHECK(strstr(error_text, "24") && strstr(error_text, "M3-3T CHEIO"));
    CHECK(!mc_get(0, "/SMWPS2/SMW.TMP"));          /* sem arquivo pela metade */
    const McFile *srm = mc_get(0, "/SMWPS2/SMW.SRM");
    CHECK(srm && !memcmp(srm->data, old, SAVE_BYTES));
    CHECK(mc_get(0, "/SMWPS2/SMW.BAK") && !memcmp(mc_get(0, "/SMWPS2/SMW.BAK")->data, old, SAVE_BYTES));
    mc_present[1] = 1;
    CHECK(smw_save_progress(yellow) && !strcmp(notice, "SALVO: MEMORY CARD 2"));
    reboot();
    CHECK(load_info.store == STORE_MC1 && load_info.generation == 6);
    CHECK(!memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));
  }
  /* Espaco para o save, mas nao para o icone: o save vence, o icone espera. */
  fresh();
  usb_denied = 1;
  reboot();
  mc_capacity_kb[0] = 12;
  CHECK(smw_save_progress(yellow) && mc_writes_icon == 0 && !mc_get(0, "/SMWPS2/icon.sys"));
  reboot();
  CHECK(load_info.store == STORE_MC0 && !memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));
  mc_capacity_kb[0] = 8000;
  CHECK(smw_save_progress(yellow) && mc_writes_icon > 0 && mc_get(0, "/SMWPS2/icon.sys"));

  /* 7. Gravacao interrompida: SMW.TMP mais novo que SMW.SRM e o escolhido,
        e o proximo save guarda o SMW.SRM anterior no BAK. */
  fresh();
  {
    uint8_t s4[SAVE_BYTES], t5[SAVE_BYTES];
    encode_save(stale, 4, s4);
    encode_save(yellow, 5, t5);
    mc_put(0, "/SMWPS2/SMW.SRM", s4, SAVE_BYTES);
    mc_put(0, "/SMWPS2/SMW.TMP", t5, SAVE_BYTES);
    reboot();
    CHECK(load_info.file == FILE_TMP && load_info.generation == 5);
    CHECK(smw_save_progress(g_test_sram));
    bak = mc_get(0, "/SMWPS2/SMW.BAK");
    CHECK(bak && !memcmp(bak->data, t5, SAVE_BYTES));
    reboot();
    CHECK(load_info.file == FILE_SRM && load_info.generation == 6);
  }

  /* 8. Pendrive e cartao com saves desta versao: vence a geracao maior,
        em qualquer midia. */
  fresh();
  {
    uint8_t u7[SAVE_BYTES], m6[SAVE_BYTES];
    encode_save(yellow, 7, u7);
    encode_save(stale, 6, m6);
    usb_put("SMW.SRM", u7, SAVE_BYTES);
    mc_put(0, "/SMWPS2/SMW.SRM", m6, SAVE_BYTES);
    reboot();
    CHECK(load_info.store == STORE_FOLDER && load_info.generation == 7);
  }

  /* 9. Pendrive de verdade (bdmfs_fatfs): arquivo que falta da errno 4, nao
        ENOENT. Pendrive vazio, sem Memory Card: nada conta como ilegivel e
        o save vai para o pendrive (antes parava em ERR_PRESERVE). */
  fresh();
  usb_fatfs = 1;
  mc_present[0] = 0;
  reboot();
  CHECK(load_info.store == -1 && load_info.bad == 0);
  {
    /* Pasta que nao existe dentro do pendrive: errno 5 (FR_NO_PATH). */
    char p[512];
    uint8_t um[1];
    snprintf(p, sizeof(p), "%snao_existe/SMW.SRM", usb_dir);
    CHECK(file_read(p, um, sizeof(um)) == FILE_ABSENT);
  }
  CHECK(smw_save_progress(yellow) && !error_text[0]);
  CHECK(!strcmp(notice, "SALVO: PENDRIVE"));
  CHECK(usb_get("SMW.SRM", buf, sizeof(buf)) == SAVE_BYTES);
  CHECK(usb_get("SMW.TMP", buf, sizeof(buf)) < 0);
  reboot();
  CHECK(load_info.store == STORE_FOLDER && load_info.bad == 0 && !memcmp(g_test_sram, yellow, SMW_SRAM_BYTES));
  /* O segundo save faz o SMW.BAK e continua sem nada ilegivel. */
  CHECK(smw_save_progress(stale) && !error_text[0]);
  CHECK(usb_get("SMW.BAK", buf, sizeof(buf)) == SAVE_BYTES);
  reboot();
  CHECK(load_info.store == STORE_FOLDER && load_info.bad == 0 && !memcmp(g_test_sram, stale, SMW_SRAM_BYTES));
  /* Arquivo que existe mas esta estragado continua sendo preservado. */
  usb_put("SMW.SRM", "lixo", 4);
  reboot();
  CHECK(load_info.bad == 1 && load_info.bad_state[0] == COPY_CORRUPT);
  CHECK(smw_save_progress(yellow) && !error_text[0]);
  CHECK(usb_get("SMW_S1.BAD", buf, sizeof(buf)) == 4);

  /* 10. Fora do pendrive, errno 5 e erro de leitura (EIO), nao "nao existe":
         o SMW.SRM que existe e nao abre continua sendo preservado como .BAD
         antes do save gravar por cima. */
  fresh();
  mc_present[0] = 0;
  snprintf(hdd_dir, sizeof(hdd_dir), "/tmp/smw_hdd_%d/", (int)getpid());
  mkdir(hdd_dir, 0777);
  {
    char p[512];
    snprintf(p, sizeof(p), "%sSMW.SRM", hdd_dir);
    FILE *f = __real_fopen(p, "wb");
    CHECK(f && fwrite(stale, 1, SMW_SRAM_BYTES, f) == SMW_SRAM_BYTES);
    fclose(f);
    hdd_eio = 2;
    active_store = -1;
    max_generation = 0;
    memset(g_test_sram, 0, sizeof(g_test_sram));
    snprintf(folder_dir, sizeof(folder_dir), "%s", hdd_dir);
    smw_load_progress(g_test_sram);
    CHECK(load_info.bad == 1 && load_info.bad_state[0] == COPY_UNREADABLE);
    CHECK(smw_save_progress(yellow) && !error_text[0] && hdd_eio == 0);
    snprintf(p, sizeof(p), "%sSMW_S1.BAD", hdd_dir);
    FILE *bad = __real_fopen(p, "rb");
    CHECK(bad && fread(buf, 1, sizeof(buf), bad) == SMW_SRAM_BYTES && !memcmp(buf, stale, SMW_SRAM_BYTES));
    fclose(bad);
    static const char *const nomes[] = {"SMW.SRM", "SMW.TMP", "SMW.BAK", "SMW_S1.BAD"};
    for (size_t i = 0; i < sizeof(nomes) / sizeof(nomes[0]); ++i) {
      snprintf(p, sizeof(p), "%s%s", hdd_dir, nomes[i]);
      unlink(p);
    }
    rmdir(hdd_dir);
  }

  printf("Saves entre midias: %d verificacoes OK.\n", checks);
  return 0;
}
