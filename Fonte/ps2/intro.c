/* Abertura do port no PS2, a mesma do port de Yu-Gi-Oh!: uma vinheta propria
 * da Retrogames (particulas que formam um cubo dourado girando, com um acorde
 * sintetizado aqui mesmo), o titulo do port e a tela "Apoie o projeto" com o
 * QR code do Pix.
 *
 * Roda depois da tela AJUSTES (main.c), antes do jogo. As tres telas sao
 * quadros 256 x 224 desenhados no formato.c, por cima de cenas do jogo
 * tiradas da ROM do jogador (a vinheta desde a v0.15, com o tema das outras
 * telas; antes era um quadro proprio de 320 x 224 em fundo preto). Desde a
 * v0.15 cada tela abre e fecha em circulo (formato_iris).
 * START pula a vinheta; na tela de apoio, X ou START (ou 20 s) seguem para
 * o jogo. Sem controle conectado, a abertura segue sozinha. */
#include "intro.h"
#include "formato.h"
#include "intro_qr.h"
#include <audsrv.h>
#include <kernel.h>
#include <libpad.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 32000           /* o formato que main.c ja configurou no audsrv */

extern unsigned char msx[];  /* fonte 8x8 da libdebug do PS2SDK (scr_printf) */

static uint32_t cena_px[FORMATO_W * FORMATO_H] __attribute__((aligned(64)));
_Static_assert(INTRO_QR_N + 1 == 46, "formato_apoio recebe linhas de 46 caracteres");

/* ---- som: um "swell" grave que sobe e um acorde em arpejo ---- */
#define SOUND_FRAMES (RATE * 9 / 2)
static int16_t *sound;
static unsigned sound_at;

static void synth(void) {
  /* Osciladores por rotacao (sem sinf por amostra). */
  static const float notes[] = {329.63f, 493.88f, 659.25f, 830.61f, 987.77f, 1318.5f};
  const int chime_at = RATE * 3 / 2;
  float sw_c = 1, sw_s = 0;
  memset(sound, 0, SOUND_FRAMES * 4);
  /* swell: 55 -> 110 Hz em 1,5 s, entrando e sumindo */
  for (int i = 0; i < chime_at + RATE / 2; i++) {
    float t = (float)i / RATE, f = 55.0f * (1.0f + t / 1.5f), a = 2 * 3.14159265f * f / RATE;
    float c = sw_c * cosf(a) - sw_s * sinf(a), s = sw_s * cosf(a) + sw_c * sinf(a);
    float env = t < 1.2f ? t / 1.2f : (t < 1.5f ? 1.0f : 1.0f - (t - 1.5f) * 2.0f);
    sw_c = c, sw_s = s;
    if (env < 0) env = 0;
    int16_t v = (int16_t)(s * env * 0.22f * 32767);
    sound[i * 2] = sound[i * 2 + 1] = v;
  }
  for (int n = 0; n < 6; n++) {
    int start = chime_at + n * RATE * 7 / 100;
    float a = 2 * 3.14159265f * notes[n] / RATE, ca = cosf(a), sa = sinf(a);
    float a2 = 2 * a, ca2 = cosf(a2), sa2 = sinf(a2);
    float c = 1, s = 0, c2 = 1, s2 = 0, env = 0.16f, decay = expf(-2.4f / RATE);
    float pan = 0.25f + 0.1f * n; /* da esquerda para a direita */
    for (int i = start; i < SOUND_FRAMES; i++) {
      float nc = c * ca - s * sa, ns = s * ca + c * sa, nc2 = c2 * ca2 - s2 * sa2, ns2 = s2 * ca2 + c2 * sa2;
      float attack = (float)(i - start) / (RATE / 200);
      c = nc, s = ns, c2 = nc2, s2 = ns2;
      float v = (s + 0.3f * s2) * env * (attack < 1 ? attack : 1);
      env *= decay;
      float l = sound[i * 2] / 32767.0f + v * (1 - pan), r = sound[i * 2 + 1] / 32767.0f + v * pan;
      if (l > 1) l = 1;
      if (l < -1) l = -1;
      if (r > 1) r = 1;
      if (r < -1) r = -1;
      sound[i * 2] = (int16_t)(l * 32767), sound[i * 2 + 1] = (int16_t)(r * 32767);
    }
  }
  /* sem estalo no fim */
  for (int i = SOUND_FRAMES - RATE / 4; i < SOUND_FRAMES; i++) {
    float f = (float)(SOUND_FRAMES - i) / (RATE / 4);
    sound[i * 2] = (int16_t)(sound[i * 2] * f), sound[i * 2 + 1] = (int16_t)(sound[i * 2 + 1] * f);
  }
}

