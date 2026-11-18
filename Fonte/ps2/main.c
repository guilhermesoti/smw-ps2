#include "files.h"
#include "formato.h"
#include "intro.h"
#include "src/ptbr.h"
#include <audsrv.h>
#include <debug.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <libmc.h>
#include <libpad.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timer.h>
#include <unistd.h>

#define SMW_VERSAO "0.13.1"

#define DECLARE_DRIVER(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx
DECLARE_DRIVER(bdm);
DECLARE_DRIVER(bdmfs_fatfs);
DECLARE_DRIVER(usbd);
DECLARE_DRIVER(usbmass_bd);
DECLARE_DRIVER(sio2man);
DECLARE_DRIVER(padman);
DECLARE_DRIVER(mcman);
DECLARE_DRIVER(mcserv);
DECLARE_DRIVER(audsrv);
DECLARE_DRIVER(freesd);

static unsigned char pad_buf[2][256] __attribute__((aligned(64)));
/* Room for the 16:9 image: 342 columns in 384-pixel rows (GS buffer width
   must be a multiple of 64). In 4:3 the rows are 256 pixels. */
static uint32_t pixels[SMW_MAX_PITCH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t stereo[SMW_MAX_SAMPLES * 2] __attribute__((aligned(64)));
static GSGLOBAL *gs;
static GSTEXTURE texture;
static bool pad_open[2];
static bool sound;
static int audio_capacity;  /* bytes in audsrv's ring buffer */
static const char *card_drivers = "nenhum";

/* Video. 240p: like the SNES, for CRT TVs. 480i: the same 224 lines shown
   in both fields of an interlaced picture (flat TVs often handle it better
   than 240p over the AV cable). 480p: component or HDMI adapter; there each
   SNES line becomes two TV lines, and optional scanlines darken every
   second line like a CRT fed with RGB. suave: 0 sharp (default: over the AV
   cable the TV already softens the picture), 1 medium, 2 strong (GS bilinear
   filter). */
static struct { int modo; bool p480, scanlines, wide, compat; int suave; } video = {
    SMW_VIDEO_240P, false, true, false, false, 0};
static SmwOptions opcoes;  /* what is saved in SMW.OPT */
/* Image columns and row length in pixels: 256/256 (4:3) or 342/384 (16:9). */
static unsigned screen_w = SMW_WIDTH, screen_pitch = SMW_WIDTH;

/* L2+R2 + Triangle: performance meter. Every 60 game frames it shows how
   many were drawn and the average EE time of a frame with and without
   drawing (game logic + PPU), not counting the wait for the TV. */
static struct {
  bool on;
  unsigned steps, drawn;
  uint32_t draw_ticks, logic_ticks;
  char text[64];
} perf;

/* Fatal errors use the PS2SDK text screen, which needs no other video. */
static void stop_with(const char *reason) {
  init_scr();
  scr_printf("RETROGAMES  |  SUPER MARIO WORLD  |  PS2 v" SMW_VERSAO "\n\n%s\n", reason);
  printf("SMW: %s\n", reason);
  for (;;) SleepThread();
}

void Die(const char *error) { stop_with(error); }

static bool load_driver(void *image, unsigned int size, const char *name) {
  int status = -1;
  int id = SifExecModuleBuffer(image, size, 0, NULL, &status);
  /* A removable resident module is also ready to use (IOP modload >= 1.3). */
  if (id >= 0 && (status == 0 || status == 2)) return true;
  if (SifSearchModuleByName(name) > 0) return true;
  printf("SMW: %s: id %d, status %d\n", name, id, status);
  return false;
}
#define LOAD(n) load_driver(n##_irx, size_##n##_irx, #n)

static bool start_audio(void) {
  if (SifLoadModule("rom0:LIBSD", 0, NULL) < 0 && !LOAD(freesd)) return false;
  if (!LOAD(audsrv) || audsrv_init() != 0) return false;
  struct audsrv_fmt_t fmt = {.freq = 32000, .bits = 16, .channels = 2};
  if (audsrv_set_format(&fmt) != 0 || audsrv_set_volume(MAX_VOLUME) != 0) return false;
  audio_capacity = audsrv_available();  /* empty ring = its capacity */
  return audio_capacity >= SMW_MAX_SAMPLES * 4 * 2;
}

static void start_drivers(bool from_disc) {
  SifInitRpc(0);
  while (!SifIopReset("", 0)) {}
  while (!SifIopSync()) {}
  SifInitRpc(0);
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();
  /* From a disc image (OPL ISO mode) the USB drive is in use by OPL's own
     driver. Loading a second USB stack fails or disturbs the ISO reads, so
     the progress goes to the memory card instead. */
  if (!from_disc) {
    /* bdmfs_fatfs registers mass: with the stock IOMAN file interface. */
    bool block = LOAD(bdm);
    bool fat = LOAD(bdmfs_fatfs);
    bool usb = LOAD(usbd);
    bool storage = LOAD(usbmass_bd);
    if (!(block && fat && usb && storage))
      printf("SMW: USB: tentando os dispositivos ja montados.\n");
  }
  /* Controller and memory card drivers: the console's own BIOS modules
     first, exactly like commercial games load them. OPL's ISO mode is made
     for that setup. The SDK copies built into the ELF are only a fallback
     for consoles whose BIOS lacks the X modules. */
  bool cards;
  if (SifLoadModule("rom0:XSIO2MAN", 0, NULL) >= 0) {
    if (SifLoadModule("rom0:XPADMAN", 0, NULL) < 0 && !LOAD(padman))
      stop_with("Falha ao iniciar o controle.");
    cards = SifLoadModule("rom0:XMCMAN", 0, NULL) >= 0;
    if (!cards) {  /* XMCMAN needs SECRMAN; load it if the reset left it out */
      SifLoadModule("rom0:SECRMAN", 0, NULL);
      cards = SifLoadModule("rom0:XMCMAN", 0, NULL) >= 0;
    }
    cards = cards && SifLoadModule("rom0:XMCSERV", 0, NULL) >= 0;
    card_drivers = "BIOS";
  } else {
    if (!LOAD(sio2man) || !LOAD(padman))
      stop_with("Falha ao iniciar o controle.");
    cards = LOAD(mcman);
    if (!cards) {  /* mcman needs SECRMAN; load it from the BIOS if missing */
      SifLoadModule("rom0:SECRMAN", 0, NULL);
      cards = LOAD(mcman);
    }
    cards = cards && LOAD(mcserv);
    card_drivers = "embutidos";
  }
  if (padInit(0) < 0) stop_with("Falha ao iniciar o controle.");
  for (int i = 0; i < 2; ++i) pad_open[i] = padPortOpen(i, 0, pad_buf[i]) != 0;
  cards = cards && mcInit(MC_TYPE_XMC) >= 0;
  smw_enable_memory_card(cards);

  sound = start_audio();
}

/* The input word uses SNES serial order, bit 0 = B and bit 11 = R. */
static uint16_t read_pad(int port, unsigned *connected, uint16_t *raw) {
  if (!pad_open[port]) return 0;
  int state = padGetState(port, 0);
  if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) return 0;
  struct padButtonStatus pad;
  if (padRead(port, 0, &pad) <= 0) return 0;
  *connected |= 1u << port;
  uint16_t held = (uint16_t)~pad.btns;
  if (raw) *raw = held;
  static const uint16_t mapping[12] = {PAD_CROSS, PAD_SQUARE, PAD_SELECT, PAD_START,
       PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_CIRCLE, PAD_TRIANGLE, PAD_L1, PAD_R1};
  uint16_t result = 0;
  for (int i = 0; i < 12; ++i)
    if (held & mapping[i]) result |= (uint16_t)(1u << i);
  return result;
}

