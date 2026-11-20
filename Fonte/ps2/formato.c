/* Telas antes do jogo: carregando, AJUSTES, o teste do modo de video, a
 * lista de trapacas e, na abertura (intro.c), a vinheta da Retrogames, o
 * titulo e a tela de apoio; e a transicao em circulo entre elas (v0.15).
 *
 * v0.14: quadro de 256 x 224, a resolucao do SNES. O fundo e uma cena do
 * proprio jogo, tirada da ROM do jogador na hora (smw_capture_scene); por
 * cima, caixas pretas de borda dupla, como as caixas de mensagem do jogo.
 * A tela "carregando" aparece antes de a ROM ser achada: nela o fundo e uma
 * paisagem desenhada aqui (ceu, morros e chao), sem nada copiado do jogo.
 *
 * O circulo do teste da tela so fica redondo quando a TV esta no modo certo
 * para a opcao marcada: no 16:9 ele e desenhado espremido, e a TV em modo
 * Wide (que estica a imagem 4:3 em 4/3) o deixa redondo de novo. */
#include "formato.h"
#include "files.h"  /* smw_16_9_vale */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define W FORMATO_W
#define H FORMATO_H

static uint32_t *px;
static const unsigned char *fonte;

enum {
  OURO = 0xF8D030, OURO_SOMBRA = 0xA04000, BRANCO = 0xFFFFFF, CINZA = 0xB4B4C0, AZUL = 0x9FD0FF,
  LARANJA = 0xFF9A4A, MARCA = 0x1E2F78, PAINEL = 0x101A40, PAINEL_BORDA = 0x3A4A8A, PRETO = 0x000001
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

/* Centrado na tela, com sombra embaixo a direita. */
static void texto_sombra(int y, const char *s, uint32_t rgb, uint32_t sombra, int escala) {
  int x = (W - largura(s, escala)) / 2;
  texto(x + escala, y + escala, s, sombra, escala);
  texto(x, y, s, rgb, escala);
}

/* Centrado na tela, com contorno preto: para ler por cima do cenario. */
static void texto_contorno(int y, const char *s, uint32_t rgb, int escala) {
  int x = (W - largura(s, escala)) / 2;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (dx || dy) texto(x + dx, y + dy, s, PRETO, escala);
  texto(x, y, s, rgb, escala);
}

/* ---- fundo ---- */

static void elipse_cheia(int cx, int cy, int rx, int ry, uint32_t cor, int y_max) {
  for (int y = cy - ry; y <= cy + ry && y < y_max; y++) {
    float t = (float)(y - cy) / (float)ry;
    int w = (int)(rx * sqrtf(1.0f - t * t) + 0.5f);
    fill(cx - w, y, 2 * w + 1, 1, cor);
  }
}

/* Morro arredondado com contorno de 1 pixel (desenho nosso, sem olhos). */
static void morro(int cx, int base, int rx, int alto, uint32_t cor) {
  elipse_cheia(cx, base, rx + 1, alto + 1, 0x1D5C22, base);
  elipse_cheia(cx, base, rx, alto, cor, base);
}

static void nuvem(int x, int y, int w) {
  fill(x + 4, y + 7, w - 8, 3, 0xC9DEFC);
  fill(x + 2, y + 2, w - 4, 6, BRANCO);
  fill(x + w / 4, y, w / 2, 3, BRANCO);
}

/* Paisagem desenhada aqui (antes de a ROM ser achada): ceu em faixas,
   nuvens, morros e chao com grama. */
static void paisagem(void) {
  static const uint32_t faixas[5] = {0x4F86F7, 0x6196F8, 0x78A7FA, 0x93BAFB, 0xB0CFFC};
  for (int i = 0; i < 5; i++) fill(0, i * 36, W, 36, faixas[i]);
  fill(0, 180, W, H - 180, faixas[4]);
  nuvem(22, 22, 44);
  nuvem(176, 40, 52);
  morro(40, 190, 70, 66, 0x5CBF5F);
  morro(212, 190, 64, 54, 0x46A84E);
  morro(128, 190, 46, 34, 0x6FD06C);
  fill(0, 188, W, 2, 0x1D5C22);
  fill(0, 190, W, 4, 0x4CC94A);
  fill(0, 194, W, 1, 0x1D5C22);
  fill(0, 195, W, H - 195, 0xC97A3C);
  for (int y = 199; y < H; y += 7)
    for (int x = (y / 7) % 2 * 6; x < W; x += 12) fill(x, y, 2, 2, 0x8F4F1F);
}

/* A cena do jogo (ou a paisagem), escurecida em escuro/256. */
static void fundo(const uint32_t *cena, int escuro) {
  if (cena) memcpy(px, cena, sizeof(uint32_t) * W * H);
  else paisagem();
  if (escuro > 0) mistura(0, 0, W, H, 0x000000, escuro);
}

/* Caixa preta de borda dupla, como as caixas de mensagem do jogo; um pouco
   transparente, para o cenario aparecer atras (os textos ficam em paineis
   ou com cores fortes, para ler bem no cabo AV). */
static void caixa_a(int x, int y, int w, int h, int opaca) {
  mistura(x, y, w, h, 0x000000, opaca);
  contorno(x, y, w, h, 2, 0xF0F0F0);
  contorno(x + 3, y + 3, w - 6, h - 6, 1, 0x808080);
}

static void caixa(int x, int y, int w, int h) { caixa_a(x, y, w, h, 215); }

/* Faixa escura embaixo, para os comandos e avisos (duas linhas). */
static void rodape(const char *l1, uint32_t c1, const char *l2, uint32_t c2) {
  mistura(0, 196, W, H - 196, 0x000000, 210);
  if (l1) texto_em(0, W, 199, l1, c1);
  if (l2) texto_em(0, W, 211, l2, c2);
}

/* Triangulo apontando para a esquerda (dir < 0) ou para a direita. */
static void seta(int x, int cy, int dir, uint32_t cor) {
  for (int i = 0; i < 4; i++) {
    int h = 2 * i + 1, cx = dir < 0 ? x + i : x + 3 - i;
    fill(cx, cy - i, 1, h, cor);
  }
}

/* Triangulo apontando para cima (dir < 0) ou para baixo. */
static void seta_v(int cx, int y, int dir, uint32_t cor) {
  for (int i = 0; i < 4; i++) {
    int w = 2 * i + 1, cy = dir < 0 ? y + i : y + 3 - i;
    fill(cx - i, cy, w, 1, cor);
  }
}

/* Contorno de elipse com 2 pixels de espessura. */
static void elipse(int cx, int cy, float rx, float ry, uint32_t cor) {
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 720; i++) {
      float a = (float)i * 3.14159265f / 360.0f;
      fill(cx + (int)lroundf((rx - k) * cosf(a)), cy + (int)lroundf((ry - k) * sinf(a)), 1, 1, cor);
    }
}

