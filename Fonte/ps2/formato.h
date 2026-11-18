#ifndef SMW_FORMATO_H
#define SMW_FORMATO_H
#include <stdint.h>

/* Tela FORMATO DA TELA (formato.c), no quadro da abertura. */
enum { FORMATO_W = 320, FORMATO_H = 224 };

/* Desenha a tela inteira em px (FORMATO_W x FORMATO_H, 0x00RRGGBB).
   fonte: a fonte 8x8 do PS2SDK (256 letras). sel: 0 = 4:3, 1 = 16:9.
   segundos: o que falta para seguir sozinho (< 0 esconde). */
void formato_desenha(uint32_t *px, const unsigned char *fonte, int sel, int frame, int segundos);

#endif
