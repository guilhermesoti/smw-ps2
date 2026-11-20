/* Trapacas (v0.14): o leitor do TRAPACAS.TXT (trapacas.c) e cada trapaca da
   lista que vai no pacote (APPS/SMW/TRAPACAS.TXT) jogada com o motor.

   Uso: trapacas <pasta com o jogo> <TRAPACAS.TXT>

   1. Leitor: formatos aceitos, enderecos recusados (ROM, SRAM, registros),
      espelho $0000-$1FFF, nomes (corte em 28 letras, acentos, caracteres
      que a fonte nao tem), limites e o TRAPACAS.LIG (ligadas pelos nomes).
   2. Lista do pacote: todas as linhas validas, sem codigo recusado.
   3. Motor: o roteiro do percurso ate a primeira fase; depois, de volta ao
      mesmo ponto para cada trapaca, 10 s andando e pulando com so ela
      ligada, conferindo o efeito na RAM e na barra de status. Sem trapaca,
      o tempo da fase cai (o teste do tempo prova alguma coisa). */
#include "files.h"
#include "trapacas.h"
#include "src/common_rtl.h"
#include "src/variables.h"
#include "src/snes/snes.h"
#include "src/snes/ppu.h"
#include "src/snes/dma.h"
#include "src/ptbr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Snes *g_snes;
static uint32_t pixels[SMW_MAX_PITCH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t amostras[SMW_MAX_SAMPLES * 2];
static int falhas, conferidos;

void Die(const char *msg) {
  fprintf(stderr, "Falha no motor: %s\n", msg);
  exit(EXIT_FAILURE);
}

#define CONFERE(c, ...)                                     \
  do {                                                      \
    ++conferidos;                                           \
    if (!(c)) {                                             \
      ++falhas;                                             \
      printf("FALHOU (linha %d): %s: ", __LINE__, #c);       \
      printf(__VA_ARGS__);                                  \
      printf("\n");                                         \
    }                                                       \
  } while (0)

static Trapacas tr;

static void leitor(void) {
  static const char texto[] =
      "\xEF\xBB\xBF# comentario\n"
      "  ; outro comentario\n"
      "\n"
      "Vidas = 7E0DBE:62\r\n"
      "Minusculas = 7e0dbf=63\n"
      "Oito juntos = 7E0F3109 + 7E0F32:09,7E0F33:09\n"
      "Espelho = 000019:02\n"
      "Espelho alto = 80:0019:02\n"
      "Banco 7F = 7F0010:AA\n"
      "ROM = 008000:EA\n"
      "SRAM = 700000:01\n"
      "Registro = 002100:0F\n"
      "Espelho fora = 002000:01\n"
      "Curto = 7E0DBE:6\n"
      "Sem codigo =\n"
      " = 7E0DC2:04\n"
      "Sem igual 7E0DBE:62\n"
      "Nome muito comprido com mais de vinte e oito letras = 7E0DBE:01\n"
      "Ação Ünica € = 7E0DBE:02\n";
  trapacas_le(&tr, texto, sizeof(texto) - 1);
  CONFERE(tr.n == 15, "n = %d", tr.n);
  CONFERE(!strcmp(tr.t[0].nome, "Vidas") && tr.t[0].codigos == 1 && tr.t[0].codigo[0].ram == 0x0DBE &&
              tr.t[0].codigo[0].valor == 0x62 && tr.t[0].linha == 4,
          "vidas");
  CONFERE(tr.t[1].codigos == 1 && tr.t[1].codigo[0].ram == 0x0DBF && tr.t[1].codigo[0].valor == 0x63, "minusculas");
  CONFERE(tr.t[2].codigos == 3 && tr.t[2].codigo[0].ram == 0x0F31 && tr.t[2].codigo[2].ram == 0x0F33 &&
              tr.t[2].recusados == 0,
          "oito juntos: %d codigos, %d recusados", tr.t[2].codigos, tr.t[2].recusados);
  CONFERE(tr.t[3].codigos == 1 && tr.t[3].codigo[0].ram == 0x0019, "espelho do banco 00");
  CONFERE(tr.t[4].codigos == 0 && tr.t[4].recusados == 1, "80:0019:02 nao e um codigo (dois-pontos no lugar errado)");
  CONFERE(tr.t[5].codigos == 1 && tr.t[5].codigo[0].ram == 0x10010 && tr.t[5].codigo[0].valor == 0xAA, "banco 7F");
  for (int i = 6; i <= 10; ++i)
    CONFERE(tr.t[i].codigos == 0 && tr.t[i].recusados == 1, "%s: recusado", tr.t[i].nome);
  CONFERE(tr.t[11].codigos == 0 && tr.t[11].recusados == 0, "sem codigo");
  CONFERE(!strcmp(tr.t[12].nome, "Linha 16") && tr.t[12].codigos == 1, "sem nome: '%s'", tr.t[12].nome);
  CONFERE(!strcmp(tr.t[13].nome, "Nome muito comprido com mais"), "corte em 28: '%s'", tr.t[13].nome);
  CONFERE(!strcmp(tr.t[14].nome, "Ação Ünica ?"), "acentos e caractere sem glifo: '%s'", tr.t[14].nome);
  CONFERE(tr.ignoradas == 0, "ignoradas");

  /* Ligadas pelo TRAPACAS.LIG: a ida e a volta. */
  CONFERE(trapacas_ligadas(&tr) == 0, "comeca tudo desligado");
  tr.t[0].ligada = tr.t[2].ligada = tr.t[14].ligada = true;
  tr.t[6].ligada = true;  /* sem codigo valido: nao conta nem vai para o arquivo */
  char lig[1024];
  int n = trapacas_texto_ligadas(&tr, lig, sizeof(lig));
  CONFERE(trapacas_ligadas(&tr) == 3, "3 ligadas");
  for (int i = 0; i < tr.n; ++i) tr.t[i].ligada = false;
  CONFERE(trapacas_liga_salvas(&tr, lig, (size_t)n) == 3, "religa pelo TRAPACAS.LIG");
  CONFERE(tr.t[0].ligada && tr.t[2].ligada && tr.t[14].ligada && !tr.t[6].ligada, "as mesmas");
  /* Aplica: so as ligadas. */
  static uint8_t ram[0x20000];
  trapacas_aplica(&tr, ram);
  CONFERE(ram[0x0DBE] == 0x02 && ram[0x0F31] == 9 && ram[0x0F33] == 9 && ram[0x0DBF] == 0, "aplica");

  /* Limites: 64 trapacas e 16 codigos por trapaca. */
  static char grande[70 * 40];
  size_t k = 0;
  for (int i = 0; i < 70; ++i) k += (size_t)snprintf(grande + k, sizeof(grande) - k, "T%d = 7E0DBE:01\n", i);
  trapacas_le(&tr, grande, k);
  CONFERE(tr.n == TRAPACA_MAX && tr.ignoradas == 70 - TRAPACA_MAX, "limite de trapacas: %d, %d", tr.n, tr.ignoradas);
  k = (size_t)snprintf(grande, sizeof(grande), "Muitos =");
  for (int i = 0; i < 20; ++i) k += (size_t)snprintf(grande + k, sizeof(grande) - k, " 7E00%02X:01", i);
  trapacas_le(&tr, grande, k);
  CONFERE(tr.t[0].codigos == TRAPACA_CODIGOS && tr.t[0].recusados == 4, "limite de codigos");

  /* Revisao da v0.14: um ':' ou '=' so; TRAPACAS.LIG com a marca do UTF-8. */
  static const char seps[] = "A = 7E0DBE::62\nB = 7E0DBE:=62\nC = 7E0DBE=62\n";
  trapacas_le(&tr, seps, sizeof(seps) - 1);
  CONFERE(tr.n == 3 && tr.t[0].codigos == 0 && tr.t[0].recusados == 1 && tr.t[1].codigos == 0 &&
              tr.t[1].recusados == 1 && tr.t[2].codigos == 1,
          "dois separadores recusados");
  static const char lig_bom[] = "\xEF\xBB\xBF" "C = 7E0DBE:62\n";
  CONFERE(trapacas_liga_salvas(&tr, lig_bom, sizeof(lig_bom) - 1) == 1 && tr.t[2].ligada, "LIG com BOM");
  CONFERE(trapacas_texto_ligadas(&tr, lig, 0) == 0, "LIG sem espaco");

  /* Revisao da v0.14 (Codex): duas com o mesmo nome; so a ligada volta. */
  static const char dupla[] = "B\xC3\xB4nus = 7E0DBE:01\nB\xC3\xB4nus = 7E0DBF:01\n";
  trapacas_le(&tr, dupla, sizeof(dupla) - 1);
  tr.t[1].ligada = true;
  n = trapacas_texto_ligadas(&tr, lig, sizeof(lig));
  CONFERE(strstr(lig, "B\xC3\xB4nus = 7E0DBF:01\n") != NULL, "LIG com o codigo: %s", lig);
  tr.t[1].ligada = false;
  CONFERE(trapacas_liga_salvas(&tr, lig, (size_t)n) == 1 && !tr.t[0].ligada && tr.t[1].ligada, "mesmo nome");
  /* Espelho: volta como 7E e liga a mesma. */
  static const char espelho[] = "E = 000019:02+7F0010:AA\n";
  trapacas_le(&tr, espelho, sizeof(espelho) - 1);
  tr.t[0].ligada = true;
  n = trapacas_texto_ligadas(&tr, lig, sizeof(lig));
  tr.t[0].ligada = false;
  CONFERE(strstr(lig, "E = 7E0019:02+7F0010:AA\n") && trapacas_liga_salvas(&tr, lig, (size_t)n) == 1, "espelho");

  /* Revisao da v0.14 (Codex): passou de 16 KB, a linha cortada sai inteira
     (sem isso, Y ficaria so com o primeiro codigo). */
  static char corte[TRAPACAS_BYTES + 1];
  memset(corte, 'x', sizeof(corte));
  corte[0] = '#';
  corte[16370] = '\n';
  memcpy(corte + 16371, "Y = 7E0DBE:01+7E0DBF:02\n", 14);
  CONFERE(trapacas_corte(corte, sizeof(corte)) == 16371, "corte: %zu", trapacas_corte(corte, sizeof(corte)));
  trapacas_le(&tr, corte, trapacas_corte(corte, sizeof(corte)));
  CONFERE(tr.n == 0, "linha cortada fica de fora");
  CONFERE(trapacas_corte(corte, TRAPACAS_BYTES) == TRAPACAS_BYTES, "16 KB inteiros");

  /* Revisao da v0.14 (Codex): o corte em 28 deixa um espaco no fim; sai,
     e a trapaca volta ligada pelo TRAPACAS.LIG. */
  static const char espaco[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAA B = 7E0DBE:01\n";
  trapacas_le(&tr, espaco, sizeof(espaco) - 1);
  CONFERE(strlen(tr.t[0].nome) == 27, "nome sem espaco no fim: '%s'", tr.t[0].nome);
  tr.t[0].ligada = true;
  n = trapacas_texto_ligadas(&tr, lig, sizeof(lig));
  tr.t[0].ligada = false;
  CONFERE(trapacas_liga_salvas(&tr, lig, (size_t)n) == 1, "volta ligada");
}

static void le_arquivo(const char *caminho, char *buf, size_t cap, size_t *n) {
  FILE *f = fopen(caminho, "rb");
  if (!f) {
    printf("FALHOU: nao abriu %s\n", caminho);
    exit(EXIT_FAILURE);
  }
  *n = fread(buf, 1, cap, f);
  fclose(f);
}

static void aplica(uint8_t *ram) { trapacas_aplica(&tr, ram); }

/* Mesmo roteiro do percurso.c: titulo, arquivo 1 e a primeira fase. */
static const struct { unsigned inicio, fim; uint16_t botoes; } roteiro[] = {
  {200, 204, 0x008}, {245, 249, 0x008}, {365, 369, 0x008},
  {700, 705, 0x001}, {760, 765, 0x001}, {975, 1000, 0x040},
  {1030, 1038, 0x001}, {1300, 1302, 0x002}, {1450, 1485, 0x040},
  {1580, 1588, 0x001}
};

static uint16_t entrada(unsigned passo) {
  uint16_t b = 0;
  for (size_t i = 0; i < sizeof(roteiro) / sizeof(roteiro[0]); ++i)
    if (passo >= roteiro[i].inicio && passo <= roteiro[i].fim) b |= roteiro[i].botoes;
  return b;
}

static uint8_t ram_base[sizeof(g_ram)];
static Snes snes_base;
static Ppu ppu_base;
static Dma dma_base;

/* Volta a RAM do jogo, o Snes, a PPU e o DMA ao ponto do roteiro (o APU, o
   SPC e a SRAM seguem: as 13 trapacas da lista nao mexem no som nem no
   save; uma que mexa precisa restaurar esses tambem). */
static void volta_ao_comeco(void) {
  memcpy(g_ram, ram_base, sizeof(g_ram));
  *g_snes = snes_base;
  *g_snes->ppu = ppu_base;
  *g_snes->dma = dma_base;
}

/* 10 s andando para a direita e pulando de vez em quando. */
static void joga(void) {
  for (unsigned f = 0; f < 600; ++f)
    smw_frame((uint16_t)(0x0100 | ((f % 70) < 20 ? 0x0080 : 0)), 0, 1, amostras, SMW_SAMPLES, f == 599);
}

int main(int argc, char **argv) {
  if (argc != 3) return EXIT_FAILURE;
  setbuf(stdout, NULL);
  leitor();

  static char texto[TRAPACAS_BYTES + 1];
  size_t n;
  le_arquivo(argv[2], texto, sizeof(texto), &n);
  trapacas_le(&tr, texto, trapacas_corte(texto, n));
  CONFERE(tr.n == 13 && tr.ignoradas == 0, "lista do pacote: %d trapacas", tr.n);
  for (int i = 0; i < tr.n; ++i)
    CONFERE(tr.t[i].codigos > 0 && tr.t[i].recusados == 0, "%s: %d codigos, %d recusados", tr.t[i].nome,
            tr.t[i].codigos, tr.t[i].recusados);

  uint8_t *rom;
  uint32_t crc;
  char arquivo[SMW_PATH];
  if (smw_find_rom(argv[1], &rom, arquivo, &crc) != ROM_GOOD) {
    printf("FALHOU: o carregador rejeitou a copia do jogo\n");
    return EXIT_FAILURE;
  }
  g_ptbr = true;
  smw_start(rom, pixels);
  free(rom);
  smw_before_frame(aplica);
  for (unsigned passo = 0; passo < 1800; ++passo) smw_frame(entrada(passo), 0, 1, amostras, SMW_SAMPLES, false);
  CONFERE(misc_game_mode == 0x14, "o roteiro chegou numa fase (modo %02x)", misc_game_mode);
  memcpy(ram_base, g_ram, sizeof(g_ram));
  snes_base = *g_snes;
  ppu_base = *g_snes->ppu;
  dma_base = *g_snes->dma;

  /* Sem trapaca: o tempo da fase cai. */
  const unsigned tempo0 = counter_timer_hundreds * 100 + counter_timer_tens * 10 + counter_timer_ones;
  joga();
  const unsigned tempo1 = counter_timer_hundreds * 100 + counter_timer_tens * 10 + counter_timer_ones;
  CONFERE(tempo1 + 5 <= tempo0, "sem trapaca o tempo cai: %u -> %u", tempo0, tempo1);
  CONFERE(player_current_life_count != 0x62, "sem trapaca as vidas nao sao 99");

  for (int i = 0; i < tr.n; ++i) {
    volta_ao_comeco();
    for (int k = 0; k < tr.n; ++k) tr.t[k].ligada = k == i;
    joga();
    const char *nome = tr.t[i].nome;
    const TrapacaCodigo *c = &tr.t[i].codigo[0];
    if (c->ram == 0x0DBE) {
      CONFERE(player_current_life_count == 0x62 && misc_status_bar_tilemap[29] == 9 &&
                  misc_status_bar_tilemap[30] == 9,
              "%s: barra mostra %d%d", nome, misc_status_bar_tilemap[29], misc_status_bar_tilemap[30]);
    } else if (c->ram == 0x0F31) {
      unsigned t = counter_timer_hundreds * 100 + counter_timer_tens * 10 + counter_timer_ones;
      CONFERE(t >= 998, "%s: tempo %u", nome, t);
    } else if (c->ram == 0x1490) {
      CONFERE(timer_star_power >= 0xFE, "%s: estrela %u", nome, timer_star_power);
    } else if (c->ram == 0x0019) {
      CONFERE(player_current_power_up == c->valor, "%s: poder %u", nome, player_current_power_up);
    } else if (c->ram == 0x0DC2) {
      CONFERE(player_current_item_box == c->valor, "%s: reserva %u", nome, player_current_item_box);
    } else if (c->ram == 0x0DBF) {
      CONFERE(player_current_coin_count == 0x63 || player_current_coin_count == 0, "%s: moedas %u", nome,
              player_current_coin_count);
    } else if (c->ram == 0x13E4) {
      CONFERE(player_pmeter >= 0x6E, "%s: corrida %u", nome, player_pmeter);
    } else if (c->ram == 0x1497) {
      CONFERE(timer_player_hurt >= 0x7E, "%s: protegido %u", nome, timer_player_hurt);
    } else {
      CONFERE(0, "%s: endereco %05X sem conferencia no teste", nome, (unsigned)c->ram);
    }
  }
  printf(falhas ? "TRAPACAS: %d FALHA(S) em %d conferencias\n" : "TRAPACAS: OK (%d conferencias)\n",
         falhas ? falhas : conferidos, conferidos);
  return falhas ? EXIT_FAILURE : EXIT_SUCCESS;
}