/* Which pads count as plugged in. A pad has to stay in the same state for
   half a second before the game switches players, so a pad that is briefly
   busy (padman state changes) does not hand control to the other one. */
static unsigned stable_pads(unsigned connected) {
  static unsigned stable, last;
  static int same = -1;
  if (same < 0 || connected != last) {
    if (same < 0) stable = connected;
    last = connected;
    same = 0;
  } else if (same < 30 && ++same == 30) {
    stable = connected;
  }
  return stable;
}

/* Settings screens (320 x 224 frame, formato.c). */
static uint32_t quadro[FORMATO_W * FORMATO_H] __attribute__((aligned(64)));
static GSTEXTURE quadro_tex;
extern unsigned char msx[];  /* PS2SDK libdebug 8x8 font */

/* Video modes. VIDEO_AJUSTES is the one of the settings screen: 480i with
   640 columns, the mode of the PS2SDK text screen, which every TV shows.
   The game uses 1280 columns in 240p and 480i (512 if compat): 5 per SNES
   pixel in 4:3 (the same picture as 512 = 2 per pixel) and about 3.74 in
   16:9, where 512 gave alternating 1- and 2-column pixels. 480p keeps 640. */
enum { VIDEO_AJUSTES = 100 };

static void video_abre(int modo, bool compat) {
  static bool dma_ready;
  if (!dma_ready) {
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    dma_ready = true;
  }
  if (gs) {
    gsKit_vram_clear(gs);
    gsKit_deinit_global(gs);
  }
  gs = gsKit_init_global();
  if (!gs) stop_with("Sem memoria para iniciar o video.");
  if (modo == SMW_VIDEO_480P) {
    gs->Mode = GS_MODE_DTV_480P;
    gs->Interlace = GS_NONINTERLACED;
    gs->Field = GS_FRAME;
    gs->Width = 640;
    gs->Height = 480;
  } else {
    gs->Mode = GS_MODE_NTSC;
    /* 480i in FIELD mode with 224 lines: every line is shown in both fields. */
    bool entrelacado = modo == SMW_VIDEO_480I || modo == VIDEO_AJUSTES;
    gs->Interlace = entrelacado ? GS_INTERLACED : GS_NONINTERLACED;
    gs->Field = entrelacado ? GS_FIELD : GS_FRAME;
    gs->Width = modo == VIDEO_AJUSTES ? 640 : compat ? 512 : 1280;
    gs->Height = SMW_HEIGHT;
  }
  gs->PSM = GS_PSM_CT32;
  gs->ZBuffering = GS_SETTING_OFF;
  gs->DoubleBuffering = GS_SETTING_ON;
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);

  texture.Width = screen_pitch;
  texture.Height = SMW_HEIGHT;
  texture.PSM = GS_PSM_CT32;
  texture.Filter = GS_FILTER_NEAREST;
  texture.TBW = screen_pitch / 64;
  texture.Mem = (u32 *)pixels;
  /* Allocated for the widest image so 4:3/16:9 can be switched in game. */
  texture.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(SMW_MAX_PITCH, SMW_HEIGHT, texture.PSM),
                                   GSKIT_ALLOC_USERBUFFER);
  quadro_tex.Width = FORMATO_W;
  quadro_tex.Height = FORMATO_H;
  quadro_tex.PSM = GS_PSM_CT32;
  quadro_tex.Filter = GS_FILTER_NEAREST;
  quadro_tex.TBW = FORMATO_W / 64;
  quadro_tex.Mem = (u32 *)quadro;
  quadro_tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(FORMATO_W, FORMATO_H, GS_PSM_CT32),
                                     GSKIT_ALLOC_USERBUFFER);
  if (texture.Vram == GSKIT_ALLOC_ERROR || quadro_tex.Vram == GSKIT_ALLOC_ERROR)
    stop_with("Sem memoria de video para a textura.");
  video.modo = modo;
  video.p480 = modo == SMW_VIDEO_480P;
  video.compat = compat;
}

