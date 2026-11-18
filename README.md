# Super Mario World para PlayStation 2

Port da Retrogames Patos de Minas baseado no trabalho de engenharia reversa do projeto snesrev/smw. A lógica de Super Mario World é compilada em C para o processador do PS2. Para jogar, o executável lê gráficos, mapas, sons e tabelas de uma cópia americana do cartucho fornecida pelo jogador.

**Versão 0.12 (beta) · NTSC 240p/480p · 4:3 ou 16:9 · português.** Projeto gratuito, feito por fãs.

- Roda direto no processador do PS2 (não é emulador do SNES inteiro): a lógica do jogo é C compilado para o Emotion Engine.
- Vídeo 240p progressivo (cabo AV e a maioria dos adaptadores HDMI) ou 480p com scanlines (componente/HDMI).
- Tela 16:9 opcional (beta): nas fases mostra 43 colunas a mais de cada lado, para TV no modo widescreen. O jogo pergunta na primeira vez (tela FORMATO DA TELA) e L2 + R2 + CRUZ troca no jogo.
- **Gravador SMW PS2**: um HTML que abre em qualquer navegador, confere a cópia do jogo e grava o aplicativo, a ISO ou os dois no pendrive.
- Em português do Brasil: mensagens, nomes das fases e textos dos castelos. Segure SELECT na abertura para jogar em inglês.
- Grava no pendrive ou no Memory Card (pasta `SMWPS2`, com ícone 3D no navegador do PS2).
- Abre pelo OPL como aplicativo (ELF) ou como ISO na lista de jogos.
- Testado num PS2 de verdade pelo OPL.

