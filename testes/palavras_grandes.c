/* v0.17: palavras grandes das telas de carregar e de morte.

   Com o motor e a sua copia americana, segue o roteiro do percurso ate a
   tela de comeco de fase (MARIO START!) e, para as telas de morte, zera o
   tempo no quadro 1700 (TIME UP!) e tambem as vidas (GAME OVER). Confere:
   - em portugues, cada frase cabe nas 24 pecas e nos sprites das letras;
   - em ingles, GAME OVER e TIME UP! mostram todas as letras (antes da v0.17
     o motor deixava as colunas de cada metade no mesmo x: so 2 letras);
   - em portugues, VAMOS, MARIO!, FIM DE JOGO e TEMPO ESGOTADO! aparecem
     inteiras e centradas, e as metades da tela de morte entram dos lados;
   - depois das telas, as pecas de sprite na VRAM ficam iguais as do jogo em
     ingles (as letras usam as mesmas pecas que o jogo devolve);
   - no fim da fase (forcado no quadro 1700, com 13 estrelas de bonus), em
     portugues a camada 3 tem FASE COMPLETA! e o acento de BONUS!, e em
     ingles continua COURSE CLEAR! sem acento;
   - com o Luigi e com o jogo de bonus forcados no quadro 1611, VAMOS,
     LUIGI! e JOGO BONUS (com o circunflexo em cima do O) centradas.

   Cada corrida usa uma pasta temporaria nova com uma copia do jogo: o
   roteiro grava o save, e um save de uma corrida mudaria a seguinte. A sua
   pasta nao recebe nada.

   Uso: palavras_grandes <pasta com SMW.SFC ou o ZIP>  (ver README) */
#include "files.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/ptbr.h"
#include "src/snes/snes.h"
#include "src/snes/ppu.h"
#include "src/snes/dsp.h"
#include "src/smw_spc_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern Snes *g_snes;

void Die(const char *msg) {
  fprintf(stderr, "Falha no motor: %s\n", msg);
  exit(EXIT_FAILURE);
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FALHOU (linha %d): %s\n", __LINE__, #c); exit(1); } } while (0)

static uint32_t pixels[SMW_WIDTH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t amostras[SMW_MAX_SAMPLES * 2];
static char rom_arquivo[SMW_PATH];
/* A pasta temporaria da corrida em andamento (apagada tambem se o teste
   parar no meio: atexit). */
static char pasta[SMW_PATH], copia[2 * SMW_PATH];

/* Apaga a pasta temporaria (so os arquivos que este teste e o jogo criam). */
static void apaga_pasta(void) {
  static const char *const saves[] = {"SMW.SRM", "SMW.TMP", "SMW.BAK"};
  char p[2 * SMW_PATH];
  if (!pasta[0]) return;
  if (copia[0]) unlink(copia);
  for (size_t i = 0; i < sizeof saves / sizeof saves[0]; i++) {
    snprintf(p, sizeof p, "%s%s", pasta, saves[i]);
    unlink(p);
  }
  rmdir(pasta);
  pasta[0] = copia[0] = '\0';
}

/* Copia o jogo para uma pasta temporaria nova (pasta, com '/' no fim). */
static void pasta_nova(void) {
  snprintf(pasta, sizeof pasta, "/tmp/smw-palavras-XXXXXX");
  CHECK(mkdtemp(pasta) != NULL);
  const char *nome = strrchr(rom_arquivo, '/');
  snprintf(copia, sizeof copia, "%s/%s", pasta, nome ? nome + 1 : rom_arquivo);
  strcat(pasta, "/");
  FILE *de = fopen(rom_arquivo, "rb"), *para = fopen(copia, "wb");
  CHECK(de && para);
  static char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, de)) > 0) CHECK(fwrite(buf, 1, n, para) == n);
  CHECK(!ferror(de));
  fclose(de);
  CHECK(fclose(para) == 0);
}

/* O mesmo roteiro de percurso.c. */
static const struct { unsigned inicio, fim; uint16_t botoes; } roteiro[] = {
  {200, 204, 0x008}, {245, 249, 0x008}, {365, 369, 0x008},
  {700, 705, 0x001}, {760, 765, 0x001}, {975, 1000, 0x040},
  {1030, 1038, 0x001}, {1300, 1302, 0x002}, {1450, 1485, 0x040},
  {1580, 1588, 0x001}, {1800, 1900, 0x082}, {1820, 1844, 0x001}};

