/* Telas da abertura: carregando, AJUSTES e o teste do modo de video.
 *
 * AJUSTES troca a antiga tela de texto do comeco: video (240p, 480i ou
 * 480p), tela (4:3 ou 16:9), suavizacao e idioma, com o jogo comecando
 * sozinho quando os ajustes ja estao salvos. Mesmo visual da tela de apoio
 * (intro.c): fundo azul com estrelas e moldura dourada dupla.
 *
 * O circulo do teste da tela so fica redondo quando a TV esta no modo certo
 * para a opcao marcada: no 16:9 ele e desenhado espremido, e a TV em modo
 * Wide (que estica a imagem 4:3 em 4/3) o deixa redondo de novo. */
#include "formato.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define W FORMATO_W
#define H FORMATO_H

static uint32_t *px;
static const unsigned char *fonte;

enum {
  DOURADO = 0xF2C14E, AZUL_CLARO = 0xA9C8FF, CINZA = 0x8890B0, TEXTO = 0xC8D0F0,
  BRANCO = 0xFFFFFF, CAIXA = 0x080C26, BORDA = 0x34407A, LARANJA = 0xFF9A4A
};

static void fill(int x, int y, int w, int h, uint32_t rgb) {
  for (int j = y; j < y + h; j++)
    if ((unsigned)j < H)
      for (int i = x; i < x + w; i++)
        if ((unsigned)i < W) px[j * W + i] = rgb;
}

/* Pinta por cima com transparencia (a = 0..256). */
static void mistura(int x, int y, int w, int h, uint32_t rgb, int a) {
  for (int j = y; j < y + h; j++)
    if ((unsigned)j < H)
      for (int i = x; i < x + w; i++) {
        if ((unsigned)i >= W) continue;
        uint32_t p = px[j * W + i], out = 0;
        for (int s = 0; s < 24; s += 8) {
          int c = (int)((p >> s) & 255), n = (int)((rgb >> s) & 255);
          out |= (uint32_t)(c + (n - c) * a / 256) << s;
        }
        px[j * W + i] = out;
      }
}

static void contorno(int x, int y, int w, int h, int b, uint32_t rgb) {
  fill(x, y, w, b, rgb), fill(x, y + h - b, w, b, rgb);
  fill(x, y, b, h, rgb), fill(x + w - b, y, b, h, rgb);
}

/* ---- texto: fonte 8x8 do PS2SDK, 6 pixels por letra ----
   Os acentos do portugues estao na metade de cima da fonte (MSX). */
static int glifo(const char **p) {
  static const unsigned char tabela[][2] = {
      {0xA1, 0xA0}, {0xA0, 0x85}, {0xA2, 0x83}, {0xA3, 0xB1}, {0xA7, 0x87}, {0xA9, 0x82},
      {0xAA, 0x88}, {0xAD, 0xA1}, {0xB3, 0xA2}, {0xB4, 0x93}, {0xB5, 0xB5}, {0xBA, 0xA3},
      {0xBC, 0x81}, {0x87, 0x80}, {0x89, 0x90}, {0x83, 0xB0}, {0x95, 0xB4}};
  const unsigned char *s = (const unsigned char *)*p;
  int c = *s++;
  if (c == 0xC3 && *s) {
    int u = *s++;
    c = '?';
    for (size_t i = 0; i < sizeof(tabela) / sizeof(tabela[0]); i++)
      if (tabela[i][0] == u) c = tabela[i][1];
  }
  *p = (const char *)s;
  return c;
}

static int largura(const char *s, int escala) {
  int n = 0;
  while (*s) glifo(&s), n++;
  return n ? n * 6 * escala - escala : 0;
}

static void texto(int x, int y, const char *s, uint32_t rgb, int escala) {
  while (*s) {
    const unsigned char *g = fonte + glifo(&s) * 8;
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 6; c++)
        if (g[r] & (0x80 >> c)) fill(x + c * escala, y + r * escala, escala, escala, rgb);
    x += 6 * escala;
  }
}

