/* Ajustes de abertura: SMW.CFG (Gravador) e SMW.OPT (escolha feita no PS2).
   Uso: opcoes <pasta vazia com SMW.ZIP ou SMW.SFC>
   A pasta faz o papel de APPS/SMW no pendrive. */
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
#define CONFERE(c) do { if (!(c)) { printf("FALHOU linha %d: %s\n", __LINE__, #c); ++falhas; } } while (0)

static char pasta[SMW_PATH];
static void grava(const char *nome, const char *texto) {
  char caminho[SMW_PATH * 2];
  snprintf(caminho, sizeof(caminho), "%s%s", pasta, nome);
  FILE *f = fopen(caminho, "wb");
  if (texto) fputs(texto, f);
  fclose(f);
}
static void apaga(const char *nome) {
  char caminho[SMW_PATH * 2];
  snprintf(caminho, sizeof(caminho), "%s%s", pasta, nome);
  remove(caminho);
}
static void le(const char *nome, char *texto, size_t n) {
  char caminho[SMW_PATH * 2];
  snprintf(caminho, sizeof(caminho), "%s%s", pasta, nome);
  texto[0] = '\0';
  FILE *f = fopen(caminho, "rb");
  if (!f) return;
  size_t k = fread(texto, 1, n - 1, f);
  texto[k] = '\0';
  fclose(f);
}

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  snprintf(pasta, sizeof(pasta), "%s%s", argv[1], argv[1][strlen(argv[1]) - 1] == '/' ? "" : "/");
  uint8_t *rom;
  char caminho[SMW_PATH];
  uint32_t crc;
  if (smw_find_rom(pasta, &rom, caminho, &crc) != ROM_GOOD) { puts("sem jogo na pasta"); return 2; }
  free(rom);
  static uint8_t sram[SMW_SRAM_BYTES];
  smw_load_progress(sram);  /* escolhe a pasta como lugar do save */

  SmwOptions o;
  apaga("SMW.CFG");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK && o.idioma == SMW_OPT_ASK &&
          o.compat == SMW_OPT_ASK);

  grava("SMW.CFG", "# Gravador SMW PS2\r\ntela=16:9\r\nvideo=480p\r\nidioma=en\r\n");
  smw_read_config(&o);
  CONFERE(o.tela == 1 && o.video == SMW_VIDEO_480P && o.idioma == 1 && o.compat == SMW_OPT_ASK);

  grava("SMW.CFG", "  TELA = 4:3 \nVideo=240P\nidioma=PT\nlixo\n=\n#tela=16:9\nchave=valor\n");
  smw_read_config(&o);
  CONFERE(o.tela == 0 && o.video == SMW_VIDEO_240P && o.idioma == 0);

  /* Suavizacao e scanlines sairam na v0.13.7: as chaves de arquivos antigos
     sao ignoradas e o resto continua valendo. */
  grava("SMW.CFG", "video=480i\nsuavizacao=media\nscanlines=sim\nidioma=en\n");
  smw_read_config(&o);
  CONFERE(o.video == SMW_VIDEO_480I && o.idioma == 1 && o.tela == SMW_OPT_ASK && o.compat == SMW_OPT_ASK);

  grava("SMW.CFG", "tela=perguntar\nvideo=perguntar\nidioma=portugues");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK && o.idioma == 0);

  grava("SMW.CFG", "tela=21:9\nvideo=1080p\nsaida=640\n");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK && o.compat == SMW_OPT_ASK);

  grava("SMW.CFG", "");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK);

  /* Escolha feita no PS2 (tela AJUSTES) */
  apaga("SMW.OPT");
  smw_options_clear(&o);
  CONFERE(!smw_read_saved_options(&o));
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK);
  SmwOptions escolha = {.tela = 1, .video = SMW_VIDEO_480I, .idioma = 0, .compat = SMW_OPT_ASK};
  CONFERE(smw_save_options(&escolha));
  char texto[128];
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=16:9\nvideo=480i\nidioma=pt\n") == 0);
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o));
  CONFERE(o.tela == 1 && o.video == SMW_VIDEO_480I && o.idioma == 0 && o.compat == SMW_OPT_ASK);

  /* SMW.OPT da v0.13.6, com suavizacao e scanlines: o resto vale */
  grava("SMW.OPT", "tela=4:3\nvideo=240p\nsuavizacao=forte\nscanlines=sim\nidioma=en\n");
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o));
  CONFERE(o.tela == 0 && o.video == SMW_VIDEO_240P && o.idioma == 1 && o.compat == SMW_OPT_ASK);

  /* Saida de 512 colunas (TV que nao aceitou a de 1280) */
  escolha.compat = 1;
  CONFERE(smw_save_options(&escolha));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strstr(texto, "saida=512\n") != NULL);
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.compat == 1);

  /* SMW.OPT da v0.11 (so a tela): os outros campos ficam sem valor */
  grava("SMW.OPT", "tela=4:3\n");
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o));
  CONFERE(o.tela == 0 && o.video == SMW_OPT_ASK && o.idioma == SMW_OPT_ASK && o.compat == SMW_OPT_ASK);

  /* Troca no jogo: so grava junto com o proximo save */
  SmwOptions jogo = {.tela = 0, .video = SMW_VIDEO_240P, .idioma = 1, .compat = SMW_OPT_ASK};
  smw_options_changed(&jogo);
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=4:3\n") == 0);
  CONFERE(smw_save_progress(sram));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=4:3\nvideo=240p\nidioma=en\n") == 0);
  /* Save seguinte sem troca nao mexe no SMW.OPT */
  grava("SMW.OPT", "tela=16:9\n");
  CONFERE(smw_save_progress(sram));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=16:9\n") == 0);

  /* v0.14.1: tamanho na TV (largura e altura em %, 80 a 100) */
  grava("SMW.OPT", "tela=16:9\nvideo=240p\nlargura=94\naltura= 90 \n");
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.largura == 94 && o.altura == 90 && o.tela == 1);
  grava("SMW.OPT", "largura=79\naltura=101\n");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.largura == SMW_OPT_ASK && o.altura == SMW_OPT_ASK);
  grava("SMW.OPT", "largura=9x\naltura=\n");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.largura == SMW_OPT_ASK && o.altura == SMW_OPT_ASK);
  grava("SMW.OPT", "largura=80\naltura=100\n");  /* os dois limites valem */
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.largura == 80 && o.altura == 100);
  CONFERE(smw_tamanho_limita(79) == 80 && smw_tamanho_limita(90) == 90 && smw_tamanho_limita(101) == 100);
  CONFERE(!smw_16_9_vale(SMW_VIDEO_240P) && smw_16_9_vale(SMW_VIDEO_480P) && !smw_16_9_vale(SMW_VIDEO_480I));
  /* 4294967390 = 2^32 + 94: convertido para int antes de conferir, viraria 94 */
  grava("SMW.OPT", "largura=4294967390\naltura=-90\n");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.largura == SMW_OPT_ASK && o.altura == SMW_OPT_ASK);
  SmwOptions tamanho = {.tela = 0, .video = SMW_VIDEO_480P, .idioma = 0, .compat = SMW_OPT_ASK, .largura = 92,
                        .altura = 100};
  CONFERE(smw_save_options(&tamanho));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=4:3\nvideo=480p\nidioma=pt\nlargura=92\naltura=100\n") == 0);
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.largura == 92 && o.altura == 100);
  grava("SMW.CFG", "largura=88\naltura=96\n");
  smw_read_config(&o);
  CONFERE(o.largura == 88 && o.altura == 96);

  /* SMW.OPT estragado: nenhum campo */
  grava("SMW.OPT", "\x01\x02 lixo");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK);

  apaga("SMW.OPT");
  apaga("SMW.CFG");
  puts(falhas ? "OPCOES: FALHOU" : "OPCOES: OK");
  return falhas != 0;
}
