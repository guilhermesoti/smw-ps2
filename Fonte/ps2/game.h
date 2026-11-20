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
/* v0.16: a arte dos menus, lida da VRAM do motor quando a sala carrega (vem
   da ROM do jogador; nada disso esta no pacote): a fonte das mensagens do
   jogo (camada 3, pecas 0x100 a 0x17F, 2 bits por ponto: plano 0 no byte
   baixo, plano 1 no alto; tinta = 3), as letras acentuadas da traducao
   (desenho do port, ptbr.c, no mesmo formato) e a moeda girando como a que
   sai dos blocos (ProcessSpinningCoinSprites, smw_02.c): de frente (OBJ
   0xE8, 16 x 16) e de lado, 0xEA e 0xFA em cima e espelhadas na vertical
   embaixo (8 x 16, no meio dos 16); paleta 2 dos OBJ; em
   0xFF000000 | 0xRRGGBB, 0 = transparente. O jogo troca a cada 4 quadros:
   0, 1, 2, 1. */
enum { SMW_ARTE_LETRAS = 128, SMW_ARTE_ACENTOS = 24 };
typedef struct SmwArteMenu {
  bool ok;
  uint16_t letras[SMW_ARTE_LETRAS][8];
  uint16_t acentos[SMW_ARTE_ACENTOS][8];
  uint8_t acento_codigo[SMW_ARTE_ACENTOS];  /* o codigo Latin-1 de cada uma */
  uint32_t moeda[3][16 * 16];
} SmwArteMenu;

/* v0.14: logo depois de smw_start, um quadro da sala room (fundo, sem barra
   de status e sem sprites) em dest (SMW_WIDTH x SMW_HEIGHT); depois o motor
   recomeca do zero, como se so smw_start tivesse rodado. false se a sala
   nao carregou. v0.16: com arte, tambem a arte dos menus (arte->ok); a
   sala tem de ser uma fase comum (a 0x105 serve): ela carrega a fonte das
   mensagens na camada 3 e a moeda e a paleta 2 nos OBJ. Recomeca o motor:
   nao chame com os efeitos dos menus ligados. */
bool smw_capture_scene(unsigned room, uint32_t *dest, SmwArteMenu *arte);

/* v0.16: os efeitos sonoros do proprio jogo nos menus antes do jogo, pelo
   reprodutor de som do motor. smw_som_menus_liga roda 1 quadro do jogo (o
   jogo manda o programa de som) e descarta o som dele (o "plim" da tela da
   Nintendo); smw_som_menus_efeito pede um efeito (o numero que o jogo poe
   em $1DFC; um por quadro do som, como no jogo: o ultimo pedido vale);
   smw_som_menus_gera da as amostras (32 kHz, estereo), em quadros de
   SMW_SAMPLES por dentro. smw_som_menus_desliga refaz o motor como logo
   depois de smw_start: chame antes do jogo. Desligados, os pedidos nao
   fazem nada e gera da silencio. */
enum { SMW_EFEITO_MOEDA = 1, SMW_EFEITO_CURSOR = 6 };
void smw_som_menus_liga(void);
void smw_som_menus_efeito(uint8_t efeito);
void smw_som_menus_gera(int16_t *stereo, int samples);
void smw_som_menus_desliga(void);
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
/* Chamada no comeco de cada passo do jogo com a RAM do SNES (g_ram), antes
   da logica: as trapacas escrevem nela (v0.14). NULL desliga. */
void smw_before_frame(void (*hook)(uint8_t *ram));
extern bool smw_save_error;
#endif