/* Centrado na faixa [x0, x0 + w). */
static void texto_em(int x0, int w, int y, const char *s, uint32_t rgb) {
  texto(x0 + (w - largura(s, 1)) / 2, y, s, rgb, 1);
}

static void texto_sombra(int y, const char *s, uint32_t rgb, int escala) {
  int x = (W - largura(s, escala)) / 2;
  texto(x + 1, y + 1, s, 0x000001, escala);
  texto(x, y, s, rgb, escala);
}

/* ---- fundo: o mesmo da tela de apoio ---- */
static unsigned semente;
static float sorteio(void) {
  semente = semente * 1103515245u + 12345u;
  return (float)((semente >> 8) & 0xFFFF) / 65535.0f;
}

static void fundo(int frame) {
  for (int y = 0; y < H; y++) {
    uint32_t c = (uint32_t)(10 + y * 14 / H) << 16 | (uint32_t)(12 + y * 6 / H) << 8 | (uint32_t)(40 + y * 30 / H);
    for (int x = 0; x < W; x++) px[y * W + x] = c;
  }
  semente = 777;
  for (int i = 0; i < 60; i++) {
    int sx = (int)(sorteio() * W), sy = (int)(sorteio() * H), tw = (int)(sorteio() * 60);
    int k = 40 + (int)(40 * (1 + sinf((frame + tw * 7) * 0.05f)));
    mistura((sx + frame / 8) % W, sy, 1, 1, (uint32_t)k << 16 | (uint32_t)k << 8 | (uint32_t)(k + 20), 200);
  }
  contorno(10, 8, W - 20, H - 16, 2, 0xC8962E);
  contorno(15, 13, W - 30, H - 26, 1, 0x7A5A1C);
}

/* ---- desenhos ---- */
static void morro(int cx, int base, int meia, int alto, int x0, int x1, uint32_t cor) {
  for (int i = 0; i < alto; i++) {
    int w = meia * (i + 1) / alto, a = cx - w, b = cx + w + 1;
    if (a < x0) a = x0;
    if (b > x1) b = x1;
    if (b > a) fill(a, base - alto + i, b - a, 1, cor);
  }
}

/* TV pequena com uma paisagem simples. larga: tela 16:9, com as faixas a
   mais (43 colunas de cada lado) marcadas em dourado. */
static void tv(int x, int y, bool larga) {
  int sw = larga ? 44 : 33, sh = 24, tw = sw + 6, th = sh + 6;
  int sx = x + 3, sy = y + 3, base = sy + sh - 6;
  fill(x, y, tw, th, 0x5A6070);
  fill(x + 1, y + 1, tw - 2, th - 2, 0x2B2F3A);
  fill(sx, sy, sw, sh, 0x4A7FE0);
  fill(sx + sw / 4 - 4, sy + 5, 9, 2, BRANCO), fill(sx + sw / 4 - 2, sy + 4, 5, 1, BRANCO);
  morro(sx + sw / 4, base, 7, 8, sx, sx + sw, 0x2F8F4E);
  morro(sx + sw * 2 / 3, base, 9, 10, sx, sx + sw, 0x3AA35A);
  fill(sx, base, sw, 2, 0x5CC04A);
  fill(sx, base + 2, sw, sy + sh - base - 2, 0x8A5A2B);
  if (larga) {
    int faixa = sw * 43 / 342 + 1;
    mistura(sx, sy, faixa, sh, DOURADO, 90);
    mistura(sx + sw - faixa, sy, faixa, sh, DOURADO, 90);
  }
  fill(x + tw / 2 - 5, y + th, 10, 2, 0x2B2F3A);
}

/* Contorno de elipse com 2 pixels de espessura. */
static void elipse(int cx, int cy, float rx, float ry, uint32_t cor) {
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 720; i++) {
      float a = (float)i * 3.14159265f / 360.0f;
      fill(cx + (int)lroundf((rx - k) * cosf(a)), cy + (int)lroundf((ry - k) * sinf(a)), 1, 1, cor);
    }
}

