#include "files.h"
#ifdef _EE
#include "smw_icon.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unzip.h>
#include <zlib.h>
#ifdef _EE
#include <libmc.h>
/* IOP open flags used by MCSERV (not the newlib values). */
enum { CARD_O_RDONLY = 0x0001, CARD_O_RDWR = 0x0003, CARD_O_CREAT = 0x0200 };
#endif

/* Progress can live next to the ROM (USB or another writable folder) or on
   a memory card. When the game boots from a disc image (OPL ISO mode) the
   USB drive belongs to OPL's own driver, so only the memory cards are used.
   Memory cards are accessed through MCSERV (libmc) directly, without the
   generic file layer, the same way commercial games do it. */
enum { STORE_FOLDER, STORE_MC0, STORE_MC1, STORE_COUNT };
typedef enum { CARD_READY, CARD_ABSENT, CARD_UNFORMATTED, CARD_WRONG_TYPE,
               CARD_NO_DRIVER } CardState;

/* Save error codes, shown as "<place><code>": place 1 = USB/folder,
   2 = memory card 1, 3 = memory card 2. */
enum { ERR_NOT_READY = 1, ERR_UNFORMATTED = 2, ERR_WRONG_TYPE = 3,
       ERR_TEMP = 4, ERR_BACKUP = 5, ERR_MAIN = 6, ERR_RECOVER = 7,
       ERR_NO_DRIVER = 8, ERR_PATH = 9 };

static char folder_dir[SMW_PATH];
static bool memory_card_ok;
static int active_store = -1;
static char error_text[32];
static const char *const kCardDirs[2] = {"mc0:/SMWPS2/", "mc1:/SMWPS2/"};

void smw_enable_memory_card(bool available) { memory_card_ok = available; }

bool smw_path_is_disc(const char *path) {
  return path && (strncmp(path, "cdrom", 5) == 0 || strncmp(path, "cdfs", 4) == 0);
}

static bool join_path(char *out, const char *dir, const char *name) {
  size_t n = strlen(dir);
  const char *sep = n && dir[n-1] != '/' && dir[n-1] != ':' ? "/" : "";
  int used = snprintf(out, SMW_PATH, "%s%s%s", dir, sep, name);
  return used >= 0 && used < SMW_PATH;
}

bool smw_boot_folder(const char *elf, char folder[SMW_PATH]) {
  if (!elf || strlen(elf) >= SMW_PATH) return false;
  strcpy(folder, elf);
  for (char *p = folder; *p; ++p)
    if (*p == '\\') *p = '/';
  char *slash = strrchr(folder, '/');
  char *colon = strchr(folder, ':');
  if (slash) slash[1] = '\0';
  else if (colon) colon[1] = '\0';
  else { folder[0] = '\0'; return false; }
  return true;
}

/* ---------------------------------------------------------------- ROM --- */

static RomResult verify_rom(uint8_t *data, uint32_t *crc) {
  *crc = (uint32_t)crc32(0, data, SMW_ROM_BYTES);
  return *crc == UINT32_C(0xB19ED489) ? ROM_GOOD : ROM_BAD_CRC;
}

static RomResult read_zip_member(unzFile z, unsigned size, uint8_t **out, uint32_t *crc) {
  if (size != SMW_ROM_BYTES && size != SMW_ROM_BYTES + 512) return ROM_BAD_SIZE;
  if (unzOpenCurrentFile(z) != UNZ_OK) return ROM_IO_ERROR;
  uint8_t *buf = malloc(size);
  if (!buf) { unzCloseCurrentFile(z); return ROM_NO_MEMORY; }
  unsigned pos = 0;
  while (pos < size) {
    int n = unzReadCurrentFile(z, buf + pos, size - pos);
    if (n <= 0) break;
    pos += (unsigned)n;
  }
  int closed = unzCloseCurrentFile(z);
  RomResult result = ROM_IO_ERROR;
  if (closed == UNZ_OK && pos == size) {
    if (size != SMW_ROM_BYTES) memmove(buf, buf + 512, SMW_ROM_BYTES);
    result = verify_rom(buf, crc);
  }
  if (result == ROM_GOOD) *out = buf;
  else free(buf);
  return result;
}