static void feed_sound(void) {
  if (!sound || sound_at >= SOUND_FRAMES) return;
  int room = audsrv_available() / 4;
  if (room <= 0) return;
  unsigned frames = SOUND_FRAMES - sound_at;
  if (frames > (unsigned)room) frames = (unsigned)room;
  if (frames > 2048) frames = 2048;
  audsrv_play_audio((const char *)(sound + sound_at * 2), (int)(frames * 4));
  sound_at += frames;
}

static unsigned read_buttons(void) {
  unsigned now = 0;
  for (int port = 0; port < 2; port++) {
    struct padButtonStatus status;
    int state = padGetState(port, 0);
    if ((state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) && padRead(port, 0, &status) > 0)
      now |= 0xFFFFu ^ status.btns;
  }
  return now;
}

/* Desenha a textura t na tela, de (x0, y0) a (x1, y1), usando u1 x v1 texels,
   com os pixels nitidos. A suavizacao (filtro bilinear do GS) e as scanlines
   sairam na v0.13.7: no console ficavam piores que a imagem nitida. */
void smw_desenha_textura(GSGLOBAL *gs, GSTEXTURE *t, float x0, float y0, float x1, float y1, float u1,
                         float v1) {
  u8 enabled = gs->PrimAlphaEnable;
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  t->Filter = GS_FILTER_NEAREST;
  gsKit_prim_sprite_texture(gs, t, x0, y0, 0, 0, x1, y1, u1, v1, 1, GS_SETREG_RGBAQ(128, 128, 128, 128, 0));
  gs->PrimAlphaEnable = enabled;
}

/* Converte o quadro para o GS (0xAABBGGRR) com o brilho 0..128 e mostra
   (480p: 448 linhas centradas nas 480). colunas: quantas colunas do SNES a
   largura da tela mostra. Com tex->Width o quadro ocupa a tela toda; no
   jogo em 16:9 (342) o quadro de 256 fica no meio, com faixas pretas dos
   lados e os pixels da mesma largura que os do jogo. */
void smw_mostra_quadro(GSGLOBAL *gs, GSTEXTURE *tex, uint32_t *quadro, bool p480, int bright, unsigned colunas) {
  unsigned k = (unsigned)bright * 2;
  const int n = (int)(tex->Width * tex->Height);
  for (int i = 0; i < n; i++) {
    uint32_t p = quadro[i], r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
    if (k < 256) r = r * k >> 8, g = g * k >> 8, b = b * k >> 8;
    quadro[i] = 0x80000000u | (b << 16) | (g << 8) | r;
  }
  SyncDCache(quadro, quadro + n);
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0, 0));
  gsKit_texture_send_inline(gs, tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW, GS_CLUT_NONE);
  float top = p480 ? 16.0f : 0.0f, height = p480 ? 2.0f * tex->Height : (float)gs->Height;
  const unsigned tela = (unsigned)gs->Width;
  unsigned largura = colunas > tex->Width ? (tela * tex->Width + colunas / 2) / colunas : tela;
  float x0 = (float)((tela - largura) / 2);
  smw_desenha_textura(gs, tex, x0, top, x0 + largura, top + height, tex->Width, tex->Height);
  gsKit_queue_exec(gs);
  gsKit_finish();
  gsKit_sync_flip(gs);
}

/* Um "blip" curto ao trocar de opcao. Ele nao vai direto para o audsrv: o
   IOP le o buffer circular sem parar e, se nada novo chega, toca de novo o
   que ficou nele (o bip repetia a cada 0,1 s). smw_som_menu() manda o bip
   junto com o silencio de cada quadro dos menus. */
