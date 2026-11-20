#include "files.h"
#include "formato.h"
#include "intro.h"
#include "registro.h"
#include "ritmo.h"
#include "trapacas.h"
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

#define SMW_VERSAO "0.16"

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

/* Video. 240p: like the SNES; the sharpest over the AV cable on any TV
   (tested on a CRT and on a flat TV). 480p: component or HDMI adapter; there
   each SNES line becomes two TV lines. The 480i option (v0.13 to v0.14.2)
   was removed in v0.15 at the user's request; only the settings screen
   still uses an interlaced mode (VIDEO_AJUSTES), which every TV shows. Always sharp pixels: the smoothing (GS
   bilinear filter) and the 480p scanlines were removed in v0.13.7 (on the
   console both looked worse than the sharp picture). */
static struct { int modo; bool p480, wide, compat; } video = {SMW_VIDEO_240P, false, false, false};

/* EE Count (cpu_ticks) runs at 294.912 MHz; it wraps every 14.5 s. */
enum { EE_HZ = 294912000, EE_TICKS_PER_TENTH_MS = EE_HZ / 10000 };
static SmwOptions opcoes;  /* what is saved in SMW.OPT */
/* Image columns and row length in pixels: 256/256 (4:3) or 342/384 (16:9). */
static unsigned screen_w = SMW_WIDTH, screen_pitch = SMW_WIDTH;

/* L2+R2 + Triangle: performance meter. Every 60 game frames it shows how
   many were drawn (TELA), the average EE time of a drawn frame (DESENHO:
   game logic + PPU), of sending it to the GS (ENVIO, v0.13.7) and of a frame
   that was not drawn (LOGICA), not counting the wait for the TV. */
static struct {
  bool on;
  unsigned steps, drawn;
  uint32_t draw_ticks, send_ticks, logic_ticks;
  char text[80];
} perf;

/* Fatal errors use the PS2SDK text screen, which needs no other video. */
static void stop_with(const char *reason) {
  init_scr();
  scr_printf("RETROGAMES  |  SUPER MARIO WORLD  |  PS2 v" SMW_VERSAO "\n\n%s\n", reason);
  smw_log("ERRO: %s", reason);
  smw_log_grava();
  for (;;) SleepThread();
}

void Die(const char *error) { stop_with(error); }

static bool load_driver(void *image, unsigned int size, const char *name) {
  int status = -1;
  int id = SifExecModuleBuffer(image, size, 0, NULL, &status);
  /* A removable resident module is also ready to use (IOP modload >= 1.3). */
  if (id >= 0 && (status == 0 || status == 2)) return true;
  if (SifSearchModuleByName(name) > 0) return true;
  smw_log("IOP: %s nao carregou (id %d, status %d)", name, id, status);
  return false;
}
#define LOAD(n) load_driver(n##_irx, size_##n##_irx, #n)

static bool start_audio(void) {
  bool libsd = SifLoadModule("rom0:LIBSD", 0, NULL) >= 0;
  if (!libsd && !LOAD(freesd)) return false;
  if (!LOAD(audsrv)) return false;
  int r = audsrv_init();
  if (r != 0) { smw_log("Som: audsrv_init devolveu %d", r); return false; }
  struct audsrv_fmt_t fmt = {.freq = 32000, .bits = 16, .channels = 2};
  if ((r = audsrv_set_format(&fmt)) != 0 || (r = audsrv_set_volume(MAX_VOLUME)) != 0) {
    smw_log("Som: formato ou volume recusado (%d)", r);
    return false;
  }
  audio_capacity = audsrv_available();  /* empty ring = its capacity */
  smw_log("Som: %s + audsrv, buffer de %d bytes", libsd ? "LIBSD da BIOS" : "freesd embutido",
          audio_capacity);
  return audio_capacity >= SMW_MAX_SAMPLES * 4 * 2;
}

/* While the log file is written the game stops for a moment; audsrv would
   keep replaying its ring buffer, so the sound is muted until the next
   audsrv_play_audio. */
static void som_mudo(void) {
  if (sound) audsrv_stop_audio();
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
    smw_log("USB: bdm %s, bdmfs_fatfs %s, usbd %s, usbmass_bd %s", block ? "ok" : "falhou",
            fat ? "ok" : "falhou", usb ? "ok" : "falhou", storage ? "ok" : "falhou");
    if (!(block && fat && usb && storage)) smw_log("USB: tentando os dispositivos ja montados.");
  } else {
    smw_log("USB: nao carregado (jogo aberto pela ISO; o pendrive fica com o OPL)");
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
  smw_log("Controle e Memory Card: drivers %s, drivers do cartao %s", card_drivers,
          cards ? "ok" : "falharam");
  if (padInit(0) < 0) stop_with("Falha ao iniciar o controle.");
  for (int i = 0; i < 2; ++i) pad_open[i] = padPortOpen(i, 0, pad_buf[i]) != 0;
  smw_log("Controle: porta 1 %s, porta 2 %s", pad_open[0] ? "aberta" : "falhou",
          pad_open[1] ? "aberta" : "falhou");
  if (cards) {
    int r = mcInit(MC_TYPE_XMC);
    if (r < 0) smw_log("Memory Card: mcInit devolveu %d", r);
    cards = r >= 0;
  }
  smw_enable_memory_card(cards);

  sound = start_audio();
  if (!sound) smw_log("Som: indisponivel, continuando sem som.");
}