/* One frame of a settings screen (already drawn in quadro). */
static void mostra_quadro(int bright) {
  smw_mostra_quadro(gs, &quadro_tex, quadro, video.p480, bright, video.suave);
}

/* Small on-screen messages: save errors (codes in the LEIA-ME), where the
   progress was loaded from and save confirmations. 3x5 capitals. */
static void draw_text(unsigned x, unsigned y, const char *msg, uint32_t bg) {
  static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:.";
  static const unsigned char glyph[][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},
    {5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},
    {7,5,5,5,7},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},
    {5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,7,0,0},{0,2,0,2,0},{0,0,0,0,2}
  };
  unsigned width = (unsigned)strlen(msg) * 4 + 2;
  if (x + width > screen_w) width = screen_w - x;
  for (unsigned row = 0; row < 7; ++row)
    for (unsigned col = 0; col < width; ++col)
      pixels[(y + row) * screen_pitch + x + col] = bg;
  for (unsigned i = 0; msg[i] && x + 1 + i*4 + 3 <= screen_w; ++i) {
    const char *at = msg[i] == ' ' ? NULL : strchr(letters, msg[i]);
    if (!at) continue;  /* unknown characters are left blank */
    const unsigned char *g = glyph[at - letters];
    for (unsigned row = 0; row < 5; ++row)
      for (unsigned col = 0; col < 3; ++col)
        if (g[row] & (4 >> col)) pixels[(y + 1 + row) * screen_pitch + x + 1 + i*4 + col] = 0x00ffffff;
  }
}