#define BLIP_FRAMES (RATE / 25)
static int16_t blip_buf[BLIP_FRAMES];
static int blip_pos = BLIP_FRAMES;  /* amostras do bip que ja tocaram */
void smw_blip(bool audio_ok) {
  if (!audio_ok) return;
  if (!blip_buf[1]) {
    float a = 2 * 3.14159265f * 880.0f / RATE;
    for (int i = 0; i < BLIP_FRAMES; i++) {
      float env = 1.0f - (float)i / BLIP_FRAMES;
      blip_buf[i] = (int16_t)(sinf(a * i) * env * env * 0.18f * 32767);
    }
  }
  blip_pos = 0;
}

void smw_som_menu(int16_t *stereo, int frames) {
  for (int i = 0; i < frames; i++) {
    int16_t v = 0;
    if (blip_pos < BLIP_FRAMES) v = blip_buf[blip_pos++];
    stereo[i * 2] = stereo[i * 2 + 1] = v;
  }
}

void smw_intro(GSGLOBAL *gs, bool p480, bool audio_ok, const char *version, const uint32_t *fundo_vinheta,
               const uint32_t *fundo_titulo, const uint32_t *fundo_apoio) {
  /* A_START: a vinheta abre (o cubo trava FORMATO_VINHETA_TRAVA depois);
     B_START: o titulo; C_START: a tela de apoio. Cada tela abre em circulo
     nos primeiros FORMATO_IRIS_QUADROS e fecha nos ultimos. */
  enum { A_START = 20, B_START = 330, C_START = 480, IRIS = FORMATO_IRIS_QUADROS };
  static GSTEXTURE tex;
  unsigned held = 0xFFFF;
  int leaving = -1, leave_at = 0, skip = 0;

  tex.Width = FORMATO_W;
  tex.Height = FORMATO_H;
  tex.PSM = GS_PSM_CT32;
  tex.Filter = GS_FILTER_NEAREST;
  tex.TBW = FORMATO_W / 64;
  tex.Mem = (u32 *)cena_px;
  tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(FORMATO_W, FORMATO_H, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
  if (tex.Vram == GSKIT_ALLOC_ERROR) return;  /* sem VRAM: vai direto ao jogo */

  sound = audio_ok ? malloc(SOUND_FRAMES * 4) : NULL;
  sound_at = 0;
  if (sound) synth();

  /* Um quadro por vsync (gsKit_sync_flip espera a TV). */
  for (int frame = 0;; frame++) {
    int f = frame + skip, r = FORMATO_IRIS_MAX;
    unsigned now = read_buttons(), pressed = now & ~held;
    held = now;  /* so vale botao apertado depois de solto */
    if (f < C_START && (pressed & PAD_START)) skip += C_START - f, f = C_START;
    if (f >= C_START + 60 && leaving < 0 && (pressed & (PAD_START | PAD_CROSS))) leaving = 0;
    if (f >= C_START + 60 * 20 && leaving < 0) leaving = 0;
    if (f >= A_START && f < C_START) feed_sound();
    if (f < A_START) {
      r = 0;  /* preto */
    } else if (f < B_START) {
      formato_vinheta(cena_px, msx, f - A_START, fundo_vinheta);
      if (f < A_START + IRIS) r = formato_iris_raio(f - A_START, true);
      else if (f >= B_START - IRIS) r = formato_iris_raio(f - (B_START - IRIS), false);
    } else if (f < C_START) {
      formato_titulo(cena_px, msx, version, fundo_titulo);
      if (f < B_START + IRIS) r = formato_iris_raio(f - B_START, true);
      else if (f >= C_START - IRIS) r = formato_iris_raio(f - (C_START - IRIS), false);
    } else {
      formato_apoio(cena_px, msx, f - C_START, f >= C_START + 60, intro_qr, INTRO_QR_N, fundo_apoio);
      if (f < C_START + IRIS) r = formato_iris_raio(f - C_START, true);
    }
    if (leaving == 0) leave_at = f, leaving = 1;
    if (leaving > 0) {
      if (f - leave_at > IRIS) break;
      r = formato_iris_raio(f - leave_at, false);
    }
    formato_iris(cena_px, r);
    smw_mostra_quadro(gs, &tex, cena_px, p480, 128, tex.Width);
  }
  memset(cena_px, 0, sizeof(cena_px));
  smw_mostra_quadro(gs, &tex, cena_px, p480, 0, tex.Width);
  free(sound);
  sound = NULL;
}
