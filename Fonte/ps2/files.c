#include "files.h"
#ifdef _EE
#include "smw_icon.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unzip.h>
#include <zlib.h>
#ifdef _EE
#include <libmc.h>
#include <timer.h>
/* IOP open flags used by MCSERV (not the newlib values). */
enum { CARD_O_RDONLY = 0x0001, CARD_O_RDWR = 0x0003, CARD_O_CREAT = 0x0200 };
/* mcSetFileInfo: change only the attributes; readable + writable + executable. */
enum { CARD_INFO_ATTR = 0x0004, CARD_ATTR_RWX = 0x0007 };
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
       ERR_TEMP = 4, ERR_BACKUP = 5, ERR_MAIN = 6, ERR_PRESERVE = 7,
       ERR_NO_DRIVER = 8, ERR_PATH = 9 };
/* Room left on a memory card besides the icon (KB), so the icon never takes
   the space a save needs. */
enum { ICON_SPARE_KB = 16 };

static char folder_dir[SMW_PATH];
static bool rom_on_disc;           /* the game came from the ISO (cdrom0:) */
static bool memory_card_ok;
static int active_store = -1;
static uint32_t max_generation;    /* newest save generation seen */
static int card_free_kb[2];
static char error_text[48];
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
        rom_on_disc = disc;
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

/* file_read() results besides the byte count. */
enum { FILE_ABSENT = -1, FILE_ERROR = -2 };

/* Detail of the last failed operation during a save, shown after the codes
   as "M<step><result><file>" for the memory card (e.g. "M2-4T") and
   "U<step><errno><file>" for the USB drive. Steps: 1 create the folder,
   2 open for writing, 3 write, 4 close, 5 open to read, 6 read, 7 contents
   differ. The result is the raw MCSERV value (memory card) or errno (USB).
   File: T = SMW.TMP, S = SMW.SRM, B = SMW.BAK. */
static bool card_recording;
static int card_step, card_result, usb_step, usb_result, fail_count;
static char card_file, usb_file;
static char file_letter(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot) return 0;
  if (!strcmp(dot, ".BAD")) return 'X';
  if (!strcmp(dot, ".TMP")) return 'T';
  if (!strcmp(dot, ".SRM")) return 'S';
  if (!strcmp(dot, ".BAK")) return 'B';
  return 0;
}
static void card_fail_on(const char *name, int step, int result) {
  if (!card_recording) return;
  card_step = step, card_result = result, card_file = name ? file_letter(name) : 0;
  ++fail_count;
}
static void card_fail(int step, int result) { card_fail_on(NULL, step, result); }
static void usb_fail(const char *path, int step, int result) {
  if (!card_recording) return;
  usb_step = step, usb_result = result, usb_file = file_letter(path);
  ++fail_count;
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

/* Some save files end up on the card without read or write permission
   (saves from older versions of this port, or the way some BIOS memory
   card drivers create files): the driver opens them, then refuses to read
   or write them with -5. This gives such a file read, write and execute
   permission again, keeping its other attributes and its contents. */
static bool card_set_access(int port, const char *name) {
  static sceMcTblGetDir info __attribute__((aligned(64)));
  memset(&info, 0, sizeof(info));
  unsigned attr = 0;
  if (mcGetDir(port, 0, name, 0, 1, &info) == 0 && card_wait() == 1) attr = info.AttrFile;
  memset(&info, 0, sizeof(info));
  info.AttrFile = (unsigned short)(attr | CARD_ATTR_RWX);
  if (mcSetFileInfo(port, 0, name, &info, CARD_INFO_ATTR) != 0) return false;
  return card_wait() == 0;
}

static int card_delete(int port, const char *name) {
  int submitted = mcDelete(port, 0, name);
  return submitted != 0 ? submitted : card_wait();
}

/* Reads at most cap bytes, never past the end of the file: some BIOS
   memory card drivers refuse a read that asks for more than the file has. */
static int card_read(int port, const char *name, uint8_t *buf, int cap) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    int submitted = mcOpen(port, 0, name, CARD_O_RDONLY);
    if (submitted != 0) { card_fail_on(name, 5, submitted); return FILE_ERROR; }
    int fd = card_wait();
    if (fd == -4) return FILE_ABSENT;
    if (fd == -5 && attempt == 0 && card_set_access(port, name)) continue;
    if (fd < 0) { card_fail_on(name, 5, fd); return FILE_ERROR; }
    int size = -1, got = -1;
    if (mcSeek(fd, 0, 2) == 0) size = card_wait();         /* SEEK_END */
    if (size >= 0 && mcSeek(fd, 0, 0) == 0 && card_wait() == 0) {
      int want = size < cap ? size : cap;
      /* Em pedacos do card_buf (v0.14: o TRAPACAS.TXT tem ate 16 KB; antes
         uma leitura maior que 4 KB era recusada). Nunca alem do fim (C11). */
      got = 0;
      while (got < want) {
        int piece = want - got < (int)sizeof(card_buf) ? want - got : (int)sizeof(card_buf);
        int r = -1;
        if (mcRead(fd, card_buf, piece) == 0) r = card_wait();
        if (r < 0) {
          got = r;
          break;
        }
        memcpy(buf + got, card_buf, (size_t)r);
        got += r;
        if (r < piece) break;
      }
    } else if (size < 0) {
      got = size;
    }
    if (mcClose(fd) == 0) card_wait();
    if (got == -5 && attempt == 0 && card_set_access(port, name)) continue;
    if (got < 0) { card_fail_on(name, 6, got); return FILE_ERROR; }
    return got;
  }
  return FILE_ERROR;
}

/* Always writes a new file: an old one may refuse writing (see
   card_set_access). The data is safe meanwhile: SMW.TMP is written and
   checked before SMW.SRM and SMW.BAK are replaced. */