/* ---- AJUSTES ---- */

typedef struct {
  const char *titulo, *jogar, *rotulo[AJUSTE_JOGAR], *idioma[2], *rodape, *contagem, *video_txt[2][2],
      *aplica, *tela_txt[2], *redondo[2], *so_480p[2], *idioma_txt[2], *idioma_tela, *trapaca_txt[4], *nenhuma,
      *ligadas, *sem_arquivo, *posicao_txt[3], *ajustando_txt[3], *rodape_ajuste, *jogar_txt[3], *centro;
} Textos;

/* Cada linha do painel cabe em 34 letras (6 pixels cada). */
static const Textos kTextos[2] = {
  {
    "AJUSTES", "JOGAR", {"Vídeo", "Tela", "Posição", "Idioma", "Trapaças"}, {"Português", "English"},
    "Direcional: escolhe e muda  X: joga", "Começa em %d s. Um botão muda.",
    {{"240p: cabo AV, em qualquer TV.", "O mais nítido, igual ao SNES."},
     {"480p: cabo componente ou HDMI.", "No cabo AV não aparece; volta."}},
    "Muda quando você apertar X.",
    {"4:3: TV de tubo ou em 4:3.", "16:9: mostra mais da fase."},
    {"O círculo fica redondo", "com a TV no modo certo."},
    {"O 16:9 é só no 480p", "(componente ou HDMI)."},
    {"Textos do jogo em português.", "Textos em inglês (o original)."}, "Esta tela também muda.",
    {"Códigos do arquivo TRAPACAS.TXT", "(ao lado do save). X abre a lista.",
     "No jogo: L2+R2+SELECT.", "Ligadas, mudam o jogo e o save."},
    "nenhuma", "%d ligada%s", "sem arquivo",
    {"Centraliza a imagem na TV.", "Aperte X e use as setas.", "No jogo: L2+R2 e as setas."},
    {"Setas: movem a imagem.", "Centralize a moldura na TV.", "X: pronto."},
    "Setas: posição  X: pronto",
    {"X ou START começa. Fica salvo.", "No jogo: L2+R2+X troca 4:3/16:9",
     "(480p); L2+R2+setas, a posição."},
    "centro",
  },
  {
    "SETTINGS", "PLAY", {"Video", "Screen", "Position", "Language", "Cheats"}, {"Português", "English"},
    "D-pad: choose and change  X: play", "Starts in %d s. Any button stops.",
    {{"240p: AV cable, any TV.", "The sharpest, like the SNES."},
     {"480p: component or HDMI.", "Not on the AV cable; it returns."}},
    "Changes when you press X.",
    {"4:3: CRT or TV set to 4:3.", "16:9: more of the level."},
    {"The circle is round", "in the right TV mode."},
    {"16:9 only in 480p", "(component or HDMI)."},
    {"Game texts in Portuguese.", "Game texts in English (original)."}, "This screen changes too.",
    {"Codes from the TRAPACAS.TXT file", "(next to the save). X opens it.",
     "In game: L2+R2+SELECT.", "When on, they change the save."},
    "none", "%d on%s", "no file",
    {"Centers the picture on the TV.", "Press X and use the D-pad.", "In game: L2+R2 and the D-pad."},
    {"D-pad: moves the picture.", "Center the frame on the TV.", "X: done."},
    "D-pad: position  X: done",
    {"X or START begins. It is saved.", "In game: L2+R2+X 4:3/16:9 (480p);",
     "L2+R2+D-pad: the position."},
    "center",
  },
};

