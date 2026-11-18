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
/* Where the progress was loaded from, or why nothing was loaded (boot screen). */
void smw_load_report(char *out, size_t size);
/* Short message for the game screen ("CARREGADO: ...", "SALVO: ...");
   serial changes whenever there is a new one. */
const char *smw_notice(unsigned *serial);
/* Short codes of the last failed save, e.g. "24 31" (see LEIA-ME). */
const char *smw_save_error_text(void);

/* Ajustes de abertura. tela: 0 = 4:3, 1 = 16:9; video: 0 = 240p, 1 = 480p;
   idioma: 0 = portugues, 1 = ingles; SMW_OPT_ASK = perguntar / sem valor. */
enum { SMW_OPT_ASK = -1 };
typedef struct { int tela, video, idioma; } SmwOptions;
/* SMW.CFG do Gravador (pasta do jogo ou ISO). Depois de smw_find_rom. */
void smw_read_config(SmwOptions *o);
/* Tela escolhida no PS2 (SMW.OPT, onde fica o save), ou SMW_OPT_ASK.
   Depois de smw_start (que carrega o progresso). */
int smw_read_saved_tela(void);
/* Grava a escolha agora (tela FORMATO DA TELA). Se nao der, tenta de novo
   no proximo save. */
bool smw_save_tela(int tela);
/* Troca feita no jogo (L2+R2+X): vai junto com o proximo save. */
void smw_tela_changed(int tela);
#endif
