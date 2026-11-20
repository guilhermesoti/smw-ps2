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
  grava("SMW.CFG", "video=480p\nsuavizacao=media\nscanlines=sim\nidioma=en\n");
  smw_read_config(&o);
  CONFERE(o.video == SMW_VIDEO_480P && o.idioma == 1 && o.tela == SMW_OPT_ASK && o.compat == SMW_OPT_ASK);

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
  /* A posicao 0 vale (centro): sem posicao, SMW_POSICAO_NADA */
  SmwOptions escolha = {.tela = 1, .video = SMW_VIDEO_480P, .idioma = 0, .compat = SMW_OPT_ASK,
                        .pos_x = SMW_POSICAO_NADA, .pos_y = SMW_POSICAO_NADA};
  CONFERE(smw_save_options(&escolha));
  char texto[128];
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=16:9\nvideo=480p\nidioma=pt\n") == 0);
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o));
  CONFERE(o.tela == 1 && o.video == SMW_VIDEO_480P && o.idioma == 0 && o.compat == SMW_OPT_ASK);

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
  SmwOptions jogo = {.tela = 0, .video = SMW_VIDEO_240P, .idioma = 1, .compat = SMW_OPT_ASK,
                     .pos_x = SMW_POSICAO_NADA, .pos_y = SMW_POSICAO_NADA};
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

  /* v0.14.2: posicao na TV (pixels e linhas do SNES, -16 a 16) */
  grava("SMW.OPT", "tela=16:9\nvideo=240p\nposicao_x=-4\nposicao_y= +6 \n");
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.pos_x == -4 && o.pos_y == 6 && o.tela == 1);
  grava("SMW.OPT", "posicao_x=-16\nposicao_y=16\n");  /* os dois limites valem */
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.pos_x == -16 && o.pos_y == 16);
  grava("SMW.OPT", "posicao_x=-17\nposicao_y=17\n");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.pos_x == SMW_POSICAO_NADA && o.pos_y == SMW_POSICAO_NADA);
  grava("SMW.OPT", "posicao_x=2x\nposicao_y=\n");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.pos_x == SMW_POSICAO_NADA && o.pos_y == SMW_POSICAO_NADA);
  /* 4294967300 = 2^32 + 4: convertido para int antes de conferir, viraria 4 */
  grava("SMW.OPT", "posicao_x=4294967300\nposicao_y=-4294967300\n");
  smw_options_clear(&o);
  smw_read_saved_options(&o);
  CONFERE(o.pos_x == SMW_POSICAO_NADA && o.pos_y == SMW_POSICAO_NADA);
  /* O tamanho da v0.14.1 saiu: largura= e altura= sao ignoradas */
  grava("SMW.OPT", "tela=4:3\nlargura=94\naltura=90\n");
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.tela == 0 && o.pos_x == SMW_POSICAO_NADA && o.pos_y == SMW_POSICAO_NADA);
  CONFERE(smw_posicao_limita(-17) == -16 && smw_posicao_limita(-4) == -4 && smw_posicao_limita(17) == 16);
  CONFERE(!smw_posicao_valida(SMW_POSICAO_NADA) && smw_posicao_valida(0));
  /* Um toque de seta: anda 2, para nos limites e no 0 vindo de um impar */
  CONFERE(smw_posicao_anda(0, 2) == 2 && smw_posicao_anda(-4, 2) == -2 && smw_posicao_anda(16, 2) == 16);
  CONFERE(smw_posicao_anda(-15, -2) == -16 && smw_posicao_anda(1, -2) == 0 && smw_posicao_anda(-1, 2) == 0);
  CONFERE(smw_posicao_anda(3, -2) == 1 && smw_posicao_anda(2, -2) == 0);
  CONFERE(!smw_16_9_vale(SMW_VIDEO_240P) && smw_16_9_vale(SMW_VIDEO_480P) && !smw_16_9_vale(2));
  /* O 480i saiu na v0.15: video=480i de um SMW.OPT antigo fica sem valor (o jogador escolhe) */
  grava("SMW.OPT", "tela=4:3\nvideo=480i\nidioma=pt\n");
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.tela == 0 && o.video == SMW_OPT_ASK && o.idioma == 0);
  SmwOptions velho = {.tela = 0, .video = 2, .idioma = 0, .compat = SMW_OPT_ASK, .pos_x = SMW_POSICAO_NADA,
                      .pos_y = SMW_POSICAO_NADA};
  CONFERE(smw_save_options(&velho));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=4:3\nidioma=pt\n") == 0);
  SmwOptions posicao = {.tela = 0, .video = SMW_VIDEO_480P, .idioma = 0, .compat = SMW_OPT_ASK, .pos_x = 0,
                        .pos_y = -8};
  CONFERE(smw_save_options(&posicao));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strcmp(texto, "tela=4:3\nvideo=480p\nidioma=pt\nposicao_x=0\nposicao_y=-8\n") == 0);
  smw_options_clear(&o);
  CONFERE(smw_read_saved_options(&o) && o.pos_x == 0 && o.pos_y == -8);
  /* O mais longo (69 bytes) */
  SmwOptions longo = {.tela = 1, .video = SMW_VIDEO_480P, .idioma = 0, .compat = 1, .pos_x = -16, .pos_y = -16};
  CONFERE(smw_save_options(&longo));
  le("SMW.OPT", texto, sizeof(texto));
  CONFERE(strlen(texto) == 69 && strstr(texto, "posicao_y=-16\n") != NULL);
  grava("SMW.CFG", "posicao_x=10\nposicao_y=-2\n");
  smw_read_config(&o);
  CONFERE(o.pos_x == 10 && o.pos_y == -2);

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