/* BIOS version (for example 0220HD20060905), to know which console made the log. */
static void log_console(void) {
  char ver[32] = "";
  FILE *f = fopen("rom0:ROMVER", "r");
  if (f) {
    if (!fgets(ver, sizeof(ver), f)) ver[0] = '\0';
    fclose(f);
  }
  ver[strcspn(ver, "\r\n")] = '\0';
  smw_log("Console: ROMVER %s", ver[0] ? ver : "(nao foi lido)");
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

/* Settings screens (256 x 224 frame, formato.c). */
static uint32_t quadro[FORMATO_W * FORMATO_H] __attribute__((aligned(64)));
/* v0.14: scenes of the game, taken from the player's ROM right after
   smw_start, as the background of the screens before the game (0x00RRGGBB,
   or NULL: formato.c draws its own landscape). */
static uint32_t cena_ajustes[SMW_WIDTH * SMW_HEIGHT], cena_titulo[SMW_WIDTH * SMW_HEIGHT],
    cena_apoio[SMW_WIDTH * SMW_HEIGHT], cena_vinheta[SMW_WIDTH * SMW_HEIGHT];
static const uint32_t *fundo_ajustes, *fundo_titulo, *fundo_apoio, *fundo_vinheta;
/* v0.16: the game's sound effects in the menus until the game starts
   (game.c). */
static bool som_do_jogo;

/* v0.14: cheats (trapacas.c) from TRAPACAS.TXT; the ones that are on (name
   and codes) go to TRAPACAS.LIG, where the save is. trapacas_on: how many
   are on, so the hook does not walk the list every frame for nothing. */
static Trapacas trapacas;
static char trapacas_lugar[40];
static bool trapacas_mudou;
static int trapacas_on;
static void aplica_trapacas(uint8_t *ram) {
  if (trapacas_on) trapacas_aplica(&trapacas, ram);
}
static GSTEXTURE quadro_tex;
extern unsigned char msx[];  /* PS2SDK libdebug 8x8 font */

/* Video modes. VIDEO_AJUSTES is the one of the settings screen: 480i with
   512 columns (the old width of the game, which every TV shows); since
   v0.14 the menus are 256 x 224 like the SNES, 2 columns per pixel, so the
   scenes of the game behind them keep their pixels even (with 640 columns,
   until v0.13.8, they would be 2.5 each).
   The game uses 1280 columns (5 per SNES pixel in 4:3, about 3.74 in 16:9):
   with 512 in 240p/480i the 16:9 pixels alternated 1 and 2 columns, and
   with 640 in 480p every SNES pixel was 2 or 3 columns (2.5 each). 480p at
   1280 x 480 uses a 16-bit frame buffer (two 32-bit ones do not fit in the
   4 MB of VRAM); SNES colours have 5 bits per channel, so nothing is lost.
   compat: the old widths (512, or 640 in 480p) for a TV that does not show
   1280. */
enum { VIDEO_AJUSTES = 100 };

static const char *nome_video(int modo) {
  switch (modo) {
  case SMW_VIDEO_240P: return "240p";
  case SMW_VIDEO_480P: return "480p";
  case VIDEO_AJUSTES: return "AJUSTES (480i)";
  default: return "?";
  }
}

/* v0.14.2: posicao da imagem na TV (como no port do Yu-Gi-Oh), em pixels e
   linhas do SNES. So muda onde o PCRTC comeca a mostrar o quadro (DISPLAY1
   e DISPLAY2, gsKit_set_display_offset): nao custa nada e nao perde
   nitidez. Um pixel do SNES e 10 VCK no NTSC (1280 colunas com MAGH 1, ou
   512 com MAGH 4) e 5 no 480p (1280 com MAGH 0, ou 640 com MAGH 1). Uma
   linha e 1 unidade do DY no 240p; 2 nos menus (o DY entrelacado
   conta as linhas do quadro inteiro, e o gsKit o mantem par) e no 480p (o
   jogo em 448 linhas). A unidade e sempre o pixel do quadro 4:3: no 16:9
   o mesmo toque anda o mesmo tanto na TV (cerca de 1,34 pixel do quadro
   de 342 colunas). O gsKit_init_screen zera o deslocamento: video_abre o
   poe de novo. O comeco nunca passa de 0 (com os inicios do gsKit, de 25 a
   652, a faixa de 16 cabe com folga; e so a garantia). */
static int posicao_x, posicao_y;
static void aplica_posicao(void) {
  if (!gs) return;
  const bool p480 = gs->Mode == GS_MODE_DTV_480P;
  int dx = posicao_x * (p480 ? 5 : 10);
  int dy = posicao_y * (p480 || gs->Interlace == GS_INTERLACED ? 2 : 1);
  if (dx < -gs->StartX) dx = -gs->StartX;
  if (dy < -gs->StartY) dy = -gs->StartY;  /* no entrelacado o StartY e par */
  gsKit_set_display_offset(gs, dx, dy);
}
static void muda_posicao(int x, int y) {
  posicao_x = smw_posicao_limita(x), posicao_y = smw_posicao_limita(y);
  aplica_posicao();
}

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
  gs->PSM = GS_PSM_CT32;
  if (modo == SMW_VIDEO_480P) {
    gs->Mode = GS_MODE_DTV_480P;
    gs->Interlace = GS_NONINTERLACED;
    gs->Field = GS_FRAME;
    gs->Width = compat ? 640 : 1280;
    gs->Height = 480;
    if (!compat) gs->PSM = GS_PSM_CT16;
  } else {
    gs->Mode = GS_MODE_NTSC;
    /* The settings screen: 480i in FIELD mode with 224 lines (every line in
       both fields), which every TV shows. */
    bool entrelacado = modo == VIDEO_AJUSTES;
    gs->Interlace = entrelacado ? GS_INTERLACED : GS_NONINTERLACED;
    gs->Field = entrelacado ? GS_FIELD : GS_FRAME;
    gs->Width = modo == VIDEO_AJUSTES || compat ? 512 : 1280;
    gs->Height = SMW_HEIGHT;
  }
  gs->ZBuffering = GS_SETTING_OFF;
  gs->DoubleBuffering = GS_SETTING_ON;
  gs->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);
  aplica_posicao();

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
  smw_log("GS: %s, quadro %dx%d de %d bits, MAGH %d MAGV %d, DW %d DH %d, inicio X %d Y %d, deslocamento %+d %+d",
          nome_video(modo), gs->Width, gs->Height, gs->PSM == GS_PSM_CT16 ? 16 : 32,
          gs->MagH, gs->MagV, gs->DW, gs->DH, gs->StartX, gs->StartY, gs->StartXOffset, gs->StartYOffset);
}

/* Menus (carregando, AJUSTES, "A IMAGEM APARECEU?"): the IOP side of
   audsrv keeps reading its ring buffer even when nothing new arrives, so
   whatever is left there plays again every ~0.1 s (a lone blip kept
   beeping). Each menu frame therefore sends sound, the blip or silence,
   keeping the queue at the same level as the game (samples_for_next_frame). */
