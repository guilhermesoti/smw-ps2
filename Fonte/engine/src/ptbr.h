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
/* v0.17: tela do fim da fase. Cada uma poe uma faixa a mais no lugar do
   0xFF em stripe_image_upload_data[pos] (o 0xFF vai para o novo fim) e
   devolve quantos bytes andou: FASE COMPLETA! e o acento de BÔNUS!. */
enum { kPtbrPecaCircunflexo = 24 };  /* g_smw_extra_font: BG3 0x3D8 */
uint16 PtbrFimDeFaseTexto(uint16 pos);
uint16 PtbrFimDeFaseBonus(uint16 pos);
/* v0.17: palavras grandes (telas de carregar e de morte) em portugues. */
enum { kPtbrGrandeMario, kPtbrGrandeLuigi, kPtbrGrandeBonus, kPtbrGrandeFimDeJogo,
       kPtbrGrandeTempo, kPtbrGrandeTotal };
/* Monta a frase nas 24 pecas de sprite das letras do jogo; devolve quantas
   pecas ou sprites nao couberam (0 = frase inteira). */
int PtbrLetrasGrandes(int frase);
void PtbrLetrasGrandesDesenha(uint8 desloca);
/* No lugar de UploadLoadingLettersTiles; false = enviar as do jogo. */
bool PtbrLetrasGrandesEnvia(void);
/* Esquece a frase montada (PtbrInit e o motor reiniciado). */
void PtbrLetrasGrandesZera(void);
#endif