/* Caixa, cinco linhas de ajuste (14 de altura a cada 15), o botao JOGAR e
   o painel de ajuda. */
enum { BX = 10, BY = 10, BW = W - 20, BH = 180, LX = BX + 10, LW = BW - 20, LINHA_Y = 41,
       LINHA_H = 14, LINHA_PASSO = 15, VALOR_X = LX + 100,
       JOGAR_Y = LINHA_Y + AJUSTE_JOGAR * LINHA_PASSO + 4, PAINEL_Y = JOGAR_Y + LINHA_H + 6,
       PAINEL_H = 46 };

/* O 16:9 so vale no 480p (v0.14.1): nos outros videos a linha Tela mostra
   4:3, apagada, e a escolha fica guardada. */
static bool tela_fixa(const Ajustes *a) { return !smw_16_9_vale(a->video); }

/* Indice das tabelas de video (240p, 480p): qualquer outro valor vira 240p. */
static int video_i(int video) { return video == SMW_VIDEO_480P ? 1 : 0; }

static const char *valor(const Textos *t, const Ajustes *a, int linha, char *buf, size_t n) {
  static const char *const videos[] = {"240p", "480p"};
  switch (linha) {
  case AJUSTE_VIDEO: return videos[video_i(a->video)];
  case AJUSTE_TELA: return a->tela && !tela_fixa(a) ? "16:9" : "4:3";
  case AJUSTE_POSICAO:
    if (!a->pos_x && !a->pos_y) return t->centro;
    snprintf(buf, n, "%+d, %+d", a->pos_x, a->pos_y);
    return buf;
  case AJUSTE_IDIOMA: return t->idioma[a->idioma];
  default:
    if (a->trapacas_total <= 0) return t->sem_arquivo;
    if (a->trapacas <= 0) return t->nenhuma;
    snprintf(buf, n, t->ligadas, a->trapacas, a->idioma == 1 || a->trapacas == 1 ? "" : "s");
    return buf;
  }
}

static void linha_ajuste(const Textos *t, const Ajustes *a, int linha, int frame) {
  char buf[24];
  int y = LINHA_Y + linha * LINHA_PASSO;
  bool marcada = a->linha == linha;
  if (marcada) {
    fill(LX, y, LW, LINHA_H, MARCA);
    contorno(LX, y, LW, LINHA_H, 1, OURO);
  }
  texto(LX + 6, y + 3, t->rotulo[linha], marcada ? OURO : BRANCO, 1);
  int fim = LX + LW - 8;
  const char *v = valor(t, a, linha, buf, sizeof(buf));
  bool sem = (linha == AJUSTE_TRAPACAS && a->trapacas_total <= 0) || (linha == AJUSTE_TELA && tela_fixa(a));
  texto_em(VALOR_X, fim - VALOR_X, y + 3, v, sem ? CINZA : marcada ? BRANCO : AZUL);
  uint32_t cor = marcada ? OURO : 0x5A6AAA;
  int pulo = marcada && (frame / 15) % 2 ? 1 : 0;
  if (linha == AJUSTE_TELA && tela_fixa(a)) return;  /* sem setas: so 4:3 */
  /* Posicao e trapacas: so a seta da direita (X ou as setas entram). */
  if (linha != AJUSTE_TRAPACAS && linha != AJUSTE_POSICAO) seta(VALOR_X + 2 - pulo, y + LINHA_H / 2, -1, cor);
  seta(fim - 6 + pulo, y + LINHA_H / 2, 1, cor);  /* trapacas: sem arquivo, X abre a explicacao */
}

static void botao_jogar(const Textos *t, const Ajustes *a, int frame) {
  int w = 72, x = (W - w) / 2, y = JOGAR_Y;
  bool marcado = a->linha == AJUSTE_JOGAR;
  fill(x, y, w, LINHA_H, marcado ? OURO : 0x000000);
  contorno(x, y, w, LINHA_H, 1, marcado ? BRANCO : 0x808080);
  texto_em(x, w, y + 3, t->jogar, marcado ? PRETO : BRANCO);
  if (marcado && (frame / 20) % 2 == 0)
    seta(x - 9, y + LINHA_H / 2, 1, OURO), seta(x + w + 5, y + LINHA_H / 2, -1, OURO);
}