static RomResult read_zip(const char *path, uint8_t **out, uint32_t *crc) {
  FILE *probe = fopen(path, "rb");
  if (!probe) return ROM_MISSING;
  fclose(probe);
  unzFile z = unzOpen(path);
  if (!z) return ROM_BAD_ZIP;
  RomResult result = ROM_BAD_ZIP;
  for (int step = unzGoToFirstFile(z); step == UNZ_OK; step = unzGoToNextFile(z)) {
    unz_file_info info;
    char name[256];
    if (unzGetCurrentFileInfo(z, &info, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK) break;
    if (info.size_filename < 4 || info.size_filename >= sizeof(name)) continue;
    name[info.size_filename] = 0;
    const char *ext = name + info.size_filename - 4;
    if (strcasecmp(ext, ".sfc") != 0 && strcasecmp(ext, ".smc") != 0) continue;
    result = read_zip_member(z, info.uncompressed_size, out, crc);
    if (result == ROM_GOOD || result == ROM_NO_MEMORY) break;
  }
  unzClose(z);
  return result;
}

static RomResult read_rom(const char *path, uint8_t **out, uint32_t *crc) {
  FILE *f = fopen(path, "rb");
  if (!f) return ROM_MISSING;
  RomResult result = ROM_IO_ERROR;
  if (fseek(f, 0, SEEK_END) != 0) goto done;
  long size = ftell(f);
  if (size < 0) goto done;
  if (size != SMW_ROM_BYTES && size != SMW_ROM_BYTES + 512) {
    result = ROM_BAD_SIZE;
    goto done;
  }
  if (fseek(f, size - SMW_ROM_BYTES, SEEK_SET) != 0) goto done;
  uint8_t *data = malloc(SMW_ROM_BYTES);
  if (!data) { result = ROM_NO_MEMORY; goto done; }
  if (fread(data, 1, SMW_ROM_BYTES, f) == SMW_ROM_BYTES) {
    result = verify_rom(data, crc);
    if (result == ROM_GOOD) {
      *out = data;
      goto done;
    }
  }
  free(data);
done:
  fclose(f);
  return result;
}

/* Tries every known name in every folder. A file that exists but is not the
   expected cartridge does not stop the search; it is only reported when no
   valid copy turns up anywhere. */
RomResult smw_find_rom(const char *boot, uint8_t **rom, char path[SMW_PATH], uint32_t *crc) {
  const char *folders[] = {boot, "mass:/APPS/SMW/", "mass0:/APPS/SMW/",
                          (boot && *boot) ? NULL : "cdrom0:/"};
  static const char *names[] = {"Super Mario World (USA).zip", "SMW.ZIP", "smw.zip",
    "SMW.SFC", "SMW.SMC", "Super Mario World (USA).sfc", "Super Mario World (USA).smc",
    "smw.sfc", "smw.smc"};
  RomResult first_error = ROM_MISSING;
  char error_path[SMW_PATH] = "";
  uint32_t error_crc = 0;
  *rom = NULL;
  for (size_t i = 0; i < sizeof(folders)/sizeof(folders[0]); ++i) {
    const char *dir = folders[i];
    if (!dir || !*dir) continue;
    bool disc = smw_path_is_disc(dir);
    for (size_t j = 0; j < sizeof(names)/sizeof(names[0]); ++j) {
      if (!join_path(path, dir, names[j])) continue;
      bool zip = strcasecmp(strrchr(names[j], '.'), ".zip") == 0;
      RomResult result = zip ? read_zip(path, rom, crc) : read_rom(path, rom, crc);
      if (disc && result == ROM_MISSING && strlen(path) + 2 < SMW_PATH) {
        strcat(path, ";1");
        result = zip ? read_zip(path, rom, crc) : read_rom(path, rom, crc);
      }
      if (result == ROM_GOOD) {
        if (disc) folder_dir[0] = '\0';
        else strcpy(folder_dir, dir);
        return ROM_GOOD;
      }
      if (result == ROM_NO_MEMORY) return result;
      if (result != ROM_MISSING && first_error == ROM_MISSING) {
        first_error = result;
        strcpy(error_path, path);
        error_crc = *crc;
      }
    }
  }
  if (first_error != ROM_MISSING) {
    strcpy(path, error_path);
    *crc = error_crc;
  }
  return first_error;
}

/* ------------------------------------------------------- file access --- */

/* Detail of the last failed memory card operation during a save, shown
   after the codes as "M<step><result>", e.g. "M2-4". Steps: 1 create the
   folder, 2 open for writing, 3 write, 4 close, 5 open to check, 6 read to
   check, 7 contents differ. The result is the raw MCSERV value. */
static bool card_recording;
static int card_step, card_result;
static void card_fail(int step, int result) {
  if (card_recording && !card_step) { card_step = step; card_result = result; }
}

#ifdef _EE
static uint8_t card_buf[4096] __attribute__((aligned(64)));

static int card_wait(void) {
  int result = -1;
  mcSync(0, NULL, &result);
  return result;
}

/* "mc0:/SMWPS2/SMW.SRM" -> port 0, "/SMWPS2/SMW.SRM" */
static bool card_path(const char *path, int *port, const char **name) {
  if (strncmp(path, "mc", 2) != 0 || (path[2] != '0' && path[2] != '1') || path[3] != ':')
    return false;
  *port = path[2] - '0';
  *name = path + 4;
  return true;
}

static int card_read(int port, const char *name, uint8_t *buf, int cap) {
  if (cap > (int)sizeof(card_buf) || mcOpen(port, 0, name, CARD_O_RDONLY) != 0) return -1;
  int fd = card_wait();
  if (fd < 0) { card_fail(5, fd); return -1; }
  int got = -1;
  if (mcRead(fd, card_buf, cap) == 0) got = card_wait();
  if (got < 0) card_fail(6, got);
  if (mcClose(fd) == 0) card_wait();
  if (got > 0) memcpy(buf, card_buf, got);
  return got;
}

static bool card_write(int port, const char *name, const uint8_t *buf, int size) {
  int submitted = mcOpen(port, 0, name, CARD_O_RDWR | CARD_O_CREAT);
  if (submitted != 0) { card_fail(2, submitted); return false; }
  int fd = card_wait();
  if (fd == -5) {
    submitted = mcDelete(port, 0, name);
    if (submitted != 0) { card_fail(2, submitted); return false; }
    int deleted = card_wait();
    if (deleted != 0) { card_fail(2, deleted); return false; }
    submitted = mcOpen(port, 0, name, CARD_O_RDWR | CARD_O_CREAT);
    if (submitted != 0) { card_fail(2, submitted); return false; }
    fd = card_wait();
  }
  if (fd < 0) { card_fail(2, fd); return false; }
  /* Written in card_buf-sized pieces: the icon is larger than one buffer. */
  int put = 0, closed = -1;
  while (put < size) {
    int piece = size - put < (int)sizeof(card_buf) ? size - put : (int)sizeof(card_buf);
    memcpy(card_buf, buf + put, piece);
    int wrote = -1;
    if (mcWrite(fd, card_buf, piece) == 0) wrote = card_wait();
    if (wrote != piece) { card_fail(3, wrote); put = -1; break; }
    put += wrote;
  }
  if (mcClose(fd) == 0) closed = card_wait();
  if (closed != 0) card_fail(4, closed);
  return put == size && closed == 0;
}
#endif

static int file_read(const char *path, uint8_t *buf, int cap) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) return card_read(port, name, buf, cap);
#endif
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  int n = (int)fread(buf, 1, cap, f);
  bool failed = ferror(f) != 0;
  fclose(f);
  return failed ? -1 : n;
}