**Para jogar, baixe o pacote na página de [Releases](https://github.com/guilhermesoti/smw-ps2/releases)** e siga o `LEIA-ME.txt`. O pacote não traz o jogo: é preciso ter a sua própria cópia americana do cartucho. As novidades de cada versão estão no `LEIA-ME.txt`.

## Compilar o executável

Use Linux x86-64, inclusive via WSL2, com GNU Make e a distribuição completa do ps2dev v2.0.0. Ela reúne o compilador do Emotion Engine, PS2SDK, bibliotecas auxiliares e gsKit usados na compilação de referência. [Baixar ps2dev v2.0.0](https://github.com/ps2dev/ps2dev/releases/tag/v2.0.0)

Na raiz do repositório, ajuste a primeira linha para o diretório onde extraiu o ps2dev. O código compila sem a ROM.

```sh
export PS2DEV="/opt/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin:$PATH"
make -C Fonte -j4
sha256sum APPS/SMW/SMW.ELF
```

O arquivo gerado é `APPS/SMW/SMW.ELF`. O módulo de som `audsrv.irx` é compilado de `Fonte/audsrv/` (PS2SDK com o volume do PCM corrigido; veja o `LEIA-ME.txt` dessa pasta). O SHA-256 do ELF da v0.12 obtido com o ps2dev v2.0.0 foi `2c3d9253c7b1c82c6fe4310f4e4dba04db829d600d10db4fa680e880b256abd7`.

## Gravador SMW PS2 (programa no PC)

`Gravador_SMW_PS2.html` é um arquivo só, com as telas, as fontes e o `SMW.ELF` dentro. Abre no navegador
(Windows, Linux ou Mac), sem instalar nada e sem internet. Ele confere a cópia do jogo (ZIP, SFC ou SMC, com
ou sem cabeçalho de copiador, CRC32 `B19ED489`), pergunta pendrive, ISO ou os dois, os ajustes de abertura e o
destino. No Chrome e no Edge grava direto no pendrive (File System Access) e confere o que gravou, sem tocar no
save; nos outros navegadores baixa um ZIP (ou a ISO). A ISO sai byte a byte igual à do `Fonte/gerar_iso.py`
(pycdlib). Nada é enviado para a internet.

Para gerar, depois de compilar o ELF:

```sh
python3 Gravador/gerar_gravador.py            # cria Gravador_SMW_PS2.html na raiz
```

O código fica em `Gravador/src/` (`nucleo.js`: ROM, ISO, ZIP e SMW.CFG; `tela.js`: os passos;
`gravador.html` e `gravador.css`). As fontes Press Start 2P e VT323 (SIL OFL 1.1) estão em `Gravador/fontes/`.

### Ajustes de abertura

O Gravador grava `SMW.CFG` ao lado do jogo (ou dentro da ISO), texto com `tela=perguntar|4:3|16:9`,
`video=perguntar|240p|480p` e `idioma=pt|en`. A tela escolhida no PS2 fica em `SMW.OPT`, no lugar do save, e
vale mais que a do `SMW.CFG`. Sem os dois, o jogo abre como antes.

## Jogar por USB

1. Coloque a sua cópia de **Super Mario World (USA)** dentro de `APPS/SMW/`. Pode usar `Super Mario World (USA).zip`, `SMW.ZIP`, `SMW.SFC` ou `SMW.SMC`. Dentro de um ZIP, o arquivo do cartucho deve ter extensão `.sfc` ou `.smc`.
2. Copie a pasta `APPS/SMW/` para o mesmo local na raiz de um pendrive FAT32. Confira se `SMW.ELF`, `title.cfg` e o jogo estão juntos.
3. No OPL com suporte a aplicativos por pasta, abra **Aplicativos → Super Mario World**. Pelo wLaunchELF, abra diretamente `mass:/APPS/SMW/SMW.ELF`.

A edição americana aceita tem 524.288 bytes e CRC32 `B19ED489` após a remoção de um eventual cabeçalho de copiador de 512 bytes. Ao salvar em um ponto oferecido pelo jogo, o progresso fica em `APPS/SMW/SMW.SRM` no pendrive. Se o pendrive recusar a gravação, o jogo usa o Memory Card (pasta `SMWPS2`).

## Gerar uma ISO para a lista de jogos do OPL

Compile o ELF e coloque seu jogo em `APPS/SMW/Super Mario World (USA).zip`. O gerador da ISO exige esse nome exato. Na raiz do repositório, use Python 3.8 ou superior.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install pycdlib==1.22.0
.venv/bin/python Fonte/gerar_iso.py
```

Copie `CD/SMWG_000.01.Super Mario World.iso` para a pasta `CD/` do pendrive e atualize a lista USB/BDM do OPL. Pela ISO, o progresso vai para o Memory Card, na pasta `SMWPS2` (cartão 1, depois cartão 2): o jogo não carrega drivers USB nesse modo, porque o OPL já usa o pendrive para ler a ISO. **A ISO gerada incorpora a sua cópia do jogo.** O pacote de código-fonte para publicação contém os fontes e a documentação e deixa a ROM, a ISO e a BIOS de fora.

## Controles

| Controle do PS2 | Ação |
| --- | --- |
| Direcional | Mover Mario e navegar nos menus |
| Cruz (×) | Pular e confirmar |
| Quadrado (□) | Correr e carregar objetos |
| Círculo (○) | Salto giratório |
| Triângulo (△) | Segunda tecla de corrida e ação (X do SNES) |
| Start / Select | Pausar ou iniciar / soltar o item da reserva |
| L1 / R1 | Deslocar a câmera nas fases que permitem |
| L2 + R2 + × | Trocar entre 4:3 e 16:9 |
| L2 + R2 + △ / □ / ○ | Medidor de desempenho / scanlines (480p) / suavização |

## Repetir o teste de regressão

Em Linux x86-64, instale um compilador C, Python 3, `pkg-config` e os pacotes de desenvolvimento do zlib e do MiniZip clássico. Em Debian e Ubuntu, esses pacotes são `build-essential`, `pkg-config`, `zlib1g-dev` e `libminizip-dev`. Execute a partir da raiz, indicando o caminho para sua própria cópia.

```sh
python3 testes/testar_percurso.py --sanitizar "/caminho/Super Mario World (USA).zip"
```

O teste compila o motor para o PC, joga o mesmo percurso duas vezes em pastas temporárias e verifica a fase, o salto, o áudio e o CRC da imagem final.

Ajustes de abertura (`SMW.CFG` e `SMW.OPT`), numa pasta só com o jogo:

```sh
cc -std=gnu11 -fsanitize=address,undefined -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   Fonte/ps2/files.c testes/opcoes.c $(pkg-config --libs minizip zlib) -o /tmp/smw-opcoes
ASAN_OPTIONS=detect_leaks=0 /tmp/smw-opcoes /caminho/da/pasta/com/o/jogo/
```

Gravador: o núcleo no Node 18+ (precisa de Python 3 com pycdlib: a ISO é comparada byte a byte com a do pycdlib)
e as telas num Chrome sem janela, com um pendrive simulado que já tem um save (precisa de `npm install puppeteer-core`):

```sh
node Gravador/testes/testar_nucleo.js "/caminho/Super Mario World (USA).zip" APPS/SMW/SMW.ELF
CHROME=/usr/bin/google-chrome node Gravador/testes/testar_telas.js Gravador_SMW_PS2.html \
   "/caminho/Super Mario World (USA).zip" /tmp/telas-gravador
```

Varredura de todas as 235 salas do jogo com AddressSanitizer e UBSan (carrega cada sala pelo próprio motor e joga alguns segundos; acusa leituras e escritas fora das tabelas). Leva uns 3 minutos:

```sh
python3 testes/testar_salas.py "/caminho/Super Mario World (USA).zip"
python3 testes/testar_salas.py --largo "/caminho/Super Mario World (USA).zip"   # mesma varredura em 16:9
```

Objetos que surgem ou somem dentro da área visível em 16:9 (cada sala é jogada com a tela larga e o
teste acusa sprites que aparecem ou desaparecem longe das bordas). Rode numa pasta só com o jogo:

```sh
cc -O1 -std=gnu11 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -IFonte/engine -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/files.c Fonte/ps2/game.c testes/aparecer.c \
   $(pkg-config --libs minizip zlib) -lm -o /tmp/smw-aparecer
/tmp/smw-aparecer /caminho/da/pasta/com/o/jogo/ 1 600 \
   $(python3 -c 'import json; print(*["0x"+s["hex"] for s in json.load(open("testes/salas.json"))["salas"]])')
```

Na v0.10 o resultado foi `TOTAL surge=0 some=2`: um Koopa que cai num buraco e some antes de sair da
tela por baixo e o bloco de mensagem da sala 1FF: comportamento normal do jogo.

Volta da câmera no mapa depois de olhar em volta com START (deslocamentos de até 273 pixels). Use uma pasta só com o jogo, sem save:

```sh
cc -O1 -std=gnu11 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -fsanitize=address,undefined -IFonte/engine -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/files.c Fonte/ps2/game.c testes/camera_mapa.c \
   $(pkg-config --libs minizip zlib) -lm -o /tmp/smw-camera
ASAN_OPTIONS=detect_leaks=0 /tmp/smw-camera /caminho/da/pasta/com/o/jogo/
```

Testes do salvamento (Memory Card simulado em `testes/mocks/mc_mock.h`, pendrive numa pasta temporária). Rode a partir da pasta `testes/`:

```sh
cd testes
P="$(pkg-config --cflags minizip zlib)"; L="$(pkg-config --libs minizip zlib)"
# fallback entre mídias + reinício, saves antigos, corrompidos, falha de leitura, cartão cheio
cc -std=gnu11 -D_EE -Imocks -I../Fonte/ps2 $P save_midias.c -Wl,--wrap=fopen $L -o /tmp/smw-save-midias && /tmp/smw-save-midias
# ícone do save e arquivos antigos com permissões erradas
cc -std=gnu11 -D_EE -Imocks -I../Fonte/ps2 $P memory_card_icon.c $L -o /tmp/smw-icone && /tmp/smw-icone
# abertura/gravação de um arquivo no MCSERV (sequência de chamadas)
cc -std=gnu11 -D_EE -ffunction-sections -fdata-sections -Imocks $P memory_card.c -Wl,--gc-sections $L -o /tmp/smw-mc && /tmp/smw-mc
```

Regressão do Palácio Amarelo com o motor do jogo (precisa da sua cópia: crie uma pasta com `SMW.SFC` ou o ZIP):

```sh
cd ..
cc -O1 -std=gnu11 -D_EE -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -Itestes/mocks -IFonte/engine -IFonte/engine/src -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/game.c testes/palacio_amarelo.c \
   -Wl,--wrap=fopen $(pkg-config --libs minizip zlib) -o /tmp/smw-palacio
/tmp/smw-palacio /caminho/da/pasta/com/o/jogo/
```

O ícone é gerado por `python3 Fonte/gerar_icone.py`, que reescreve `Fonte/ps2/smw_icon.h`.

## Créditos

Projeto **gratuito**, feito por fãs. Ele existe graças a:

- **[snesrev/smw](https://github.com/snesrev/smw)** (snesrev e contribuidores), revisão
  `bd80629bc968267239c4a2fb77fa96d0ed62da7b`: *Super Mario World* reimplementado em C, a base de todo o
  port, com a emulação de PPU e som derivada de **[LakeSnes](https://github.com/elzo-d/LakeSnes)**.
- **[AI Super Mario World PS2 Native Port](https://github.com/blashy101/-AI-Super-Mario-World-PS2-Native-Port)**
  (blashy101, licença MIT): a PPU otimizada e os achados da câmera do mapa, das tabelas e do volume do
  áudio, trazidos para este port na v0.9. A tela 16:9 da v0.10 foi escrita neste port, sem usar a
  desse projeto (que tem outra licença).
- **[Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P)** (CodeMan38) e
  **[VT323](https://fonts.google.com/specimen/VT323)** (Peter Hull), licença SIL OFL 1.1: as fontes do Gravador.
- **[ps2dev](https://github.com/ps2dev/ps2dev)**, **[PS2SDK](https://github.com/ps2dev/ps2sdk)** e
  **[gsKit](https://github.com/ps2dev/gsKit)**: as ferramentas e bibliotecas do PS2.
- **[OPL](https://github.com/ps2homebrew/Open-PS2-Loader)**: para rodar no console.

Feito por **Guilherme Soti** ([Retrogames Patos de Minas](https://github.com/guilhermesoti)), que
dirigiu o projeto e testou tudo num PS2 de verdade, com a ajuda de inteligência artificial:
**Claude** (Anthropic) no desenvolvimento. O que cada um fez está em **[CREDITOS.md](CREDITOS.md)**;
as licenças dos componentes estão em [Licencas/](Licencas/).

## Apoie o projeto

O port é e vai continuar gratuito. Se ele te ajudou e você quiser apoiar os próximos (como um port do
Tekken 3), pode doar qualquer valor:

<p align="center">
  <img src="docs/pix-qrcode.png" alt="QR code Pix para doação" width="220"><br>
  <b>Pix (chave aleatória):</b> <code>f19e4d1b-f35c-4541-919b-87a2cb540b9c</code>
</p>

<details>
<summary>Pix copia e cola</summary>

```
00020126580014br.gov.bcb.pix0136f19e4d1b-f35c-4541-919b-87a2cb540b9c5204000053039865802BR5914GUILHERME SOTI6014PATOS DE MINAS62070503***63048E1F
```
</details>

De fora do Brasil? / Outside Brazil? **[ko-fi.com/guilhermesoti](https://ko-fi.com/guilhermesoti)**

<a href="https://ko-fi.com/guilhermesoti"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi"></a>

Toda doação é voluntária e não dá acesso a nada extra: o código e as builds continuam livres para
todo mundo. Obrigado por apoiar a cena retro!

