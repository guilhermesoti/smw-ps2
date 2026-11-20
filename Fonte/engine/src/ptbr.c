#include "ptbr.h"
#include "common_rtl.h"
#include "smw_rtl.h"
#include "variables.h"
#include "funcs.h"
#include <string.h>

bool g_ptbr = true;
uint16 g_smw_extra_font[64 * 8];

#include "ptbr_levels.h"
#include "ptbr_letras.h"

/* ------------------------------------------------------------------ font --- */

/* The message font: background colour 1, ink colour 3; lowercase letters use
   rows 2-6, capitals rows 0-6. Accented capitals keep 5 of their 7 rows so
   the accent fits above them, as in commercial 8x8 fonts. */
static const char *const kBase[12][8] = {
  {"11133111","11311311","13311331","13311331","13333331","13311331","13311331","11111111"},  /* A */
  {"13333331","13311111","13311111","13333311","13311111","13311111","13333331","11111111"},  /* E */
  {"11333311","11133111","11133111","11133111","11133111","11133111","11333311","11111111"},  /* I */
  {"11333311","13311331","13311331","13311331","13311331","13311331","11333311","11111111"},  /* O */
  {"13311331","13311331","13311331","13311331","13311331","13311331","11333311","11111111"},  /* U */
  {"11333311","13311331","13311111","13311111","13311111","13311331","11333311","11111111"},  /* C */
  {"11111111","11111111","11333311","11111331","11333331","13311331","11333133","11111111"},  /* a */
  {"11111111","11111111","11333311","13311331","13333331","13311111","11333311","11111111"},  /* e */
  {"11111111","11111111","11111111","11133111","11133111","11133111","11133111","11111111"},  /* i (no dot) */
  {"11111111","11111111","11333311","13311331","13311331","13311331","11333311","11111111"},  /* o */
  {"11111111","11111111","13311331","13311331","13311331","13311331","11333331","11111111"},  /* u */
  {"11111111","11111111","11333331","13311111","13311111","13311111","11333331","11111111"},  /* c */
};
static const char *const kAccent[4][2] = {
  {"11113311","11133111"},  /* acute */
  {"11331111","11133111"},  /* grave */
  {"11133111","11311311"},  /* circumflex */
  {"11331131","13113311"},  /* tilde */
};
/* Glyph order: á à â ã é ê í ó ô õ ú ç, then the same in capitals. */
const uint8 g_ptbr_acentos[12] = {0xE1, 0xE0, 0xE2, 0xE3, 0xE9, 0xEA, 0xED, 0xF3, 0xF4, 0xF5, 0xFA, 0xE7};
static const struct { uint8 base, accent; } kGlyphs[12] = {
  {0,0},{0,1},{0,2},{0,3},{1,0},{1,2},{2,0},{3,0},{3,2},{3,3},{4,0},{5,255},
};

static void PutRow(uint16 *glyph, int row, const char *bits) {
  uint16 lo = 0, hi = 0;
  for (int c = 0; c < 8; c++) {
    uint8 v = bits[c] - '0';
    lo |= (v & 1) << (7 - c);
    hi |= ((v >> 1) & 1) << (7 - c);
  }
  glyph[row] = lo | hi << 8;
}

