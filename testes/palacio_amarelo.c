/* Regressao do problema visto no PS2 (v0.7.3): Palacio Amarelo salvo, mas
   "progresso 0" depois de reiniciar.

   Usa o motor do jogo de verdade com a sua copia americana:
   1. liga o jogo aberto pelo pendrive, com o pendrive recusando gravar (como
      o "U27" do PS2) e o Memory Card 1 vazio; comeca um jogo novo no
      arquivo 1 (titulo, tela de arquivos, 1 jogador) e entra na primeira fase;
   2. aperta o interruptor amarelo e conclui o palacio como o jogo faz
      (flag_activated_switches[1] e counter_events_triggered, copiados para o
      buffer de save como em OwProcess02_HandleLevelBeaten) e responde "sim"
      ao prompt de salvar (SaveGame -> RtlWriteSram);
   3. reinicia: a tela de arquivos tem de mostrar 1 fase e o palacio;
      escolhe o arquivo (o jogo grava a SRAM de novo nessa hora);
   4. reinicia de novo: o progresso continua la.

   Uso: palacio_amarelo <pasta com SMW.SFC ou o ZIP>  (ver README) */
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../Fonte/ps2/files.c"
#include "mc_mock.h"
#include "src/common_rtl.h"
#include "src/funcs.h"
#include "src/variables.h"
#include "src/ptbr.h"

static char usb_dir[SMW_PATH];
static int usb_denied;
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) {
  if (usb_denied && strchr(mode, 'w') && usb_dir[0] && !strncmp(path, usb_dir, strlen(usb_dir))) {
    errno = 7;
    return NULL;
  }
  return __real_fopen(path, mode);
}

void Die(const char *msg) {
  fprintf(stderr, "Falha no motor: %s\n", msg);
  exit(EXIT_FAILURE);
}

static uint32_t pixels[SMW_WIDTH * SMW_HEIGHT] __attribute__((aligned(64)));
static int16_t sound[SMW_MAX_SAMPLES * 2];
static uint8_t *rom_copy;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FALHOU (linha %d): %s\n", __LINE__, #c); exit(1); } } while (0)

/* Liga o PS2: estado do port zerado, motor reiniciado com a copia do jogo. */
static void boot(const char *folder) {
  char path[SMW_PATH];
  uint8_t *rom;
  uint32_t crc;
  active_store = -1;
  max_generation = 0;
  smw_enable_memory_card(true);
  CHECK(smw_find_rom(folder, &rom, path, &crc) == ROM_GOOD);
  if (!rom_copy) {
    rom_copy = malloc(SMW_ROM_BYTES);
    memcpy(rom_copy, rom, SMW_ROM_BYTES);
  }
  if (!usb_dir[0]) snprintf(usb_dir, sizeof(usb_dir), "%s", folder_dir);
  g_ptbr = false;
  memset(g_ram, 0, 0x20000);
  smw_start(rom, pixels);
  free(rom);
}

/* Botoes do SNES por intervalo de quadros (como em percurso.c). */
typedef struct { unsigned from, to; uint16_t buttons; } Press;
static void run(unsigned from, unsigned to, const Press *p, size_t n) {
  for (unsigned step = from; step < to; ++step) {
    uint16_t buttons = 0;
    for (size_t i = 0; i < n; ++i)
      if (step >= p[i].from && step <= p[i].to) buttons |= p[i].buttons;
    smw_frame(buttons, 0, 1, sound, SMW_SAMPLES, false);
  }
}