/* Painel de ajuda da linha marcada (ate 4 linhas). */
static void painel(const Textos *t, const Ajustes *a) {
  const int x = LX, y = PAINEL_Y, w = LW, h = PAINEL_H;
  fill(x, y, w, h, PAINEL);
  contorno(x, y, w, h, 1, PAINEL_BORDA);
  int tx = x + 6, l1 = y + 4, l2 = y + 14, l3 = y + 24, l4 = y + 34;
  switch (a->linha) {
  case AJUSTE_VIDEO:
    texto(tx, l1, t->video_txt[video_i(a->video)][0], BRANCO, 1);
    texto(tx, l2, t->video_txt[video_i(a->video)][1], AZUL, 1);
    texto(tx, l4, t->aplica, CINZA, 1);
    break;
  case AJUSTE_TELA: {
    /* Pixels do quadro: 256 colunas na largura de uma TV 4:3 e 224 linhas
       na altura, entao cada pixel e 1,17 vez mais largo do que alto. Na TV
       em modo 16:9 a imagem e esticada 4/3 e ele passa a 1,56. */
    const bool larga = a->tela && !tela_fixa(a);
    const float ry = 17.0f, rx = ry / (larga ? 1.1667f * 4.0f / 3.0f : 1.1667f);
    int cx = x + 22, cy = y + h / 2;
    elipse(cx, cy, rx, ry, OURO);
    fill(cx - 1, cy - 1, 3, 3, OURO);
    texto(x + 44, l1 + 4, t->tela_txt[larga], BRANCO, 1);
    const char *const *l = tela_fixa(a) ? t->so_480p : t->redondo;
    texto(x + 44, l2 + 6, l[0], tela_fixa(a) ? LARANJA : AZUL, 1);
    texto(x + 44, l3 + 6, l[1], tela_fixa(a) ? LARANJA : AZUL, 1);
    break;
  }
  case AJUSTE_POSICAO: {
    const char *const *l = a->ajustando ? t->ajustando_txt : t->posicao_txt;
    texto(tx, l1, l[0], BRANCO, 1);
    texto(tx, l2, l[1], a->ajustando ? BRANCO : AZUL, 1);
    texto(tx, l3, l[2], a->ajustando ? OURO : CINZA, 1);
    break;
  }
  case AJUSTE_IDIOMA:
    texto(tx, l1, t->idioma_txt[a->idioma], BRANCO, 1);
    texto(tx, l2, t->idioma_tela, AZUL, 1);
    break;
  case AJUSTE_TRAPACAS:
    texto(tx, l1, t->trapaca_txt[0], BRANCO, 1);
    texto(tx, l2, t->trapaca_txt[1], BRANCO, 1);
    texto(tx, l3, t->trapaca_txt[2], AZUL, 1);
    texto(tx, l4, t->trapaca_txt[3], LARANJA, 1);
    break;
  default:
    texto(tx, l1, t->jogar_txt[0], BRANCO, 1);
    texto(tx, l2, t->jogar_txt[1], AZUL, 1);
    texto(tx, l3, t->jogar_txt[2], AZUL, 1);
    break;
  }
}

void formato_ajustes(uint32_t *destino, const unsigned char *letras, const Ajustes *a, int frame,
                     const uint32_t *cena) {
  px = destino;
  fonte = letras;
  const Textos *t = &kTextos[a->idioma == 1];
  fundo(cena, 0);
  caixa(BX, BY, BW, BH);
  texto_sombra(BY + 9, t->titulo, OURO, OURO_SOMBRA, 2);
  fill(W / 2 - 60, BY + 27, 120, 1, 0x806020);
  for (int l = 0; l < AJUSTE_JOGAR; l++) linha_ajuste(t, a, l, frame);
  botao_jogar(t, a, frame);
  painel(t, a);
  char linha[64];
  const char *l2 = a->situacao;
  uint32_t c2 = CINZA;
  if (a->aviso) {
    l2 = a->aviso, c2 = LARANJA;
  } else if (a->segundos >= 0) {
    snprintf(linha, sizeof(linha), t->contagem, a->segundos);
    l2 = linha, c2 = OURO;
  }
  /* No ajuste da posicao a segunda linha sai: ali fica a seta de baixo. */
  rodape(a->ajustando ? t->rodape_ajuste : t->rodape, BRANCO, a->ajustando ? NULL : l2, c2);
  if (a->ajustando) {
    /* No ajuste da posicao: setas piscando no meio de cada borda do quadro,
       para centralizar a moldura na TV (por ultimo, por cima do rodape). */
    uint32_t cor = (frame / 15) % 2 ? OURO : BRANCO;
    for (int k = 0; k < 2; k++) {
      seta_v(W / 2, 2 + k * 5, -1, cor);
      seta_v(W / 2, H - 6 - k * 5, 1, cor);
      seta(2 + k * 5, H / 2, -1, cor);
      seta(W - 6 - k * 5, H / 2, 1, cor);
    }
  }
}

