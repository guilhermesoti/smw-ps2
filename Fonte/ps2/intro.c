/* Abertura do port no PS2, a mesma do port de Yu-Gi-Oh!: uma vinheta propria
 * da Retrogames (particulas que formam um cubo dourado girando, com um acorde
 * sintetizado aqui mesmo), o titulo do port e a tela "Apoie o projeto" com o
 * QR code do Pix.
 *
 * Roda depois da tela AJUSTES (main.c), antes do jogo. Cada quadro e
 * desenhado pela CPU num quadro 320 x 224 e esticado na tela pelo GS, com a
 * mesma suavizacao do jogo.
 * START pula a vinheta; na tela de apoio, X ou START (ou 20 s) seguem para
 * o jogo. Sem controle conectado, a abertura segue sozinha. */
#include "intro.h"
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

#define W 320
#define H 224
#define RATE 32000           /* o formato que main.c ja configurou no audsrv */

extern unsigned char msx[];  /* fonte 8x8 da libdebug do PS2SDK (scr_printf) */

static uint32_t canvas_px[W * H] __attribute__((aligned(64)));

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

/* ---- desenho (0x00RRGGBB) ---- */
static inline void add_px(int x, int y, unsigned r, unsigned g, unsigned b) {
  if ((unsigned)x >= W || (unsigned)y >= H) return;
  uint32_t p = canvas_px[y * W + x];
  uint32_t nr = ((p >> 16) & 255) + r, ng = ((p >> 8) & 255) + g, nb = (p & 255) + b;
  canvas_px[y * W + x] = (nr > 255 ? 255 : nr) << 16 | (ng > 255 ? 255 : ng) << 8 | (nb > 255 ? 255 : nb);
}

static void fill(int x, int y, int w, int h, uint32_t rgb) {
  for (int j = y; j < y + h; j++)
    if ((unsigned)j < H)
      for (int i = x; i < x + w; i++)
        if ((unsigned)i < W) canvas_px[j * W + i] = rgb;
}

/* Linha com brilho: o nucleo e um halo, somados (aditivo). */
static void glow_line(float x0, float y0, float x1, float y1, unsigned r, unsigned g, unsigned b) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  int steps = (int)len + 1;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    int x = (int)(x0 + dx * t), y = (int)(y0 + dy * t);
    add_px(x, y, r, g, b);
    add_px(x + 1, y, r / 3, g / 3, b / 3), add_px(x - 1, y, r / 3, g / 3, b / 3);
    add_px(x, y + 1, r / 3, g / 3, b / 3), add_px(x, y - 1, r / 3, g / 3, b / 3);
  }
}

static void glow_dot(int x, int y, unsigned r, unsigned g, unsigned b) {
  add_px(x, y, r, g, b);
  add_px(x + 1, y, r / 3, g / 3, b / 3), add_px(x - 1, y, r / 3, g / 3, b / 3);
  add_px(x, y + 1, r / 3, g / 3, b / 3), add_px(x, y - 1, r / 3, g / 3, b / 3);
}

static uint32_t scale_rgb(uint32_t rgb, int level) /* level 0..256 */ {
  if (level <= 0) return 0;
  if (level >= 256) return rgb;
  return ((((rgb >> 16) & 255) * level >> 8) << 16) | ((((rgb >> 8) & 255) * level >> 8) << 8) |
         ((rgb & 255) * level >> 8);
}

/* Texto na fonte 8x8 do PS2SDK, 6 pixels de avanco por letra. */
static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }
static void text(int x, int y, const char *s, uint32_t rgb, int scale) {
  if (!rgb) return;
  for (; *s; s++, x += 6 * scale) {
    const unsigned char *g = msx + (unsigned char)*s * 8;
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 6; c++)
        if (g[r] & (0x80 >> c)) fill(x + c * scale, y + r * scale, scale, scale, rgb);
  }
}
static void text_c(int y, const char *s, uint32_t rgb, int scale) {
  text((W - text_w(s, scale)) / 2, y, s, rgb, scale);
}
/* Texto com sombra, para ler bem na TV. */
static void text_cs(int y, const char *s, uint32_t rgb, int scale) {
  int x = (W - text_w(s, scale)) / 2;
  text(x + 1, y + 1, s, 0x000001, scale);
  text(x, y, s, rgb, scale);
}

