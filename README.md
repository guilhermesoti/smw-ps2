# Super Mario World para PlayStation 2

Port da Retrogames Patos de Minas baseado no trabalho de engenharia reversa do projeto snesrev/smw. A lógica de Super Mario World é compilada em C para o processador do PS2. Para jogar, o executável lê gráficos, mapas, sons e tabelas de uma cópia americana do cartucho fornecida pelo jogador.

**Versão 0.14.2 (beta) · NTSC 240p/480i/480p · 4:3 ou 16:9 · português.** Projeto gratuito, feito por fãs.

- Roda direto no processador do PS2 (não é emulador do SNES inteiro): a lógica do jogo é C compilado para o Emotion Engine.
- Tela AJUSTES no começo: vídeo 240p (cabo AV, qualquer TV; o recomendado), 480i (só para TV que não mostra 240p) ou 480p (componente/HDMI), tela 4:3 ou 16:9 e idioma, sempre com os pixels nítidos. Vídeo novo só fica se você confirmar que a imagem apareceu.
- 1280 pontos por linha em todos os vídeos (no 480p, quadro de 16 bits): todo pixel do SNES com a mesma largura, também no 16:9.
- Tela 16:9 opcional (beta), só no vídeo 480p (componente ou HDMI): nas fases mostra 43 colunas a mais de cada lado, para TV no modo widescreen (no começo e no fim da fase, o que fica fora dela aparece preto). Escolha na tela AJUSTES; L2 + R2 + CRUZ troca no jogo e mostra "TELA 4:3" ou "TELA 16:9" por 5 segundos.
- Posição da imagem na TV (para centralizar pelo AV), como o ajuste de posição de um monitor, sem perder nitidez: linha POSIÇÃO na tela AJUSTES e L2 + R2 + setas no jogo. O tamanho da v0.14.1 saiu (pelo AV a imagem reduzida embaçava ou perdia linhas).
- Telas de carregamento, AJUSTES e apresentação com um cenário do próprio jogo de fundo (desenhado da cópia do jogador ao abrir) e caixas pretas como as mensagens do jogo.
- Trapaças (códigos Pro Action Replay) num texto, `TRAPACAS.TXT`: liga e desliga na tela AJUSTES ou com L2 + R2 + SELECT no jogo. O pacote traz 13, conferidas no jogo, todas desligadas.
- Desenha todos os 60 quadros por segundo quando o quadro cabe no tempo da TV; quando não cabe, um sim, outro não, sempre no mesmo ritmo, com a velocidade e a música corretas.
- **Gravador SMW PS2**: um HTML que abre em qualquer navegador, confere a cópia do jogo e grava o aplicativo, a ISO ou os dois no pendrive.
- Em português do Brasil: mensagens, nomes das fases e textos dos castelos. O inglês (o original) se escolhe na tela AJUSTES.
- Grava no pendrive ou no Memory Card (pasta `SMWPS2`, com ícone 3D no navegador do PS2).
- Log do console (`SMW_LOG.TXT`, ao lado do ELF ou no Memory Card): drivers, vídeo testado e confirmado, saves e erros.
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

O arquivo gerado é `APPS/SMW/SMW.ELF`. O módulo de som `audsrv.irx` é compilado de `Fonte/audsrv/` (PS2SDK com o volume do PCM corrigido; veja o `LEIA-ME.txt` dessa pasta). O SHA-256 do ELF da v0.14.2 obtido com o ps2dev v2.0.0 foi `c68a5fce079a64aceafc57f542c0c66b4f262d3fd4a0d311e742bbb0e6fe3d49`.

## Gravador SMW PS2 (programa no PC)

`Gravador_SMW_PS2.html` é um arquivo só, com as telas, as fontes, o `SMW.ELF` e a lista de trapaças
(`APPS/SMW/TRAPACAS.TXT`) dentro. Abre no navegador (Windows, Linux ou Mac), sem instalar nada e sem internet.
Ele confere a cópia do jogo (ZIP, SFC ou SMC, com ou sem cabeçalho de copiador, CRC32 `B19ED489`), pergunta
pendrive, ISO ou os dois, os ajustes de abertura, a lista de trapaças (gravar ou não, editar; cada linha é
conferida com o mesmo leitor do jogo) e o destino. No Chrome e no Edge grava direto no pendrive (File System Access) e confere o que gravou, sem tocar no
save; nos outros navegadores baixa um ZIP (ou a ISO). A ISO sai byte a byte igual à do `Fonte/gerar_iso.py`
(pycdlib). Nada é enviado para a internet.