/* ---- carregando ---- */

void formato_carregando(uint32_t *destino, const unsigned char *letras, const char *etapa, int passo,
                        int passos, int frame) {
  (void)frame;
  px = destino;
  fonte = letras;
  paisagem();
  texto_contorno(40, "SUPER MARIO WORLD", BRANCO, 2);
  const char *sub = "Port nativo para PlayStation 2";
  int sw = largura(sub, 1);
  fill((W - sw) / 2 - 6, 62, sw + 12, 12, 0x101010);
  texto_em(0, W, 64, sub, BRANCO);
  /* Uma pedra por etapa: acesa quando termina; a atual um pouco mais alta. */
  if (passos > 0) {
    const int lado = 18, passo_x = 32, x0 = (W - (passos - 1) * passo_x - lado) / 2;
    for (int i = 0; i < passos; i++) {
      bool acesa = i < passo, atual = i == passo - 1;
      int x = x0 + i * passo_x, y = atual ? 101 : 104;
      fill(x, y, lado, lado, acesa ? 0x5A3000 : 0x2A3350);
      fill(x + 1, y + 1, lado - 2, lado - 2, acesa ? 0xF8C030 : 0x6B7A99);
      fill(x + 2, y + 2, lado - 4, 2, acesa ? 0xFFE9A0 : 0x8E9BB8);
      fill(x + 2, y + lado - 4, lado - 4, 2, acesa ? 0xB06A10 : 0x4A5675);
      char n[2] = {(char)('1' + i), 0};
      texto(x + 6, y + 5, n, acesa ? 0x5A3000 : 0x2A3350, 1);
    }
  }
  if (etapa) texto_contorno(132, etapa, BRANCO, 1);
  texto_contorno(208, "Retrogames Patos de Minas", BRANCO, 1);
}

/* ---- teste do modo novo ---- */

void formato_confirma(uint32_t *destino, const unsigned char *letras, int video, int idioma, int segundos,
                      int frame, const uint32_t *cena) {
  (void)frame;
  static const char *const videos[] = {"240p", "480p"};
  px = destino;
  fonte = letras;
  fundo(cena, 140);
  caixa(20, 26, W - 40, 158);
  char linha[64];
  if (idioma == 1) {
    texto_sombra(36, "CAN YOU", BRANCO, 0x404040, 2);
    texto_sombra(54, "SEE THIS?", BRANCO, 0x404040, 2);
    snprintf(linha, sizeof(linha), "Press X to keep %s.", videos[video_i(video)]);
    texto_em(0, W, 78, linha, AZUL);
    texto_em(0, W, 166, "No answer: back to the settings.", CINZA);
  } else {
    texto_sombra(36, "A IMAGEM", BRANCO, 0x404040, 2);
    texto_sombra(54, "APARECEU?", BRANCO, 0x404040, 2);
    snprintf(linha, sizeof(linha), "Aperte X para ficar com o %s.", videos[video_i(video)]);
    texto_em(0, W, 78, linha, AZUL);
    texto_em(0, W, 166, "Sem resposta, volta aos ajustes.", CINZA);
  }
  snprintf(linha, sizeof(linha), "%d", segundos < 0 ? 0 : segundos % 100);
  int lw = largura(linha, 6);
  texto((W - lw) / 2 + 3, 98, linha, OURO_SOMBRA, 6);
  texto((W - lw) / 2, 95, linha, OURO, 6);
}

/* ---- trapacas ---- */

enum { T_LINHAS = 10, T_Y = 40, T_PASSO = 14 };