/* Um brilho que corre pela faixa [y0, y1): clareia o que nao e fundo preto. */
static void sweep(int y0, int y1, int at) {
  for (int y = y0; y < y1; y++)
    for (int x = at - 15; x < at + 15; x++) {
      if ((unsigned)x >= W || (unsigned)y >= H) continue;
      if (!(canvas_px[y * W + x] & 0xFFFFFF)) continue;
      int k = 15 - (x > at ? x - at : at - x);
      add_px(x, y, k * 10, k * 10, k * 8);
    }
}

/* ---- cena 1: a vinheta ---- */
#define PARTICLES 160
static float pa_[PARTICLES], pr_[PARTICLES];
static unsigned seed = 0x1234567u;
static float rnd(void) {
  seed = seed * 1103515245u + 12345u;
  return (float)((seed >> 8) & 0xFFFF) / 65535.0f;
}

static void cube(float cx, float cy, float size, float ay, float ax, int level) {
  static const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
                                   {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  float sx[8], sy[8];
  for (int i = 0; i < 8; i++) {
    float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f, z = (i & 4) ? 1.f : -1.f;
    float x1 = x * cosf(ay) + z * sinf(ay), z1 = -x * sinf(ay) + z * cosf(ay);
    float y1 = y * cosf(ax) - z1 * sinf(ax), z2 = y * sinf(ax) + z1 * cosf(ax);
    float k = 4.0f / (z2 + 6.0f);
    sx[i] = cx + x1 * size * k, sy[i] = cy + y1 * size * k * 0.9f;
  }
  for (int i = 0; i < 12; i++)
    glow_line(sx[edges[i][0]], sy[edges[i][0]], sx[edges[i][1]], sy[edges[i][1]], 255 * level >> 8,
              190 * level >> 8, 60 * level >> 8);
}

/* ---- cena 3: apoie o projeto ---- */
static void support_screen(int frame, int can_go) {
  for (int y = 0; y < H; y++) {
    uint32_t c = (uint32_t)(10 + y * 14 / H) << 16 | (uint32_t)(12 + y * 6 / H) << 8 | (uint32_t)(40 + y * 30 / H);
    for (int x = 0; x < W; x++) canvas_px[y * W + x] = c;
  }
  /* estrelas lentas */
  seed = 777;
  for (int i = 0; i < 60; i++) {
    int sx = (int)(rnd() * W), sy = (int)(rnd() * H), tw = (int)(rnd() * 60);
    int k = 40 + (int)(40 * (1 + sinf((frame + tw * 7) * 0.05f)));
    add_px((sx + frame / 8) % W, sy, k, k, k + 20);
  }
  /* moldura dourada dupla */
  fill(10, 8, W - 20, 2, 0xC8962E), fill(10, H - 10, W - 20, 2, 0xC8962E);
  fill(10, 8, 2, H - 16, 0xC8962E), fill(W - 12, 8, 2, H - 16, 0xC8962E);
  fill(15, 13, W - 30, 1, 0x7A5A1C), fill(15, H - 14, W - 30, 1, 0x7A5A1C);
  fill(15, 13, 1, H - 27, 0x7A5A1C), fill(W - 16, 13, 1, H - 27, 0x7A5A1C);

  text_cs(22, "APOIE O PROJETO", 0xF2C14E, 2);
  fill(W / 2 - 95, 41, 190, 1, 0xC8962E);
  {
    static const struct { const char *t; uint32_t c; } lines[] = {
        {"Este port e gratuito", 0xFFFFFF}, {"e sempre vai ser.", 0xFFFFFF}, {"", 0},
        {"Se ele te divertiu,", 0xC8D0F0}, {"ajude a fazer os", 0xC8D0F0}, {"proximos ports, como", 0xC8D0F0},
        {"o Tekken 3 para PS2.", 0xC8D0F0}, {"", 0},
        {"Pix: aponte a camera", 0xF2C14E}, {"do celular para o QR.", 0xF2C14E}};
    for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++)
      if (lines[i].t[0]) text(26, 54 + i * 11, lines[i].t, lines[i].c, 1);
  }
  /* QR: 2 px por modulo, borda branca de 4 modulos */
  {
    const int m = 2, n = INTRO_QR_N, q = 4, size = (n + 2 * q) * m;
    int ox = W - 26 - size, oy = 52;
    fill(ox - 2, oy - 2, size + 4, size + 4, 0xC8962E);
    fill(ox, oy, size, size, 0xFFFFFF);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        if (intro_qr[y][x] == '1') fill(ox + (q + x) * m, oy + (q + y) * m, m, m, 0x000000);
  }
  text_c(170, "Do exterior: ko-fi.com/guilhermesoti", 0x9FE0FF, 1);
  text_c(182, "github.com/guilhermesoti/smw-ps2", 0x9FE0FF, 1);
  if (can_go && (frame / 30) % 2 == 0) text_cs(199, "Pressione X ou START para jogar", 0xFFFFFF, 1);
  else if (!can_go) text_cs(199, "Retrogames Patos de Minas", 0x8890B0, 1);
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
   ocupando a tela (480p: 448 linhas centradas nas 480). */
void smw_mostra_quadro(GSGLOBAL *gs, GSTEXTURE *tex, uint32_t *quadro, bool p480, int bright) {
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
  smw_desenha_textura(gs, tex, 0, top, gs->Width, top + height, tex->Width, tex->Height);
  gsKit_queue_exec(gs);
  gsKit_finish();
  gsKit_sync_flip(gs);
}

static void present(GSGLOBAL *gs, GSTEXTURE *tex, bool p480, int bright) {
  smw_mostra_quadro(gs, tex, canvas_px, p480, bright);
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

void smw_intro(GSGLOBAL *gs, bool p480, bool audio_ok, const char *version) {
  enum { A_START = 20, LOCK = 110, A_FADE = 300, B_START = 330, B_FADE = 450, C_START = 480 };
  static GSTEXTURE tex;
  unsigned held = 0xFFFF;
  int leaving = -1, leave_at = 0, skip = 0;
  char line[64];

  tex.Width = W;
  tex.Height = H;
  tex.PSM = GS_PSM_CT32;
  tex.Filter = GS_FILTER_NEAREST;
  tex.TBW = W / 64;
  tex.Mem = (u32 *)canvas_px;
  tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(W, H, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
  if (tex.Vram == GSKIT_ALLOC_ERROR) return;  /* sem VRAM: vai direto ao jogo */

  sound = audio_ok ? malloc(SOUND_FRAMES * 4) : NULL;
  sound_at = 0;
  if (sound) synth();
  for (int i = 0; i < PARTICLES; i++) pa_[i] = rnd() * 6.2831853f, pr_[i] = 130 + rnd() * 130;
  snprintf(line, sizeof(line), "versao %s  -  projeto de fa, gratuito", version);

  /* Um quadro por vsync (gsKit_sync_flip espera a TV). */
  for (int frame = 0;; frame++) {
    int f = frame + skip, bright = 128;
    unsigned now = read_buttons(), pressed = now & ~held;
    held = now;  /* so vale botao apertado depois de solto */
    if (f < C_START && (pressed & PAD_START)) skip += C_START - f, f = C_START;
    if (f >= C_START + 60 && leaving < 0 && (pressed & (PAD_START | PAD_CROSS))) leaving = 0;
    if (f >= C_START + 60 * 20 && leaving < 0) leaving = 0;
    if (f >= A_START && f < C_START) feed_sound();
    memset(canvas_px, 0, sizeof(canvas_px));
    if (f < A_START) {
      /* preto */
    } else if (f < B_START) {
      float t = (float)(f - A_START);
      /* particulas em espiral ate o centro */
      for (int i = 0; i < PARTICLES; i++) {
        if (f < LOCK) {
          float k = 1.0f - (float)(f - A_START) / (LOCK - A_START);
          float r = pr_[i] * k * k, a = pa_[i] + t * 0.04f + (1 - k) * 3.0f;
          glow_dot(W / 2 + (int)(r * cosf(a)), 80 + (int)(r * sinf(a) * 0.75f), 160, 120, 50);
        } else if (f < LOCK + 40) {
          /* estouro: as particulas saem de novo, sumindo */
          float e = (float)(f - LOCK) * 3.0f * (0.5f + pr_[i] / 260.0f);
          int fade = 200 - (f - LOCK) * 5;
          if (fade > 0)
            glow_dot(W / 2 + (int)(e * cosf(pa_[i])), 80 + (int)(e * sinf(pa_[i]) * 0.75f),
                     (unsigned)fade, (unsigned)fade * 3 / 4, (unsigned)fade / 3);
        }
      }
      if (f >= LOCK - 30) {
        int grow = f < LOCK ? (f - (LOCK - 30)) * 256 / 30 : 256;
        cube(W / 2, 80, 35.0f * (0.4f + 0.6f * grow / 256.0f), t * 0.035f, 0.5f + 0.25f * sinf(t * 0.02f),
             f < LOCK ? grow / 2 : 256);
      }
      if (f >= LOCK && f < LOCK + 12) {
        /* clarao */
        int k = (LOCK + 12 - f) * 20;
        for (int i = 0; i < W * H; i++) {
          uint32_t p = canvas_px[i];
          unsigned r = ((p >> 16) & 255) + k, g = ((p >> 8) & 255) + k, b = (p & 255) + k;
          canvas_px[i] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
        }
      }
      if (f >= LOCK + 20) {
        text_c(140, "RETROGAMES", scale_rgb(0xF2C14E, (f - (LOCK + 20)) * 8), 3);
        if (f >= LOCK + 40 && f < LOCK + 130) sweep(136, 166, 40 + (f - (LOCK + 40)) * 3);
      }
      if (f >= LOCK + 60) text_c(172, "Patos de Minas - MG", scale_rgb(0xC8D0F0, (f - (LOCK + 60)) * 8), 1);
      if (f >= LOCK + 100) text_c(192, "apresenta", scale_rgb(0x9098B8, (f - (LOCK + 100)) * 6), 1);
      if (f >= A_FADE) bright = 128 - (f - A_FADE) * 128 / (B_START - A_FADE);
    } else if (f < C_START) {
      int a = (f - B_START) * 6;
      text_cs(56, "Super Mario", scale_rgb(0xF2C14E, a), 4);
      text_cs(94, "World", scale_rgb(0xFFFFFF, a), 2);
      fill(W / 2 - 80, 116, 160, 1, scale_rgb(0xC8962E, a));
      text_c(132, "Port nativo para PlayStation 2", scale_rgb(0xC8D0F0, a - 120), 1);
      text_c(156, line, scale_rgb(0x8890B0, a - 200), 1);
      if (f < B_START + 20) bright = (f - B_START) * 128 / 20;
      if (f >= B_FADE) bright = 128 - (f - B_FADE) * 128 / (C_START - B_FADE);
    } else {
      support_screen(f - C_START, f >= C_START + 60);
      if (f < C_START + 20) bright = (f - C_START) * 128 / 20;
    }
    if (leaving == 0) leave_at = f, leaving = 1;
    if (leaving > 0) {
      bright = 128 - (f - leave_at) * 128 / 20;
      if (f - leave_at > 20) break;
    }
    if (bright < 0) bright = 0;
    present(gs, &tex, p480, bright);
  }
  memset(canvas_px, 0, sizeof(canvas_px));
  present(gs, &tex, p480, 0);
  free(sound);
  sound = NULL;
}
