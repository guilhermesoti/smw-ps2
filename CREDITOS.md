# Créditos

Este port é **gratuito** e sem fins lucrativos. Ele só existe por causa do trabalho de muita gente
antes dele: abaixo está quem fez cada parte.

## O port para PS2

| Quem | O que fez |
|---|---|
| **Guilherme Soti** — [Retrogames Patos de Minas](https://github.com/guilhermesoti) | Ideia e direção do projeto, todos os testes num PlayStation 2 de verdade (pelo OPL), relatos dos erros vistos no console e as escolhas da tradução (Princesa Peach, botões do PS2 nas dicas, "FIM DE JOGO") e do visual |
| **Claude** (Anthropic, pelo Claude Code) | Desenvolvimento do port: camada PS2 (vídeo 240p/480p no GS, áudio, controles, Memory Card, USB), correções a partir dos testes no console, tradução para português feita dentro do port, ícone do save, abertura e documentação |

## O jogo em C (a base de tudo)

- **[snesrev/smw](https://github.com/snesrev/smw)** — por **snesrev** e contribuidores: a
  reimplementação de *Super Mario World* em C, de onde vem `Fonte/engine/`. Este port parte da revisão
  `bd80629bc968267239c4a2fb77fa96d0ed62da7b`. Licença em [Licencas/snesrev.txt](Licencas/snesrev.txt).
- **[LakeSnes](https://github.com/elzo-d/LakeSnes)** — por **elzo-d**: a emulação do PPU e do DSP
  de som do SNES usada pelo motor.

## Ferramentas e bibliotecas do PlayStation 2

- **[ps2dev](https://github.com/ps2dev/ps2dev)** — o compilador (`ee-gcc`) e as ferramentas para o
  Emotion Engine e o IOP.
- **[PS2SDK](https://github.com/ps2dev/ps2sdk)** — o kernel e os drivers usados pelo port (USB,
  controle, Memory Card, `audsrv`) e a fonte 8x8 da abertura.
- **[gsKit](https://github.com/ps2dev/gsKit)** — biblioteca gráfica do GS.
- **[zlib](https://zlib.net)**, **MiniZip** e **[FatFs](http://elm-chan.org/fsw/ff/)** — leitura do
  ZIP do jogo e do pendrive. As licenças de todos os componentes estão em [Licencas/](Licencas/).

## Para rodar

- **[Open PS2 Loader (OPL)](https://github.com/ps2homebrew/Open-PS2-Loader)** — para abrir o jogo pelo
  pendrive ou pela ISO no console.

## O jogo

*Super Mario World* © Nintendo. Este repositório **não** contém o jogo nem seus dados: é preciso ter a
sua própria cópia americana do cartucho. Projeto feito por fãs, sem ligação com a Nintendo.
