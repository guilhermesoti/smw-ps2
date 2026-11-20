/* Memory Card simulado para os testes (dois cartoes, MCSERV assincrono).
   Comporta-se como o driver da BIOS nos pontos que importam aqui:
   - arquivos criados com leitura e gravacao;
   - mcRead/mcWrite recusam (-5) sem permissao, e mcRead recusa ler alem do fim;
   - espaco em blocos de 1 KB: gravar alem da capacidade devolve -3 (cartao cheio);
   - mcRename/mcGetDir/mcSetFileInfo como no MCMAN.
   Falhas podem ser injetadas por arquivo (McFile.fail_read) e em mcRename. */
#ifndef TEST_MC_MOCK_H
#define TEST_MC_MOCK_H
#include <assert.h>
#include <string.h>

#define MC_MAXF 48
typedef struct {
  int used, port, perm, size, fail_read;  /* fail_read: mcRead devolve -5 */
  char name[64];
  unsigned char data[65536];
} McFile;
static McFile mc_fs[MC_MAXF];
static struct { int open, file, pos; } mc_fds[8];
static int mc_pending, mc_present[2] = {1, 0}, mc_capacity_kb[2] = {8000, 8000};
static int mc_fail_rename;      /* mcRename devolve -5 */
static int mc_swapped[2];       /* cartao trocado: a consulta n (1 ou 2) da -1 */
static int mc_writes_icon, mc_max_piece, mc_writes;  /* mc_writes: chamadas de mcWrite */

static void mc_reset(void) {
  memset(mc_fs, 0, sizeof(mc_fs));
  memset(mc_fds, 0, sizeof(mc_fds));
  mc_present[0] = 1, mc_present[1] = 0;
  mc_capacity_kb[0] = mc_capacity_kb[1] = 8000;
  mc_fail_rename = 0, mc_writes_icon = 0, mc_max_piece = 0, mc_writes = 0;
  mc_swapped[0] = mc_swapped[1] = 0;
}
static int mc_find(int port, const char *n) {
  for (int i = 0; i < MC_MAXF; i++)
    if (mc_fs[i].used && mc_fs[i].port == port && !strcmp(mc_fs[i].name, n)) return i;
  return -1;
}
static int mc_used_kb(int port) {
  int kb = 0;
  for (int i = 0; i < MC_MAXF; i++)
    if (mc_fs[i].used && mc_fs[i].port == port && mc_fs[i].size > 0) kb += (mc_fs[i].size + 1023) / 1024;
  return kb;
}
/* Coloca um arquivo no cartao, como se viesse de uma versao anterior. */
static void mc_put(int port, const char *name, const void *data, int size) {
  int f = mc_find(port, name);
  for (int i = 0; f < 0 && i < MC_MAXF; i++) if (!mc_fs[i].used) f = i;
  assert(f >= 0 && size <= (int)sizeof(mc_fs[f].data));
  mc_fs[f].used = 1, mc_fs[f].port = port, mc_fs[f].perm = 3, mc_fs[f].size = size, mc_fs[f].fail_read = 0;
  strcpy(mc_fs[f].name, name);
  memcpy(mc_fs[f].data, data, size);
}
static const McFile *mc_get(int port, const char *name) {
  int f = mc_find(port, name);
  return f < 0 ? NULL : &mc_fs[f];
}

