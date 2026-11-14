/* Simula um Memory Card (arquivos, pastas e permissoes R/W como no MCMAN)
   e confere que salvar cria icon.sys + icone uma vez (e troca
   o de versoes anteriores), e que saves antigos so-escrita voltam a ser lidos, em blocos de ate
   4096 bytes, e que o progresso volta igual. */
#include <assert.h>
#include <stdio.h>

#include "../Fonte/ps2/files.c"

#define MAXF 16
static struct { char name[64]; uint8_t data[65536]; int size, perm, used; } fs[MAXF];
static struct { int file, pos, open; } fds[8];
static int pending, writes_icon, max_piece, chmods;

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
  int n = fs[f].size - fds[fd].pos;
  if (size > n) { pending = -5; return 0; }  /* como o driver da BIOS: nao le alem do fim */
  memcpy(buf, fs[f].data + fds[fd].pos, n); fds[fd].pos += n; pending = n; return 0; }
int mcSeek(int fd, int offset, int origin) {
  int f = fds[fd].file;
  fds[fd].pos = (origin == 2 ? fs[f].size : origin == 1 ? fds[fd].pos : 0) + offset;
  pending = fds[fd].pos; return 0; }
int mcWrite(int fd, const void *buf, int size) {
  int f = fds[fd].file;
  if (!(fs[f].perm & 2)) { pending = -5; return 0; }
  assert(size <= 4096);
  if (size > max_piece) max_piece = size;
  assert(fds[fd].pos + size <= (int)sizeof(fs[f].data));
  if (strstr(fs[f].name, ".icn")) writes_icon++;
  memcpy(fs[f].data + fds[fd].pos, buf, size); fds[fd].pos += size;
  if (fds[fd].pos > fs[f].size) fs[f].size = fds[fd].pos;
  pending = size; return 0; }
int mcClose(int fd) { fds[fd].open = 0; pending = 0; return 0; }
int mcDelete(int port, int slot, const char *name) { (void)port; (void)slot;
  int f = find(name); if (f < 0) { pending = -4; return 0; } fs[f].used = 0; pending = 0; return 0; }
int mcGetDir(int port, int slot, const char *name, unsigned mode, int maxent, sceMcTblGetDir *table) {
  (void)port; (void)slot; (void)mode; int f = find(name);
  assert(maxent == 1);
  if (f < 0) { pending = 0; return 0; }
  table->AttrFile = (unsigned short)(0x8410 | fs[f].perm); pending = 1; return 0; }
int mcSetFileInfo(int port, int slot, const char *name, const sceMcTblGetDir *info, unsigned flags) {
  (void)port; (void)slot; int f = find(name);
  assert(flags == 0x0004);
  if (f < 0) { pending = -4; return 0; }
  assert((info->AttrFile & 0x8410) == 0x8410); fs[f].perm = info->AttrFile & 3; chmods++; pending = 0; return 0; }
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
  /* Cartao com icone da versao anterior (maior, icon.sys sem a marca nova,
     gravado so-escrita): o jogo deve trocar os dois e manter o progresso. */
  memset(fs[s].data + 452, 0, 8);
  memset(fs[i].data, 0xEE, 50000); fs[i].size = 50000; fs[i].perm = 2;
  int before = writes_icon;
  memset(save, 0x44, sizeof(save));
  assert(smw_save_progress(save));
  s = find("/SMWPS2/icon.sys"); i = find("/SMWPS2/" SMW_ICON_NAME);
  assert(s >= 0 && i >= 0 && writes_icon == before + icon_writes);
  assert(fs[s].size == (int)sizeof(kSmwIconSys) && !memcmp(fs[s].data, kSmwIconSys, sizeof(kSmwIconSys)));
  assert(fs[i].size == (int)sizeof(kSmwIcon) && !memcmp(fs[i].data, kSmwIcon, sizeof(kSmwIcon)));
  assert(fs[i].perm == 3);
  before = writes_icon;
  assert(smw_save_progress(save));               /* ja atualizado: nao regrava */
  assert(writes_icon == before);
  memset(back, 0, sizeof(back));
  smw_load_progress(back);
  assert(!memcmp(back, save, sizeof(save)));
  /* Saves de versoes antigas que ficaram so-escrita no cartao (o driver da
     BIOS abre, mas recusa ler com -5, o erro "M6-5"): o jogo devolve a
     permissao de leitura, recupera o progresso e volta a salvar. */
  for (int k = 0; k < MAXF; k++)
    if (fs[k].used && strstr(fs[k].name, "/SMWPS2/SMW.")) fs[k].perm = 2;
  memset(back, 0, sizeof(back));
  smw_load_progress(back);
  assert(!memcmp(back, save, sizeof(save)) && chmods >= 1);
  memset(save, 0x55, sizeof(save));
  for (int k = 0; k < MAXF; k++)
    if (fs[k].used && strstr(fs[k].name, "/SMWPS2/SMW.")) fs[k].perm = 2;
  assert(smw_save_progress(save) && !smw_save_error_text()[0]);
  memset(back, 0, sizeof(back));
  smw_load_progress(back);
  assert(!memcmp(back, save, sizeof(save)));
  /* Cartao do teste no PS2 ("M3-5T"): SMW.TMP antigo abre, mas recusa
     gravar. O jogo grava um arquivo novo no lugar. */
  for (int k = 0; k < MAXF; k++)
    if (fs[k].used && strstr(fs[k].name, "/SMWPS2/SMW.")) fs[k].perm = 1;
  {
    int t = -1;
    for (int k = 0; k < MAXF; k++) if (!fs[k].used) { t = k; break; }
    if (find("/SMWPS2/SMW.TMP") < 0) {
      fs[t].used = 1; strcpy(fs[t].name, "/SMWPS2/SMW.TMP"); fs[t].size = 3; fs[t].perm = 1;
    }
  }
  memset(save, 0x66, sizeof(save));
  assert(smw_save_progress(save) && !smw_save_error_text()[0]);
  memset(back, 0, sizeof(back));
  smw_load_progress(back);
  assert(!memcmp(back, save, sizeof(save)));
  printf("memory card + icone: OK (icone %zu bytes em %d blocos, icon.sys %zu bytes)\n",
         sizeof(kSmwIcon), icon_writes, sizeof(kSmwIconSys));
  return 0;
}