/* "CARREGADO: ...", "SALVO: ..." and similar, for 4 seconds. */
static void draw_notice(void) {
  static unsigned shown_serial = ~0u, frames;
  unsigned serial;
  const char *text = smw_notice(&serial);
  if (serial != shown_serial) shown_serial = serial, frames = 240;
  if (!frames || !text[0]) return;
  --frames;
  bool bad = strncmp(text, "SAVE ILEGIVEL", 13) == 0;
  draw_text(8, SMW_HEIGHT - 16, text, bad ? 0x00000070 : 0x00104010);
}

static void present(void) {
  if (smw_save_error) {
    char msg[64];
    snprintf(msg, sizeof(msg), "ERRO AO SALVAR %s", smw_save_error_text());
    draw_text(8, 8, msg, 0x00000070);
  }
  draw_notice();
  if (perf.on && perf.text[0]) draw_text(8, SMW_HEIGHT - 26, perf.text, 0x00402000);
  SyncDCache(pixels, pixels + screen_pitch * SMW_HEIGHT);
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0, 0));
  gsKit_texture_send_inline(gs, texture.Mem, texture.Width, texture.Height,
                            texture.Vram, texture.PSM, texture.TBW, GS_CLUT_NONE);
  /* 480p: 224 SNES lines -> 448 TV lines, centred in the 480 visible. */
  float top = video.p480 ? 16.0f : 0.0f;
  float height = video.p480 ? 2.0f * SMW_HEIGHT : (float)gs->Height;
  /* 16:9: 342 columns in the same 4:3 picture; a TV in wide mode stretches
     it back to the right proportions. */
  smw_sprite_suave(gs, &texture, 0, top, gs->Width, top + height, screen_w, SMW_HEIGHT, video.suave);
  if (video.p480 && video.scanlines) {
    /* Scanlines: dest + (black - dest) * alpha / 128 */
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gs->PrimAlphaEnable = GS_SETTING_ON;
    for (int line = 0; line < SMW_HEIGHT; ++line)
      gsKit_prim_sprite(gs, 0, top + 2 * line + 1, gs->Width, top + 2 * line + 2, 2,
                        GS_SETREG_RGBAQ(0, 0, 0, 0x50, 0));
    gs->PrimAlphaEnable = GS_SETTING_OFF;
  }
  gsKit_queue_exec(gs);
  gsKit_finish();
  gsKit_sync_flip(gs);
}