void PtbrInit(void) {
  static const char kCedilla[2][9] = {"11113111", "11133111"};
  static const int kCapRows[5] = {0, 1, 3, 4, 6};
  for (int g = 0; g < 12; g++) {
    uint8 b = kGlyphs[g].base, a = kGlyphs[g].accent;
    uint16 *lower = g_smw_extra_font + g * 8, *upper = g_smw_extra_font + (12 + g) * 8;
    if (a == 255) {  /* ç / Ç: letter moved up, cedilla below */
      for (int r = 0; r < 5; r++) PutRow(lower, r + 1, kBase[11][r + 2]);
      PutRow(lower, 0, "11111111");
      for (int r = 0; r < 6; r++) PutRow(upper, r, kBase[5][r < 2 ? r : r + 1]);
      for (int r = 0; r < 2; r++) PutRow(lower, 6 + r, kCedilla[r]), PutRow(upper, 6 + r, kCedilla[r]);
      continue;
    }
    for (int r = 0; r < 8; r++) PutRow(lower, r, kBase[6 + b][r]);
    PutRow(lower, 0, kAccent[a][0]);
    PutRow(lower, 1, kAccent[a][1]);
    PutRow(upper, 0, kAccent[a][0]);
    PutRow(upper, 1, kAccent[a][1]);
    for (int r = 0; r < 5; r++) PutRow(upper, 2 + r, kBase[b][kCapRows[r]]);
    PutRow(upper, 7, "11111111");
  }
  /* v0.17: circunflexo no estilo da fonte da barra de status (contorno 1,
     tinta 3), na peca de cima do O de BONUS! (BG3 0x3D8). */
  static const char *const kCircunflexoStatus[8] = {
    "00000000", "00000000", "00000000", "00000000",
    "00111100", "01333310", "13311331", "11100111"};
  for (int r = 0; r < 8; r++) PutRow(g_smw_extra_font + kPtbrPecaCircunflexo * 8, r, kCircunflexoStatus[r]);
  PtbrLetrasGrandesZera();
}

/* --------------------------------------------------------------- encoding --- */

/* Writes one character as (tile, property) for the message font (property
   0x39 = tile 0x100 bank). Returns the bytes of text consumed. */
static int EncodeChar(const char *s, uint8 *tile, uint8 *prop) {
  uint8 c = (uint8)s[0];
  *prop = 0x39;
  if (c >= 'A' && c <= 'Z') { *tile = c - 'A'; return 1; }
  if (c >= 'a' && c <= 'z') { *tile = 0x40 + c - 'a'; return 1; }
  if (c >= '1' && c <= '8') { *tile = 0x64 + c - '1'; return 1; }
  switch (c) {
  case '0': *tile = 0x6B; return 1;
  case ' ': *tile = 0x1F; return 1;
  case '!': *tile = 0x1A; return 1;
  case '.': *tile = 0x1B; return 1;
  case '-': *tile = 0x1C; return 1;
  case ',': *tile = 0x1D; return 1;
  case '?': *tile = 0x1E; return 1;
  case '\'': *tile = 0x5D; return 1;
  case '#': *tile = 0x5A; return 1;
  case '{': {  /* {XX}: a raw font tile, e.g. the heart in Yoshi's note */
    int v = 0;
    for (int i = 1; i <= 2; i++) {
      char h = s[i];
      v = v * 16 + (h >= 'A' ? h - 'A' + 10 : h - '0');
    }
    *tile = (uint8)v;
    return 4;
  }
  }
  if (c == 0xC3) {  /* UTF-8 Latin-1 supplement */
    uint8 d = (uint8)s[1];
    bool upper = d < 0xA0;
    uint8 low = upper ? d + 0x20 : d;
    for (int g = 0; g < 12; g++) {
      if ((uint8)(g_ptbr_acentos[g] - 0x40) == low) {  /* Latin-1 -> 2o byte do UTF-8 */
        *tile = 0xC0 + g + (upper ? 12 : 0);  /* tile 0x3C0 + glyph */
        *prop = 0x3B;
        return 2;
      }
    }
    *tile = 0x1F;
    return 2;
  }
  *tile = 0x1F;
  return 1;
}

/* Fills width (tile, prop) pairs at dst from text, padding with spaces. */
static void WriteText(uint8 *dst, const char *text, int width) {
  int col = 0;
  while (*text && col < width) {
    uint8 tile, prop;
    text += EncodeChar(text, &tile, &prop);
    dst[2 * col] = tile;
    dst[2 * col + 1] = prop;
    col++;
  }
  for (; col < width; col++) {
    dst[2 * col] = 0x1F;
    dst[2 * col + 1] = 0x39;
  }
}

/* --------------------------------------------------------------- messages --- */

#define PALACE {"-PALÁCIO DO BOTÃO-", "O poder do botão", "que você apertou", "vai transformar", \
                "", "       em      .", "Seu progresso", "também foi salvo."}