/* Triangulo apontando para a esquerda (dir < 0) ou para a direita. */
static void seta(int x, int cy, int dir, uint32_t cor) {
  for (int i = 0; i < 4; i++) {
    int h = 2 * i + 1, cx = dir < 0 ? x + i : x + 3 - i;
    fill(cx, cy - i, 1, h, cor);
  }
}

/* ---- AJUSTES ---- */

typedef struct {
  const char *titulo, *jogar, *rotulo[4], *suave[3], *idioma[2], *rodape, *contagem, *video_txt[3][2],
      *tela_txt[2], *redondo[2], *suave_txt[3], *suave_bom, *suave_ve, *idioma_txt[2], *idioma_tela, *jogar_txt[3],
      *aplica;
} Textos;

static const Textos kTextos[2] = {
  {
    "AJUSTES", "JOGAR", {"Vídeo", "Tela", "Suavização", "Idioma"},
    {"Desligada", "Média", "Forte"}, {"Português", "English"},
    "Direcional: escolhe e muda  -  X: joga", "Começa em %d s. Aperte um botão para mudar.",
    {{"240p: cabo AV, em qualquer TV.", "O mais nítido, igual ao SNES."},
     {"480p: cabo componente ou adaptador HDMI.", "No cabo AV não aparece; volta sozinho."},
     {"480i: se a TV não mostrar o 240p.", "Em geral fica menos nítido que o 240p."}},
    {"4:3: TV de tubo ou em 4:3.", "16:9: mostra mais da fase."},
    {"O círculo fica redondo", "com a TV no modo certo."},
    {"Pixels nítidos, como no cartucho.", "Pixels um pouco mais macios.", "Pixels macios, sem serrilhado."},
    "No cabo AV a TV já deixa macio.", "Esta tela já mostra o efeito.",
    {"Textos do jogo em português.", "Textos do jogo em inglês (original)."}, "Esta tela também muda.",
    {"X ou START começa. Fica tudo salvo.", "No jogo, L2+R2+X troca a tela e",
     "L2+R2+Círculo troca a suavização."},
    "Muda quando você apertar X.",
  },
  {
    "SETTINGS", "PLAY", {"Video", "Screen", "Smoothing", "Language"},
    {"Off", "Medium", "Strong"}, {"Português", "English"},
    "D-pad: choose and change  -  X: play", "Starts in %d s. Press a button to change.",
    {{"240p: AV cable, any TV.", "The sharpest, like the SNES."},
     {"480p: component cable or HDMI adapter.", "Not on the AV cable; it comes back."},
     {"480i: if the TV does not show 240p.", "Usually softer than 240p."}},
    {"4:3: CRT or TV set to 4:3.", "16:9: more of the level."},
    {"The circle is round", "in the right TV mode."},
    {"Sharp pixels, like the cartridge.", "Slightly softer pixels.", "Soft pixels, no jagged edges."},
    "Over AV the TV already softens it.", "This screen already shows it.",
    {"Game texts in Portuguese.", "Game texts in English (original)."}, "This screen changes too.",
    {"X or START begins. Everything is saved.", "In game, L2+R2+X changes the screen",
     "and L2+R2+CIRCLE the smoothing."},
    "Changes when you press X.",
  },
};

enum { LX = 28, LW = W - 56, LINHA_Y = 42, LINHA_H = 16, LINHA_PASSO = 18, VALOR_X = 152 };

static const char *valor(const Textos *t, const Ajustes *a, int linha) {
  static const char *const videos[] = {"240p", "480p", "480i"};
  switch (linha) {
  case AJUSTE_VIDEO: return videos[a->video];
  case AJUSTE_TELA: return a->tela ? "16:9" : "4:3";
  case AJUSTE_SUAVE: return t->suave[a->suave];
  default: return t->idioma[a->idioma];
  }
}