void formato_trapacas(uint32_t *destino, const unsigned char *letras, const ListaTrapacas *l, int frame,
                      const uint32_t *cena) {
  px = destino;
  fonte = letras;
  const bool en = l->idioma == 1;
  fundo(cena, l->no_jogo ? 110 : 0);
  caixa(8, 8, W - 16, 184);
  texto_sombra(16, en ? "CHEATS" : "TRAPAÇAS", OURO, OURO_SOMBRA, 2);
  fill(W / 2 - 60, 34, 120, 1, 0x806020);
  const char *l1 = en ? (l->no_jogo ? "X: on/off  O: back to game" : "X: on/off  O: back")
                      : (l->no_jogo ? "X: liga/desliga  O: volta ao jogo" : "X: liga/desliga  O: volta");
  if (l->n <= 0) {
    static const char *const pt[] = {"Nenhum TRAPACAS.TXT achado.", "Ponha o arquivo ao lado do",
                                     "save (APPS/SMW ou SMWPS2) ou", "use o Gravador no PC.",
                                     "Formato e lista: LEIA-ME."};
    static const char *const in[] = {"No TRAPACAS.TXT found.", "Put the file next to the",
                                     "save (APPS/SMW or SMWPS2) or", "use the PC Recorder.",
                                     "Format and list: LEIA-ME."};
    for (int i = 0; i < 5; i++) texto_em(0, W, 56 + i * 14, en ? in[i] : pt[i], i ? BRANCO : LARANJA);
    rodape(en ? "O: back" : "O: volta", BRANCO, NULL, 0);
    return;
  }
  int primeira = l->marcada - T_LINHAS / 2;
  if (primeira > l->n - T_LINHAS) primeira = l->n - T_LINHAS;
  if (primeira < 0) primeira = 0;
  for (int i = 0; i < T_LINHAS && primeira + i < l->n; i++) {
    const FormatoTrapaca *it = &l->itens[primeira + i];
    int y = T_Y + i * T_PASSO;
    bool marcada = primeira + i == l->marcada;
    if (marcada) {
      fill(16, y - 2, W - 32, 12, MARCA);
      contorno(16, y - 2, W - 32, 12, 1, OURO);
    }
    contorno(21, y, 8, 8, 1, it->valida ? BRANCO : 0x808080);
    if (it->ligada && it->valida) fill(23, y + 2, 4, 4, 0x60E060);
    /* O nome ja vem cortado em 28 letras (trapacas.c): 168 pontos, cabe. */
    texto(34, y, it->nome, !it->valida ? LARANJA : it->ligada ? OURO : BRANCO, 1);
  }
  if (primeira > 0) seta_v(W - 22, 38, -1, (frame / 20) % 2 ? OURO : BRANCO);
  if (primeira + T_LINHAS < l->n) seta_v(W - 22, T_Y + T_LINHAS * T_PASSO - 4, 1, (frame / 20) % 2 ? OURO : BRANCO);
  const FormatoTrapaca *m = &l->itens[l->marcada];
  const char *l2 = l->lugar;
  uint32_t c2 = CINZA;
  if (!m->valida) l2 = en ? "No valid code on this line." : "Nenhum código válido nesta linha.", c2 = LARANJA;
  rodape(l1, BRANCO, l2, c2);
}

/* ---- abertura: a vinheta da Retrogames (intro.c) ----
   Ate a v0.14.2 era desenhada no intro.c num quadro proprio de 320 x 224,
   em fundo preto; na v0.15 (pedido do usuario) ganhou o tema das outras
   telas: a cena do jogo atras, a caixa preta de borda dupla e, dentro dela,
   as particulas, o cubo dourado e os textos. As distancias na horizontal
   valem 0,8 das de antes: o pixel do quadro de 256 e 1,25 vez mais largo
   que o do de 320. */

enum { VX = 18, VY = 16, VW = W - 36, VH = 192, V_PARTICULAS = 160, V_CY = 76 };
static float v_angulo[V_PARTICULAS], v_raio[V_PARTICULAS];
static bool v_pronta;

/* Soma luz (brilho aditivo), so dentro da caixa. */
static void soma(int x, int y, unsigned r, unsigned g, unsigned b) {
  if (x < VX + 3 || x >= VX + VW - 3 || y < VY + 3 || y >= VY + VH - 3) return;
  uint32_t p = px[y * W + x];
  uint32_t nr = ((p >> 16) & 255) + r, ng = ((p >> 8) & 255) + g, nb = (p & 255) + b;
  px[y * W + x] = (nr > 255 ? 255 : nr) << 16 | (ng > 255 ? 255 : ng) << 8 | (nb > 255 ? 255 : nb);
}

static void ponto_brilho(int x, int y, unsigned r, unsigned g, unsigned b) {
  soma(x, y, r, g, b);
  soma(x + 1, y, r / 3, g / 3, b / 3), soma(x - 1, y, r / 3, g / 3, b / 3);
  soma(x, y + 1, r / 3, g / 3, b / 3), soma(x, y - 1, r / 3, g / 3, b / 3);
}

static void linha_brilho(float x0, float y0, float x1, float y1, unsigned r, unsigned g, unsigned b) {
  float dx = x1 - x0, dy = y1 - y0;
  int passos = (int)sqrtf(dx * dx + dy * dy) + 1;
  for (int i = 0; i <= passos; i++) {
    float t = (float)i / passos;
    ponto_brilho((int)(x0 + dx * t), (int)(y0 + dy * t), r, g, b);
  }
}

