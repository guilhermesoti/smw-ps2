#ifndef SMW_PORT_FILES_H
#define SMW_PORT_FILES_H
#include "game.h"

enum { SMW_PATH = 512 };
typedef enum { ROM_MISSING, ROM_GOOD, ROM_BAD_SIZE, ROM_BAD_CRC,
               ROM_IO_ERROR, ROM_BAD_ZIP, ROM_NO_MEMORY } RomResult;
bool smw_boot_folder(const char *elf, char folder[SMW_PATH]);
RomResult smw_find_rom(const char *boot, uint8_t **rom, char path[SMW_PATH], uint32_t *crc);
bool smw_path_is_disc(const char *path);
/* Call once after mcman/mcserv and mcInit, with the result. */
void smw_enable_memory_card(bool available);
/* Human-readable place where progress is being kept (after smw_start). */
const char *smw_save_location(void);
/* One line per save location, for the boot screen. */
void smw_storage_report(char *out, size_t size);
/* Short codes of the last failed save, e.g. "24 31" (see LEIA-ME). */
const char *smw_save_error_text(void);
#endif