static const char *const kMessages[25][8] = {
  PALACE, PALACE, PALACE, PALACE,
  {"Bem-vindo à Terra", "dos Dinossauros!", "Aqui descobrimos", "que a Princesa",
   "Peach sumiu de", "novo! Parece que", "o Bowser voltou a", "atacar!"},
  {"     - DICA -", "Você pode guardar", "um item extra na", "caixa no topo da",
   "tela. Para usá-lo,", "aperte o botão", "SELECT.", ""},
  {"     - DICA -", "Este portão marca", "o meio desta área.", "Cortando a fita",
   "aqui, você poderá", "continuar de um", "ponto perto daqui.", ""},
  {"Ao pular em um", "inimigo, segure o", "botão de pulo para", "saltar mais alto.",
   "Em água rasa,", "aperte para cima", "no direcional para", "pular mais alto."},
  {"Você ganha", "Estrelas Bônus ao", "cortar a fita no", "fim de cada área.",
   "Juntando 100", "Estrelas Bônus,", "você joga um jogo", "bônus divertido!"},
  {"Esta é uma Mansão", "Assombrada. Você", "consegue achar a", "saída? Hi, hi,",
   "hi... Não vá se", "perder!", "", ""},
  {"Use a capa do", "Mario para voar!", "Corra rápido, pule", "e segure o botão",
   "Quadrado. Para se", "equilibrar, use", "esquerda e direita", "no direcional."},
  {"     - DICA -", "Para pegar um", "casco, use", "Triângulo ou",
   "Quadrado. Para", "jogá-lo para cima,", "olhe para cima e", "solte o botão."},
  {"As áreas com ponto", "vermelho no mapa", "têm duas saídas", "diferentes. Se",
   "tiver tempo e", "habilidade, não", "deixe de", "procurá-las."},
  {"Há cinco entradas", "para o Mundo", "Estrela na Terra", "dos Dinossauros.",
   "Encontre todas e", "você poderá viajar", "entre muitos", "lugares."},
  {"Incrível! Poucos", "chegaram até aqui.", "Adiante fica a", "Zona Especial.",
   "Complete-a e você", "poderá explorar um", "estranho mundo", "novo. BOA SORTE!"},
  {"     - DICA -", "As moedas grandes", "são Moedas Dragão.", "Pegando cinco",
   "delas na mesma", "área, você ganha", "um Mario extra.", ""},
  {"Aqui, as moedas", "que você pegar ou", "o tempo restante", "podem mudar o seu",
   "progresso. Você", "consegue achar o", "objetivo especial?", ""},
  {"Aperte para cima", "no direcional", "enquanto pula para", "se agarrar na",
   "grade. Para entrar", "na porta no fim", "desta área, aperte", "para cima também."},
  {"     - DICA -", "Um dos amigos do", "Yoshi está preso", "no castelo de Iggy",
   "Koopa. Para", "derrotá-lo,", "empurre-o para o", "poço de lava."},
  {"Você pode deslizar", "a tela para a", "esquerda ou", "direita apertando",
   "L1 ou R1 no", "controle. Assim", "você pode ver mais", "à frente."},
  {"Se você estiver", "numa área que já", "completou, pode", "voltar ao mapa",
   "apertando START e", "depois SELECT.", "", ""},
  {"Para dar um pulo", "giratório, aperte", "Círculo. O pulo", "giratório do Super",
   "Mario quebra", "alguns blocos e", "derrota inimigos", "mais resistentes."},
  {"Olá! Desculpe, não", "estou em casa. Fui", "resgatar meus", "amigos, que foram",
   "capturados pelo", "Bowser.", "                {60}{61}", "         - Yoshi{62}{63}"},
  {"É possível", "preencher os", "blocos tracejados.", "Para preencher os",
   "amarelos, vá para", "o oeste e depois", "para o norte até o", "topo da montanha."},
  {"Oba! Obrigado por", "me salvar. Meu", "nome é Yoshi. A", "caminho de salvar",
   "meus amigos, o", "Bowser me prendeu", "naquele ovo.", ""},
};

