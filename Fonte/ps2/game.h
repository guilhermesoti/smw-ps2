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
void smw_frame(uint16_t p1, uint16_t p2, unsigned pads, int16_t *stereo, int samples,
               bool draw_pixels);
void smw_load_progress(uint8_t *save);
bool smw_save_progress(const uint8_t *save);
extern bool smw_save_error;
#endif
