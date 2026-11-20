#ifndef SMW_PTBR_H
#define SMW_PTBR_H
#include "types.h"
/* Brazilian Portuguese text layer. When g_ptbr is set, the message boxes,
   overworld level names and castle texts are written from the tables in
   ptbr.c instead of the cartridge. Accented letters use BG3 tile numbers
   0x3C0-0x3FF, which the PPU reads from g_smw_extra_font (never VRAM). */
extern bool g_ptbr;
extern uint16 g_smw_extra_font[64 * 8];
/* Codigo Latin-1 das 12 minusculas acentuadas, na ordem dos glifos de
   g_smw_extra_font (as maiusculas vem depois, na mesma ordem: codigo -
   0x20). Os menus do PS2 (v0.16) usam a mesma tabela. */
extern const uint8 g_ptbr_acentos[12];
void PtbrInit(void);
/* Each returns false when the original routine should run instead. */
bool PtbrMessageStripe(uint8 message, uint16 *stripe_pos);
bool PtbrLevelName(uint16 name_id, uint16 start, uint16 limit, uint16 *end);
bool PtbrStripeImage(uint8 index);
#endif
