/* Tela FORMATO DA TELA: aparece uma vez, entre a vinheta e o "Apoie o
 * projeto", para o jogador escolher 4:3 ou 16:9. A escolha fica gravada
 * (SMW.OPT) e depois so muda com L2+R2+X no jogo.
 *
 * Mesmo visual da tela de apoio (intro.c): fundo azul com estrelas e
 * moldura dourada dupla. O circulo do TESTE DA TV so fica redondo quando a
 * TV esta no modo certo para a opcao marcada: no 16:9 ele e desenhado
 * espremido, e a TV em modo Wide (que estica a imagem 4:3 em 4/3) o deixa
 * redondo de novo. */
#include "formato.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define W FORMATO_W
#define H FORMATO_H

static uint32_t *px;
static const unsigned char *fonte;

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

/* TV com uma paisagem simples. larga: tela 16:9, com as faixas a mais
   (43 colunas de cada lado) marcadas em dourado. */
static void tv(int x, int y, bool larga) {
  int sw = larga ? 56 : 42, sh = 31, tw = sw + 8, th = sh + 8;
  int sx = x + 4, sy = y + 4, base = sy + sh - 7;
  fill(x, y, tw, th, 0x5A6070);
  fill(x + 1, y + 1, tw - 2, th - 2, 0x2B2F3A);
  fill(sx, sy, sw, sh, 0x4A7FE0);
  fill(sx + sw / 4 - 5, sy + 7, 11, 3, 0xFFFFFF), fill(sx + sw / 4 - 2, sy + 5, 5, 2, 0xFFFFFF);
  fill(sx + sw * 3 / 4 - 4, sy + 10, 8, 2, 0xFFFFFF);
  morro(sx + sw / 4, base, 9, 10, sx, sx + sw, 0x2F8F4E);
  morro(sx + sw * 2 / 3, base, 12, 13, sx, sx + sw, 0x3AA35A);
  if (larga) {
    morro(sx + 4, base, 6, 7, sx, sx + sw, 0x2F8F4E);
    morro(sx + sw - 5, base, 7, 8, sx, sx + sw, 0x2F8F4E);
  }
  fill(sx, base, sw, 2, 0x5CC04A);
  fill(sx, base + 2, sw, sy + sh - base - 2, 0x8A5A2B);
  if (larga) {
    int faixa = sw * 43 / 342 + 1;
    mistura(sx, sy, faixa, sh, 0xF2C14E, 90);
    mistura(sx + sw - faixa, sy, faixa, sh, 0xF2C14E, 90);
    for (int j = 0; j < sh; j += 3) fill(sx + faixa, sy + j, 1, 2, 0xF2C14E), fill(sx + sw - faixa - 1, sy + j, 1, 2, 0xF2C14E);
  }
  fill(x + tw / 2 - 6, y + th, 12, 3, 0x2B2F3A);
}

static void seta(int x, int cy) {
  for (int i = 0; i < 5; i++) fill(x, cy - 4 + i, i + 1, 1, 0xF2C14E), fill(x, cy + 4 - i, i + 1, 1, 0xF2C14E);
}

enum { CARTAO_W = 124, CARTAO_H = 86 };

static void cartao(int x, int y, bool marcado, bool larga, int frame) {
  fill(x, y, CARTAO_W, CARTAO_H, 0x080C26);
  contorno(x, y, CARTAO_W, CARTAO_H, marcado ? 2 : 1, marcado ? 0xF2C14E : 0x34407A);
  tv(x + (CARTAO_W - (larga ? 64 : 50)) / 2, y + 6, larga);
  texto_em(x, CARTAO_W, y + 53, larga ? "16:9 TELA LARGA" : "4:3 NORMAL", marcado ? 0xF2C14E : 0xA9C8FF);
  texto_em(x, CARTAO_W, y + 65, larga ? "Mostra mais da fase." : "TV de tubo ou", 0x9AA3C8);
  texto_em(x, CARTAO_W, y + 75, larga ? "TV no modo 16:9" : "TV no modo 4:3", 0x9AA3C8);
  if (marcado && (frame / 20) % 2 == 0) seta(x - 8, y + CARTAO_H / 2);
}

/* Contorno de elipse com 2 pixels de espessura. */
static void elipse(int cx, int cy, float rx, float ry, uint32_t cor) {
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 720; i++) {
      float a = (float)i * 3.14159265f / 360.0f;
      fill(cx + (int)lroundf((rx - k) * cosf(a)), cy + (int)lroundf((ry - k) * sinf(a)), 1, 1, cor);
    }
}

/* Pixels do quadro: 320 colunas na largura de uma TV 4:3 e 224 linhas na
   altura, entao cada pixel e 0,93 vez mais estreito do que alto. Na TV em
   modo 16:9 a imagem e esticada 4/3 e ele passa a 1,24. */
static void teste_tv(int sel) {
  const int x = 30, y = 148, w = W - 60, h = 34;
  fill(x, y, w, h, 0x080C26);
  contorno(x, y, w, h, 1, 0x34407A);
  const float ry = 13.0f, rx = ry / (sel ? 0.9333f * 4.0f / 3.0f : 0.9333f);
  int cx = x + 22, cy = y + h / 2;
  elipse(cx, cy, rx, ry, 0xF2C14E);
  fill(cx - 1, cy - 1, 3, 3, 0xF2C14E);
  texto(x + 44, y + 4, "TESTE DA TV", 0xF2C14E, 1);
  texto(x + 44, y + 14, sel ? "Fica redondo com a TV em 16:9." : "Fica redondo com a TV em 4:3.", 0xFFFFFF, 1);
  texto(x + 44, y + 24, sel ? "Se ficar fino, mude o modo da TV." : "Se ficar largo, a TV está em 16:9.",
        0xC8D0F0, 1);
}

void formato_desenha(uint32_t *destino, const unsigned char *letras, int sel, int frame, int segundos) {
  px = destino;
  fonte = letras;
  fundo(frame);
  texto_sombra(18, "FORMATO DA TELA", 0xF2C14E, 2);
  fill(W / 2 - 95, 37, 190, 1, 0xC8962E);
  texto_em(0, W, 42, "Como está a sua TV?", 0xC8D0F0);
  cartao(30, 56, sel == 0, false, frame);
  cartao(W - 30 - CARTAO_W, 56, sel == 1, true, frame);
  teste_tv(sel);
  texto_em(0, W, 188, "Direcional escolhe  -  X confirma", 0xFFFFFF);
  texto_em(0, W, 199, "Fica salvo. No jogo: L2+R2+X troca.", 0x8890B0);
  if (segundos >= 0) {
    char s[16];
    snprintf(s, sizeof(s), "%d", segundos % 100);
    texto(W - 22 - largura(s, 1), 20, s, 0x8890B0, 1);
  }
}
