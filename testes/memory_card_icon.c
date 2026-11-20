/* Icone do save no Memory Card (icon.sys + smwps2.icn) e arquivos de
   versoes antigas com permissoes erradas.
   - o icone e criado depois do progresso, uma vez, em blocos de ate 4096
     bytes, com leitura e gravacao, e trocado quando e de uma versao anterior;
   - saves antigos so-escrita (o driver abre, mas recusa ler) sao lidos de
     novo; SMW.TMP antigo que recusa gravacao ("M3-5T") nao impede o save. */
#include <assert.h>
#include <stdio.h>

#include "../Fonte/ps2/files.c"
#include "mc_mock.h"

static uint8_t sram[SMW_SRAM_BYTES];
static void reboot(void) {
  active_store = -1;
  max_generation = 0;
  memset(sram, 0, sizeof(sram));
  smw_enable_memory_card(true);
  folder_dir[0] = '\0';                  /* aberto pela ISO: so Memory Card */
  smw_load_progress(sram);
}

int main(void) {
  uint8_t save[SMW_SRAM_BYTES];
  mc_reset();
  reboot();
  memset(save, 0x5A, sizeof(save));      /* sem arquivos do jogo: SRAM "em branco" */
  assert(smw_save_progress(save));
  const McFile *sys = mc_get(0, "/SMWPS2/icon.sys"), *icn = mc_get(0, "/SMWPS2/" SMW_ICON_NAME);
  assert(sys && icn && mc_get(0, "/SMWPS2/SMW.SRM"));
  assert(sys->size == (int)sizeof(kSmwIconSys) && !memcmp(sys->data, kSmwIconSys, sizeof(kSmwIconSys)));
  assert(icn->size == (int)sizeof(kSmwIcon) && !memcmp(icn->data, kSmwIcon, sizeof(kSmwIcon)));
  assert(sys->perm == 3 && icn->perm == 3);
  int icon_writes = mc_writes_icon;
  assert(icon_writes == (int)((sizeof(kSmwIcon) + 4095) / 4096) && mc_max_piece == 4096);
  memset(save, 0x33, sizeof(save));
  assert(smw_save_progress(save) && mc_writes_icon == icon_writes);  /* ja existe: nao regrava */
  reboot();
  assert(!memcmp(sram, save, sizeof(save)));

  /* icone de versao anterior: trocado no proximo save, mesmo quando o
     progresso nao mudou (v0.13.8: sem progresso novo o SMW.SRM nao e
     regravado, mas o icone e) */
  mc_put(0, "/SMWPS2/icon.sys", kSmwIconSys, 452);  /* sem a marca da versao */
  reboot();
  int save_writes = mc_writes - mc_writes_icon;
  assert(!smw_save_unchanged(save));
  assert(smw_save_progress(save) && mc_writes_icon == 2 * icon_writes);
  assert(smw_save_unchanged(save));
  /* so o icone (smwps2.icn + icon.sys) foi gravado, o save ficou igual */
  assert(mc_writes - mc_writes_icon == save_writes + 1);
  sys = mc_get(0, "/SMWPS2/icon.sys");
  assert(sys->size == (int)sizeof(kSmwIconSys) && !memcmp(sys->data, kSmwIconSys, sizeof(kSmwIconSys)));

  /* saves antigos que ficaram so-escrita: lidos de novo */
  for (int k = 0; k < MC_MAXF; k++)
    if (mc_fs[k].used && strstr(mc_fs[k].name, "/SMWPS2/SMW.")) mc_fs[k].perm = 2;
  reboot();
  assert(!memcmp(sram, save, sizeof(save)) && load_info.store == STORE_MC0);
  memset(save, 0x55, sizeof(save));
  assert(smw_save_progress(save) && !smw_save_error_text()[0]);

  /* SMW.TMP antigo somente leitura (o "M3-5T" do PS2) */
  uint8_t three[3] = {1, 2, 3};
  mc_put(0, "/SMWPS2/SMW.TMP", three, 3);
  mc_fs[mc_find(0, "/SMWPS2/SMW.TMP")].perm = 1;
  reboot();
  assert(load_info.bad == 1);                       /* 3 bytes: nao e um save */
  memset(save, 0x66, sizeof(save));
  assert(smw_save_progress(save) && !smw_save_error_text()[0]);
  assert(mc_get(0, "/SMWPS2/SMW_T1.BAD") && mc_get(0, "/SMWPS2/SMW_T1.BAD")->size == 3);
  reboot();
  assert(!memcmp(sram, save, sizeof(save)) && load_info.bad == 0);
  printf("memory card + icone: OK (icone %zu bytes em %d blocos, icon.sys %zu bytes)\n",
         sizeof(kSmwIcon), icon_writes, sizeof(kSmwIconSys));
  return 0;
}