static void som_menu(void) {
  if (!sound) return;
  int target = 3 * 535 * 4;
  if (target > audio_capacity / 2) target = audio_capacity / 2;
  int frames = (target - (audio_capacity - audsrv_available())) / 4;
  while (frames > 0) {
    int n = frames < SMW_MAX_SAMPLES ? frames : SMW_MAX_SAMPLES;
    if (som_do_jogo) smw_som_menus_gera(stereo, n);
    else smw_som_menu(stereo, n);
    audsrv_play_audio((const char *)stereo, n * 4);
    frames -= n;
  }
}

/* v0.16: a menu sound. Before the game, the game's own effect (the menu
   cursor of the file screen, 6, or the coin, 1); in the game (the cheats,
   L2+R2+SELECT, with the game paused) the port's blip, as before. */
static void efeito(uint8_t qual) {
  if (som_do_jogo) smw_som_menus_efeito(qual);
  else smw_blip(sound);
}

/* One frame of a settings screen (already drawn in quadro). In the game in
   16:9 (the cheats, L2+R2+SELECT) screen_w is 342: the 256-column screen
   sits in the middle, its pixels as wide as the game's (v0.14). */
static void mostra_quadro(int bright) {
  som_menu();
  smw_mostra_quadro(gs, &quadro_tex, quadro, video.p480, bright, screen_w);
}

/* Width in pixels of a draw_text_scaled message: 4 per letter + 2. */
static unsigned text_width(const char *msg, unsigned scale) {
  return ((unsigned)strlen(msg) * 4 + 2) * scale;
}

/* Small on-screen messages: save errors (codes in the LEIA-ME), where the
   progress was loaded from and save confirmations. 3x5 capitals, each dot
   scale x scale pixels. */
static void draw_text_scaled(unsigned x, unsigned y, const char *msg, uint32_t bg, unsigned scale) {
  static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:.%+,";
  static const unsigned char glyph[][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},
    {5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},
    {7,5,5,5,7},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},
    {5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
    {0,0,7,0,0},{0,2,0,2,0},{0,0,0,0,2},{5,1,2,4,5},{0,2,7,2,0},{0,0,0,2,4}
  };
  const unsigned s = scale;
  if (!s || x >= screen_w || y + 7 * s > SMW_HEIGHT) return;
  unsigned width = text_width(msg, s);
  if (x + width > screen_w) width = screen_w - x;
  for (unsigned row = 0; row < 7 * s; ++row)
    for (unsigned col = 0; col < width; ++col)
      pixels[(y + row) * screen_pitch + x + col] = bg;
  for (unsigned i = 0; msg[i] && x + (1 + i*4 + 3) * s <= screen_w; ++i) {
    const char *at = msg[i] == ' ' ? NULL : strchr(letters, msg[i]);
    if (!at) continue;  /* unknown characters are left blank */
    const unsigned char *g = glyph[at - letters];
    for (unsigned row = 0; row < 5; ++row)
      for (unsigned col = 0; col < 3; ++col) {
        if (!(g[row] & (4 >> col))) continue;
        uint32_t *dot = &pixels[(y + (1 + row) * s) * screen_pitch + x + (1 + i*4 + col) * s];
        for (unsigned dy = 0; dy < s; ++dy)
          for (unsigned dx = 0; dx < s; ++dx) dot[dy * screen_pitch + dx] = 0x00ffffff;
      }
  }
}

static void draw_text(unsigned x, unsigned y, const char *msg, uint32_t bg) {
  draw_text_scaled(x, y, msg, bg, 1);
}

/* Notices for 5 s at double size under the status bar (EE ticks; 5 s fits
   before the counter wraps): "16:9 SO NO 480P" (v0.14.1) and the position
   on the TV ("POSICAO +2, -4" or "POSICAO CENTRO", v0.14.2). The "TELA 4:3"
   / "TELA 16:9" of the v0.13.7 (at the start and after L2+R2+X) was removed
   in v0.15 at the user's request. */
