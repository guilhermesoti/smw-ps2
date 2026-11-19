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

/* Ajustes (tela AJUSTES na abertura, SMW.CFG do Gravador e SMW.OPT).
   tela: 0 = 4:3, 1 = 16:9. video: SMW_VIDEO_*. idioma: 0 = portugues,
   1 = ingles. suave: 0 = desligada, 1 = media, 2 = forte. compat: 1 = saida
   de 512 colunas (TV ou adaptador que nao aceitou a de 1280).
   SMW_OPT_ASK = sem valor (o jogador escolhe na tela de ajustes). */
enum { SMW_OPT_ASK = -1 };
enum { SMW_VIDEO_240P = 0, SMW_VIDEO_480P = 1, SMW_VIDEO_480I = 2 };
typedef struct { int tela, video, idioma, suave, compat; } SmwOptions;
/* Todos os campos em SMW_OPT_ASK. */
void smw_options_clear(SmwOptions *o);
/* SMW.CFG do Gravador (pasta do jogo ou ISO). Depois de smw_find_rom. */
void smw_read_config(SmwOptions *o);
/* SMW.OPT (onde fica o save): preenche so os campos que estao no arquivo.
   Depois de smw_start (que carrega o progresso). true se o arquivo existe. */
bool smw_read_saved_options(SmwOptions *o);
/* Grava agora. Se nao der, tenta de novo no proximo save. */
bool smw_save_options(const SmwOptions *o);
/* Troca feita no jogo (L2+R2+X, L2+R2+CIRCULO): vai junto com o proximo save. */
void smw_options_changed(const SmwOptions *o);

/* SMW_LOG.TXT (registro.c). Lugares: 0 = a pasta do jogo (pendrive; antes
   de achar o jogo, a pasta do ELF; pela ISO nao ha), 1 = Memory Card 1,
   2 = Memory Card 2. smw_log_order: a ordem de tentativa (pasta, o cartao do
   save, o outro), sem ler nada. smw_log_place: o caminho do lugar id, se ele
   estiver pronto (confere o cartao uma vez). */
void smw_log_order(int order[3]);
bool smw_log_place(const char *boot, int id, char path[SMW_PATH]);
/* Grava o arquivo inteiro. Falhas aqui nao entram no ERRO AO SALVAR. Em
   qualquer pasta do Memory Card so grava se sobrar a reserva do save. */
bool smw_write_log(const char *path, const char *text, int size);
#endif