Para gerar, depois de compilar o ELF:

```sh
python3 Gravador/gerar_gravador.py            # cria Gravador_SMW_PS2.html na raiz
```

O código fica em `Gravador/src/` (`nucleo.js`: ROM, ISO, ZIP, SMW.CFG e o leitor do TRAPACAS.TXT; `tela.js`: os passos;
`gravador.html` e `gravador.css`). As fontes Press Start 2P e VT323 (SIL OFL 1.1) estão em `Gravador/fontes/`.

### Ajustes de abertura

O Gravador grava `SMW.CFG` ao lado do jogo (ou dentro da ISO), texto com `tela=perguntar|4:3|16:9`,
`video=perguntar|240p|480i|480p` e `idioma=pt|en`: o que já vem marcado na tela AJUSTES (as linhas `suavizacao=` e
`scanlines=` de versões antigas são ignoradas). O que foi escolhido no PS2 fica em `SMW.OPT`, no lugar do save, e vale mais que o `SMW.CFG`.
O `tela=16:9` só vale com `video=480p` (nos outros o jogo fica em 4:3 e a escolha fica guardada). A posição na TV
fica em `posicao_x=` e `posicao_y=` (pixels e linhas do SNES, de -16 a 16; também aceitos no `SMW.CFG`): o jogo só
muda onde o PCRTC começa a mostrar o quadro (`gsKit_set_display_offset`, `main.c`). As linhas `largura=` e `altura=`
da v0.14.1 são ignoradas.

### Trapaças

`TRAPACAS.TXT` (UTF-8), uma por linha: `Nome = 7E0DBE:62`, vários códigos com `+` (`7E0F31:09+7E0F32:09`).
Só códigos de memória no formato Pro Action Replay (6 algarismos do endereço e 2 do valor, com `:` ou `=` no
meio, ou os 8 juntos), de `$7E0000-$7FFFFF` ou do espelho `$0000-$1FFF` dos bancos `$00-$3F` e `$80-$BF`; o
valor é escrito na RAM do jogo (`g_ram`) a cada quadro, antes da lógica. Game Genie não funciona: muda a ROM, e
aqui o programa é o jogo em C. Até 64 trapaças, 16 códigos em cada, nomes com até 28 letras, 16 KB lidos (a
linha cortada nesse limite sai) (`Fonte/ps2/trapacas.c`). Os valores não são conferidos: como no SNES, um valor
que o jogo não usa pode travá-lo. O jogo procura o arquivo onde está o save, depois no pendrive e nos Memory
Cards e, pela ISO, no CD (vale o primeiro que achar). As ligadas ficam em `TRAPACAS.LIG`, no lugar do save, no
mesmo formato (nome e códigos: duas trapaças com o mesmo nome não voltam ligadas juntas).

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

A lista `APPS/SMW/TRAPACAS.TXT` (ao lado do ELF) também entra na ISO; sem ela, a ISO sai sem trapaças. Copie `CD/SMWG_000.01.Super Mario World.iso` para a pasta `CD/` do pendrive e atualize a lista USB/BDM do OPL. Pela ISO, o progresso vai para o Memory Card, na pasta `SMWPS2` (cartão 1, depois cartão 2): o jogo não carrega drivers USB nesse modo, porque o OPL já usa o pendrive para ler a ISO. **A ISO gerada incorpora a sua cópia do jogo.** O pacote de código-fonte para publicação contém os fontes e a documentação e deixa a ROM, a ISO e a BIOS de fora.

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
| L2 + R2 + × | Trocar entre 4:3 e 16:9, só no 480p (mostra "TELA 4:3" ou "TELA 16:9" por 5 s; nos outros vídeos, "16:9 SO NO 480P") |
| L2 + R2 + setas | Mover a imagem na TV para centralizar (2 pixels do SNES a cada toque, até 16 para cada lado) |
| L2 + R2 + SELECT | Lista de trapaças (o jogo fica parado; ○ ou SELECT volta) |
| L2 + R2 + △ | Medidor de desempenho (quadros desenhados por segundo e tempos de desenho, envio ao GS e lógica) |

## Repetir o teste de regressão

Todos os testes do PC de uma vez (cada um numa pasta temporária com uma cópia do jogo; no fim, um resumo com
OK, FALHOU ou PULADO e a pasta com as saídas completas). Leva uns 5 minutos; `--rapido` pula as 235 salas e as
telas do Gravador, `--so saves,largo_169` roda só os escolhidos e `--lista` mostra os nomes:

```sh
python3 testes/rodar_todos.py "/caminho/Super Mario World (USA).zip"
```

Os testes do Gravador usam o `APPS/SMW/SMW.ELF` (compile antes) e o Node; o núcleo também o `python3` com o
`pycdlib` e o compilador C (o leitor do `TRAPACAS.TXT` do jogo, para comparar com o do Gravador); as telas também o `puppeteer-core` (`NODE_PATH`) e um Chrome (`CHROME=`). O que faltar na máquina
aparece como PULADO. Os programas compilados ficam numa pasta temporária apagada no fim; as saídas ficam em
`/tmp/smw-testes-*`. Os comandos de cada teste, um por um:

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

Ritmo do jogo (`Fonte/ps2/ritmo.c`: quantos quadros desenhar), simulado com os tempos medidos no console: no 4:3
desenha quase todos, no 16:9 um sim, outro não, sem trancos, e o jogo anda junto com a TV:

```sh
cc -std=gnu11 -fsanitize=address,undefined -IFonte/ps2 Fonte/ps2/ritmo.c testes/ritmo.c -lm -o /tmp/smw-ritmo
/tmp/smw-ritmo
```

Trapaças: o leitor do `TRAPACAS.TXT` (formatos, endereços recusados, nomes, limites, `TRAPACAS.LIG`), a lista
do pacote (13, todas válidas) e cada trapaça jogada 10 s com o motor, conferindo o efeito na RAM e na barra de
status. Rode numa pasta só com o jogo:

```sh
cc -O1 -std=gnu11 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -fsanitize=address,undefined -fno-sanitize-recover=all -IFonte/engine -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/files.c Fonte/ps2/game.c Fonte/ps2/registro.c Fonte/ps2/trapacas.c \
   testes/trapacas.c $(pkg-config --libs minizip zlib) -lm -o /tmp/smw-trapacas
ASAN_OPTIONS=detect_leaks=0 /tmp/smw-trapacas /caminho/da/pasta/com/o/jogo/ APPS/SMW/TRAPACAS.TXT
```

Gravador: o núcleo no Node 18+ (precisa de Python 3 com pycdlib: a ISO é comparada byte a byte com a do pycdlib;
com `TRAPACAS_REF`, o leitor de trapaças do Gravador é comparado com o do jogo em 224 textos sorteados) e as
telas num Chrome sem janela, com um pendrive simulado que já tem um save (precisa de `npm install puppeteer-core`):

```sh
cc -std=gnu11 -fsanitize=address,undefined -IFonte/ps2 Fonte/ps2/trapacas.c Gravador/testes/trapacas_ref.c \
   -o /tmp/trapacas_ref
TRAPACAS_REF=/tmp/trapacas_ref node Gravador/testes/testar_nucleo.js "/caminho/Super Mario World (USA).zip" APPS/SMW/SMW.ELF
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
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/files.c Fonte/ps2/game.c Fonte/ps2/registro.c testes/aparecer.c \
   $(pkg-config --libs minizip zlib) -lm -o /tmp/smw-aparecer
/tmp/smw-aparecer /caminho/da/pasta/com/o/jogo/ 1 600 \
   $(python3 -c 'import json; print(*["0x"+s["hex"] for s in json.load(open("testes/salas.json"))["salas"]])')
```

Na v0.10 o resultado foi `TOTAL surge=0 some=2`: um Koopa que cai num buraco e some antes de sair da
tela por baixo e o bloco de mensagem da sala 1FF: comportamento normal do jogo.

Mario na borda da imagem em 16:9 (v0.13.5): cada sala é jogada andando para a esquerda e depois
para a direita, e o teste mede quantos pixels sobram entre o Mario e a borda da parte desenhada
quando ele para no limite da tela. Nas pontas das fases as colunas fora da fase ficam pretas e sobram
de 6 a 8 pixels, como no 4:3; nas fases que andam sozinhas a sobra cresce quando a câmera sai do começo.
Em cada sala ele também desenha o mesmo quadro com 43 colunas extras de cada lado e com 10 e 30, e confere
que a tela nativa fica no mesmo lugar e que só as colunas que sobram ficam pretas (termina com erro se não).
Com uma pasta no terceiro argumento, grava um PPM do primeiro quadro em cada limite:

```sh
cc -O2 -std=gnu11 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -IFonte/engine -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/files.c Fonte/ps2/game.c Fonte/ps2/registro.c testes/largo_mario.c \
   $(pkg-config --libs minizip zlib) -lm -o /tmp/smw-largo-mario
SMW_LARGO=1 /tmp/smw-largo-mario /caminho/da/pasta/com/o/jogo/ 1500 - \
   $(python3 -c 'import json; print(*["0x"+s["hex"] for s in json.load(open("testes/salas.json"))["salas"]])')
```

Volta da câmera no mapa depois de olhar em volta com START (deslocamentos de até 273 pixels). Use uma pasta só com o jogo, sem save:

```sh
cc -O1 -std=gnu11 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -fsanitize=address,undefined -IFonte/engine -IFonte/ps2 $(pkg-config --cflags minizip zlib) \
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/files.c Fonte/ps2/game.c Fonte/ps2/registro.c testes/camera_mapa.c \
   $(pkg-config --libs minizip zlib) -lm -o /tmp/smw-camera
ASAN_OPTIONS=detect_leaks=0 /tmp/smw-camera /caminho/da/pasta/com/o/jogo/
```

Testes do salvamento (Memory Card simulado em `testes/mocks/mc_mock.h`, pendrive numa pasta temporária). Rode a partir da pasta `testes/`:

```sh
cd testes
P="$(pkg-config --cflags minizip zlib)"; L="$(pkg-config --libs minizip zlib)"
# fallback entre mídias + reinício, saves antigos, corrompidos, falha de leitura, cartão cheio,
# save sem mudança (não grava nada) e Memory Card trocado com o jogo ligado
cc -std=gnu11 -D_EE -Imocks -I../Fonte/ps2 $P save_midias.c -Wl,--wrap=fopen $L -o /tmp/smw-save-midias && /tmp/smw-save-midias
# log do console (SMW_LOG.TXT): pendrive, Memory Card pela ISO, pendrive recusando, memória cheia
cc -std=gnu11 -D_EE -Imocks -I../Fonte/ps2 $P registro.c -Wl,--wrap=fopen $L -o /tmp/smw-registro && /tmp/smw-registro
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
   $(find Fonte/engine/src -name '*.c') Fonte/ps2/game.c Fonte/ps2/registro.c testes/palacio_amarelo.c \
   -Wl,--wrap=fopen $(pkg-config --libs minizip zlib) -o /tmp/smw-palacio
/tmp/smw-palacio /caminho/da/pasta/com/o/jogo/
```

O ícone é gerado por `python3 Fonte/gerar_icone.py`, que reescreve `Fonte/ps2/smw_icon.h`.

## Montar os pacotes de uma versão

Com a pasta do projeto organizada (`Codigo`, `ROM`, `ELF OU ISO TESTE`, `Github`), o ELF da versão compilado em
`APPS/SMW/SMW.ELF` e o commit feito, de dentro de `Codigo`:

```sh
python3 Fonte/fazer_pacotes.py --simular   # confere e mostra o que vai fazer
python3 Fonte/fazer_pacotes.py
```

Ele confere a ROM (ZIP com o cartucho americano, CRC32 `B19ED489`, com as mesmas regras do carregador do jogo),
a versão (`SMW_VERSAO`, `LEIA-ME.txt`, `README.md` e a que está dentro do ELF), o SHA-256 do ELF (tem de ser o
que este README registra para a versão) e o git sem mudanças; monta numa pasta temporária o Gravador, o pacote
do jogo, o do código (`git archive` do HEAD) e a ISO de teste (essa tem a ROM e fica só no PC); confere que
nenhum pacote tem a ROM: arquivo com extensão de jogo, conteúdo igual ao da ROM (com ou sem cabeçalho), um
pedaço dela de 8 KiB ou mais, a ROM em base64, ou qualquer disso dentro de ZIP, gzip, bzip2 ou xz (até 4
níveis); o que não der para conferir (7z, RAR, arquivo grande demais) também para tudo. Só então copia cada
arquivo para `ELF OU ISO TESTE` e `Github` (para um nome temporário, conferido, e troca todos de uma vez,
voltando os antigos se uma troca falhar), move os pacotes da versão anterior para `Github/versoes-anteriores` e
mostra o SHA-256 de tudo. Os arquivos de `ELF OU ISO TESTE` são substituídos pelos da versão nova; fora isso,
não apaga nada. Se algo não bater, para antes de mexer nas pastas.

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

