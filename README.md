# Super Mario World para PlayStation 2

Port da Retrogames Patos de Minas baseado no trabalho de engenharia reversa do projeto snesrev/smw. A lógica de Super Mario World é compilada em C para o processador do PS2. Para jogar, o executável lê gráficos, mapas, sons e tabelas de uma cópia americana do cartucho fornecida pelo jogador.

**Versão 0.6.4 · NTSC 240p/480p · português.** A v0.6.4 troca o ícone do save por um cartucho cinza em 3D escrito só "SMW PS2". A v0.6.3 troca o ícone do save por um cartucho cinza em 3D com rótulo original e atualiza o ícone dos saves já existentes. A v0.6.2 grava icon.sys e um ícone 3D na pasta do save no Memory Card, para o navegador do PS2 mostrar o nome do jogo. A v0.6.1 corrige as permissões dos arquivos criados no Memory Card e recria arquivos antigos quando a abertura para gravação retorna -5. A v0.6 traz a tradução para português do Brasil das mensagens, dos nomes das fases e dos textos dos castelos (SELECT na abertura volta ao inglês). A v0.5 acrescenta o modo 480p opcional (Triângulo na abertura) com scanlines. A v0.4 usa os drivers de controle e Memory Card da BIOS do PS2, como os jogos comerciais. Desde a v0.3, o port troca o vídeo para 240p progressivo, sincroniza o jogo e o áudio com a TV, grava no Memory Card direto pelo MCSERV e mostra códigos em “ERRO AO SALVAR” (tabela no LEIA-ME). O jogo já rodou em PS2 físico pelo OPL; a v0.3 corrige os problemas observados nesse teste (gravação, cortes na imagem e engasgos). [Relatório técnico da v0.1](RELATORIO_TECNICO_SMW_PS2_v0.1.md)

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

O arquivo gerado é `APPS/SMW/SMW.ELF`. O SHA-256 do ELF da v0.6.4 obtido com o ps2dev v2.0.0 foi `e6ae82c2d75d31d218513b8ae94fe9986d8666610cb7e798110f03e8e0232c16`.

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

## Repetir o teste de regressão

Em Linux x86-64, instale um compilador C, Python 3, `pkg-config` e os pacotes de desenvolvimento do zlib e do MiniZip clássico. Em Debian e Ubuntu, esses pacotes são `build-essential`, `pkg-config`, `zlib1g-dev` e `libminizip-dev`. Execute a partir da raiz, indicando o caminho para sua própria cópia.

```sh
python3 testes/testar_percurso.py --sanitizar "/caminho/Super Mario World (USA).zip"
```

O teste compila o motor para o PC, joga o mesmo percurso duas vezes em pastas temporárias e verifica a fase, o salto, o áudio e o CRC da imagem final. O relatório descreve o teste separado do executável de PS2.

Para testar a gravação no Memory Card com chamadas MCSERV simuladas:

```sh
cc -std=gnu11 -D_EE -ffunction-sections -fdata-sections -Itestes/mocks $(pkg-config --cflags minizip zlib) testes/memory_card.c -Wl,--gc-sections $(pkg-config --libs minizip zlib) -o /tmp/smw-memory-card-test
/tmp/smw-memory-card-test
```

Para testar o ícone do save (criação, gravação em blocos de 4096 bytes e troca do ícone de versões anteriores):

```sh
cc -std=gnu11 -D_EE -Itestes/mocks -IFonte/ps2 $(pkg-config --cflags minizip zlib) testes/memory_card_icon.c $(pkg-config --libs minizip zlib) -o /tmp/smw-memory-card-icon-test
/tmp/smw-memory-card-icon-test
```

O ícone é gerado por `python3 Fonte/gerar_icone.py`, que reescreve `Fonte/ps2/smw_icon.h`.

## Créditos

A base desta versão é a revisão `bd80629bc968267239c4a2fb77fa96d0ed62da7b` de snesrev/smw, com componentes derivados de LakeSnes. A integração do console usa PS2SDK e gsKit. Os avisos de autoria e as licenças dos componentes acompanham os fontes. [Revisão de origem](https://github.com/snesrev/smw/tree/bd80629bc968267239c4a2fb77fa96d0ed62da7b) · [LakeSnes](https://github.com/elzo-d/LakeSnes) · [Licenças incluídas](Licencas/)