static void linha_ajuste(const Textos *t, const Ajustes *a, int linha, int frame) {
  int y = LINHA_Y + linha * LINHA_PASSO;
  bool marcada = a->linha == linha;
  fill(LX, y, LW, LINHA_H, CAIXA);
  contorno(LX, y, LW, LINHA_H, marcada ? 2 : 1, marcada ? DOURADO : BORDA);
  texto(LX + 9, y + 4, t->rotulo[linha], marcada ? DOURADO : AZUL_CLARO, 1);
  int fim = LX + LW - 8;
  texto_em(VALOR_X, fim - VALOR_X, y + 4, valor(t, a, linha), marcada ? BRANCO : TEXTO);
  uint32_t cor = marcada ? DOURADO : BORDA;
  int pulo = marcada && (frame / 15) % 2 ? 1 : 0;
  seta(VALOR_X + 2 - pulo, y + LINHA_H / 2, -1, cor);
  seta(fim - 6 + pulo, y + LINHA_H / 2, 1, cor);
}

static void botao_jogar(const Textos *t, const Ajustes *a, int frame) {
  int w = 96, x = (W - w) / 2, y = LINHA_Y + 4 * LINHA_PASSO + 4;
  bool marcado = a->linha == AJUSTE_JOGAR;
  fill(x, y, w, LINHA_H, marcado ? 0x3A2A08 : CAIXA);
  contorno(x, y, w, LINHA_H, marcado ? 2 : 1, marcado ? DOURADO : BORDA);
  texto_em(x, w, y + 4, t->jogar, marcado ? DOURADO : AZUL_CLARO);
  if (marcado && (frame / 20) % 2 == 0) seta(x - 9, y + LINHA_H / 2, 1, DOURADO), seta(x + w + 5, y + LINHA_H / 2, -1, DOURADO);
}

/* Painel de ajuda da linha marcada. */
static void painel(const Textos *t, const Ajustes *a) {
  const int x = LX, y = 140, w = LW, h = 42;
  fill(x, y, w, h, CAIXA);
  contorno(x, y, w, h, 1, BORDA);
  int tx = x + 8, l1 = y + 6, l2 = y + 17, l3 = y + 28;
  switch (a->linha) {
  case AJUSTE_VIDEO:
    texto(tx, l1, t->video_txt[a->video][0], BRANCO, 1);
    texto(tx, l2, t->video_txt[a->video][1], TEXTO, 1);
    texto(tx, l3, t->aplica, CINZA, 1);
    break;
  case AJUSTE_TELA: {
    tv(x + 6, y + 6, a->tela);
    /* Pixels do quadro: 320 colunas na largura de uma TV 4:3 e 224 linhas
       na altura, entao cada pixel e 0,93 vez mais estreito do que alto. Na
       TV em modo 16:9 a imagem e esticada 4/3 e ele passa a 1,24. */
    const float ry = 13.0f, rx = ry / (a->tela ? 0.9333f * 4.0f / 3.0f : 0.9333f);
    int cx = x + 76, cy = y + h / 2;
    elipse(cx, cy, rx, ry, DOURADO);
    fill(cx - 1, cy - 1, 3, 3, DOURADO);
    texto(x + 98, l1, t->tela_txt[a->tela], BRANCO, 1);
    texto(x + 98, l2, t->redondo[0], TEXTO, 1);
    texto(x + 98, l3, t->redondo[1], TEXTO, 1);
    break;
  }
  case AJUSTE_SUAVE:
    texto(tx, l1, t->suave_txt[a->suave], BRANCO, 1);
    texto(tx, l2, t->suave_bom, TEXTO, 1);
    texto(tx, l3, t->suave_ve, CINZA, 1);
    break;
  case AJUSTE_IDIOMA:
    texto(tx, l1, t->idioma_txt[a->idioma], BRANCO, 1);
    texto(tx, l2, t->idioma_tela, TEXTO, 1);
    break;
  default:
    texto(tx, l1, t->jogar_txt[0], BRANCO, 1);
    texto(tx, l2, t->jogar_txt[1], TEXTO, 1);
    texto(tx, l3, t->jogar_txt[2], TEXTO, 1);
    break;
  }
}