bool PtbrMessageStripe(uint8 message, uint16 *stripe_pos) {
  static const uint16 kLineHeaders[8] = {0xa751, 0x8751, 0x6751, 0x4751, 0x2751, 0x751, 0xe750, 0xc750};
  if (!g_ptbr || message >= 25) return false;
  uint16 v = *stripe_pos;
  for (int line = 0; line < 8; line++) {
    WORD(stripe_image_upload_data[v]) = kLineHeaders[7 - line];
    WORD(stripe_image_upload_data[v + 2]) = 0x2300;  /* 18 tiles */
    WriteText(&stripe_image_upload_data[v + 4], kMessages[message][line], 18);
    v += 4 + 36;
  }
  *stripe_pos = v;
  return true;
}

/* ------------------------------------------------------------ level names --- */

bool PtbrLevelName(uint16 name_id, uint16 start, uint16 limit, uint16 *end) {
  if (!g_ptbr) return false;
  for (size_t i = 0; i < sizeof(kPtLevelNames) / sizeof(kPtLevelNames[0]); i++) {
    if (kPtLevelNames[i].id == name_id) {
      WriteText(&stripe_image_upload_data[start + 4], kPtLevelNames[i].name, (limit - start) / 2);
      *end = limit;
      return true;
    }
  }
  return false;
}

/* ---------------------------------------------------------- castle texts --- */

static const char *const kCastles[7][8] = {
  {"Mario derrotou o", "maluco Iggy Koopa no", "castelo #1 e salvou um", "amigo do Yoshi ainda",
   "preso dentro de um ovo.", "Juntos, eles agora", "vão à Terra Donut.", ""},
  {"Morton Koopa Jr., do", "castelo #2, agora é", "só uma lembrança. A", "próxima área é o",
   "Domo Baunilha, sob a", "terra. Que armadilhas", "esperam Mario por lá?", ""},
  {"Mario venceu Lemmy", "Koopa, do castelo #3.", "A missão do Mario está", "ficando bem mais",
   "difícil. Você já achou", "os Botões Vermelho e", "Verde?", ""},
  {"Acabaram-se os dias de", "Ludwig von Koopa", "compondo sinfonias no", "castelo #4. Adiante fica",
   "a Floresta da Ilusão.", "Mario terá que usar a", "cabeça para desvendar", "essa floresta confusa."},
  {"Mario achou o caminho", "pela Floresta da Ilusão", "e acabou com Roy Koopa,", "do castelo #5. Agora,",
   "rumo à perigosa, mas", "deliciosa, Ilha", "Chocolate!", ""},
  {"Wendy O. Koopa, do", "castelo #6, cantou sua", "última canção. Mario", "precisa enfrentar o",
   "desafio à sua frente.", "Há um navio afundado que", "parece ser a entrada", "para o Vale do Bowser."},
  {"Mario derrotou Larry", "Koopa, do castelo #7.", "Só resta o Castelo do", "Bowser, onde a Princesa",
   "Peach está presa. Será", "que Mario vai salvá-la e", "trazer a paz de volta à", "Terra dos Dinossauros?"},
};

/* Castle text lines are stripe images 0x0B-0x42: castle b (0-6), line l
   (0 = top) is image 0x12 + 8b - l, drawn at layer 3 row 19 + l, column 4. */
bool PtbrStripeImage(uint8 index) {
  static uint8 buf[4 + 48 + 1];
  if (!g_ptbr || index < 0x0B || index > 0x42) return false;
  int b = (index - 0x0B) / 8, line = 0x12 + 8 * b - index;
  const char *text = kCastles[b][line];
  uint16 dest = 0x5264 + 0x20 * line;
  buf[0] = dest >> 8;
  buf[1] = dest & 0xff;
  buf[2] = 0;
  buf[3] = 48 - 1;  /* 24 tiles = 48 bytes */
  WriteText(buf + 4, text, 24);
  buf[52] = 0xFF;
  LoadStripeImage_UploadToVRAM(buf);
  return true;
}