static uint16_t botoes(unsigned p) {
  uint16_t b = 0;
  for (size_t i = 0; i < sizeof roteiro / sizeof roteiro[0]; i++)
    if (p >= roteiro[i].inicio && p <= roteiro[i].fim) b |= roteiro[i].botoes;
  return b;
}

/* Retangulo dos pontos acesos nas linhas 90-130 (onde ficam as letras). */
typedef struct { int x0, x1, y0, y1, n; } Caixa;
static Caixa caixa(void) {
  Caixa c = {SMW_WIDTH, -1, SMW_HEIGHT, -1, 0};
  for (int y = 0; y < SMW_HEIGHT; y++)
    for (int x = 0; x < SMW_WIDTH; x++)
      if (pixels[y * SMW_WIDTH + x] & 0xffffff) {
        if (y < 90 || y > 130) { c.n = -1; return c; }  /* algo fora da faixa */
        if (x < c.x0) c.x0 = x;
        if (x > c.x1) c.x1 = x;
        if (y < c.y0) c.y0 = y;
        if (y > c.y1) c.y1 = y;
        c.n++;
      }
  return c;
}

typedef struct {
  Caixa comeco, morte_fim, morte_meio;
  uint16_t linha10[14], acento, bonus_o;
  uint8_t acima[8][SMW_WIDTH];  /* tela de comeco, linhas 96-103 acesas */
  uint16_t vram_fase[0x2000], vram_depois[0x2000];
} Corrida;

/* O que cada corrida forca no roteiro do percurso. */
typedef enum {
  kNormal,      /* so o roteiro: a tela de comeco da segunda fase */
  kTempo,       /* o tempo acaba no quadro 1700 (TIME UP!) */
  kSemVidas,    /* o tempo acaba sem vidas (GAME OVER) */
  kFimDaFase,   /* a fase acaba no 1700 (como a esfera do fim), 13 estrelas */
  kLuigi,       /* o Luigi entra na fase (1611) */
  kBonus,       /* o jogo de bonus (1611): so a tela de comeco */
} Cenario;

static void corre(bool pt, Cenario cena, Corrida *r) {
  char achado[SMW_PATH];
  uint8_t *rom;
  uint32_t crc;
  pasta_nova();
  CHECK(smw_find_rom(pasta, &rom, achado, &crc) == ROM_GOOD);
  memset(r, 0, sizeof *r);
  g_ptbr = pt;
  memset(g_ram, 0, 0x20000);
  smw_start(rom, pixels);
  free(rom);
  bool viu_comeco = false, viu_meio = false;
  bool morte = cena == kTempo || cena == kSemVidas;
  /* Ate o ultimo quadro que cada cenario olha (a morte, ate o mapa). */
  unsigned fim = morte ? 2700 : cena == kFimDaFase ? 2101 : 1651;
  for (unsigned p = 0; p < fim; p++) {
    if (morte && p == 1700) {
      counter_timer_hundreds = 0, counter_timer_tens = 0, counter_timer_ones = 2;
      if (cena == kSemVidas) player_current_life_count = 0;
    }
    if (cena == kLuigi && p == 1611) player_current_character = 1;
    if (cena == kBonus && p == 1611) flag_active_bonus_game = 1;
    if (cena == kFimDaFase && p == 1700) {
      timer_end_level = 0xff, misc_music_register_backup = 0xff, io_music_ch1 = 11;
      counter_bonus_stars_earned = 0x13;
    }
    smw_frame(p < 1930 ? botoes(p) : 0, 0, 1, amostras, SMW_SAMPLES, true);
    /* A tela de comeco da segunda fase: o quadro em que o modo e 0x11. */
    if (p > 1600 && misc_game_mode == 0x11 && !viu_comeco) {
      r->comeco = caixa();
      for (int y = 96; y < 104; y++)
        for (int x = 0; x < SMW_WIDTH; x++) r->acima[y - 96][x] = (pixels[y * SMW_WIDTH + x] & 0xffffff) != 0;
      viu_comeco = true;
    }
    if (p == 1650)
      memcpy(r->vram_fase, g_snes->ppu->vram + 0x6000, sizeof r->vram_fase);
    if (cena == kFimDaFase && p == 2100) {  /* a tela do fim da fase inteira */
      memcpy(r->linha10, g_snes->ppu->vram + 0x5149, sizeof r->linha10);
      r->acento = g_snes->ppu->vram[0x51EB];
      r->bonus_o = g_snes->ppu->vram[0x520B];
    }
    if (misc_game_mode == 0x17) {
      if (timer_display_death_message_animation == 0x40 && !viu_meio) {
        r->morte_meio = caixa();
        viu_meio = true;
      }
      if (timer_display_death_message_animation == 0) r->morte_fim = caixa();
    }
  }
  memcpy(r->vram_depois, g_snes->ppu->vram + 0x6000, sizeof r->vram_depois);
  /* Solta o motor desta corrida (como o restart_engine do game.c). */
  dsp_free(g_spc_player->dsp);
  free(g_spc_player);
  g_spc_player = NULL;
  snes_free(g_snes);
  g_snes = NULL;
  apaga_pasta();
  CHECK(viu_comeco);
  if (morte) CHECK(viu_meio && r->morte_fim.n > 0);
}