static const uint32_t ASPECT_NOTICE_TICKS = 5u * EE_HZ;
static bool aspect_notice;
static uint32_t aspect_notice_start;
static char aspect_notice_text[24];
static void show_video_notice(const char *text) {
  snprintf(aspect_notice_text, sizeof(aspect_notice_text), "%s", text);
  aspect_notice = true;
  aspect_notice_start = cpu_ticks();
}
static void draw_aspect_notice(void) {
  if (!aspect_notice) return;
  if (cpu_ticks() - aspect_notice_start > ASPECT_NOTICE_TICKS) {
    aspect_notice = false;
    return;
  }
  const char *text = aspect_notice_text;
  draw_text_scaled((screen_w - text_width(text, 2)) / 2, 44, text, 0x00402000, 2);
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

/* Sends the frame to the GS and waits for the TV. Returns the EE ticks
   spent before the wait (text, cache, GS work). */
static uint32_t present(void) {
  const uint32_t start = cpu_ticks();
  if (smw_save_error) {
    char msg[64];
    snprintf(msg, sizeof(msg), "ERRO AO SALVAR %s", smw_save_error_text());
    draw_text(8, 8, msg, 0x00000070);
  }
  draw_notice();
  draw_aspect_notice();
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
  smw_desenha_textura(gs, &texture, 0, top, gs->Width, top + height, screen_w, SMW_HEIGHT);
  gsKit_queue_exec(gs);
  gsKit_finish();
  const uint32_t ticks = cpu_ticks() - start;
  /* v0.15: 30 images a second, each one exactly 2 TV frames. The flip is
     never at the first vertical blank after the last one: wait until it
     came (the GS VSINT flag, cleared right after the last flip), and
     gsKit_sync_flip waits for the next. Waiting after the flip instead kept
     the loop at 2 frames but let images stay 1 or 3 frames when the work
     went back and forth around one frame (Antigravity and Codex review).
     The wait only reads the flag: gsKit_vsync_wait clears it first, so a
     blank arriving between a test and that call was lost (review 2). */
  while (!(*GS_CSR & 8)) {}
  gsKit_sync_flip(gs);
  gsKit_vsync_nowait();  /* clears VSINT: the next blank says a frame passed */
  return ticks;
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

/* Setas da posicao (AJUSTES e L2+R2): movem a imagem SMW_POSICAO_PASSO por
   toque (cima sobe, direita vai para a direita), dentro de
   -SMW_POSICAO_MAX..SMW_POSICAO_MAX (smw_posicao_anda). */
static void passo_posicao(unsigned apertou, int *x, int *y) {
  /* Setas opostas no mesmo quadro se anulam (revisao do Codex). */
  int dx = ((apertou & PAD_RIGHT) ? 1 : 0) - ((apertou & PAD_LEFT) ? 1 : 0);
  int dy = ((apertou & PAD_DOWN) ? 1 : 0) - ((apertou & PAD_UP) ? 1 : 0);
  if (dx) *x = smw_posicao_anda(*x, dx * SMW_POSICAO_PASSO);
  if (dy) *y = smw_posicao_anda(*y, dy * SMW_POSICAO_PASSO);
}

/* L2+R2 + Triangle: performance meter; each second its line also goes to
   the log, and turning it off writes SMW_LOG.TXT (the game stops for a
   moment), so the numbers reach the PC (v0.13.8). L2+R2 + Cross: 4:3 / 16:9
   (since v0.14.1 only in 480p, otherwise "16:9 SO NO 480P"). L2+R2 + Select: the cheats (v0.14; the game
   pauses). L2+R2 + D-pad (v0.14.2): moves the picture on the TV, 2 SNES
   pixels/lines a step, up to 16 each way, like a monitor's position
   adjustment (saved with the next save, like the screen; the size of the
   v0.14.1 is gone). While L2+R2 are held the pad sends nothing to the game.
   Square and Circle (scanlines and smoothing until v0.13.6) do nothing now.
   The log is written by the main loop, after its timed part. */
static bool log_after_loop, abre_trapacas;
static uint16_t video_hotkeys(uint16_t game, uint16_t raw, uint16_t *previous) {
  uint16_t pressed = raw & ~*previous;
  *previous = raw;
  if ((raw & (PAD_L2 | PAD_R2)) != (PAD_L2 | PAD_R2)) return game;
  if (pressed & PAD_TRIANGLE) {
    perf.on = !perf.on, perf.text[0] = 0;
    perf.steps = perf.drawn = 0;  /* starts a new window */
    perf.draw_ticks = perf.send_ticks = perf.logic_ticks = 0;
    smw_log("L2+R2+TRIANGULO: medidor %s", perf.on ? "ligado" : "desligado");
    if (!perf.on) log_after_loop = true;
  }
  if (pressed & PAD_SELECT) abre_trapacas = true;  /* cheats, by the main loop */
  if ((pressed & PAD_CROSS) && !smw_16_9_vale(video.modo)) {
    show_video_notice("16:9 SO NO 480P");
    smw_log("L2+R2+X: 16:9 so no 480p (video %s)", nome_video(video.modo));
  } else if (pressed & PAD_CROSS) {
    set_aspect(!video.wide);
    opcoes.tela = video.wide;
    smw_options_changed(&opcoes);  /* fica gravado junto com o proximo save */
    smw_log("L2+R2+X: tela %s", video.wide ? "16:9" : "4:3");
  }
  if (pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) {
    int x = opcoes.pos_x, y = opcoes.pos_y;
    passo_posicao(pressed, &x, &y);
    char texto[sizeof(aspect_notice_text)];
    if (!x && !y) snprintf(texto, sizeof(texto), "POSICAO CENTRO");
    else snprintf(texto, sizeof(texto), "POSICAO %+d, %+d", x, y);
    show_video_notice(texto);  /* also at the limit, so it shows the value */
    if (x != opcoes.pos_x || y != opcoes.pos_y) {
      opcoes.pos_x = x, opcoes.pos_y = y;
      muda_posicao(x, y);
      smw_options_changed(&opcoes);  /* fica gravado junto com o proximo save */
      smw_log("L2+R2+setas: posicao %+d %+d", x, y);
    }
  }
  return 0;
}

/* One loop of the main loop: skipped game frames (logic), the drawn one and
   its sending to the GS. TELA is the frames drawn per 60 game frames (a
   window can end at 61 or 62, when a loop ran 2 or 3 of them). Returns true
   when a new line of the meter is ready. */
static bool perf_loop(unsigned skipped, uint32_t logic, uint32_t draw, uint32_t send) {
  if (!perf.on) return false;
  perf.steps += skipped + 1;
  ++perf.drawn;
  perf.logic_ticks += logic, perf.draw_ticks += draw, perf.send_ticks += send;
  if (perf.steps < 60) return false;
  unsigned not_drawn = perf.steps - perf.drawn;
  unsigned shown = (perf.drawn * 60 + perf.steps / 2) / perf.steps;
  unsigned d = perf.draw_ticks / perf.drawn / EE_TICKS_PER_TENTH_MS;
  unsigned e = perf.send_ticks / perf.drawn / EE_TICKS_PER_TENTH_MS;
  int n = snprintf(perf.text, sizeof(perf.text), "TELA %u - DESENHO %u.%u MS - ENVIO %u.%u MS",
                   shown, d / 10, d % 10, e / 10, e % 10);
  if (not_drawn && n > 0 && (size_t)n < sizeof(perf.text)) {
    unsigned l = perf.logic_ticks / not_drawn / EE_TICKS_PER_TENTH_MS;
    snprintf(perf.text + n, sizeof(perf.text) - (size_t)n, " - LOGICA %u.%u MS", l / 10, l % 10);
  }
  perf.steps = perf.drawn = 0;
  perf.draw_ticks = perf.send_ticks = perf.logic_ticks = 0;
  return true;
}

/* One game frame (pads, game logic and sound; PPU too when draw). Returns
   the EE ticks of the game frame itself. */
static uint32_t step_game(bool draw) {
  static uint16_t previous[2];
  unsigned connected = 0;
  uint16_t raw1 = 0, raw2 = 0;
  uint16_t p1 = read_pad(0, &connected, &raw1), p2 = read_pad(1, &connected, &raw2);
  p1 = video_hotkeys(p1, raw1, &previous[0]);
  p2 = video_hotkeys(p2, raw2, &previous[1]);
  int samples = samples_for_next_frame();
  uint32_t start = cpu_ticks();
  smw_frame(p1, p2, stable_pads(connected), stereo, samples, draw);
  uint32_t ticks = cpu_ticks() - start;
  if (sound) {
    int bytes = samples * 4;
    if (audsrv_available() >= bytes) audsrv_play_audio((const char *)stereo, bytes);
  }
  return ticks;
}

/* EE Count ticks between two vertical blanks, measured on this console. */
static uint32_t measure_field(void) {
  gsKit_vsync_wait();
  uint32_t start = cpu_ticks();
  for (int i = 0; i < 16; ++i) gsKit_vsync_wait();
  uint32_t field = (cpu_ticks() - start) / 16;
  unsigned hz100 = field ? (unsigned)(EE_HZ * 100ull / field) : 0;
  smw_log("TV: um quadro = %lu ciclos do EE (%u.%02u por segundo)", (unsigned long)field,
          hz100 / 100, hz100 % 100);
  /* 294.912 MHz / 59.83 Hz (NTSC 240p); used if the measure looks wrong. */
  if (field < 4000000 || field > 6500000) {
    field = 4929000;
    smw_log("TV: medida fora do normal; usando 59.83 por segundo");
  }
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

/* One frame of room sala as a background (smw_capture_scene; the engine
   starts over afterwards), converted from the GS format to 0x00RRGGBB. */
static void tira_cena(unsigned sala, uint32_t *cena, const uint32_t **fundo, SmwArteMenu *arte) {
  unsigned inicio = smw_log_ms();
  if (!smw_capture_scene(sala, cena, arte)) {
    smw_log("Cena %03X para o fundo: nao carregou", sala);
    return;
  }
  for (int i = 0; i < SMW_WIDTH * SMW_HEIGHT; ++i) {
    uint32_t p = cena[i];
    cena[i] = (p & 0xFF) << 16 | (p & 0xFF00) | ((p >> 16) & 0xFF);
  }
  *fundo = cena;
  smw_log("Cena %03X para o fundo: %u ms", sala, smw_log_ms() - inicio);
}

static void carregando(const char *etapa, int passo) {
  formato_carregando(quadro, msx, etapa, passo, 4, 0);
  mostra_quadro(128);
}

/* The video modes on the screen: 240p and 480p (480i left in v0.15). */
static int outro_video(int v, int dir) {
  (void)dir;
  return v == SMW_VIDEO_480P ? SMW_VIDEO_240P : SMW_VIDEO_480P;
}

/* Le o TRAPACAS.TXT (o do lugar do save, os outros, o CD da ISO) e liga as
   que estao no TRAPACAS.LIG. Depois de smw_start. */
static void trapacas_carrega(void) {
  static char texto[TRAPACAS_BYTES + 1];  /* o TRAPACAS.TXT, depois o TRAPACAS.LIG */
  trapacas.n = 0;
  int n = smw_read_text("TRAPACAS.TXT", texto, sizeof(texto), true, trapacas_lugar, sizeof(trapacas_lugar));
  if (n < 0) {
    trapacas_lugar[0] = '\0';
    smw_log("Trapacas: sem TRAPACAS.TXT");
    return;
  }
  trapacas_le(&trapacas, texto, trapacas_corte(texto, (size_t)n));
  int sem_codigo = 0, recusados = 0;
  for (int i = 0; i < trapacas.n; ++i) {
    sem_codigo += trapacas.t[i].codigos == 0;
    recusados += trapacas.t[i].recusados;
  }
  smw_log("Trapacas: %d no TRAPACAS.TXT (%s); %d sem codigo valido, %d codigos recusados%s", trapacas.n,
          trapacas_lugar, sem_codigo, recusados, trapacas.ignoradas ? ", linhas demais" : "");
  int m = smw_read_text("TRAPACAS.LIG", texto, sizeof(texto), false, NULL, 0);
  if (m > 0) trapacas_liga_salvas(&trapacas, texto, trapacas_corte(texto, (size_t)m));
  trapacas_on = trapacas_ligadas(&trapacas);
  smw_log("Trapacas ligadas: %d", trapacas_on);
  smw_before_frame(aplica_trapacas);
}

/* As ligadas vao para o TRAPACAS.LIG (so se mudou). */
static void trapacas_grava(void) {
  if (!trapacas_mudou) return;
  static char texto[TRAPACAS_BYTES];
  int n = trapacas_texto_ligadas(&trapacas, texto, sizeof(texto));
  som_mudo();  /* gravar para o jogo: o audsrv repetiria o ultimo bip (N2) */
  bool ok = smw_write_text("TRAPACAS.LIG", texto, n);
  smw_log("Trapacas ligadas: %d (TRAPACAS.LIG %s)", trapacas_ligadas(&trapacas), ok ? "gravado" : "nao gravado");
  if (ok) trapacas_mudou = false;
}

/* Lista de trapacas: X liga e desliga, CIRCULO (ou TRIANGULO, START, e no
   jogo SELECT) volta. No jogo, o fundo e o quadro do jogo, escurecido. */
static void tela_trapacas(bool no_jogo, const uint32_t *fundo, int idioma) {
  static FormatoTrapaca itens[TRAPACA_MAX];
  ListaTrapacas l = {.itens = itens, .n = trapacas.n, .marcada = 0, .idioma = idioma,
                     .lugar = NULL, .no_jogo = no_jogo};
  static char lugar[64];
  if (trapacas_lugar[0]) {
    snprintf(lugar, sizeof(lugar), idioma == 1 ? "File: %s" : "Arquivo: %s", trapacas_lugar);
    l.lugar = lugar;
  }
  unsigned held = botoes();
  const unsigned voltar = PAD_CIRCLE | PAD_TRIANGLE | PAD_START | (no_jogo ? PAD_SELECT : 0);
  for (int frame = 0;; ++frame) {
    unsigned agora = botoes(), apertou = agora & ~held;
    held = agora;
    if (apertou & voltar) break;
    if (l.n > 0) {
      if (apertou & PAD_UP) l.marcada = (l.marcada + l.n - 1) % l.n, efeito(SMW_EFEITO_CURSOR);
      if (apertou & PAD_DOWN) l.marcada = (l.marcada + 1) % l.n, efeito(SMW_EFEITO_CURSOR);
      if (apertou & PAD_LEFT) l.marcada = l.marcada >= 10 ? l.marcada - 10 : 0, efeito(SMW_EFEITO_CURSOR);
      if (apertou & PAD_RIGHT) l.marcada = l.marcada + 10 < l.n ? l.marcada + 10 : l.n - 1, efeito(SMW_EFEITO_CURSOR);
      Trapaca *t = &trapacas.t[l.marcada];
      if ((apertou & PAD_CROSS) && t->codigos > 0) {
        t->ligada = !t->ligada;
        trapacas_on = trapacas_ligadas(&trapacas);
        trapacas_mudou = true;
        smw_log("Trapaca %s: %s", t->ligada ? "ligada" : "desligada", t->nome);
        efeito(SMW_EFEITO_MOEDA);
      }
    }
    for (int i = 0; i < l.n; ++i)
      itens[i] = (FormatoTrapaca){trapacas.t[i].nome, trapacas.t[i].ligada, trapacas.t[i].codigos > 0};
    formato_trapacas(quadro, msx, &l, frame, fundo);
    mostra_quadro(128);
  }
  /* Espera soltar os botoes: o que voltou nao pode chegar no jogo. */
  for (int i = 0; i < 60 && botoes(); ++i) {
    formato_trapacas(quadro, msx, &l, 0, fundo);
    mostra_quadro(128);
  }
}

/* L2+R2+SELECT no jogo: pausa e abre a lista por cima do quadro atual. */
static void trapacas_no_jogo(int idioma) {
  static uint32_t cena[FORMATO_W * FORMATO_H];
  const unsigned x0 = (screen_w - FORMATO_W) / 2;
  for (int y = 0; y < FORMATO_H; ++y)
    for (int x = 0; x < FORMATO_W; ++x) {
      uint32_t p = pixels[y * screen_pitch + x0 + x];
      cena[y * FORMATO_W + x] = (p & 0xFF) << 16 | (p & 0xFF00) | ((p >> 16) & 0xFF);
    }
  smw_log("Trapacas: menu no jogo (L2+R2+SELECT)");
  tela_trapacas(true, cena, idioma);
  trapacas_grava();
}

/* Tela AJUSTES. contagem: comeca sozinha em 5 s (ajustes ja salvos) ate o
   jogador apertar um botao. Volta com X ou START (ou no fim da contagem).
   v0.14.1: a linha Tela so muda no 480p (o 16:9 so vale nele). v0.14.2: na
   linha Posicao X (ou uma seta do lado) entra no ajuste: as setas movem a
   imagem na hora (a propria tela e a previa); X, CIRCULO ou START saem. */
static bool tela_ajustes(SmwOptions *o, bool contagem, const char *aviso, const char *situacao) {
  Ajustes a = {.linha = contagem ? AJUSTE_JOGAR : AJUSTE_VIDEO, .video = o->video, .tela = o->tela,
               .pos_x = o->pos_x, .pos_y = o->pos_y, .ajustando = false,
               .idioma = o->idioma, .segundos = -1, .aviso = aviso, .situacao = situacao,
               .trapacas_total = trapacas.n};
  unsigned held = botoes();
  int saindo = -1;
  bool sozinho = false;
  for (int frame = 0;; ++frame) {
    unsigned agora = botoes(), apertou = agora & ~held;
    held = agora;
    if (saindo < 0 && apertou && a.ajustando) {
      int x = a.pos_x, y = a.pos_y;
      passo_posicao(apertou, &x, &y);
      if (x != a.pos_x || y != a.pos_y) {
        a.pos_x = x, a.pos_y = y;
        muda_posicao(x, y);
        efeito(SMW_EFEITO_CURSOR);
      }
      if (apertou & (PAD_CROSS | PAD_CIRCLE | PAD_START)) {
        a.ajustando = false;
        smw_log("AJUSTES: posicao %+d %+d", a.pos_x, a.pos_y);
        efeito(SMW_EFEITO_MOEDA);
      }
    } else if (saindo < 0 && apertou) {
      contagem = false;
      int dir = (apertou & PAD_LEFT) ? -1 : (apertou & PAD_RIGHT) ? 1 : 0;
      if (a.linha == AJUSTE_TRAPACAS && (apertou & (PAD_CROSS | PAD_RIGHT))) {
        efeito(SMW_EFEITO_MOEDA);
        tela_trapacas(false, fundo_ajustes, a.idioma);
        held = botoes();
      } else if (a.linha == AJUSTE_POSICAO && (apertou & (PAD_CROSS | PAD_LEFT | PAD_RIGHT))) {
        a.ajustando = true;
        a.aviso = NULL;
        efeito(SMW_EFEITO_MOEDA);
      } else if (apertou & (PAD_CROSS | PAD_START)) {
        saindo = frame;
        efeito(SMW_EFEITO_MOEDA);  /* como escolher o arquivo no jogo */
      } else if (apertou & (PAD_UP | PAD_DOWN)) {
        a.linha = (a.linha + ((apertou & PAD_UP) ? AJUSTE_LINHAS - 1 : 1)) % AJUSTE_LINHAS;
        efeito(SMW_EFEITO_CURSOR);
      } else if (dir && a.linha != AJUSTE_JOGAR && (a.linha != AJUSTE_TELA || smw_16_9_vale(a.video))) {
        if (a.linha == AJUSTE_VIDEO) a.video = outro_video(a.video, dir);
        if (a.linha == AJUSTE_TELA) a.tela = !a.tela;
        if (a.linha == AJUSTE_IDIOMA) a.idioma = !a.idioma;
        a.aviso = NULL;
        efeito(SMW_EFEITO_CURSOR);
      }
    }
    if (saindo < 0 && contagem && frame >= 5 * 60) saindo = frame, sozinho = true;
    a.segundos = contagem ? 5 - frame / 60 : -1;
    a.trapacas = trapacas_ligadas(&trapacas);
    formato_ajustes(quadro, msx, &a, frame, fundo_ajustes);
    /* v0.15: a tela abre e fecha em circulo (antes, clareava e escurecia).
       Saindo enquanto ainda abre, fecha do raio que ja esta na tela: a curva
       de fechar e a de abrir ao contrario, entao comeca no quadro dela que
       tem esse raio e so diminui (revisao 2). */
    int raio = formato_iris_raio(frame, true);
    if (saindo >= 0) {
      int aberto = saindo < FORMATO_IRIS_QUADROS ? saindo : FORMATO_IRIS_QUADROS;
      int q = frame - saindo + FORMATO_IRIS_QUADROS - aberto;
      if (q > FORMATO_IRIS_QUADROS) break;
      raio = formato_iris_raio(q, false);
    }
    formato_iris(quadro, raio);
    mostra_quadro(128);
  }
  o->video = a.video, o->tela = a.tela, o->idioma = a.idioma;
  o->pos_x = a.pos_x, o->pos_y = a.pos_y;
  return sozinho;
}

/* No modo novo: X ou START em 10 s confirma que a TV mostra a imagem. */
static bool confirma_video(int idioma) {
  unsigned held = botoes();
  for (int frame = 0; frame < 10 * 60; ++frame) {
    unsigned agora = botoes(), apertou = agora & ~held;
    held = agora;
    if (frame > 20 && (apertou & (PAD_CROSS | PAD_START))) {
      smw_log("Video: confirmado pelo jogador em %d.%d s", frame / 60, frame % 60 / 6);
      return true;
    }
    formato_confirma(quadro, msx, video.modo, idioma, 10 - frame / 60, frame, fundo_ajustes);
    formato_iris(quadro, formato_iris_raio(frame, true));  /* v0.15: abre em circulo */
    mostra_quadro(128);
  }
  smw_log("Video: sem resposta em 10 s");
  return false;
}

/* "tela=4:3 video=240p idioma=pt saida=1280 posicao=+0,-4"; "-" = sem valor. */
static void texto_opcoes(const SmwOptions *o, char *out, size_t size) {
  char x[8] = "-", y[8] = "-";
  if (smw_posicao_valida(o->pos_x)) snprintf(x, sizeof(x), "%+d", o->pos_x);
  if (smw_posicao_valida(o->pos_y)) snprintf(y, sizeof(y), "%+d", o->pos_y);
  snprintf(out, size, "tela=%s video=%s idioma=%s saida=%s posicao=%s,%s",
           o->tela < 0 ? "-" : o->tela ? "16:9" : "4:3",
           o->video < 0 ? "-" : nome_video(o->video),
           o->idioma < 0 ? "-" : o->idioma ? "en" : "pt",
           o->compat < 0 ? "-" : o->compat ? "512" : "1280", x, y);
}

/* v0.14.1: o 16:9 so vale no 480p (componente ou HDMI); nos outros videos o
   jogo fica em 4:3 e a escolha (tela=16:9) fica guardada. */
static bool tela_larga(const SmwOptions *o) { return o->tela == 1 && smw_16_9_vale(o->video); }

static bool mesmas_opcoes(const SmwOptions *a, const SmwOptions *b) {
  return a->tela == b->tela && a->video == b->video && a->idioma == b->idioma &&
         (a->compat == 1) == (b->compat == 1) && a->pos_x == b->pos_x && a->pos_y == b->pos_y;
}

int main(int argc, char **argv) {
  char folder[SMW_PATH] = "", actual[SMW_PATH] = "", report[1024], msg[256];
  if (argc && argv && argv[0]) smw_boot_folder(argv[0], folder);
  bool from_disc = smw_path_is_disc(folder);
  smw_log_inicia(folder);
  smw_log_antes_de_gravar(som_mudo);
  smw_before_save(som_mudo);
  smw_log("Super Mario World PS2 v" SMW_VERSAO " - RetroGames");
  smw_log("ELF: %s (%s)", argc && argv && argv[0] ? argv[0] : "(sem nome)",
          from_disc ? "aberto pela ISO" : "aberto como aplicativo");
  video_abre(VIDEO_AJUSTES, false);
  carregando(from_disc ? "Controles e Memory Card..." : "Pendrive, controles e Memory Card...", 1);
  start_drivers(from_disc);
  log_console();
  carregando("Procurando o jogo...", 2);
  uint8_t *rom = NULL;
  uint32_t crc = 0;
  RomResult result = ROM_MISSING;
  unsigned busca = smw_log_ms();
  int tentativas = 0;
  for (int i = 0; i < 25 && result == ROM_MISSING; ++i) {
    ++tentativas;
    result = smw_find_rom(folder, &rom, actual, &crc);
    if (result == ROM_MISSING) usleep(200000);
  }
  if (result == ROM_GOOD)
    smw_log("Jogo: %s (CRC32 %08lX), %d tentativa(s), %u ms", actual, (unsigned long)crc,
            tentativas, smw_log_ms() - busca);
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
  smw_log("%s", report);
  smw_load_report(report, sizeof(report));
  smw_log("%sNovos saves vao para: %s", report, smw_save_location());
  /* v0.16: com a primeira, a fonte das mensagens e a moeda do jogo
     (formato_usa_arte copia o que usa). */
  static SmwArteMenu arte_menus;
  tira_cena(0x105, cena_ajustes, &fundo_ajustes, &arte_menus);  /* Yoshi's Island 1 */
  tira_cena(0x0C7, cena_titulo, &fundo_titulo, NULL);    /* a fase do titulo */
  tira_cena(0x104, cena_apoio, &fundo_apoio, NULL);      /* casa do Yoshi */
  tira_cena(0x125, cena_vinheta, &fundo_vinheta, NULL);  /* ceu de noite: a vinheta da Retrogames (v0.15) */
  formato_usa_arte(&arte_menus);
  smw_log("Menus: %s", arte_menus.ok ? "fonte e moeda do jogo" : "fonte do PS2SDK (a cena nao carregou)");
  if (sound) {
    unsigned inicio = smw_log_ms();
    smw_som_menus_liga();
    som_do_jogo = true;
    smw_log("Menus: efeitos do jogo prontos (%u ms)", smw_log_ms() - inicio);
  }
  trapacas_carrega();

  /* Ajustes: o padrao, depois o SMW.CFG (Gravador no PC) e por cima o
     SMW.OPT (o que foi escolhido no PS2). Com tudo salvo, a tela AJUSTES
     comeca sozinha em 5 s. */
  SmwOptions cfg, salvo;
  smw_read_config(&cfg);
  smw_options_clear(&salvo);
  bool tem_opt = smw_read_saved_options(&salvo);
  texto_opcoes(&cfg, msg, sizeof(msg));
  smw_log("SMW.CFG (Gravador): %s", msg);
  texto_opcoes(&salvo, msg, sizeof(msg));
  smw_log("SMW.OPT (escolha no PS2): %s", tem_opt ? msg : "nao existe");
  opcoes = (SmwOptions){.tela = 0, .video = SMW_VIDEO_240P, .idioma = 0, .compat = 0, .pos_x = 0, .pos_y = 0};
  const SmwOptions *fontes[2] = {&cfg, &salvo};
  for (int i = 0; i < 2; ++i) {
    if (fontes[i]->tela >= 0) opcoes.tela = fontes[i]->tela;
    if (fontes[i]->video >= 0) opcoes.video = fontes[i]->video;
    if (fontes[i]->idioma >= 0) opcoes.idioma = fontes[i]->idioma;
    if (fontes[i]->compat >= 0) opcoes.compat = fontes[i]->compat;
    if (smw_posicao_valida(fontes[i]->pos_x)) opcoes.pos_x = fontes[i]->pos_x;
    if (smw_posicao_valida(fontes[i]->pos_y)) opcoes.pos_y = fontes[i]->pos_y;
  }
  muda_posicao(opcoes.pos_x, opcoes.pos_y);  /* o Pronto! e a tela AJUSTES ja na posicao salva */
  carregando("Pronto!", 4);
  const bool conhecido = salvo.video >= 0 && salvo.tela >= 0;
  const SmwOptions inicio = opcoes;
  const char *lugar = smw_save_location();
  char situacao[64], aviso_texto[64];
  if (strncmp(lugar, "NENHUM", 6) == 0) snprintf(situacao, sizeof(situacao), "Sem save: insira um Memory Card formatado.");
  else snprintf(situacao, sizeof(situacao), "Save: %s", lugar);
  const char *aviso = NULL;
  bool contagem = conhecido;
  for (;;) {
    bool sozinho = tela_ajustes(&opcoes, contagem, aviso, situacao);
    texto_opcoes(&opcoes, msg, sizeof(msg));
    smw_log("AJUSTES: %s (%s)", msg, sozinho ? "contagem de 5 s" : "o jogador escolheu");
    contagem = false;
    aviso = NULL;
    /* O modo que ja foi confirmado nesta TV entra direto; um modo novo
       pergunta se a imagem apareceu. Se 1280 colunas nao aparecerem, tenta
       a largura antiga (512, ou 640 no 480p) antes de voltar para os
       ajustes. */
    if (conhecido && opcoes.video == salvo.video) {
      smw_log("Video: %s ja confirmado nesta TV, entra direto", nome_video(opcoes.video));
      video_abre(opcoes.video, opcoes.compat == 1);
      break;
    }
    opcoes.compat = 0;
    /* Gravado antes de trocar: se a TV ficar preta e o console for
       desligado, o log mostra o que foi tentado. */
    smw_log("Video: tentando %s com %d pontos por linha", nome_video(opcoes.video), 1280);
    smw_log_grava();
    video_abre(opcoes.video, false);
    if (confirma_video(opcoes.idioma)) break;
    smw_log("Video: tentando %s com a largura antiga (%d pontos)", nome_video(opcoes.video),
            opcoes.video == SMW_VIDEO_480P ? 640 : 512);
    smw_log_grava();
    video_abre(opcoes.video, true);
    if (confirma_video(opcoes.idioma)) {
      opcoes.compat = 1;
      break;
    }
    smw_log("Video: a TV nao mostrou o %s; volta para os ajustes", nome_video(opcoes.video));
    snprintf(aviso_texto, sizeof(aviso_texto),
             opcoes.idioma == 1 ? "The TV did not show %s. Pick another." : "A TV não mostrou o %s. Escolha outro.",
             nome_video(opcoes.video));
    aviso = aviso_texto;
    video_abre(VIDEO_AJUSTES, false);
  }
  smw_log("Video em uso: %s com %d pontos por linha%s", nome_video(opcoes.video), gs->Width,
          opcoes.compat == 1 ? " (largura antiga: saida=512 no SMW.OPT)" : "");
  if (!conhecido || !mesmas_opcoes(&opcoes, &inicio))
    smw_log("SMW.OPT: %s", smw_save_options(&opcoes) ? "gravado" : "nao gravado (sem lugar para salvar)");
  else
    smw_log("SMW.OPT: sem mudanca");
  trapacas_grava();
  if (som_do_jogo) {  /* v0.16: o jogo comeca do motor novo, como sem os efeitos */
    smw_som_menus_desliga();
    som_do_jogo = false;
  }
  smw_log_grava();
  g_ptbr = opcoes.idioma != 1;

  smw_intro(gs, video.p480, sound, SMW_VERSAO, fundo_vinheta, fundo_titulo, fundo_apoio);
  set_aspect(tela_larga(&opcoes));
  const uint32_t field = measure_field();
  smw_log("Jogo comecou.");
  smw_log_fixa();
  /* The TV drives the game: 60 game frames a second, one drawn and one not
     (v0.15, ritmo.c). Each loop runs the frames not drawn, the drawn one,
     sends it to the GS; present() flips at the second TV frame after the
     last flip (or later, when the work took longer), so every image stays 2
     TV frames. A longer loop leaves frames behind and the next one runs one
     more, keeping the game, and so the sound, at the TV's pace. */
  Ritmo ritmo;
  ritmo_inicia(&ritmo);
  uint32_t last = cpu_ticks();
  int pending = 1;
  for (;;) {
    uint32_t logic = 0;
    for (int i = 0; i < pending; ++i) logic += step_game(false);
    const uint32_t draw = step_game(true);
    const uint32_t send = present();
    const uint32_t now = cpu_ticks();
    int fields = (int)((now - last + field / 2) / field);
    /* The first loop, or the first after a pause (last not taken at a
       flip), can measure less than the 2 frames the flips are apart. */
    if (fields < 2) fields = 2;
    last = now;
    if (perf_loop((unsigned)pending, logic, draw, send)) smw_log("Medidor: %s", perf.text);
    pending = ritmo_proxima(&ritmo, fields, pending + 1);
    if (log_after_loop) {
      /* Meter turned off: the write stops the game for a moment; like a
         save, it is not counted as late frames to catch up. */
      log_after_loop = false;
      smw_log_grava();
      last = cpu_ticks();
    }
    if (abre_trapacas) {  /* the game waits while the cheats are open */
      abre_trapacas = false;
      trapacas_no_jogo(opcoes.idioma);
      last = cpu_ticks();
    }
  }
}