/* --------------------------------------------------- fim de fase --- */

/* v0.17: COURSE CLEAR! vira FASE COMPLETA! (as letras da barra de status,
   A=0x0A ... Z=0x23, ! = 0x28, espaco 0xFC). A faixa do jogo tem 13 pecas
   e as seguintes dependem do tamanho dela, entao a frase vai numa faixa a
   mais na mesma linha (camada 3, linha 10, colunas 9-22), escrita depois. */
/* Como o jogo nessas duas rotinas, a posicao na fila cabe em 1 byte: se a
   faixa e o 0xFF nao couberem ate 0xFF, nada e escrito (fica o ingles). */
static uint16 PtbrPoeFaixa(uint16 pos, uint16 vram, const uint8 *pecas, int n, uint8 prop) {
  if (n <= 0 || pos + 4 + n * 2 + 1 > 0xFF) return 0;
  stripe_image_upload_data[pos] = vram >> 8;
  stripe_image_upload_data[pos + 1] = vram & 0xff;
  stripe_image_upload_data[pos + 2] = 0;
  stripe_image_upload_data[pos + 3] = n * 2 - 1;
  for (int i = 0; i < n; i++) {
    stripe_image_upload_data[pos + 4 + i * 2] = pecas[i];
    stripe_image_upload_data[pos + 5 + i * 2] = prop;
  }
  stripe_image_upload_data[pos + 4 + n * 2] = 0xFF;
  return 4 + n * 2;
}

uint16 PtbrFimDeFaseTexto(uint16 pos) {
  static const char kTexto[] = "FASE COMPLETA!";
  uint8 pecas[sizeof(kTexto) - 1];
  for (size_t i = 0; i < sizeof(pecas); i++)
    pecas[i] = kTexto[i] == ' ' ? 0xFC : kTexto[i] == '!' ? 0x28 : 0x0A + (kTexto[i] - 'A');
  return PtbrPoeFaixa(pos, 0x5149, pecas, sizeof(pecas), 0x38);
}

/* BONUS! fica BÔNUS!: o circunflexo vai na linha de cima, sobre o O (camada 3,
   linha 15, coluna 11), com a peca 0x3D8 da fonte extra. */
uint16 PtbrFimDeFaseBonus(uint16 pos) {
  const uint8 peca = (0x3C0 + kPtbrPecaCircunflexo) & 0xff;
  return PtbrPoeFaixa(pos, 0x51EB, &peca, 1, 0x38 | ((0x3C0 + kPtbrPecaCircunflexo) >> 8));
}

/* ------------------------------------------------- palavras grandes --- */

/* v0.17: as palavras grandes das telas de carregar e de morte (MARIO START!,
   LUIGI START!, BONUS GAME, GAME OVER, TIME UP!) com letras desenhadas pelo
   port (ptbr_letras.h). A frase vira uma imagem de 16 linhas cortada em
   colunas de 8 pontos; cada coluna sao 2 sprites 8x8 (em cima e embaixo),
   como no jogo. '|' separa as duas metades que entram de lados opostos na
   tela de morte; '^' poe circunflexo na letra seguinte. */

static const char *const kPtbrGrandeFrases[kPtbrGrandeTotal] = {
  [kPtbrGrandeMario] = "VAMOS, MARIO!",
  [kPtbrGrandeLuigi] = "VAMOS, LUIGI!",
  [kPtbrGrandeBonus] = "JOGO B^ONUS",
  [kPtbrGrandeFimDeJogo] = "FIM DE|JOGO",
  [kPtbrGrandeTempo] = "TEMPO|ESGOTADO!",
};

/* As mesmas 24 pecas de sprite que o jogo usa para as letras dele (as 12
   do Mario sao reenviadas todo quadro e o jogo devolve as outras 12 depois,
   em RestoreSP1AfterMarioStart). Pecas repetidas sao aproveitadas: a frase
   mais longa, TEMPO ESGOTADO!, usa as 24 (o teste do percurso confere). */
