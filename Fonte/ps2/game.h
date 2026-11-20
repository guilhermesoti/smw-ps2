#ifndef SMW_PORT_GAME_H
#define SMW_PORT_GAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SMW_SAMPLES is what the engine produces per frame at 32 kHz; the port may
   ask for a few more or less to follow the real TV refresh rate. */
enum { SMW_WIDTH = 256, SMW_HEIGHT = 224, SMW_SAMPLES = 534, SMW_MAX_SAMPLES = 540,
       SMW_ROM_BYTES = 512 * 1024, SMW_SRAM_BYTES = 2048 };

void smw_start(uint8_t *rom, uint32_t *pixels);
/* Tela larga 16:9: SMW_WIDE_WIDTH colunas por linha (43 + 256 + 43), em
   linhas de pitch pixels (>= SMW_WIDE_WIDTH). Com on = false volta a 256
   colunas em linhas de SMW_WIDTH. As cenas sem suporte ficam com faixas
   pretas dos lados. */
enum { SMW_WIDE_WIDTH = 342, SMW_MAX_PITCH = 384 };
void smw_set_widescreen(bool on, uint32_t *pixels, unsigned pitch);
void smw_frame(uint16_t p1, uint16_t p2, unsigned pads, int16_t *stereo, int samples,
               bool draw_pixels);
void smw_load_progress(uint8_t *save);
bool smw_save_progress(const uint8_t *save);
/* true se o lugar do save ja tem exatamente isto no SMW.SRM e nao ha ajuste
   pendente: gravar de novo so pararia o jogo (v0.13.8). */
bool smw_save_unchanged(const uint8_t *save);
/* Chamada antes de cada save que grava de verdade (o jogo para enquanto
   grava; o main deixa o som mudo, senao o audsrv repete o que tem). */
void smw_before_save(void (*hook)(void));
extern bool smw_save_error;
#endif
