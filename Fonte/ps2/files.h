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
/* Tempo (ms) de cada parte do ultimo save: leitura, reserva, novo, icone. */
const char *smw_save_steps(void);
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
   tela: 0 = 4:3, 1 = 16:9 (desde a v0.14.1 so vale com o video 480p; nos
   outros o jogo fica em 4:3 e a escolha fica guardada). video: SMW_VIDEO_*.
   idioma: 0 = portugues, 1 = ingles. compat: 1 = saida de 512 colunas (TV
   ou adaptador que nao aceitou a de 1280). pos_x, pos_y (v0.14.2): a
   posicao da imagem na TV, em pixels e linhas do SNES, de -SMW_POSICAO_MAX
   a SMW_POSICAO_MAX (como o ajuste de posicao de um monitor, para
   centralizar; como no port do Yu-Gi-Oh); x positivo = direita, y positivo
   = para baixo; SMW_POSICAO_NADA = sem valor (fora da faixa; o 0 e o
   centro, entao um SmwOptions zerado tem posicao: comece com
   smw_options_clear). SMW_OPT_ASK = sem valor nos
   outros (o jogador escolhe na tela de ajustes). As chaves suavizacao= e
   scanlines= (ate a v0.13.6) e largura= e altura= (o tamanho da v0.14.1,
   que saiu) de arquivos antigos sao ignoradas. */
enum { SMW_OPT_ASK = -1 };
enum { SMW_VIDEO_240P = 0, SMW_VIDEO_480P = 1, SMW_VIDEO_480I = 2 };
enum { SMW_POSICAO_MAX = 16, SMW_POSICAO_PASSO = 2, SMW_POSICAO_NADA = -(SMW_POSICAO_MAX + 1) };
typedef struct { int tela, video, idioma, compat, pos_x, pos_y; } SmwOptions;
/* Uma posicao (x ou y) que vale: de -SMW_POSICAO_MAX a SMW_POSICAO_MAX. */
static inline bool smw_posicao_valida(int v) { return v >= -SMW_POSICAO_MAX && v <= SMW_POSICAO_MAX; }
/* A posicao trazida para dentro de -SMW_POSICAO_MAX..SMW_POSICAO_MAX. */
static inline int smw_posicao_limita(int v) {
  return v < -SMW_POSICAO_MAX ? -SMW_POSICAO_MAX : v > SMW_POSICAO_MAX ? SMW_POSICAO_MAX : v;
}
/* Um toque de seta: v + passo, dentro da faixa. Uma posicao impar (escrita a
   mao no SMW.OPT ou no SMW.CFG) para no 0 ao passar por ele, para o centro
   continuar a um toque (revisao do Codex). */
static inline int smw_posicao_anda(int v, int passo) {
  int n = smw_posicao_limita(v + passo);
  return (v > 0 && n < 0) || (v < 0 && n > 0) ? 0 : n;
}
/* O 16:9 so vale com o video 480p (v0.14.1). */
static inline bool smw_16_9_vale(int video) { return video == SMW_VIDEO_480P; }
/* Todos os campos sem valor (SMW_OPT_ASK; a posicao, SMW_POSICAO_NADA). */
void smw_options_clear(SmwOptions *o);
/* SMW.CFG do Gravador (pasta do jogo ou ISO). Depois de smw_find_rom. */
void smw_read_config(SmwOptions *o);
/* SMW.OPT (onde fica o save): preenche so os campos que estao no arquivo.
   Depois de smw_start (que carrega o progresso). true se o arquivo existe. */
bool smw_read_saved_options(SmwOptions *o);
/* Grava agora. Se nao der, tenta de novo no proximo save. */
bool smw_save_options(const SmwOptions *o);
/* Troca feita no jogo (L2+R2+X): vai junto com o proximo save. */
void smw_options_changed(const SmwOptions *o);

/* v0.14, trapacas: um texto pequeno do lugar do save (ou dos outros; com
   disc_too, tambem do CD da ISO). Volta os bytes lidos ou -1; lugar diz de
   onde veio. E a gravacao no primeiro lugar que der. */
int smw_read_text(const char *name, char *buf, int cap, bool disc_too, char *lugar, size_t lugar_n);
bool smw_write_text(const char *name, const char *text, int n);

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