static bool card_write(int port, const char *name, const uint8_t *buf, int size) {
  int removed = card_delete(port, name);
  if (removed == -5 && card_set_access(port, name)) removed = card_delete(port, name);
  if (removed != 0 && removed != -4) { card_fail_on(name, 2, removed); return false; }  /* -4: none */
  for (int attempt = 0; ; ++attempt) {
    int submitted = mcOpen(port, 0, name, CARD_O_RDWR | CARD_O_CREAT);
    if (submitted != 0) { card_fail_on(name, 2, submitted); return false; }
    int fd = card_wait();
    if (fd == -5 && attempt == 0 && card_set_access(port, name)) continue;
    if (fd < 0) { card_fail_on(name, 2, fd); return false; }
    /* Written in card_buf-sized pieces: the icon is larger than one buffer. */
    int put = 0, closed = -1;
    bool denied = false;
    while (put < size) {
      int piece = size - put < (int)sizeof(card_buf) ? size - put : (int)sizeof(card_buf);
      memcpy(card_buf, buf + put, piece);
      int wrote = -1;
      if (mcWrite(fd, card_buf, piece) == 0) wrote = card_wait();
      if (wrote != piece) {
        denied = wrote == -5 && put == 0 && attempt == 0;
        if (!denied) card_fail_on(name, 3, wrote);
        put = -1;
        break;
      }
      put += wrote;
    }
    if (mcClose(fd) == 0) closed = card_wait();
    if (denied) {
      if (card_set_access(port, name)) continue;
      card_fail_on(name, 3, -5);
      return false;
    }
    if (closed != 0) card_fail_on(name, 4, closed);
    return put == size && closed == 0;
  }
}
#endif


/* Caminho no pendrive pelo bdmfs_fatfs (o driver do proprio ELF). Os testes
   no PC trocam por uma pasta deles com SMW_TESTE_FATFS. */
static bool caminho_fatfs(const char *path) {
#ifdef SMW_TESTE_FATFS
  return SMW_TESTE_FATFS(path);
#else
  return strncmp(path, "mass", 4) == 0;
#endif
}

static int file_read(const char *path, uint8_t *buf, int cap) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) return card_read(port, name, buf, cap);
#endif
  errno = 0;
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (errno == ENOENT || errno == 0) return FILE_ABSENT;
    /* v0.13.5: o driver do pendrive (bdmfs_fatfs, "mass:") devolve o codigo
       do FatFs negativo e a libc do PS2SDK poe esse numero em errno:
       FR_NO_FILE (4) para arquivo que nao existe e FR_NO_PATH (5) para pasta
       que nao existe. Antes eles contavam como "nao pode ser lido" (SMW_LOG.TXT
       da v0.13.4 no console) e, com o SMW.TMP sempre faltando, o save no
       pendrive parava em ERR_PRESERVE e ia para o Memory Card. Em outros
       drivers 5 pode ser EIO (erro de leitura): la nada muda. */
    enum { FR_NO_FILE = 4, FR_NO_PATH = 5 };  /* FRESULT do FatFs (ff.h) */
    if (caminho_fatfs(path) && (errno == FR_NO_FILE || errno == FR_NO_PATH)) return FILE_ABSENT;
    usb_fail(path, 5, errno);
    return FILE_ERROR;
  }
  int n = (int)fread(buf, 1, cap, f);
  bool failed = ferror(f) != 0;
  if (failed) usb_fail(path, 6, errno);
  fclose(f);
  return failed ? FILE_ERROR : n;
}

static bool file_write(const char *path, const uint8_t *buf, int size) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) return card_write(port, name, buf, size);
#endif
  errno = 0;
  FILE *f = fopen(path, "wb");
  if (!f) { usb_fail(path, 2, errno); return false; }
  bool ok = fwrite(buf, 1, size, f) == (size_t)size;
  if (!ok) usb_fail(path, 3, errno);
  errno = 0;
  bool closed = fclose(f) == 0;
  if (!closed) usb_fail(path, 4, errno);
  return closed && ok;
}

static void file_remove(const char *path) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) {
    card_delete(port, name);
    return;
  }
#endif
  remove(path);
}