static bool centrada(Caixa c) {
  return c.n > 0 && abs((c.x0 + c.x1 + 1) - 256) <= 8;
}

int main(int argc, char **argv) {
  if (argc != 2) return EXIT_FAILURE;
  uint32_t crc;
  uint8_t *rom;
  CHECK(smw_find_rom(argv[1], &rom, rom_arquivo, &crc) == ROM_GOOD);
  free(rom);
  atexit(apaga_pasta);

  static Corrida en0, pt0, en1, pt1, en2, pt2, en3, pt3, en4, pt4, en5, pt5;
  corre(false, kNormal, &en0);
  corre(true, kNormal, &pt0);
  corre(false, kTempo, &en1);
  corre(true, kTempo, &pt1);
  corre(false, kSemVidas, &en2);
  corre(true, kSemVidas, &pt2);
  corre(false, kFimDaFase, &en3);
  corre(true, kFimDaFase, &pt3);
  corre(false, kLuigi, &en4);
  corre(true, kLuigi, &pt4);
  corre(false, kBonus, &en5);
  corre(true, kBonus, &pt5);
  const struct { const char *nome; Caixa c; } telas[] = {
    {"MARIO START!", en0.comeco}, {"VAMOS, MARIO!", pt0.comeco},
    {"LUIGI START!", en4.comeco}, {"VAMOS, LUIGI!", pt4.comeco},
    {"BONUS GAME", en5.comeco}, {"JOGO BONUS", pt5.comeco},
    {"TIME UP!", en1.morte_fim}, {"TEMPO ESGOTADO!", pt1.morte_fim},
    {"GAME OVER", en2.morte_fim}, {"FIM DE JOGO", pt2.morte_fim},
    {"TEMPO ESGOTADO! (entrando)", pt1.morte_meio}, {"FIM DE JOGO (entrando)", pt2.morte_meio},
  };
  for (size_t i = 0; i < sizeof telas / sizeof telas[0]; i++)
    printf("%-28s x %3d-%3d  y %3d-%3d  %5d pontos\n", telas[i].nome, telas[i].c.x0, telas[i].c.x1,
           telas[i].c.y0, telas[i].c.y1, telas[i].c.n);

  /* Ingles: as letras de cada metade em colunas diferentes (largura de
     8 letras; antes da v0.17, 2 letras davam uns 48 pontos). */
  CHECK(en1.morte_fim.x1 - en1.morte_fim.x0 >= 70);
  CHECK(en2.morte_fim.x1 - en2.morte_fim.x0 >= 70);
  CHECK(centrada(en1.morte_fim) && centrada(en2.morte_fim) && centrada(en0.comeco));
  /* Portugues: inteiras, centradas, nas linhas 104-119 das letras do jogo
     (o circunflexo de BONUS fica acima). */
  CHECK(centrada(pt0.comeco) && pt0.comeco.y0 >= 104 && pt0.comeco.y1 <= 119);
  CHECK(pt0.comeco.x1 - pt0.comeco.x0 >= 90);
  CHECK(centrada(en4.comeco) && centrada(en5.comeco));
  CHECK(centrada(pt4.comeco) && pt4.comeco.y0 >= 104 && pt4.comeco.y1 <= 119);
  CHECK(pt4.comeco.x1 - pt4.comeco.x0 >= 80);
  /* JOGO BONUS: o circunflexo (linhas 97-102) em cima do O, a 5a letra:
     JOGO (32) + espaco (6) + B (8) da esquerda da frase. */
  CHECK(centrada(pt5.comeco) && pt5.comeco.y0 >= 96 && pt5.comeco.y0 < 104 && pt5.comeco.y1 <= 119);
  {
    int o = pt5.comeco.x0 + 46, n_acima = 0, fora = 0;
    for (int y = 96; y < 104; y++)
      for (int x = 0; x < SMW_WIDTH; x++)
        if (pt5.acima[y - 96][x]) (x >= o && x < o + 8) ? n_acima++ : fora++;
    printf("JOGO BONUS: circunflexo %d pontos sobre o O (x %d-%d), %d fora\n", n_acima, o, o + 7, fora);
    CHECK(n_acima >= 6 && fora == 0);
  }
  CHECK(centrada(pt1.morte_fim) && pt1.morte_fim.x1 - pt1.morte_fim.x0 >= 110);
  CHECK(centrada(pt2.morte_fim) && pt2.morte_fim.x1 - pt2.morte_fim.x0 >= 80);
  /* Entrando: as metades ainda longe uma da outra, a da esquerda cortada
     pela borda e a da direita para alem do meio. */
  CHECK(pt1.morte_meio.x1 - pt1.morte_meio.x0 > pt1.morte_fim.x1 - pt1.morte_fim.x0 + 100);
  CHECK(pt2.morte_meio.x1 - pt2.morte_meio.x0 > pt2.morte_fim.x1 - pt2.morte_fim.x0 + 100);
  /* As pecas de sprite voltam as do jogo depois das telas. */
  CHECK(!memcmp(pt0.vram_fase, en0.vram_fase, sizeof en0.vram_fase));
  CHECK(!memcmp(pt1.vram_depois, en1.vram_depois, sizeof en1.vram_depois));
  CHECK(!memcmp(pt2.vram_depois, en2.vram_depois, sizeof en2.vram_depois));
  /* Fim da fase: letras da barra de status (A = 0x0A, ! = 0x28, espaco 0xFC,
     propriedade 0x38). Ingles: COURSE CLEAR! nas colunas 9-21. */
  static const char kEn[] = "COURSE CLEAR!", kPt[] = "FASE COMPLETA!";
  for (int i = 0; i < 14; i++) {
    char e = i < 13 ? kEn[i] : ' ', q = kPt[i];
    uint16_t pe = (e == ' ' ? 0xFC : e == '!' ? 0x28 : 0x0A + e - 'A') | 0x3800;
    uint16_t pq = (q == ' ' ? 0xFC : q == '!' ? 0x28 : 0x0A + q - 'A') | 0x3800;
    if (i < 13) CHECK(en3.linha10[i] == pe);
    CHECK(pt3.linha10[i] == pq);
  }
  printf("fim da fase: acento %04x (ingles %04x), O de BONUS %04x\n", pt3.acento, en3.acento, pt3.bonus_o);
  CHECK(pt3.bonus_o == (0x18 | 0x3800) && en3.bonus_o == pt3.bonus_o);
  CHECK(pt3.acento == (0x3D8 | 0x3800) && (en3.acento & 0x3FF) != 0x3D8);
  /* A peca do acento tem desenho (fonte extra, nunca a VRAM). */
  int tinta = 0;
  for (int r = 0; r < 8; r++) tinta += g_smw_extra_font[kPtbrPecaCircunflexo * 8 + r] != 0;
  CHECK(tinta >= 3);
  /* Cada frase cabe nas 24 pecas e nos sprites, sem letra que falte. */
  for (int f = 0; f < kPtbrGrandeTotal; f++) {
    int faltaram = PtbrLetrasGrandes(f);
    printf("frase %d: %d pecas, sprites ou letras perdidos\n", f, faltaram);
    CHECK(faltaram == 0);
  }
  printf("PALAVRAS: OK\n");
  return EXIT_SUCCESS;
}