static bool file_write(const char *path, const uint8_t *buf, int size) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) return card_write(port, name, buf, size);
#endif
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(buf, 1, size, f) == (size_t)size;
  return fclose(f) == 0 && ok;
}

static void file_remove(const char *path) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) {
    if (mcDelete(port, 0, name) == 0) card_wait();
    return;
  }
#endif
  remove(path);
}

static void make_folder(const char *dir) {
  char folder[SMW_PATH];
  snprintf(folder, sizeof(folder), "%s", dir);
  size_t n = strlen(folder);
  if (n && folder[n-1] == '/') folder[n-1] = '\0';
#ifdef _EE
  int port;
  const char *name;
  if (card_path(folder, &port, &name)) {
    if (mcMkDir(port, 0, name) == 0) {
      int result = card_wait();
      if (result != 0 && result != -4) card_fail(1, result);  /* -4 = already exists */
    }
    return;
  }
#endif
  mkdir(folder, 0777);
}

/* --------------------------------------------------------- save data --- */

/* The raw .SRM is compatible with the SNES version. Only the temporary
   journal carries a header so we can identify incomplete writes without
   depending on rename support. */
static void journal_header(const uint8_t *save, uint8_t header[8]) {
  uint32_t crc = (uint32_t)crc32(0, save, SMW_SRAM_BYTES);
  memcpy(header, "SMWJ", 4);
  for (unsigned i = 0; i < 4; ++i) header[4 + i] = (uint8_t)(crc >> (8 * i));
}