/* Renames dir/from to dir/to (same folder). */
static bool file_rename(const char *dir, const char *from, const char *to) {
  char a[SMW_PATH], b[SMW_PATH];
  if (!join_path(a, dir, from) || !join_path(b, dir, to)) return false;
#ifdef _EE
  int port;
  const char *name;
  if (card_path(a, &port, &name)) {
    if (mcRename(port, 0, name, to) != 0) return false;
    return card_wait() == 0;
  }
#endif
  return rename(a, b) == 0;
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

/* Save file formats.
   Current (v0.8): the 2048 bytes of SNES SRAM followed by a 16-byte trailer:
     "SMW2", generation (u32 LE), CRC32 of the SRAM + generation (u32 LE),
     CRC32 of the 12 trailer bytes before it (u32 LE).
   The generation grows by one on every save, on whatever medium it lands,
   so the most recent copy can be found among USB, memory card 1 and 2.
   Older versions wrote, and are still read:
     raw SRAM, 2048 bytes (SMW.SRM / SMW.BAK), with no checksum of its own;
     journal "SMWJ" + CRC32 (u32 LE) + SRAM (SMW.TMP).
   An old file has generation 0: any file of this version is newer. Among old
   files, the one with more game progress wins (see srm_score). */
enum { SAVE_TRAILER = 16, SAVE_BYTES = SMW_SRAM_BYTES + SAVE_TRAILER,
       JOURNAL_BYTES = SMW_SRAM_BYTES + 8 };

typedef enum { COPY_ABSENT, COPY_UNREADABLE, COPY_CORRUPT, COPY_VALID } CopyState;
typedef struct {
  CopyState state;
  bool legacy;            /* written by an older version (no generation) */
  uint32_t generation;
  int score;
  uint8_t sram[SMW_SRAM_BYTES];
} SaveCopy;

/* The three files kept in each place, in the order they are tried when two
   copies are equally recent. */
enum { FILE_SRM, FILE_TMP, FILE_BAK, FILE_COUNT };
static const char *const kSaveNames[FILE_COUNT] = {"SMW.SRM", "SMW.TMP", "SMW.BAK"};

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put_le32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t save_crc(const uint8_t *sram, uint32_t generation) {
  uint8_t g[4];
  put_le32(g, generation);
  uLong crc = crc32(0, sram, SMW_SRAM_BYTES);
  return (uint32_t)crc32(crc, g, 4);
}

static void encode_save(const uint8_t *sram, uint32_t generation, uint8_t out[SAVE_BYTES]) {
  memcpy(out, sram, SMW_SRAM_BYTES);
  uint8_t *t = out + SMW_SRAM_BYTES;
  memcpy(t, "SMW2", 4);
  put_le32(t + 4, generation);
  put_le32(t + 8, save_crc(sram, generation));
  put_le32(t + 12, (uint32_t)crc32(0, t, 12));
}

/* The game keeps 3 save files, each twice (at +0 and +429), as 141 bytes
   plus a 16-bit checksum that makes the sum 0x5A5A (SaveGame, 009bc9).
   Returns the copy the game would use (the same test as 009db5) or NULL. */
static const uint8_t *srm_slot(const uint8_t *sram, int slot) {
  static const uint16_t kSlot[3] = {0x000, 0x08f, 0x11e};
  for (int copy = 0; copy < 2; ++copy) {
    const uint8_t *p = sram + kSlot[slot] + copy * 429;
    uint16_t sum = (uint16_t)(p[141] | p[142] << 8);
    for (int i = 0; i < 141; ++i) sum += p[i];
    if (sum == 0x5A5A) return p;
  }
  return NULL;
}

/* Game progress, to compare copies without a generation: exits completed
   (byte 140 of a file, the number on the file select screen) and switch
   palaces pressed (bytes 133-136), over the 3 files. */
static int srm_score(const uint8_t *sram, int *files) {
  int score = 0, n = 0;
  for (int slot = 0; slot < 3; ++slot) {
    const uint8_t *p = srm_slot(sram, slot);
    if (!p) continue;
    ++n;
    score += 1 + 8 * (p[140] < 0x60 ? p[140] : 0);
    for (int i = 133; i <= 136; ++i) score += p[i] ? 2 : 0;
  }
  if (files) *files = n;
  return score;
}

static bool all_same(const uint8_t *p, int n) {
  for (int i = 1; i < n; ++i)
    if (p[i] != p[0]) return false;
  return true;
}

/* Reads and classifies one file. */
static void read_copy(const char *path, SaveCopy *c) {
  uint8_t data[SAVE_BYTES + 1];
  memset(c, 0, sizeof(*c));
  int n = file_read(path, data, sizeof(data));
  if (n == FILE_ABSENT) { c->state = COPY_ABSENT; return; }
  if (n < 0) { c->state = COPY_UNREADABLE; return; }
  c->state = COPY_CORRUPT;
  if (n == SAVE_BYTES) {
    const uint8_t *t = data + SMW_SRAM_BYTES;
    if (memcmp(t, "SMW2", 4) != 0 || le32(t + 12) != (uint32_t)crc32(0, t, 12)) return;
    c->generation = le32(t + 4);
    if (le32(t + 8) != save_crc(data, c->generation)) return;
    memcpy(c->sram, data, SMW_SRAM_BYTES);
  } else if (n == JOURNAL_BYTES) {
    if (memcmp(data, "SMWJ", 4) != 0 ||
        le32(data + 4) != (uint32_t)crc32(0, data + 8, SMW_SRAM_BYTES)) return;
    memcpy(c->sram, data + 8, SMW_SRAM_BYTES);
    c->legacy = true;
  } else if (n == SMW_SRAM_BYTES) {
    /* No checksum of its own: accept it if the game would find a save file
       in it, or if it is still blank (a game started but never saved). */
    int files = 0;
    srm_score(data, &files);
    if (!files && !all_same(data, SMW_SRAM_BYTES)) return;
    memcpy(c->sram, data, SMW_SRAM_BYTES);
    c->legacy = true;
  } else {
    return;
  }
  c->state = COPY_VALID;
  c->score = srm_score(c->sram, NULL);
}

/* > 0 if a is more recent than b. Valid copies only. */
static int copy_cmp(const SaveCopy *a, const SaveCopy *b) {
  if (a->generation != b->generation) return a->generation > b->generation ? 1 : -1;
  if (a->score != b->score) return a->score > b->score ? 1 : -1;
  return 0;
}

static bool write_copy(const char *path, const uint8_t *sram, uint32_t generation) {
  static SaveCopy check;
  uint8_t data[SAVE_BYTES];
  encode_save(sram, generation, data);
  if (!file_write(path, data, SAVE_BYTES)) {
    /* A write that stopped half way (e.g. card full) leaves a file that is
       not a valid save: remove it so it is not reported as corrupt later.
       A file that is still valid (the write never started) stays. */
    bool recording = card_recording;
    card_recording = false;
    read_copy(path, &check);
    if (check.state == COPY_CORRUPT) file_remove(path);
    card_recording = recording;
    return false;
  }
  int fails = fail_count;
  read_copy(path, &check);
  bool card = strncmp(path, "mc", 2) == 0;
  if (check.state != COPY_VALID || check.generation != generation) {
    if (fail_count == fails) {  /* read fine, but wrong size or checksum */
      if (card) card_fail_on(path, 6, 0);
      else usb_fail(path, 6, 0);
    }
    return false;
  }
  if (memcmp(check.sram, sram, SMW_SRAM_BYTES) != 0) {
    if (card) card_fail_on(path, 7, 0);
    else usb_fail(path, 7, 0);
    return false;
  }
  return true;
}

/* Quantas vezes o libmc avisou cartao trocado em cada entrada (-1 ou -2
   na consulta depois da troca). */
static unsigned card_changes[2];

static CardState card_state(int port) {
  if (!memory_card_ok) return CARD_NO_DRIVER;
#ifdef _EE
  int type = 0, free_clusters = 0, format = 0, result = -10;
  /* Twice: the first call after a card swap only reports the change. */
  for (int i = 0; i < 2; ++i) {
    if (mcGetInfo(port, 0, &type, &free_clusters, &format) != 0) return CARD_ABSENT;
    mcSync(0, NULL, &result);
    if (result == -1 || result == -2) ++card_changes[port];
  }
  if (result < -2 || type == MC_TYPE_NONE) return CARD_ABSENT;
  if (type != MC_TYPE_PS2) return CARD_WRONG_TYPE;
  if (result == -2) return CARD_UNFORMATTED;
  card_free_kb[port] = free_clusters;
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

static const char *const kStoreNames[STORE_COUNT] = {"PENDRIVE", "MEMORY CARD 1", "MEMORY CARD 2"};

/* What was found at start-up, for the boot screen and the in-game notice. */
static struct {
  int store, file;         /* copy loaded, or -1 */
  bool legacy;
  uint32_t generation;
  int bad;                 /* files found but unreadable or corrupt */
  int bad_store[STORE_COUNT * FILE_COUNT], bad_file[STORE_COUNT * FILE_COUNT];
  CopyState bad_state[STORE_COUNT * FILE_COUNT];
} load_info = {-1, -1, false, 0, 0, {0}, {0}, {0}};

/* v0.13.8: o que o SMW.SRM do lugar ativo tem (o progresso carregado ou o
   ultimo save que deu certo). Ao escolher o arquivo na tela inicial o jogo
   regrava a SRAM (copia o arquivo para a copia de reserva), quase sempre
   sem mudar nada; no Memory Card isso parava o jogo por ate 5 s. */
static uint8_t last_written[SMW_SRAM_BYTES];
static bool last_written_ok;
static unsigned written_changes;  /* card_changes do lugar ativo nessa hora */
static bool icon_current(const char *dir);
static bool icon_missing[2];

static unsigned store_changes(int store) {
  return store >= STORE_MC0 ? card_changes[store - STORE_MC0] : 0;
}
static void remember_written(const uint8_t *save) {
  memcpy(last_written, save, SMW_SRAM_BYTES);
  written_changes = store_changes(active_store);
  last_written_ok = true;
}

static char notice[48];
static unsigned notice_serial;
static void set_notice(const char *text) {
  snprintf(notice, sizeof(notice), "%s", text);
  ++notice_serial;
}

void smw_load_progress(uint8_t *save) {
  static SaveCopy copy;  /* large: kept off the stack */
  static uint8_t best[SMW_SRAM_BYTES];
  SaveCopy best_info;
  bool found = false;
  memset(&best_info, 0, sizeof(best_info));
  load_info.store = load_info.file = -1;
  load_info.bad = 0;
  max_generation = 0;
  active_store = -1;
  last_written_ok = false;
  icon_missing[0] = icon_missing[1] = false;
  bool bak_valid[STORE_COUNT] = {false}, store_bad[STORE_COUNT] = {false};
  for (int store = 0; store < STORE_COUNT; ++store) {
    if (store_check(store) != 0) continue;
    if (active_store < 0) active_store = store;  /* preferred place for new saves */
    for (int file = 0; file < FILE_COUNT; ++file) {
      char path[SMW_PATH];
      if (!join_path(path, store_dir(store), kSaveNames[file])) continue;
      read_copy(path, &copy);
      if (copy.state == COPY_ABSENT) continue;
      if (copy.state != COPY_VALID) {
        int i = load_info.bad++;
        load_info.bad_store[i] = store, load_info.bad_file[i] = file, load_info.bad_state[i] = copy.state;
        store_bad[store] = true;
        continue;
      }
      if (file == FILE_BAK) bak_valid[store] = true;
      if (copy.generation > max_generation) max_generation = copy.generation;
      if (!found || copy_cmp(&copy, &best_info) > 0) {
        found = true;
        memcpy(best, copy.sram, SMW_SRAM_BYTES);
        best_info = copy;
        load_info.store = store, load_info.file = file;
        load_info.legacy = copy.legacy, load_info.generation = copy.generation;
      }
    }
  }
  if (found) {
    memcpy(save, best, SMW_SRAM_BYTES);
    active_store = load_info.store;
    /* So quando o SMW.SRM e o mais novo, no formato atual, com o SMW.BAK e
       sem arquivo ruim no mesmo lugar: senao o proximo save arruma o lugar
       (arquivo ruim em outro lugar o save nao mexe; revisao do Antigravity). */
    if (load_info.file == FILE_SRM && !load_info.legacy && !store_bad[active_store] &&
        bak_valid[active_store]) {
      remember_written(best);
      if (active_store >= STORE_MC0)
        icon_missing[active_store - STORE_MC0] = !icon_current(store_dir(active_store));
    }
    char text[48];
    snprintf(text, sizeof(text), "CARREGADO: %s", kStoreNames[load_info.store]);
    set_notice(text);
  } else {
    set_notice(load_info.bad ? "SAVE ILEGIVEL: VEJA LEIA-ME" : "NENHUM SAVE: JOGO NOVO");
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

void smw_load_report(char *out, size_t size) {
  static const char *const kPlace[STORE_COUNT] = {"pendrive", "Memory Card 1", "Memory Card 2"};
  int used;
  if (load_info.store >= 0)
    used = snprintf(out, size, "Progresso carregado de: %s, %s%s\n", kPlace[load_info.store],
                    kSaveNames[load_info.file],
                    load_info.legacy ? " (formato antigo)" : "");
  else if (load_info.bad)
    used = snprintf(out, size, "Progresso: SAVE ENCONTRADO, MAS NENHUMA COPIA PODE SER LIDA.\n"
                               "O jogo comeca vazio e os arquivos sao preservados (.BAD).\n");
  else
    used = snprintf(out, size, "Progresso: nenhum save encontrado (jogo novo).\n");
  for (int i = 0; i < load_info.bad && used >= 0 && (size_t)used < size; ++i)
    used += snprintf(out + used, size - used, "  ATENCAO: %s %s %s\n", kPlace[load_info.bad_store[i]],
                     kSaveNames[load_info.bad_file[i]],
                     load_info.bad_state[i] == COPY_UNREADABLE ? "nao pode ser lido" : "corrompido");
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

const char *smw_notice(unsigned *serial) {
  if (serial) *serial = notice_serial;
  return notice;
}

/* icon.sys + 3D icon, so the PS2 browser shows "Super Mario World" instead
   of "corrupted data". Written after the progress, only when the card has
   room for it, and never blocks or undoes a save. */
/* icon.sys desta versao no lugar (o desenho antigo conta como faltando). */
static bool icon_current(const char *dir) {
#ifdef _EE
  char path[SMW_PATH];
  uint8_t probe[sizeof(kSmwIconSys) + 1];
  return join_path(path, dir, "icon.sys") &&
         file_read(path, probe, sizeof(probe)) == (int)sizeof(kSmwIconSys) &&
         memcmp(probe, kSmwIconSys, sizeof(kSmwIconSys)) == 0;
#else
  (void)dir;
  return true;
#endif
}

/* v0.13.8: guarda em icon_missing (declarado acima) o cartao onde o icone
   ficou faltando (sem espaco): o proximo save, mesmo sem progresso novo,
   tenta de novo. */
static void write_card_icon(const char *dir, int port) {
#ifdef _EE
  char path[SMW_PATH];
  bool recording = card_recording;
  bool ok = true;
  card_recording = false;
  /* Regrava se faltar ou se for de uma versao anterior (desenho antigo). */
  if (!icon_current(dir)) {
    char icon[SMW_PATH];
    int iport;
    const char *name;
    int need_kb = (int)((sizeof(kSmwIcon) + 1023) / 1024 + 2);
    bool named = join_path(icon, dir, SMW_ICON_NAME) && join_path(path, dir, "icon.sys");
    ok = false;
    card_state(port);  /* refresh the free space */
    if (named && card_free_kb[port] >= need_kb + ICON_SPARE_KB) {
      /* Apaga o icone antigo antes: a gravacao nao trunca arquivos maiores. */
      if (card_path(icon, &iport, &name)) card_delete(iport, name);
      ok = file_write(icon, kSmwIcon, sizeof(kSmwIcon)) &&
           file_write(path, kSmwIconSys, sizeof(kSmwIconSys));
    }
  }
  icon_missing[port] = !ok;
  card_recording = recording;
#else
  (void)dir;
  (void)port;
#endif
}

/* A file that exists but cannot be read, or fails its checks, is renamed
   SMW_<S|T|B><1-9>.BAD instead of being overwritten. */
static bool preserve_bad(const char *dir, int file) {
  static const char kLetter[FILE_COUNT] = {'S', 'T', 'B'};
  for (int i = 1; i <= 9; ++i) {
    char name[16], path[SMW_PATH];
    snprintf(name, sizeof(name), "SMW_%c%d.BAD", kLetter[file], i);
    uint8_t probe[1];
    if (!join_path(path, dir, name) || file_read(path, probe, sizeof(probe)) != FILE_ABSENT) continue;
    return file_rename(dir, kSaveNames[file], name);
  }
  return false;
}

/* 0 on success, otherwise the step that failed. Order of writes, so that the
   newest copy that existed before always survives on this medium:
     1. SMW.BAK <- the most recent valid copy already here, if newer than BAK
     2. SMW.TMP <- the new save   (checked)
     3. SMW.SRM <- the new save   (checked)
     4. delete SMW.TMP (same as SMW.SRM)
   Unreadable or corrupt files are renamed .BAD before anything replaces
   them. */
/* v0.13.8: tempo de cada parte do ultimo save, para o SMW_LOG.TXT. */
static unsigned now_ms(void) {
#ifdef _EE
  u32 s, us;
  TimerBusClock2USec(GetTimerSystemTime(), &s, &us);
  return s * 1000u + us / 1000u;
#else
  return 0;
#endif
}
enum { STEP_READ, STEP_BACKUP, STEP_NEW, STEP_ICON, STEP_COUNT };
static unsigned save_step_ms[STEP_COUNT], save_step_at;
static void save_steps_start(void) {
  memset(save_step_ms, 0, sizeof(save_step_ms));
  save_step_at = now_ms();
}

static void save_step(int step) {
  unsigned t = now_ms();
  save_step_ms[step] = t - save_step_at;
  save_step_at = t;
}

const char *smw_save_steps(void) {
  static char text[64];
  snprintf(text, sizeof(text), "leitura %u, reserva %u, novo %u, icone %u", save_step_ms[STEP_READ],
           save_step_ms[STEP_BACKUP], save_step_ms[STEP_NEW], save_step_ms[STEP_ICON]);
  return text;
}

/* *backup_ok: deu certo e deixou um SMW.BAK valido (havia uma copia anterior). */
static int save_to(int store, const uint8_t *save, uint32_t generation, bool *backup_ok) {
  static SaveCopy have[FILE_COUNT];
  const char *dir = store_dir(store);
  char path[FILE_COUNT][SMW_PATH];
  save_steps_start();
  for (int f = 0; f < FILE_COUNT; ++f)
    if (!join_path(path[f], dir, kSaveNames[f])) return ERR_PATH;
  if (store != STORE_FOLDER) make_folder(dir);

  int newest = -1;
  for (int f = 0; f < FILE_COUNT; ++f) {
    read_copy(path[f], &have[f]);
    if (have[f].state == COPY_VALID && (newest < 0 || copy_cmp(&have[f], &have[newest]) > 0))
      newest = f;
  }
  save_step(STEP_READ);
  /* 1. backup of the previous state */
  if (newest >= 0 && newest != FILE_BAK &&
      (have[FILE_BAK].state != COPY_VALID ||
       memcmp(have[FILE_BAK].sram, have[newest].sram, SMW_SRAM_BYTES) != 0)) {
    if ((have[FILE_BAK].state == COPY_UNREADABLE || have[FILE_BAK].state == COPY_CORRUPT) &&
        !preserve_bad(dir, FILE_BAK)) return ERR_PRESERVE;
    if (!write_copy(path[FILE_BAK], have[newest].sram, have[newest].generation)) return ERR_BACKUP;
  }
  save_step(STEP_BACKUP);
  /* 2-3. the new save */
  for (int f = FILE_TMP; ; f = FILE_SRM) {
    if ((have[f].state == COPY_UNREADABLE || have[f].state == COPY_CORRUPT) && !preserve_bad(dir, f))
      return ERR_PRESERVE;
    if (!write_copy(path[f], save, generation)) return f == FILE_TMP ? ERR_TEMP : ERR_MAIN;
    if (f == FILE_SRM) break;
  }
  file_remove(path[FILE_TMP]);  /* If removal fails, it holds the same save as SMW.SRM. */
  save_step(STEP_NEW);
  if (store != STORE_FOLDER) write_card_icon(dir, store - STORE_MC0);
  save_step(STEP_ICON);
  /* Sem copia anterior valida o SMW.BAK nao foi gravado (o primeiro save). */
  *backup_ok = newest >= 0;
  return 0;
}

static SmwOptions pending_options;
static bool options_pending;
static bool write_options_to(int store, const SmwOptions *o);

/* O SMW.SRM do lugar ativo ja tem este progresso: o lugar continua la, o
   cartao nao foi trocado e o conteudo e o mesmo. */
static bool same_as_written(const uint8_t *save) {
  /* Primeiro a comparacao na memoria: progresso novo nao consulta o cartao. */
  if (!last_written_ok || active_store < 0 || memcmp(save, last_written, SMW_SRAM_BYTES) != 0)
    return false;
  if (store_check(active_store) != 0 || store_changes(active_store) != written_changes) {
    last_written_ok = false;
    return false;
  }
  if (active_store == STORE_FOLDER) {
    /* O pendrive pode ter saido, sido trocado ou o arquivo mudado no PC:
       rele o SMW.SRM e o SMW.BAK (rapido no pendrive; no Memory Card a
       troca e avisada pelo libmc). Revisoes do Codex. */
    static SaveCopy now;  /* grande: fora da pilha */
    char path[SMW_PATH];
    bool ok = join_path(path, folder_dir, kSaveNames[FILE_SRM]);
    if (ok) read_copy(path, &now);
    ok = ok && now.state == COPY_VALID && !now.legacy &&
         memcmp(now.sram, last_written, SMW_SRAM_BYTES) == 0 &&
         join_path(path, folder_dir, kSaveNames[FILE_BAK]);
    if (ok) read_copy(path, &now);
    if (!ok || now.state != COPY_VALID) {
      last_written_ok = false;
      return false;
    }
  }
  return true;
}

static bool active_icon_missing(void) {
  return active_store >= STORE_MC0 && icon_missing[active_store - STORE_MC0];
}

bool smw_save_unchanged(const uint8_t *save) {
  return !options_pending && !active_icon_missing() && same_as_written(save);
}

bool smw_save_progress(const uint8_t *save) {
  save_steps_start();
  if (same_as_written(save)) {
    /* Nada novo no progresso: so o ajuste trocado no jogo e o icone que
       faltava, se houver (no log, so o tempo do icone). */
    error_text[0] = '\0';
    if (options_pending && write_options_to(active_store, &pending_options)) options_pending = false;
    save_step_at = now_ms();
    if (active_icon_missing()) write_card_icon(store_dir(active_store), active_store - STORE_MC0);
    save_step(STEP_ICON);
    return true;
  }
  int order[STORE_COUNT], count = 0;
  if (active_store >= 0) order[count++] = active_store;
  for (int store = 0; store < STORE_COUNT; ++store)
    if (store != active_store) order[count++] = store;

  size_t used = 0;
  error_text[0] = '\0';
  card_recording = true;
  card_step = card_result = usb_step = usb_result = 0;
  card_file = usb_file = 0;
  uint32_t generation = max_generation + 1;
  last_written_ok = false;  /* o SMW.SRM pode ficar diferente se falhar no meio */
  for (int i = 0; i < count; ++i) {
    int store = order[i];
    int code = store_check(store);
    /* The USB slot is simply unused when the game came from a disc. */
    if (code == ERR_NOT_READY && store == STORE_FOLDER) continue;
    /* Cartao trocado durante o save (a consulta do icone avisa): o que foi
       gravado nao esta no cartao de agora (revisao do Codex). */
    const unsigned changes = store_changes(store);
    bool backup_ok = false;
    if (code == 0) code = save_to(store, save, generation, &backup_ok);
    if (code == 0) {
      active_store = store;
      max_generation = generation;
      if (backup_ok && store_changes(store) == changes) remember_written(save);
      error_text[0] = '\0';
      card_recording = false;
      char text[48];
      snprintf(text, sizeof(text), "SALVO: %s", kStoreNames[store]);
      set_notice(text);
      /* Ajuste trocado no jogo (L2+R2+X) vai junto com o save. */
      if (options_pending && write_options_to(store, &pending_options)) options_pending = false;
      return true;
    }
    if (used + 4 < sizeof(error_text))
      used += snprintf(error_text + used, sizeof(error_text) - used, "%s%d%d",
                       used ? " " : "", store + 1, code);
  }
  card_recording = false;
  if (usb_step && used + 12 < sizeof(error_text))
    used += snprintf(error_text + used, sizeof(error_text) - used, " U%d%d%s", usb_step, usb_result,
                     usb_file ? (char[]){usb_file, 0} : "");
  if (card_step && used + 12 < sizeof(error_text))
    used += snprintf(error_text + used, sizeof(error_text) - used, " M%d%d%s", card_step, card_result,
                     card_file ? (char[]){card_file, 0} : "");
  if (card_step && card_result == -3 && used + 8 < sizeof(error_text))
    snprintf(error_text + used, sizeof(error_text) - used, " CHEIO");
  return false;
}

/* ------------------------------------------------------- options --- */

/* SMW.CFG: feito pelo Gravador no PC, ao lado do jogo (APPS/SMW) ou dentro
   da ISO. So e lido. SMW.OPT: a escolha feita no PS2 (tela AJUSTES ou
   L2+R2+X), gravada onde fica o save. Os dois sao texto, uma linha
   "chave=valor" cada; linhas com # e chaves desconhecidas sao ignoradas. */

static bool same_word(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
  size_t n = strlen(s);
  while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) s[--n] = '\0';
  return s;
}

void smw_options_clear(SmwOptions *o) {
  o->tela = o->video = o->idioma = o->compat = SMW_OPT_ASK;
}

static void parse_options(char *text, SmwOptions *o) {
  char *line = text;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next) *next++ = '\0';
    char *eq = strchr(line, '=');
    if (eq && *trim(line) != '#') {
      *eq = '\0';
      const char *key = trim(line), *value = trim(eq + 1);
      if (same_word(key, "tela")) {
        if (same_word(value, "4:3")) o->tela = 0;
        else if (same_word(value, "16:9")) o->tela = 1;
        else if (same_word(value, "perguntar")) o->tela = SMW_OPT_ASK;
      } else if (same_word(key, "video")) {
        if (same_word(value, "240p")) o->video = SMW_VIDEO_240P;
        else if (same_word(value, "480p")) o->video = SMW_VIDEO_480P;
        else if (same_word(value, "480i")) o->video = SMW_VIDEO_480I;
        else if (same_word(value, "perguntar")) o->video = SMW_OPT_ASK;
      } else if (same_word(key, "idioma")) {
        if (same_word(value, "pt") || same_word(value, "portugues")) o->idioma = 0;
        else if (same_word(value, "en") || same_word(value, "ingles")) o->idioma = 1;
      } else if (same_word(key, "saida")) {
        if (same_word(value, "1280")) o->compat = 0;
        else if (same_word(value, "512")) o->compat = 1;
      }
    }
    line = next;
  }
}

static bool read_options(const char *path, SmwOptions *o) {
  char text[512];
  int n = file_read(path, (uint8_t *)text, sizeof(text) - 1);
  if (n <= 0) return false;
  text[n] = '\0';
  parse_options(text, o);
  return true;
}

void smw_read_config(SmwOptions *o) {
  char path[SMW_PATH];
  smw_options_clear(o);
  if (rom_on_disc) {
    if (!read_options("cdrom0:/SMW.CFG", o)) read_options("cdrom0:/SMW.CFG;1", o);
  }
  else if (folder_dir[0] && join_path(path, folder_dir, "SMW.CFG")) read_options(path, o);
}

/* Onde procurar e gravar o SMW.OPT: primeiro o lugar do save. */
static int store_order(int order[STORE_COUNT]) {
  int count = 0;
  if (active_store >= 0) order[count++] = active_store;
  for (int store = 0; store < STORE_COUNT; ++store)
    if (store != active_store) order[count++] = store;
  return count;
}

bool smw_read_saved_options(SmwOptions *o) {
  int order[STORE_COUNT], count = store_order(order);
  for (int i = 0; i < count; ++i) {
    char path[SMW_PATH];
    if (store_check(order[i]) != 0 || !join_path(path, store_dir(order[i]), "SMW.OPT")) continue;
    if (read_options(path, o)) return true;
  }
  return false;
}

/* Texto do SMW.OPT: so os campos com valor. */
static int options_text(const SmwOptions *o, char *text, size_t size) {
  static const char *const videos[] = {"240p", "480p", "480i"};
  int n = 0;
  if (o->tela >= 0) n += snprintf(text + n, size - n, "tela=%s\n", o->tela ? "16:9" : "4:3");
  if (o->video >= 0 && o->video <= 2) n += snprintf(text + n, size - n, "video=%s\n", videos[o->video]);
  if (o->idioma >= 0) n += snprintf(text + n, size - n, "idioma=%s\n", o->idioma ? "en" : "pt");
  if (o->compat == 1) n += snprintf(text + n, size - n, "saida=512\n");
  return n;
}

static bool write_options_to(int store, const SmwOptions *o) {
  char path[SMW_PATH], text[128];
  const char *dir = store_dir(store);
  if (!join_path(path, dir, "SMW.OPT")) return false;
  bool recording = card_recording;
  card_recording = false;  /* falha aqui nao entra no ERRO AO SALVAR */
  if (store != STORE_FOLDER) make_folder(dir);
  int n = options_text(o, text, sizeof(text));
  bool ok = file_write(path, (const uint8_t *)text, n);
  card_recording = recording;
  if (ok && store != STORE_FOLDER) write_card_icon(dir, store - STORE_MC0);
  return ok;
}

bool smw_save_options(const SmwOptions *o) {
  int order[STORE_COUNT], count = store_order(order);
  pending_options = *o;
  options_pending = true;
  for (int i = 0; i < count; ++i)
    if (store_check(order[i]) == 0 && write_options_to(order[i], o)) {
      options_pending = false;
      return true;
    }
  return false;
}

void smw_options_changed(const SmwOptions *o) {
  pending_options = *o;
  options_pending = true;
}

/* ------------------------------------------------------- trapacas --- */

static const char *const kStorePlaces[STORE_COUNT] = {[STORE_FOLDER] = "pendrive (APPS/SMW)",
                                                      [STORE_MC0] = "Memory Card 1 (SMWPS2)",
                                                      [STORE_MC1] = "Memory Card 2 (SMWPS2)"};

/* Um texto pequeno (TRAPACAS.TXT, TRAPACAS.LIG): primeiro onde fica o save,
   depois os outros lugares; com disco, por ultimo o CD (a ISO que o Gravador
   fez). Volta os bytes lidos (0 a cap) ou -1; lugar diz de onde veio. */
int smw_read_text(const char *name, char *buf, int cap, bool disc_too, char *lugar, size_t lugar_n) {
  int order[STORE_COUNT], count = store_order(order);
  bool recording = card_recording;
  card_recording = false;
  int n = -1;
  for (int i = 0; i < count && n < 0; ++i) {
    char path[SMW_PATH];
    if (store_check(order[i]) != 0 || !join_path(path, store_dir(order[i]), name)) continue;
    n = file_read(path, (uint8_t *)buf, cap);
    if (n >= 0 && lugar) snprintf(lugar, lugar_n, "%s", kStorePlaces[order[i]]);
  }
  if (n < 0 && disc_too && rom_on_disc) {
    char path[SMW_PATH];
    snprintf(path, sizeof(path), "cdrom0:/%s", name);
    n = file_read(path, (uint8_t *)buf, cap);
    if (n < 0) {
      snprintf(path, sizeof(path), "cdrom0:/%s;1", name);
      n = file_read(path, (uint8_t *)buf, cap);
    }
    if (n >= 0 && lugar) snprintf(lugar, lugar_n, "CD (ISO)");
  }
  card_recording = recording;
  return n;
}

/* Grava no primeiro lugar que der (o do save primeiro). */
bool smw_write_text(const char *name, const char *text, int n) {
  int order[STORE_COUNT], count = store_order(order);
  bool recording = card_recording, ok = false;
  card_recording = false;
  for (int i = 0; i < count && !ok; ++i) {
    char path[SMW_PATH];
    int store = order[i];
    if (store_check(store) != 0 || !join_path(path, store_dir(store), name)) continue;
    if (store != STORE_FOLDER) make_folder(store_dir(store));
    ok = file_write(path, (const uint8_t *)text, n);
    if (ok && store != STORE_FOLDER) write_card_icon(store_dir(store), store - STORE_MC0);
  }
  card_recording = recording;
  return ok;
}

/* ------------------------------------------------------------- log --- */

void smw_log_order(int order[3]) {
  order[0] = 0;
  order[1] = active_store == STORE_MC1 ? 2 : 1;  /* o cartao do save primeiro */
  order[2] = order[1] == 1 ? 2 : 1;
}

bool smw_log_place(const char *boot, int id, char path[SMW_PATH]) {
  if (id == 0) {
    const char *dir = folder_dir[0] ? folder_dir
                      : (boot && *boot && !smw_path_is_disc(boot)) ? boot : NULL;
    return dir && join_path(path, dir, "SMW_LOG.TXT");  /* caminho longo: nao cabe */
  }
  if (id != 1 && id != 2) return false;
  return store_check(STORE_MC0 + id - 1) == 0 && join_path(path, kCardDirs[id - 1], "SMW_LOG.TXT");
}

static bool write_log_file(const char *path, const char *text, int size) {
#ifdef _EE
  int port;
  const char *name;
  if (card_path(path, &port, &name)) {
    /* Em qualquer pasta do cartao o log nunca toma o lugar do save: como o
       icone, so grava se sobrar a reserva de ICON_SPARE_KB para o progresso. */
    card_state(port);  /* atualiza o espaco livre */
    if (card_free_kb[port] < (size + 1023) / 1024 + 2 + ICON_SPARE_KB) return false;
    const char *dir = kCardDirs[port];
    bool save_dir = strncmp(path, dir, strlen(dir)) == 0;
    if (save_dir) make_folder(dir);  /* pasta SMWPS2 e icone, como o save */
    if (!file_write(path, (const uint8_t *)text, size)) return false;
    if (save_dir) write_card_icon(dir, port);
    return true;
  }
#endif
  return file_write(path, (const uint8_t *)text, size);
}

bool smw_write_log(const char *path, const char *text, int size) {
  bool recording = card_recording;
  card_recording = false;  /* falha no log nao entra no ERRO AO SALVAR */
  bool ok = write_log_file(path, text, size);
  card_recording = recording;
  return ok;
}