static const uint8 kPtbrGrandePecas[24] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
  0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
};
enum {
  kGrandePecas = sizeof(kPtbrGrandePecas),
  kGrandeSprites = 40,
  kGrandeLargura = 192,
  kGrandeEspaco = 6,            /* espaco entre palavras */
  kGrandeVao = 8,               /* entre as metades da tela de morte */
  kGrandeOam = 68,              /* primeira entrada de OAM, como no jogo */
};
_Static_assert(kGrandeLargura % 8 == 0, "a imagem da frase e cortada em colunas de 8");
_Static_assert(sizeof(kPtbrCircunflexo) - 1 == 64, "circunflexo: 8 linhas de 8");

static struct {
  bool pronto;
  uint8 npecas, nsprites, faltaram;
  uint8 pecas[kGrandePecas][32];          /* 4bpp, formato do SNES */
  struct { int16 x; uint8 y, peca, metade; } spr[kGrandeSprites];
} g_grande;

static const PtbrLetra *PtbrLetraDe(char c) {
  for (size_t i = 0; i < sizeof(kPtbrLetras) / sizeof(kPtbrLetras[0]); i++)
    if (kPtbrLetras[i].c == c) return &kPtbrLetras[i];
  return NULL;
}

static uint8 PtbrCorGrande(char px, int y) {
  if (px == '.') return 0;
  if (px == '+') return 2;
  return (y <= 3 || y >= 12) ? 4 : (y <= 6 || y >= 9) ? 5 : 1;
}

/* Converte 8x8 cores (0-7) para uma peca 4bpp do SNES. */
static void PtbrPeca4bpp(uint8 *dst, const uint8 px[8][8]) {
  memset(dst, 0, 32);
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      uint8 v = px[y][x], bit = 0x80 >> x;
      if (v & 1) dst[y * 2] |= bit;
      if (v & 2) dst[y * 2 + 1] |= bit;
      if (v & 4) dst[16 + y * 2] |= bit;
    }
}

/* Guarda uma peca (repetidas reaproveitam a mesma) e devolve o indice, ou
   -1 se a peca for vazia ou nao couber. */
static int PtbrGuardaPeca(const uint8 px[8][8]) {
  uint8 bits[32];
  PtbrPeca4bpp(bits, px);
  static const uint8 kVazia[32];
  if (!memcmp(bits, kVazia, 32)) return -1;
  for (int i = 0; i < g_grande.npecas; i++)
    if (!memcmp(g_grande.pecas[i], bits, 32)) return i;
  if (g_grande.npecas >= kGrandePecas) { g_grande.faltaram++; return -1; }
  memcpy(g_grande.pecas[g_grande.npecas], bits, 32);
  return g_grande.npecas++;
}

static void PtbrPoeSprite(int x, int y, int peca, int metade) {
  if (peca < 0) return;
  if (g_grande.nsprites >= kGrandeSprites) { g_grande.faltaram++; return; }
  g_grande.spr[g_grande.nsprites].x = x;
  g_grande.spr[g_grande.nsprites].y = y;
  g_grande.spr[g_grande.nsprites].peca = peca;
  g_grande.spr[g_grande.nsprites].metade = metade;
  g_grande.nsprites++;
}

/* Largura em pontos de um pedaco da frase (ate '|' ou o fim). */
static int PtbrLarguraGrande(const char *s) {
  int w = 0;
  for (; *s && *s != '|'; s++) {
    const PtbrLetra *l = (*s == ' ') ? NULL : PtbrLetraDe(*s);
    w += (*s == ' ') ? kGrandeEspaco : (l && strlen(l->px) == (size_t)l->w * 16) ? l->w : 0;
  }
  return w;
}