static bool read_save(const char *path, uint8_t *save, bool journal) {
  uint8_t data[8 + SMW_SRAM_BYTES + 1];
  int want = SMW_SRAM_BYTES + (journal ? 8 : 0);
  if (file_read(path, data, want + 1) != want) return false;  /* exact size */
  const uint8_t *body = data + (journal ? 8 : 0);
  if (journal) {
    uint8_t expected[8];
    journal_header(body, expected);
    if (memcmp(data, expected, sizeof(expected)) != 0) return false;
  }
  memcpy(save, body, SMW_SRAM_BYTES);
  return true;
}

static bool write_save(const char *path, const uint8_t *save, bool journal) {
  uint8_t data[8 + SMW_SRAM_BYTES], verify[SMW_SRAM_BYTES];
  int n = 0;
  if (journal) { journal_header(save, data); n = 8; }
  memcpy(data + n, save, SMW_SRAM_BYTES);
  n += SMW_SRAM_BYTES;
  if (!file_write(path, data, n)) return false;
  if (!read_save(path, verify, journal)) { card_fail(6, 0); return false; }
  if (memcmp(save, verify, sizeof(verify)) != 0) { card_fail(7, 0); return false; }
  return true;
}

static CardState card_state(int port) {
  if (!memory_card_ok) return CARD_NO_DRIVER;
#ifdef _EE
  int type = 0, free_clusters = 0, format = 0, result = -10;
  /* Twice: the first call after a card swap only reports the change. */
  for (int i = 0; i < 2; ++i) {
    if (mcGetInfo(port, 0, &type, &free_clusters, &format) != 0) return CARD_ABSENT;
    mcSync(0, NULL, &result);
  }
  if (result < -2 || type == MC_TYPE_NONE) return CARD_ABSENT;
  if (type != MC_TYPE_PS2) return CARD_WRONG_TYPE;
  if (result == -2) return CARD_UNFORMATTED;
  return CARD_READY;
#else
  (void)port;
  return CARD_READY;
#endif
}

static const char *store_dir(int store) {
  return store == STORE_FOLDER ? folder_dir : kCardDirs[store - STORE_MC0];
}

/* 0 when the place can be used, otherwise an error code. */
static int store_check(int store) {
  if (store == STORE_FOLDER) return folder_dir[0] ? 0 : ERR_NOT_READY;
  switch (card_state(store - STORE_MC0)) {
  case CARD_READY: return 0;
  case CARD_UNFORMATTED: return ERR_UNFORMATTED;
  case CARD_WRONG_TYPE: return ERR_WRONG_TYPE;
  case CARD_NO_DRIVER: return ERR_NO_DRIVER;
  default: return ERR_NOT_READY;
  }
}

static bool load_from(const char *dir, uint8_t *save) {
  char path[SMW_PATH];
  if (join_path(path, dir, "SMW.TMP") && read_save(path, save, true)) return true;
  if (join_path(path, dir, "SMW.SRM") && read_save(path, save, false)) return true;
  return join_path(path, dir, "SMW.BAK") && read_save(path, save, false);
}

void smw_load_progress(uint8_t *save) {
  active_store = -1;
  for (int store = 0; store < STORE_COUNT; ++store) {
    if (store_check(store) != 0) continue;
    if (active_store < 0) active_store = store;  /* preferred place for new saves */
    if (load_from(store_dir(store), save)) {
      active_store = store;
      return;
    }
  }
}

const char *smw_save_location(void) {
  switch (active_store) {
  case STORE_FOLDER: return strncmp(folder_dir, "mass", 4) == 0 ? "pendrive (APPS/SMW)" : folder_dir;
  case STORE_MC0: return "Memory Card 1 (pasta SMWPS2)";
  case STORE_MC1: return "Memory Card 2 (pasta SMWPS2)";
  default: return "NENHUM - insira um Memory Card formatado";
  }
}