static void cubo(float cx, float cy, float lado, float ay, float ax, int nivel) {
  static const int arestas[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
                                     {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  float sx[8], sy[8];
  const float cy_ = cosf(ay), sy_ = sinf(ay), cx_ = cosf(ax), sx_ = sinf(ax);
  for (int i = 0; i < 8; i++) {
    float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f, z = (i & 4) ? 1.f : -1.f;
    float x1 = x * cy_ + z * sy_, z1 = -x * sy_ + z * cy_;
    float y1 = y * cx_ - z1 * sx_, z2 = y * sx_ + z1 * cx_;
    float k = 4.0f / (z2 + 6.0f);
    sx[i] = cx + x1 * lado * k * 0.8f, sy[i] = cy + y1 * lado * k * 0.9f;
  }
  for (int i = 0; i < 12; i++)
    linha_brilho(sx[arestas[i][0]], sy[arestas[i][0]], sx[arestas[i][1]], sy[arestas[i][1]],
                 255 * nivel >> 8, 190 * nivel >> 8, 60 * nivel >> 8);
}

static uint32_t nivel_cor(uint32_t rgb, int nivel) /* nivel 0..256 */ {
  if (nivel <= 0) return 0;
  if (nivel >= 256) return rgb;
  return ((((rgb >> 16) & 255) * nivel >> 8) << 16) | ((((rgb >> 8) & 255) * nivel >> 8) << 8) |
         ((rgb & 255) * nivel >> 8);
}

/* Um brilho que corre pela faixa [y0, y1) clareando so as letras (o fundo da
   caixa e escuro, mas nao preto). */
static void varre(int y0, int y1, int em) {
  for (int y = y0; y < y1; y++)
    for (int x = em - 15; x < em + 15; x++) {
      if ((unsigned)x >= W || (unsigned)y >= H || ((px[y * W + x] >> 16) & 255) < 100) continue;
      int k = 15 - (x > em ? x - em : em - x);
      soma(x, y, k * 10, k * 10, k * 8);
    }
}

void formato_vinheta(uint32_t *destino, const unsigned char *letras, int f, const uint32_t *cena) {
  px = destino;
  fonte = letras;
  if (!v_pronta) {  /* sempre as mesmas particulas */
    unsigned semente = 0x1234567u;
    for (int i = 0; i < V_PARTICULAS; i++) {
      semente = semente * 1103515245u + 12345u;
      v_angulo[i] = (float)((semente >> 8) & 0xFFFF) / 65535.0f * 6.2831853f;
      semente = semente * 1103515245u + 12345u;
      v_raio[i] = 104 + (float)((semente >> 8) & 0xFFFF) / 65535.0f * 104;
    }
    v_pronta = true;
  }
  const int trava = FORMATO_VINHETA_TRAVA;
  const float t = (float)f;
  fundo(cena, 50);
  caixa_a(VX, VY, VW, VH, 228);  /* um pouco mais escura que as outras: o brilho aparece mais */
  if (f < trava) {  /* particulas em espiral ate o centro */
    const float k = 1.0f - (float)f / trava, gira = t * 0.04f + (1 - k) * 3.0f;
    for (int i = 0; i < V_PARTICULAS; i++) {
      float r = v_raio[i] * k * k, a = v_angulo[i] + gira;
      ponto_brilho(W / 2 + (int)(r * cosf(a) * 0.8f), V_CY + (int)(r * sinf(a) * 0.75f), 160, 120, 50);
    }
  } else if (f < trava + 40) {  /* estouro: saem de novo, sumindo */
    const unsigned some = (unsigned)(200 - (f - trava) * 5);
    for (int i = 0; i < V_PARTICULAS; i++) {
      float e = (float)(f - trava) * 3.0f * (0.5f + v_raio[i] / 208.0f);
      ponto_brilho(W / 2 + (int)(e * cosf(v_angulo[i]) * 0.8f), V_CY + (int)(e * sinf(v_angulo[i]) * 0.75f),
                   some, some * 3 / 4, some / 3);
    }
  }
  if (f >= trava - 30) {
    int cresce = f < trava ? (f - (trava - 30)) * 256 / 30 : 256;
    cubo(W / 2, V_CY, 35.0f * (0.4f + 0.6f * cresce / 256.0f), t * 0.035f, 0.5f + 0.25f * sinf(t * 0.02f),
         f < trava ? cresce / 2 : 256);
  }
  if (f >= trava && f < trava + 12) {  /* clarao, dentro da caixa */
    unsigned k = (unsigned)(trava + 12 - f) * 20;
    for (int y = VY + 3; y < VY + VH - 3; y++)
      for (int x = VX + 3; x < VX + VW - 3; x++) soma(x, y, k, k, k);
  }
  /* Os textos aparecem aos poucos (cor 0 = ainda nao aparece). */
  const char *nome = "RETROGAMES";
  uint32_t cor = f > trava + 20 ? nivel_cor(0xF2C14E, (f - (trava + 20)) * 8) : 0;
  if (cor) texto((W - largura(nome, 3)) / 2, 132, nome, cor, 3);
  if (f >= trava + 40 && f < trava + 130) varre(128, 158, 20 + (f - (trava + 40)) * 3);
  cor = f > trava + 60 ? nivel_cor(0xC8D0F0, (f - (trava + 60)) * 8) : 0;
  if (cor) texto_em(0, W, 166, "Patos de Minas - MG", cor);
  cor = f > trava + 100 ? nivel_cor(0x9098B8, (f - (trava + 100)) * 6) : 0;
  if (cor) texto_em(0, W, 186, "apresenta", cor);
}

/* ---- transicao em circulo (v0.15) ---- */

void formato_iris(uint32_t *destino, int r) {
  if (r >= FORMATO_IRIS_MAX) return;
  if (r <= 0) {
    memset(destino, 0, sizeof(uint32_t) * W * H);
    return;
  }
  const float aspecto = 1.1667f;  /* o pixel de 256 x 224 numa TV 4:3 e mais largo que alto */
  const float r2 = (float)r * (float)r;
  for (int y = 0; y < H; y++) {
    float dy = (float)y + 0.5f - H / 2.0f;
    int x0 = W, x1 = W;  /* fora do circulo: a linha toda */
    if (dy * dy < r2) {
      float meia = sqrtf(r2 - dy * dy) / aspecto;
      if (meia >= W / 2.0f) continue;  /* a linha toda dentro */
      x0 = (int)(W / 2.0f - meia + 0.5f), x1 = (int)(W / 2.0f + meia + 0.5f);
    }
    if (x0 > 0) memset(destino + y * W, 0, sizeof(uint32_t) * (size_t)x0);
    if (x1 < W) memset(destino + y * W + x1, 0, sizeof(uint32_t) * (size_t)(W - x1));
  }
}

int formato_iris_raio(int q, bool abrindo) {
  if (q <= 0) return abrindo ? 0 : FORMATO_IRIS_MAX;
  if (q >= FORMATO_IRIS_QUADROS) return abrindo ? FORMATO_IRIS_MAX : 0;
  float t = (float)q / FORMATO_IRIS_QUADROS;
  t = t * t * (3 - 2 * t);  /* comeca e termina devagar */
  return (int)lroundf(FORMATO_IRIS_MAX * (abrindo ? t : 1 - t));
}

/* ---- abertura: titulo e "apoie o projeto" (intro.c) ---- */

void formato_titulo(uint32_t *destino, const unsigned char *letras, const char *versao,
                    const uint32_t *cena) {
  px = destino;
  fonte = letras;
  fundo(cena, 60);
  texto_contorno(44, "Super Mario", OURO, 3);
  texto_contorno(74, "World", BRANCO, 2);
  const char *sub = "Port nativo para PlayStation 2";
  char linha[48];
  snprintf(linha, sizeof(linha), "versão %s - projeto de fã, gratuito", versao);
  int w1 = largura(sub, 1), w2 = largura(linha, 1), w = w1 > w2 ? w1 : w2;
  mistura((W - w) / 2 - 8, 102, w + 16, 30, 0x000000, 200);
  texto_em(0, W, 106, sub, BRANCO);
  texto_em(0, W, 120, linha, CINZA);
}

void formato_apoio(uint32_t *destino, const unsigned char *letras, int frame, bool pode_seguir,
                   const char (*qr)[46], int n, const uint32_t *cena) {
  px = destino;
  fonte = letras;
  fundo(cena, 0);
  texto_contorno(8, "APOIE O PROJETO", OURO, 2);
  /* QR: 2 pixels por modulo, borda branca de 4 modulos, preto no branco. */
  const int m = 2, q = 4, lado = (n + 2 * q) * m, ox = 8, oy = 32;
  fill(ox - 2, oy - 2, lado + 4, lado + 4, 0x000000);
  fill(ox, oy, lado, lado, 0xFFFFFF);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (qr[y][x] == '1') fill(ox + (q + x) * m, oy + (q + y) * m, m, m, 0x000000);
  const int bx = ox + lado + 6, bw = W - 6 - bx;
  caixa_a(bx, oy - 2, bw, 142, 245);  /* quase opaca: o cenario aqui e cheio */
  static const struct { const char *t; uint32_t c; } linhas[] = {
      {"Este port é", BRANCO}, {"gratuito e sempre", BRANCO}, {"vai ser.", BRANCO}, {"", 0},
      {"Se ele te divertiu,", AZUL}, {"ajude a fazer os", AZUL}, {"próximos ports,", AZUL},
      {"como o Tekken 3", AZUL}, {"para PS2.", AZUL}, {"", 0},
      {"Pix: aponte a", OURO}, {"câmera do celular", OURO}, {"para o QR.", OURO}};
  for (int i = 0; i < (int)(sizeof(linhas) / sizeof(linhas[0])); i++)
    if (linhas[i].t[0]) texto(bx + 7, oy + 5 + i * 10, linhas[i].t, linhas[i].c, 1);
  mistura(0, 174, W, H - 174, 0x000000, 210);
  texto_em(0, W, 177, "Do exterior: ko-fi.com/guilhermesoti", AZUL);
  texto_em(0, W, 188, "github.com/guilhermesoti/smw-ps2", AZUL);
  if (pode_seguir && (frame / 30) % 2 == 0) texto_em(0, W, 205, "Pressione X ou START para jogar", BRANCO);
  else if (!pode_seguir) texto_em(0, W, 205, "Retrogames Patos de Minas", CINZA);
}