/* The TV refresh drives the game. Audio follows it: each frame asks the
   engine for slightly more or fewer than 534 samples to keep audsrv's
   buffer about three frames full, and never waits for audio (waiting on
   two clocks is what caused periodic hitches). */
static int samples_for_next_frame(void) {
  if (!sound) return SMW_SAMPLES;
  int target = 3 * 535 * 4;
  if (target > audio_capacity / 2) target = audio_capacity / 2;
  int queued = audio_capacity - audsrv_available();
  int n = 535 + (target - queued) / 1024;
  if (n < SMW_SAMPLES - 2) n = SMW_SAMPLES - 2;
  if (n > SMW_MAX_SAMPLES - 2) n = SMW_MAX_SAMPLES - 2;
  return n;
}

/* 4:3 (256 columns) or 16:9 (342 columns, for a TV set to wide mode). */
static void set_aspect(bool wide) {
  video.wide = wide;
  screen_w = wide ? SMW_WIDE_WIDTH : SMW_WIDTH;
  screen_pitch = wide ? SMW_MAX_PITCH : SMW_WIDTH;
  memset(pixels, 0, sizeof(pixels));
  smw_set_widescreen(wide, pixels, screen_pitch);
  texture.Width = screen_pitch;
  texture.TBW = screen_pitch / 64;
}

/* L2+R2 + Square: scanlines on/off (480p). L2+R2 + Circle: smoothing
   (off, medium, strong).
   L2+R2 + Triangle: performance meter. L2+R2 + Cross: 4:3 / 16:9. While
   L2+R2 are held the pad sends nothing to the game. */
static uint16_t video_hotkeys(uint16_t game, uint16_t raw, uint16_t *previous) {
  uint16_t pressed = raw & ~*previous;
  *previous = raw;
  if ((raw & (PAD_L2 | PAD_R2)) != (PAD_L2 | PAD_R2)) return game;
  if (pressed & PAD_SQUARE) video.scanlines = !video.scanlines;
  if (pressed & PAD_CIRCLE) {
    video.suave = (video.suave + 1) % 3;
    opcoes.suave = video.suave;
    smw_options_changed(&opcoes);  /* saved with the next game save */
  }
  if (pressed & PAD_TRIANGLE) perf.on = !perf.on, perf.text[0] = 0;
  if (pressed & PAD_CROSS) {
    set_aspect(!video.wide);
    opcoes.tela = video.wide;
    smw_options_changed(&opcoes);  /* fica gravado junto com o proximo save */
  }
  return 0;
}

/* EE Count runs at 294.912 MHz: 29491 ticks = 0.1 ms. */
static void perf_step(bool draw, uint32_t ticks) {
  ++perf.steps;
  if (draw) ++perf.drawn, perf.draw_ticks += ticks;
  else perf.logic_ticks += ticks;
  if (perf.steps < 60) return;
  unsigned skipped = perf.steps - perf.drawn;
  unsigned d = perf.drawn ? perf.draw_ticks / perf.drawn / 29491 : 0;
  if (skipped) {
    unsigned l = perf.logic_ticks / skipped / 29491;
    snprintf(perf.text, sizeof(perf.text), "TELA %u - DESENHO %u.%u MS - LOGICA %u.%u MS",
             perf.drawn, d / 10, d % 10, l / 10, l % 10);
  } else {
    snprintf(perf.text, sizeof(perf.text), "TELA %u - DESENHO %u.%u MS", perf.drawn, d / 10, d % 10);
  }
  perf.steps = perf.drawn = 0;
  perf.draw_ticks = perf.logic_ticks = 0;
}

