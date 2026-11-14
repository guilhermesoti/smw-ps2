#include "smw_rtl.h"
#include "snes/snes.h"
#include "common_cpu_infra.h"
#include "variables.h"
#include "funcs.h"

void SmwDrawPpuFrame(bool draw_pixels) {
  SimpleHdma hdma_chans[3];

  Snes *snes = g_snes;

  dma_startDma(snes->dma, mirror_hdmaenable, true);
//  dma_initHdma(snes->dma);

  SimpleHdma_Init(&hdma_chans[0], &g_snes->dma->channel[5]);
  SimpleHdma_Init(&hdma_chans[1], &g_snes->dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[2], &g_snes->dma->channel[7]);

  int trigger = snes->vIrqEnabled ? snes->vTimer + 1 : -1;

  for (int i = 0; i <= 224; i++) {
    /* Preserve frame parity, HDMA and the status-bar IRQ on catch-up frames. */
    if (draw_pixels || i == 0) ppu_runLine(snes->ppu, i);
    SimpleHdma_DoLine(&hdma_chans[0]);
    SimpleHdma_DoLine(&hdma_chans[1]);
    SimpleHdma_DoLine(&hdma_chans[2]);
//    dma_doHdma(snes->dma);
    if (i == trigger) {
      SmwVectorIRQ();
      trigger = snes->vIrqEnabled ? snes->vTimer + 1 : -1;
    }
  }

  /*
  while (!snes->inNmi) {
    snes_handle_pos_stuff(snes);

    if (snes->cpu->irqWanted) {
      snes->cpu->irqWanted = false;
      SmwVectorIRQ();
    }
  }*/
}

void SmwRunOneFrameOfGame(void) {
  Snes *snes = g_snes;
  snes->vPos = snes->hPos = 0;
  snes->cpu->nmiWanted = snes->cpu->irqWanted = false;
  snes->inVblank = snes->inNmi = false;

  if (snes->cpu->pc == 0x8000) {
    snes->cpu->pc = 0x8001;
    SmwVectorReset();
  }
  SmwRunOneFrameOfGame_Internal();

  SmwVectorNMI();
}

