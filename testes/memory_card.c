#include <assert.h>
#include <stdio.h>

#include "../Fonte/ps2/files.c"

static int results[8], result_count, result_index;
static int opens, deletes, writes, closes, delete_submit;

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

int mcSync(int mode, int *command, int *result) {
  assert(mode == 0 && command == NULL && result_index < result_count);
  *result = results[result_index++];
  return 0;
}

static void reset(const int *steps, int count) {
  memcpy(results, steps, count * sizeof(*steps));
  result_count = count;
  result_index = opens = deletes = writes = closes = delete_submit = 0;
  card_recording = true;
  card_step = card_result = 0;
}

static bool write_one(void) {
  const uint8_t data[] = {42};
  return card_write(0, "/SMWPS2/SMW.TMP", data, sizeof(data));
}

int main(void) {
  const int normal[] = {7, 1, 0};
  reset(normal, 3);
  assert(write_one() && opens == 1 && deletes == 0 && writes == 1 && closes == 1);
  assert(result_index == result_count);

  const int retry[] = {-5, 0, 7, 1, 0};
  reset(retry, 5);
  assert(write_one() && opens == 2 && deletes == 1 && writes == 1 && closes == 1);
  assert(result_index == result_count && card_step == 0);

  const int other_error[] = {-4};
  reset(other_error, 1);
  assert(!write_one() && opens == 1 && deletes == 0 && card_step == 2 && card_result == -4);

  const int delete_error[] = {-5, -8};
  reset(delete_error, 2);
  assert(!write_one() && opens == 1 && deletes == 1 && card_step == 2 && card_result == -8);

  const int submit_error[] = {-5};
  reset(submit_error, 1);
  delete_submit = -3;
  assert(!write_one() && opens == 1 && deletes == 1 && card_step == 2 && card_result == -3);

  const int retry_error[] = {-5, 0, -6};
  reset(retry_error, 3);
  assert(!write_one() && opens == 2 && deletes == 1 && card_step == 2 && card_result == -6);
  puts("Memory Card: testes passaram.");
  return 0;
}
