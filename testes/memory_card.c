#include <assert.h>
#include <stdio.h>

#include "../Fonte/ps2/files.c"

static int results[16], result_count, result_index;
static int opens, deletes, writes, closes, delete_submit, chmods;

int mcOpen(int port, int slot, const char *name, int flags) {
  assert(port == 0 && slot == 0 && strcmp(name, "/SMWPS2/SMW.TMP") == 0);
  assert(flags == (0x0003 | 0x0200));
  ++opens;
  return 0;
}

int mcDelete(int port, int slot, const char *name) {
  assert(port == 0 && slot == 0 && strcmp(name, "/SMWPS2/SMW.TMP") == 0);
  ++deletes;
  return delete_submit;
}

int mcWrite(int fd, const void *buffer, int size) {
  assert(fd == 7 && size == 1 && ((const uint8_t *)buffer)[0] == 42);
  ++writes;
  return 0;
}

int mcClose(int fd) {
  assert(fd == 7);
  ++closes;
  return 0;
}

int mcGetDir(int port, int slot, const char *name, unsigned mode, int maxent, sceMcTblGetDir *table) {
  assert(strcmp(name, "/SMWPS2/SMW.TMP") == 0 && maxent == 1);
  table->AttrFile = 0x8411;  /* existe, arquivo, so leitura */
  return 0;
}

int mcSetFileInfo(int port, int slot, const char *name, const sceMcTblGetDir *info, unsigned flags) {
  assert(strcmp(name, "/SMWPS2/SMW.TMP") == 0 && flags == 0x0004 && info->AttrFile == 0x8417);
  ++chmods;
  return 0;
}

int mcSync(int mode, int *command, int *result) {
  assert(mode == 0 && command == NULL && result_index < result_count);
  *result = results[result_index++];
  return 0;
}

static void reset(const int *steps, int count) {
  memcpy(results, steps, count * sizeof(*steps));
  result_count = count;
  result_index = opens = deletes = writes = closes = delete_submit = chmods = 0;
  card_recording = true;
  card_step = card_result = 0;
}

static bool write_one(void) {
  const uint8_t data[] = {42};
  return card_write(0, "/SMWPS2/SMW.TMP", data, sizeof(data));
}

int main(void) {
  /* Sync results in order: delete, open, write, close (and, when the card
     refuses, getdir + setinfo before trying again). */
  const int normal[] = {0, 7, 1, 0};
  reset(normal, 4);
  assert(write_one() && deletes == 1 && opens == 1 && writes == 1 && closes == 1);
  assert(result_index == result_count && card_step == 0);

  const int absent[] = {-4, 7, 1, 0};
  reset(absent, 4);
  assert(write_one() && opens == 1 && card_step == 0);

  /* Arquivo protegido: apagar recusa (-5) -> devolve a permissao e apaga. */
  const int protected_delete[] = {-5, 1, 0, 0, 7, 1, 0};
  reset(protected_delete, 7);
  assert(write_one() && deletes == 2 && chmods == 1 && opens == 1 && writes == 1);
  assert(result_index == result_count && card_step == 0);

  /* O caso do PS2 do teste: abre, mas recusa gravar (M3-5). */
  const int write_denied[] = {0, 7, -5, 0, 1, 0, 7, 1, 0};
  reset(write_denied, 9);
  assert(write_one() && opens == 2 && writes == 2 && closes == 2 && chmods == 1);
  assert(result_index == result_count && card_step == 0);

  const int still_denied[] = {0, 7, -5, 0, 1, 0, 7, -5, 0};
  reset(still_denied, 9);
  assert(!write_one() && card_step == 3 && card_result == -5);

  const int open_denied[] = {0, -5, 1, 0, 7, 1, 0};
  reset(open_denied, 7);
  assert(write_one() && opens == 2 && chmods == 1 && card_step == 0);

  const int delete_error[] = {-8};
  reset(delete_error, 1);
  assert(!write_one() && opens == 0 && card_step == 2 && card_result == -8);

  reset(normal, 0);
  delete_submit = -3;
  assert(!write_one() && opens == 0 && card_step == 2 && card_result == -3);

  const int open_error[] = {0, -6};
  reset(open_error, 2);
  assert(!write_one() && opens == 1 && card_step == 2 && card_result == -6);
  puts("Memory Card: testes passaram.");
  return 0;
}
