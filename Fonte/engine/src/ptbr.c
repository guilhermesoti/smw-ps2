#include "ptbr.h"
#include "common_rtl.h"
#include "variables.h"
#include "funcs.h"
#include <string.h>

bool g_ptbr = true;
uint16 g_smw_extra_font[64 * 8];

#include "ptbr_levels.h"

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
    static const uint8 kCodes[12] = {0xA1, 0xA0, 0xA2, 0xA3, 0xA9, 0xAA, 0xAD, 0xB3, 0xB4, 0xB5, 0xBA, 0xA7};
    uint8 d = (uint8)s[1];
    bool upper = d < 0xA0;
    uint8 low = upper ? d + 0x20 : d;
    for (int g = 0; g < 12; g++) {
      if (kCodes[g] == low) {
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