static void step_game(bool draw) {
  static uint16_t previous[2];
  unsigned connected = 0;
  uint16_t raw1 = 0, raw2 = 0;
  uint16_t p1 = read_pad(0, &connected, &raw1), p2 = read_pad(1, &connected, &raw2);
  p1 = video_hotkeys(p1, raw1, &previous[0]);
  p2 = video_hotkeys(p2, raw2, &previous[1]);
  int samples = samples_for_next_frame();
  uint32_t start = cpu_ticks();
  smw_frame(p1, p2, stable_pads(connected), stereo, samples, draw);
  perf_step(draw, cpu_ticks() - start);
  if (sound) {
    int bytes = samples * 4;
    if (audsrv_available() >= bytes) audsrv_play_audio((const char *)stereo, bytes);
  }
}

/* EE Count ticks between two vertical blanks, measured on this console. */
static uint32_t measure_field(void) {
  gsKit_vsync_wait();
  uint32_t start = cpu_ticks();
  for (int i = 0; i < 16; ++i) gsKit_vsync_wait();
  uint32_t field = (cpu_ticks() - start) / 16;
  /* 294.912 MHz / 59.83 Hz (NTSC 240p); used if the measure looks wrong. */
  if (field < 4000000 || field > 6500000) field = 4929000;
  return field;
}

/* PS2 buttons held on either pad (1 = held). */
static unsigned botoes(void) {
  unsigned connected = 0;
  uint16_t r0 = 0, r1 = 0;
  read_pad(0, &connected, &r0);
  read_pad(1, &connected, &r1);
  return (unsigned)(r0 | r1);
}

static void carregando(const char *etapa, int passo) {
  formato_carregando(quadro, msx, etapa, passo, 4, 0);
  mostra_quadro(128);
}

/* Order of the video modes on the screen: 240p, 480i, 480p. */
static int outro_video(int v, int dir) {
  static const int ordem[3] = {SMW_VIDEO_240P, SMW_VIDEO_480I, SMW_VIDEO_480P};
  int i = 0;
  while (i < 2 && ordem[i] != v) ++i;
  return ordem[(i + 3 + dir) % 3];
}

/* Tela AJUSTES. contagem: comeca sozinha em 5 s (ajustes ja salvos) ate o
   jogador apertar um botao. Volta com X ou START (ou no fim da contagem). */
static void tela_ajustes(SmwOptions *o, bool contagem, const char *aviso, const char *situacao) {
  Ajustes a = {contagem ? AJUSTE_JOGAR : AJUSTE_VIDEO, o->video, o->tela, o->suave, o->idioma,
               -1, aviso, situacao};
  unsigned held = botoes();
  int saindo = -1;
  for (int frame = 0;; ++frame) {
    unsigned agora = botoes(), apertou = agora & ~held;
    held = agora;
    if (saindo < 0 && apertou) {
      contagem = false;
      int dir = (apertou & PAD_LEFT) ? -1 : (apertou & PAD_RIGHT) ? 1 : 0;
      if (apertou & (PAD_CROSS | PAD_START)) {
        saindo = frame;
      } else if (apertou & (PAD_UP | PAD_DOWN)) {
        a.linha = (a.linha + ((apertou & PAD_UP) ? AJUSTE_LINHAS - 1 : 1)) % AJUSTE_LINHAS;
        smw_blip(sound);
      } else if (dir && a.linha != AJUSTE_JOGAR) {
        if (a.linha == AJUSTE_VIDEO) a.video = outro_video(a.video, dir);
        if (a.linha == AJUSTE_TELA) a.tela = !a.tela;
        if (a.linha == AJUSTE_SUAVE) a.suave = (a.suave + 3 + dir) % 3;
        if (a.linha == AJUSTE_IDIOMA) a.idioma = !a.idioma;
        a.aviso = NULL;
        smw_blip(sound);
      }
    }
    if (saindo < 0 && contagem && frame >= 5 * 60) saindo = frame;
    a.segundos = contagem ? 5 - frame / 60 : -1;
    formato_ajustes(quadro, msx, &a, frame);
    video.suave = a.suave;  /* the screen itself shows the smoothing */
    int bright = frame < 15 ? frame * 128 / 15 : 128;
    if (saindo >= 0) {
      bright = 128 - (frame - saindo) * 128 / 15;
      if (frame - saindo > 15) break;
    }
    mostra_quadro(bright < 0 ? 0 : bright);
  }
  o->video = a.video, o->tela = a.tela, o->suave = a.suave, o->idioma = a.idioma;
}