int mcGetInfo(int port, int slot, int *type, int *free_clusters, int *format) {
  (void)slot;
  *type = mc_present[port] ? MC_TYPE_PS2 : 0;
  *free_clusters = mc_capacity_kb[port] - mc_used_kb(port);
  *format = 1;
  mc_pending = !mc_present[port] ? -10 : mc_swapped[port] == 1 ? -1 : 0;
  if (mc_swapped[port]) --mc_swapped[port];
  return 0;
}
int mcMkDir(int port, int slot, const char *name) {
  (void)slot;
  if (mc_find(port, name) >= 0) { mc_pending = -4; return 0; }
  mc_put(port, name, "", 0);
  mc_fs[mc_find(port, name)].size = -1;
  mc_pending = 0;
  return 0;
}
int mcOpen(int port, int slot, const char *name, int flags) {
  (void)slot;
  int f = mc_find(port, name);
  if (f < 0) {
    if (!(flags & 0x200)) { mc_pending = -4; return 0; }
    mc_put(port, name, "", 0);
    f = mc_find(port, name);
  }
  for (int d = 0; d < 8; d++)
    if (!mc_fds[d].open) { mc_fds[d].open = 1, mc_fds[d].file = f, mc_fds[d].pos = 0; mc_pending = d; return 0; }
  mc_pending = -7;
  return 0;
}
int mcSeek(int fd, int offset, int origin) {
  int f = mc_fds[fd].file;
  mc_fds[fd].pos = (origin == 2 ? mc_fs[f].size : origin == 1 ? mc_fds[fd].pos : 0) + offset;
  mc_pending = mc_fds[fd].pos;
  return 0;
}
int mcRead(int fd, void *buf, int size) {
  int f = mc_fds[fd].file;
  if (!(mc_fs[f].perm & 1) || mc_fs[f].fail_read) { mc_pending = -5; return 0; }
  if (size > mc_fs[f].size - mc_fds[fd].pos) { mc_pending = -5; return 0; }
  memcpy(buf, mc_fs[f].data + mc_fds[fd].pos, size);
  mc_fds[fd].pos += size;
  mc_pending = size;
  return 0;
}
int mcWrite(int fd, const void *buf, int size) {
  int f = mc_fds[fd].file;
  if (!(mc_fs[f].perm & 2)) { mc_pending = -5; return 0; }
  assert(size <= 4096);
  if (size > mc_max_piece) mc_max_piece = size;
  int end = mc_fds[fd].pos + size, old = mc_fs[f].size > 0 ? mc_fs[f].size : 0;
  int grow = (end > old ? (end + 1023) / 1024 - (old + 1023) / 1024 : 0);
  if (mc_used_kb(mc_fs[f].port) + grow > mc_capacity_kb[mc_fs[f].port]) { mc_pending = -3; return 0; }
  if (strstr(mc_fs[f].name, ".icn")) mc_writes_icon++;
  mc_writes++;
  memcpy(mc_fs[f].data + mc_fds[fd].pos, buf, size);
  mc_fds[fd].pos = end;
  if (end > mc_fs[f].size) mc_fs[f].size = end;
  mc_pending = size;
  return 0;
}
int mcClose(int fd) { mc_fds[fd].open = 0; mc_pending = 0; return 0; }
int mcDelete(int port, int slot, const char *name) {
  (void)slot;
  int f = mc_find(port, name);
  if (f < 0) { mc_pending = -4; return 0; }
  mc_fs[f].used = 0;
  mc_pending = 0;
  return 0;
}
int mcRename(int port, int slot, const char *name, const char *newname) {
  (void)slot;
  int f = mc_find(port, name);
  if (f < 0) { mc_pending = -4; return 0; }
  if (mc_fail_rename || strchr(newname, '/')) { mc_pending = -5; return 0; }
  char full[64];
  snprintf(full, sizeof(full), "%s", name);
  char *slash = strrchr(full, '/');
  snprintf(slash + 1, sizeof(full) - (size_t)(slash + 1 - full), "%s", newname);
  if (mc_find(port, full) >= 0) { mc_pending = -4; return 0; }
  strcpy(mc_fs[f].name, full);
  mc_pending = 0;
  return 0;
}
int mcGetDir(int port, int slot, const char *name, unsigned mode, int maxent, sceMcTblGetDir *table) {
  (void)slot; (void)mode; (void)maxent;
  int f = mc_find(port, name);
  if (f < 0) { mc_pending = 0; return 0; }
  table->AttrFile = (unsigned short)(0x8410 | mc_fs[f].perm);
  mc_pending = 1;
  return 0;
}
int mcSetFileInfo(int port, int slot, const char *name, const sceMcTblGetDir *info, unsigned flags) {
  (void)slot;
  int f = mc_find(port, name);
  assert(flags == 0x0004);
  if (f < 0) { mc_pending = -4; return 0; }
  mc_fs[f].perm = info->AttrFile & 3;
  mc_pending = 0;
  return 0;
}
int mcSync(int mode, int *cmd, int *result) { (void)mode; (void)cmd; *result = mc_pending; return 1; }
#endif