void smw_storage_report(char *out, size_t size) {
  static const char *const kState[] = {"pronto", "sem cartao", "nao formatado",
                                       "cartao de PS1 (nao serve)", "driver nao carregou"};
  int used = snprintf(out, size, "Pendrive: %s\n",
                      folder_dir[0] ? "em uso" : "nao usado (jogo aberto pela ISO)");
  for (int port = 0; port < 2 && used >= 0 && (size_t)used < size; ++port)
    used += snprintf(out + used, size - used, "Memory Card %d: %s\n", port + 1,
                     kState[card_state(port)]);
}

const char *smw_save_error_text(void) { return error_text; }

/* icon.sys + 3D icon, so the PS2 browser shows "Super Mario World" instead
   of "corrupted data". Written once per card; a failure here never blocks
   the save itself. */
static void write_card_icon(const char *dir) {
#ifdef _EE
  char path[SMW_PATH];
  uint8_t probe[sizeof(kSmwIconSys) + 1];
  bool recording = card_recording;
  card_recording = false;
  if (join_path(path, dir, "icon.sys") &&
      file_read(path, probe, sizeof(probe)) != (int)sizeof(kSmwIconSys)) {
    char icon[SMW_PATH];
    if (join_path(icon, dir, SMW_ICON_NAME) && file_write(icon, kSmwIcon, sizeof(kSmwIcon)))
      file_write(path, kSmwIconSys, sizeof(kSmwIconSys));
  }
  card_recording = recording;
#else
  (void)dir;
#endif
}

/* 0 on success, otherwise the step that failed. */
static int save_to(int store, const uint8_t *save) {
  const char *dir = store_dir(store);
  char primary[SMW_PATH], backup[SMW_PATH], pending[SMW_PATH];
  if (!join_path(primary, dir, "SMW.SRM") ||
      !join_path(backup, dir, "SMW.BAK") ||
      !join_path(pending, dir, "SMW.TMP")) return ERR_PATH;
  if (store != STORE_FOLDER) {
    make_folder(dir);
    write_card_icon(dir);
  }
  uint8_t previous[SMW_SRAM_BYTES], staged[SMW_SRAM_BYTES];
  bool has_primary = read_save(primary, previous, false);
  if (read_save(pending, staged, true) &&
      (!has_primary || memcmp(staged, previous, sizeof(staged)))) {
    if (!write_save(primary, staged, false)) return ERR_RECOVER;
    memcpy(previous, staged, sizeof(previous));
    has_primary = true;
  }
  if (!write_save(pending, save, true)) return ERR_TEMP;
  if (has_primary && memcmp(previous, save, sizeof(previous)) &&
      !write_save(backup, previous, false)) return ERR_BACKUP;
  if (!write_save(primary, save, false)) return ERR_MAIN;
  file_remove(pending);  /* If removal fails, both copies contain the same save. */
  return 0;
}

bool smw_save_progress(const uint8_t *save) {
  int order[STORE_COUNT], count = 0;
  if (active_store >= 0) order[count++] = active_store;
  for (int store = 0; store < STORE_COUNT; ++store)
    if (store != active_store) order[count++] = store;

  size_t used = 0;
  error_text[0] = '\0';
  card_recording = true;
  card_step = card_result = 0;
  for (int i = 0; i < count; ++i) {
    int store = order[i];
    int code = store_check(store);
    /* The USB slot is simply unused when the game came from a disc. */
    if (code == ERR_NOT_READY && store == STORE_FOLDER) continue;
    if (code == 0) code = save_to(store, save);
    if (code == 0) {
      active_store = store;
      error_text[0] = '\0';
      card_recording = false;
      return true;
    }
    if (used + 4 < sizeof(error_text))
      used += snprintf(error_text + used, sizeof(error_text) - used, "%s%d%d",
                       used ? " " : "", store + 1, code);
  }
  card_recording = false;
  if (card_step && used + 8 < sizeof(error_text))
    snprintf(error_text + used, sizeof(error_text) - used, " M%d%d", card_step, card_result);
  return false;
}