/* No modo novo: X ou START em 10 s confirma que a TV mostra a imagem. */
static bool confirma_video(int idioma) {
  unsigned held = botoes();
  for (int frame = 0; frame < 10 * 60; ++frame) {
    unsigned agora = botoes(), apertou = agora & ~held;
    held = agora;
    if (frame > 20 && (apertou & (PAD_CROSS | PAD_START))) return true;
    formato_confirma(quadro, msx, video.modo, idioma, 10 - frame / 60, frame);
    mostra_quadro(frame < 15 ? frame * 128 / 15 : 128);
  }
  return false;
}

static bool mesmas_opcoes(const SmwOptions *a, const SmwOptions *b) {
  return a->tela == b->tela && a->video == b->video && a->idioma == b->idioma &&
         a->suave == b->suave && (a->compat == 1) == (b->compat == 1);
}

int main(int argc, char **argv) {
  char folder[SMW_PATH] = "", actual[SMW_PATH] = "", report[1024], msg[256];
  if (argc && argv && argv[0]) smw_boot_folder(argv[0], folder);
  bool from_disc = smw_path_is_disc(folder);
  video_abre(VIDEO_AJUSTES, false);
  carregando(from_disc ? "Controles e Memory Card..." : "Pendrive, controles e Memory Card...", 1);
  start_drivers(from_disc);
  if (!sound) printf("SMW: audio indisponivel, continuando.\n");
  carregando("Procurando o jogo...", 2);
  uint8_t *rom = NULL;
  uint32_t crc = 0;
  RomResult result = ROM_MISSING;
  for (int i = 0; i < 25 && result == ROM_MISSING; ++i) {
    result = smw_find_rom(folder, &rom, actual, &crc);
    if (result == ROM_MISSING) usleep(200000);
  }
  if (result == ROM_MISSING)
    stop_with("Coloque Super Mario World (USA).zip ou SMW.SFC\nem APPS/SMW no pendrive FAT32 e abra o ELF de novo.");
  if (result != ROM_GOOD) {
    const char *motivo =
        result == ROM_BAD_ZIP ? "ZIP invalido ou sem arquivo .sfc/.smc."
        : result == ROM_BAD_SIZE ? "Tamanho incorreto. Use o jogo USA de SNES, 512 KiB,\ncom ou sem cabecalho de copiador de 512 bytes."
        : result == ROM_NO_MEMORY ? "Memoria insuficiente." : "Erro de leitura da midia.";
    if (result == ROM_BAD_CRC)
      snprintf(msg, sizeof(msg), "%s\nCRC32 lido: %08lX  |  Esperado: B19ED489\nEsta versao precisa do cartucho USA original.",
               actual, (unsigned long)crc);
    else
      snprintf(msg, sizeof(msg), "%s\n%s", actual, motivo);
    stop_with(msg);
  }
  carregando("Lendo o save...", 3);
  smw_start(rom, pixels);
  free(rom);
  smw_storage_report(report, sizeof(report));
  printf("SMW: drivers de controle e Memory Card: %s\n%s", card_drivers, report);
  smw_load_report(report, sizeof(report));
  printf("SMW: %sNovos saves vao para: %s\n", report, smw_save_location());
  carregando("Pronto!", 4);

  /* Ajustes: o padrao, depois o SMW.CFG (Gravador no PC) e por cima o
     SMW.OPT (o que foi escolhido no PS2). Com tudo salvo, a tela AJUSTES
     comeca sozinha em 5 s. */
  SmwOptions cfg, salvo;
  smw_read_config(&cfg);
  smw_options_clear(&salvo);
  smw_read_saved_options(&salvo);
  opcoes = (SmwOptions){0, SMW_VIDEO_240P, 0, 0, 0};
  const SmwOptions *fontes[2] = {&cfg, &salvo};
  for (int i = 0; i < 2; ++i) {
    if (fontes[i]->tela >= 0) opcoes.tela = fontes[i]->tela;
    if (fontes[i]->video >= 0) opcoes.video = fontes[i]->video;
    if (fontes[i]->idioma >= 0) opcoes.idioma = fontes[i]->idioma;
    if (fontes[i]->suave >= 0) opcoes.suave = fontes[i]->suave;
    if (fontes[i]->compat >= 0) opcoes.compat = fontes[i]->compat;
  }
  const bool conhecido = salvo.video >= 0 && salvo.tela >= 0 && salvo.suave >= 0;
  const SmwOptions inicio = opcoes;
  const char *lugar = smw_save_location();
  char situacao[64], aviso_texto[64];
  if (strncmp(lugar, "NENHUM", 6) == 0) snprintf(situacao, sizeof(situacao), "Sem save: insira um Memory Card formatado.");
  else snprintf(situacao, sizeof(situacao), "Save: %s", lugar);
  const char *aviso = NULL;
  bool contagem = conhecido;
  for (;;) {
    video.suave = opcoes.suave;
    tela_ajustes(&opcoes, contagem, aviso, situacao);
    contagem = false;
    aviso = NULL;
    bool analogico = opcoes.video != SMW_VIDEO_480P;
    /* O modo que ja foi confirmado nesta TV entra direto; um modo novo
       pergunta se a imagem apareceu. Se 1280 colunas nao aparecerem, tenta
       512 antes de voltar para os ajustes. */
    if (conhecido && opcoes.video == salvo.video) {
      video_abre(opcoes.video, analogico && opcoes.compat == 1);
      break;
    }
    video_abre(opcoes.video, analogico && opcoes.compat == 1);
    if (confirma_video(opcoes.idioma)) break;
    if (analogico && opcoes.compat != 1) {
      video_abre(opcoes.video, true);
      if (confirma_video(opcoes.idioma)) {
        opcoes.compat = 1;
        break;
      }
    }
    static const char *const nomes[] = {"240p", "480p", "480i"};
    snprintf(aviso_texto, sizeof(aviso_texto),
             opcoes.idioma == 1 ? "The TV did not show %s. Pick another." : "A TV não mostrou o %s. Escolha outro.",
             nomes[opcoes.video]);
    aviso = aviso_texto;
    video_abre(VIDEO_AJUSTES, false);
  }
  if (!conhecido || !mesmas_opcoes(&opcoes, &inicio)) smw_save_options(&opcoes);
  g_ptbr = opcoes.idioma != 1;
  video.suave = opcoes.suave;

  smw_intro(gs, video.p480, sound, SMW_VERSAO, video.suave);
  set_aspect(opcoes.tela == 1);
  const uint32_t field = measure_field();
  uint32_t last = cpu_ticks();
  int pending = 0, debt = 0;
  for (;;) {
    /* If the previous image took more than one TV refresh, catch the game
       up without drawing (at most 2 frames), so speed and music stay right. */
    for (int i = 0; i < pending; ++i) step_game(false);
    step_game(true);
    present();
    uint32_t now = cpu_ticks();
    int fields = (int)((now - last + field / 2) / field);
    last = now;
    debt += fields - (1 + pending);
    if (debt < 0) debt = 0;
    if (debt > 6) debt = 0;  /* long pause (saving): resume, don't fast-forward */
    pending = debt > 2 ? 2 : debt;
  }
}