void formato_ajustes(uint32_t *destino, const unsigned char *letras, const Ajustes *a, int frame) {
  px = destino;
  fonte = letras;
  const Textos *t = &kTextos[a->idioma == 1];
  fundo(frame);
  texto_sombra(17, t->titulo, DOURADO, 2);
  fill(W / 2 - 80, 35, 160, 1, 0xC8962E);
  for (int l = 0; l < 4; l++) linha_ajuste(t, a, l, frame);
  botao_jogar(t, a, frame);
  painel(t, a);
  texto_em(0, W, 188, t->rodape, BRANCO);
  char linha[64];
  if (a->aviso) {
    texto_em(0, W, 199, a->aviso, LARANJA);
  } else if (a->segundos >= 0) {
    snprintf(linha, sizeof(linha), t->contagem, a->segundos);
    texto_em(0, W, 199, linha, DOURADO);
  } else if (a->situacao) {
    texto_em(0, W, 199, a->situacao, CINZA);
  }
  if (a->segundos >= 0) {
    snprintf(linha, sizeof(linha), "%d", a->segundos % 100);
    texto(W - 24 - largura(linha, 1), 20, linha, CINZA, 1);
  }
}

/* ---- carregando ---- */

void formato_carregando(uint32_t *destino, const unsigned char *letras, const char *etapa, int passo,
                        int passos, int frame) {
  px = destino;
  fonte = letras;
  fundo(frame);
  texto_sombra(64, "SUPER MARIO WORLD", DOURADO, 2);
  fill(W / 2 - 100, 83, 200, 1, 0xC8962E);
  texto_em(0, W, 90, "Port nativo para PlayStation 2", TEXTO);
  const int bx = 70, by = 124, bw = W - 140, bh = 6;
  fill(bx - 1, by - 1, bw + 2, bh + 2, BORDA);
  fill(bx, by, bw, bh, CAIXA);
  if (passos > 0) fill(bx, by, bw * passo / passos, bh, DOURADO);
  if (etapa) texto_em(0, W, by + 14, etapa, BRANCO);
  texto_em(0, W, 196, "Retrogames Patos de Minas", CINZA);
}

/* ---- teste do modo novo ---- */

void formato_confirma(uint32_t *destino, const unsigned char *letras, int video, int idioma, int segundos,
                      int frame) {
  static const char *const videos[] = {"240p", "480p", "480i"};
  px = destino;
  fonte = letras;
  fundo(frame);
  char linha[64];
  if (idioma == 1) {
    texto_sombra(46, "CAN YOU SEE THIS?", DOURADO, 2);
    snprintf(linha, sizeof(linha), "Press X to keep %s.", videos[video]);
    texto_em(0, W, 80, linha, BRANCO);
    texto_em(0, W, 92, "No answer: back to the settings.", TEXTO);
  } else {
    texto_sombra(46, "A IMAGEM APARECEU?", DOURADO, 2);
    snprintf(linha, sizeof(linha), "Aperte X para ficar com o %s.", videos[video]);
    texto_em(0, W, 80, linha, BRANCO);
    texto_em(0, W, 92, "Sem resposta, volta para os ajustes.", TEXTO);
  }
  fill(W / 2 - 80, 65, 160, 1, 0xC8962E);
  snprintf(linha, sizeof(linha), "%d", segundos < 0 ? 0 : segundos % 100);
  int lw = largura(linha, 5);
  texto((W - lw) / 2 + 1, 116, linha, 0x000001, 5);
  texto((W - lw) / 2, 115, linha, DOURADO, 5);
}