int PtbrLetrasGrandes(int frase) {
  g_grande.pronto = false;
  if (frase < 0 || frase >= kPtbrGrandeTotal) return -1;
  g_grande.npecas = g_grande.nsprites = g_grande.faltaram = 0;
  const char *s = kPtbrGrandeFrases[frase];
  const char *barra = strchr(s, '|');
  /* Largura de cada metade; uma que passe da imagem e cortada e conta como
     perdida (o teste exige 0). */
  int w[2] = {PtbrLarguraGrande(s), barra ? PtbrLarguraGrande(barra + 1) : 0};
  for (int m = 0; m < 2; m++)
    if (w[m] > kGrandeLargura) w[m] = kGrandeLargura, g_grande.faltaram++;
  int x0 = 128 - (w[0] + (barra ? kGrandeVao + w[1] : 0)) / 2;
  for (int metade = 0; metade < (barra ? 2 : 1); metade++) {
    static uint8 img[16][kGrandeLargura];
    int x = 0;
    memset(img, 0, sizeof(img));
    bool circunflexo = false;
    for (; *s && *s != '|'; s++) {
      if (*s == '^') { circunflexo = true; continue; }
      if (*s == ' ') {
        if (circunflexo) g_grande.faltaram++, circunflexo = false;  /* '^' sem letra */
        x += kGrandeEspaco;
        continue;
      }
      const PtbrLetra *l = PtbrLetraDe(*s);
      /* Letra que falta ou desenho com tamanho errado: conta como perdida
         (o teste das palavras grandes exige 0). */
      if (!l || strlen(l->px) != (size_t)l->w * 16) {
        g_grande.faltaram += 1 + circunflexo;
        circunflexo = false;
        continue;
      }
      for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < l->w && x + xx < kGrandeLargura; xx++)
          img[yy][x + xx] = PtbrCorGrande(l->px[yy * l->w + xx], yy);
      if (circunflexo) {
        uint8 px[8][8];
        for (int yy = 0; yy < 8; yy++)
          for (int xx = 0; xx < 8; xx++)
            px[yy][xx] = kPtbrCircunflexo[yy * 8 + xx] == '.' ? 0 :
                         kPtbrCircunflexo[yy * 8 + xx] == '+' ? 2 : 5;
        PtbrPoeSprite(x0 + x + (l->w - 8) / 2, 96, PtbrGuardaPeca(px), metade);
        circunflexo = false;
      }
      x += l->w;
    }
    if (circunflexo) g_grande.faltaram++;  /* '^' no fim da metade */
    for (int c = 0; c * 8 < w[metade]; c++)
      for (int linha = 0; linha < 2; linha++) {
        uint8 px[8][8];
        for (int yy = 0; yy < 8; yy++)
          for (int xx = 0; xx < 8; xx++)
            px[yy][xx] = img[linha * 8 + yy][c * 8 + xx];
        PtbrPoeSprite(x0 + c * 8, 104 + linha * 8, PtbrGuardaPeca(px), metade);
      }
    x0 += w[metade] + kGrandeVao;
    if (*s == '|') s++;
  }
  g_grande.pronto = true;
  return g_grande.faltaram;
}

/* desloca: tela de morte, a metade 0 vem da esquerda e a 1 da direita. */
void PtbrLetrasGrandesDesenha(uint8 desloca) {
  if (!g_grande.pronto) return;
  for (int i = 0; i < g_grande.nsprites; i++) {
    int e = kGrandeOam + i;
    if (e >= 128) break;
    int x = g_grande.spr[i].x + (g_grande.spr[i].metade ? desloca : -desloca);
    OamEnt *o = &oam_buf[e];
    o->xpos = x & 0xff;
    o->ypos = g_grande.spr[i].y;
    o->charnum = kPtbrGrandePecas[g_grande.spr[i].peca];
    o->flags = 0x34;
    sprites_oamtile_size_buffer[e] = (x & 0x100) >> 8;  /* 9o bit do x */
  }
}

void PtbrLetrasGrandesZera(void) {
  memset(&g_grande, 0, sizeof(g_grande));
}

bool PtbrLetrasGrandesEnvia(void) {
  if (!g_ptbr || !g_grande.pronto) return false;
  for (int i = 0; i < g_grande.npecas; i++)
    SmwCopyToVram(0x6000 + kPtbrGrandePecas[i] * 16, g_grande.pecas[i], 32);
  return true;
}