static const uint8_t *file1(void) {
  PairU16 at = BufferFileSelectText_009DB5(0);
  return at.first == 0xffff ? NULL : g_sram + at.first;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "uso: %s <pasta com SMW.SFC>\n", argv[0]);
    return 2;
  }
  /* START no titulo, START na tela de arquivos (arquivo 1), START e B para
     1 jogador e a abertura: as mesmas entradas do percurso de regressao. */
  static const Press new_game[] = {{200, 204, 0x008}, {245, 249, 0x008}, {365, 369, 0x008},
                                   {700, 705, 0x001}, {760, 765, 0x001}};
  mc_reset();
  /* Pasta temporaria com a copia do jogo, como APPS/SMW no pendrive. */
  boot(argv[1]);
  usb_denied = 1;

  /* 1. Jogo novo no arquivo 1, ja dentro da primeira fase. */
  CHECK(!strcmp(smw_notice(NULL), "NENHUM SAVE: JOGO NOVO"));
  run(0, 1200, new_game, 5);
  CHECK(misc_game_mode == 0x14 && misc_current_save_file == 0);
  CHECK(!file1());                                 /* nada salvo ainda */

  /* 2. Palacio Amarelo: o interruptor (smw_00.c, RunPlayerBlockCode) marca
        flag_activated_switches[1]; concluir o palacio conta um evento. Ao
        voltar ao mapa, OwProcess02_HandleLevelBeaten copia os 45 bytes de
        ow_event_flags (inclui os interruptores e o contador) para o buffer
        de save; o prompt chama UpdateSaveBuffer e, com "sim", SaveGame. */
  flag_activated_switches[1] = 1;
  counter_events_triggered = 1;
  for (int i = 44; i >= 0; --i) ow_save_buffer[i + 96] = ow_event_flags[i];
  flag_show_save_prompt = 1;
  UpdateSaveBuffer();
  SaveGame();
  CHECK(!smw_save_error && !strcmp(smw_notice(NULL), "SALVO: MEMORY CARD 1"));
  CHECK(file1() && file1()[140] == 1 && file1()[134] == 1);

  /* 3. Reinicia: carregado do cartao, tela de arquivos com o palacio. */
  boot(argv[1]);
  CHECK(!strcmp(smw_notice(NULL), "CARREGADO: MEMORY CARD 1"));
  CHECK(load_info.store == STORE_MC0);
  CHECK(file1() && file1()[140] == 1 && file1()[134] == 1);
  /* A tela de arquivos copia o arquivo 1 para a reserva e chama RtlWriteSram
     sem mudar nada. Depois do primeiro save o cartao ainda nao tem SMW.BAK:
     essa vez grava (cria o BAK). */
  CHECK(!mc_get(0, "/SMWPS2/SMW.BAK"));
  run(0, 420, new_game, 3);  /* titulo -> tela de arquivos -> escolhe o arquivo 1 (grava a SRAM) */
  CHECK(!smw_save_error);
  CHECK(mc_get(0, "/SMWPS2/SMW.BAK"));
  CHECK(ow_save_buffer[140] == 1 && ow_save_buffer[134] == 1);

  /* 4. Reinicia de novo: o progresso continua e, com o SMW.BAK no lugar, a
        tela de arquivos nao grava nada (v0.13.8; no console isso parava o
        jogo por 5 s). */
  boot(argv[1]);
  CHECK(load_info.store == STORE_MC0);
  CHECK(file1() && file1()[140] == 1 && file1()[134] == 1);
  CHECK(mc_get(0, "/SMWPS2/SMW.SRM") && mc_get(0, "/SMWPS2/SMW.SRM")->size == SAVE_BYTES);
  CHECK(load_info.generation == 2);
  int gravacoes = mc_writes;
  run(0, 420, new_game, 3);
  CHECK(!smw_save_error && mc_writes == gravacoes);
  CHECK(ow_save_buffer[140] == 1 && ow_save_buffer[134] == 1);
  boot(argv[1]);
  CHECK(load_info.generation == 2 && file1() && file1()[140] == 1);

  printf("Palacio Amarelo: salvo no Memory Card 1 com o pendrive recusando gravar, "
         "carregado apos 2 reinicios (geracao %u).\n", (unsigned)load_info.generation);
  return 0;
}
