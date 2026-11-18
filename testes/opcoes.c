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
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK && o.idioma == SMW_OPT_ASK);

  grava("SMW.CFG", "# Gravador SMW PS2\r\ntela=16:9\r\nvideo=480p\r\nidioma=en\r\n");
  smw_read_config(&o);
  CONFERE(o.tela == 1 && o.video == 1 && o.idioma == 1);

  grava("SMW.CFG", "  TELA = 4:3 \nVideo=240P\nidioma=PT\nlixo\n=\n#tela=16:9\nchave=valor\n");
  smw_read_config(&o);
  CONFERE(o.tela == 0 && o.video == 0 && o.idioma == 0);

  grava("SMW.CFG", "tela=perguntar\nvideo=perguntar\nidioma=portugues");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK && o.idioma == 0);

  grava("SMW.CFG", "tela=21:9\nvideo=1080p\n");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK && o.video == SMW_OPT_ASK);

  grava("SMW.CFG", "");
  smw_read_config(&o);
  CONFERE(o.tela == SMW_OPT_ASK);

  /* Escolha feita no PS2 */
  apaga("SMW.OPT");
  CONFERE(smw_read_saved_tela() == SMW_OPT_ASK);
  CONFERE(smw_save_tela(1));
  char texto[64];
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=16:9\n") == 0);
  CONFERE(smw_read_saved_tela() == 1);

  /* Troca no jogo: so grava junto com o proximo save */
  smw_tela_changed(0);
  CONFERE(smw_read_saved_tela() == 1);
  CONFERE(smw_save_progress(sram));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=4:3\n") == 0);
  CONFERE(smw_read_saved_tela() == 0);
  /* Save seguinte sem troca nao mexe no SMW.OPT */
  grava("SMW.OPT", "tela=16:9\n");
  CONFERE(smw_save_progress(sram));
  CONFERE(smw_read_saved_tela() == 1);

  /* SMW.OPT estragado vale como "perguntar" */
  grava("SMW.OPT", "\x01\x02 lixo");
  CONFERE(smw_read_saved_tela() == SMW_OPT_ASK);

  apaga("SMW.OPT");
  apaga("SMW.CFG");
  puts(falhas ? "OPCOES: FALHOU" : "OPCOES: OK");
  return falhas != 0;
}
