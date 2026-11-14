/* Simula um Memory Card (arquivos, pastas e permissoes R/W como no MCMAN)
   e confere que salvar cria icon.sys + icone uma vez, em blocos de ate
   4096 bytes, e que o progresso volta igual. */
#include <assert.h>
#include <stdio.h>

#include "../Fonte/ps2/files.c"

#define MAXF 16
static struct { char name[64]; uint8_t data[40000]; int size, perm, used; } fs[MAXF];
static struct { int file, pos, open; } fds[8];
static int pending, writes_icon, max_piece;

static int find(const char *n) { for (int i = 0; i < MAXF; i++) if (fs[i].used && !strcmp(fs[i].name, n)) return i; return -1; }

int mcGetInfo(int port, int slot, int *type, int *free_clusters, int *format) {
  (void)slot; *type = port == 0 ? MC_TYPE_PS2 : 0; *free_clusters = 8000; *format = 1;
  pending = port == 0 ? 0 : -10; return 0;
}
int mcMkDir(int port, int slot, const char *name) { (void)port; (void)slot;
  if (find(name) >= 0) { pending = -4; return 0; }
  for (int i = 0; i < MAXF; i++) if (!fs[i].used) { fs[i].used = 1; strcpy(fs[i].name, name); fs[i].perm = 3; fs[i].size = -1; break; }
  pending = 0; return 0; }
int mcOpen(int port, int slot, const char *name, int flags) { (void)port; (void)slot;
  int f = find(name);
  if (f < 0) {
    if (!(flags & 0x200)) { pending = -4; return 0; }
    for (int i = 0; i < MAXF; i++) if (!fs[i].used) { f = i; break; }
    fs[f].used = 1; strcpy(fs[f].name, name); fs[f].size = 0; fs[f].perm = flags & 3;  /* MCMAN keeps the open mode */
  }
  for (int d = 0; d < 8; d++) if (!fds[d].open) { fds[d].open = 1; fds[d].file = f; fds[d].pos = 0; pending = d; return 0; }
  pending = -7; return 0; }
int mcRead(int fd, void *buf, int size) {
  int f = fds[fd].file;
  if (!(fs[f].perm & 1)) { pending = -5; return 0; }
  int n = fs[f].size - fds[fd].pos; if (n > size) n = size;
  memcpy(buf, fs[f].data + fds[fd].pos, n); fds[fd].pos += n; pending = n; return 0; }
int mcWrite(int fd, const void *buf, int size) {
  int f = fds[fd].file;
  if (!(fs[f].perm & 2)) { pending = -5; return 0; }
  assert(size <= 4096);
  if (size > max_piece) max_piece = size;
  if (strstr(fs[f].name, ".icn")) writes_icon++;
  memcpy(fs[f].data + fds[fd].pos, buf, size); fds[fd].pos += size;
  if (fds[fd].pos > fs[f].size) fs[f].size = fds[fd].pos;
  pending = size; return 0; }
int mcClose(int fd) { fds[fd].open = 0; pending = 0; return 0; }
int mcDelete(int port, int slot, const char *name) { (void)port; (void)slot;
  int f = find(name); if (f < 0) { pending = -4; return 0; } fs[f].used = 0; pending = 0; return 0; }
int mcSync(int mode, int *cmd, int *result) { (void)mode; (void)cmd; *result = pending; return 1; }

int main(void) {
  smw_enable_memory_card(true);
  uint8_t save[SMW_SRAM_BYTES], back[SMW_SRAM_BYTES];
  memset(save, 0x5A, sizeof(save));
  smw_load_progress(back);                       /* nada salvo ainda: escolhe o cartao 1 */
  assert(smw_save_progress(save));
  int s = find("/SMWPS2/icon.sys"), i = find("/SMWPS2/" SMW_ICON_NAME), p = find("/SMWPS2/SMW.SRM");
  assert(s >= 0 && i >= 0 && p >= 0);
  assert(fs[s].size == (int)sizeof(kSmwIconSys) && !memcmp(fs[s].data, kSmwIconSys, sizeof(kSmwIconSys)));
  assert(fs[i].size == (int)sizeof(kSmwIcon) && !memcmp(fs[i].data, kSmwIcon, sizeof(kSmwIcon)));
  assert(fs[s].perm == 3 && fs[i].perm == 3 && fs[p].perm == 3);
  int icon_writes = writes_icon;
  assert(icon_writes == (int)((sizeof(kSmwIcon) + 4095) / 4096) && max_piece == 4096);
  memset(save, 0x33, sizeof(save));
  assert(smw_save_progress(save));               /* segunda gravacao: icone ja existe */
  assert(writes_icon == icon_writes);
  memset(back, 0, sizeof(back));
  smw_load_progress(back);
  assert(!memcmp(back, save, sizeof(save)));
  printf("memory card + icone: OK (icone %zu bytes em %d blocos, icon.sys %zu bytes)\n",
         sizeof(kSmwIcon), icon_writes, sizeof(kSmwIconSys));
  return 0;
}
