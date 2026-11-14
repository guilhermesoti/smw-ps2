# Relatório técnico · Super Mario World para PlayStation 2

**Retrogames Patos de Minas · versão 0.1 · 12 de novembro de 2026**

A recompilação a partir dos fontes da v0.1 produziu um executável idêntico, byte a byte, ao do pacote jogável. Uma ISO criada com esse executável e uma cópia americana do jogo iniciou no emulador de PS2 ParaLLEl PS2. O percurso passou pela abertura, pela escolha do arquivo e pelo mapa até Yoshi’s Island 1, onde Mario correu e pulou. A validação em PS2 físico permanece pendente.

## 1. O que roda no PS2

O projeto usa a implementação em C de Super Mario World do snesrev/smw, na revisão de 16 de julho de 2023 identificada abaixo. A compilação gera instruções MIPS para o Emotion Engine do PS2. A função `SmwRunOneFrameOfGame` executa a lógica do jogo. A imagem e o áudio dependem de componentes que reproduzem o comportamento do SNES, entre eles sua unidade gráfica (PPU), as transferências DMA/HDMA e o processador de sinais de áudio (DSP). A cópia do cartucho fornece os dados que o motor consulta durante o jogo. [Revisão exata do motor](https://github.com/snesrev/smw/tree/bd80629bc968267239c4a2fb77fa96d0ed62da7b)

| Parte do repositório | Responsabilidade |
| --- | --- |
| `Fonte/engine/src/smw_*.c` | Lógica das fases e do mapa, rotinas por quadro e sequenciador de áudio do jogo |
| `Fonte/engine/src/snes/` | Comportamento do cartucho, da PPU, do DSP, dos controles e das transferências de dados |
| `Fonte/ps2/main.c` | Inicialização do console, leitura dos controles, envio de áudio e apresentação da imagem |
| `Fonte/ps2/game.c` | Ligação entre o motor, a memória do cartucho, os quadros e o salvamento |
| `Fonte/ps2/files.c` | Busca e validação da ROM, leitura do ZIP e recuperação do progresso |
| `Fonte/Makefile` e `Fonte/gerar_iso.py` | Compilação para o PS2 e criação opcional do CD com dados fornecidos pelo jogador |
| `APPS/SMW/title.cfg` e `testes/` | Entrada do aplicativo no OPL e percurso de regressão reproduzível no PC |

O ELF intermediário, antes da remoção dos símbolos, contém `SmwRunOneFrameOfGame`, `SmwDrawPpuFrame` e `SmwSpcPlayer_Create`. O vinculador descarta as rotinas do interpretador geral `cpu_runOpcode` e `snes_runCpu`. Essa distinção permite verificar, no próprio artefato compilado, como a lógica e os componentes de hardware se dividem.

## 2. Inicialização, controles, vídeo e áudio

O aplicativo reinicia o IOP, o processador de entrada e saída do PS2. Carrega os módulos de USB, armazenamento em bloco e FAT, abre as portas de controle com `sio2man` e `padman` e configura a saída sonora com `audsrv`. Para o som, tenta primeiro `rom0:LIBSD` e depois `freesd`. Uma falha na inicialização de áudio permite continuar em silêncio. Uma falha na inicialização do serviço de controle interrompe a abertura e mostra o erro.

| Recurso | Implementação da v0.1 |
| --- | --- |
| Vídeo do jogo | Imagem de 256 × 224 pixels, com 4 bytes por pixel, enviada ao Graphics Synthesizer por DMA |
| Saída do PS2 | NTSC entrelaçada, área de 640 × 448, dois buffers de vídeo e filtro de vizinho mais próximo |
| Áudio | PCM estéreo de 16 bits a 32 kHz. Cada passo produz 534 quadros estéreo, ou 2.136 bytes |
| Controle | As duas portas são consultadas. Os botões são convertidos nos 12 bits do controle SNES. Pares de direções opostas são neutralizados |

O laço usa um intervalo de 4.920.115 ciclos do contador do Emotion Engine, com alvo de aproximadamente 59,94 passos por segundo. Se o desenho atrasar, pode executar até três passos adicionais antes de apresentar a próxima imagem. A lógica, o controle, a geração de áudio, o HDMA e a interrupção que atualiza a barra de estado continuam nesses passos. O limite evita acumular atrasos indefinidamente. A taxa efetivamente sustentada no console ainda precisa de medição.

## 3. Identificação do cartucho e da mídia

O carregador aceita a edição americana com 524.288 bytes úteis. Também aceita uma imagem de 524.800 bytes e descarta os 512 bytes do cabeçalho de copiador. O CRC32 conferido pelo programa é `B19ED489`. Para conferência externa, o SHA-256 dos dados sem cabeçalho é `0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b`. Esses valores se aplicam à imagem do cartucho após descompactação.

O programa procura primeiro na pasta de inicialização, depois em `mass:/APPS/SMW/` e `mass0:/APPS/SMW/`. Quando o caminho de inicialização é desconhecido, há também uma tentativa em `cdrom0:/`. O leitor de CD aceita o sufixo ISO 9660 `;1`. Um ZIP pode conter uma imagem `.sfc` ou `.smc` em subpasta. Em caso de arquivo ausente, o aplicativo repete a busca até 25 vezes, com intervalos de 200 ms. Se um candidato existente tiver tamanho ou CRC incorreto, a tela indica esse arquivo. Nesse caso, confira primeiro a cópia que aparece na tela, mesmo se houver outra na pasta.

## 4. Como o progresso é gravado e recuperado

O port grava quando o jogo chama `RtlWriteSram`. Na instalação USB descrita no README, o destino é `APPS/SMW/` no pendrive. Quando o jogo vem do CD, o port cria essa pasta em `mass:` ao gravar. Um ELF que carregue os dados de outra pasta gravável usa a pasta onde encontrou o cartucho. A v0.1 usa o formato de progresso do jogo, com os três arquivos abaixo.

| Arquivo | Conteúdo | Quando é usado |
| --- | --- | --- |
| `SMW.SRM` | 2.048 bytes de SRAM bruta | Gravação principal, compatível com o formato bruto do SNES |
| `SMW.BAK` | 2.048 bytes de SRAM bruta | Cópia do progresso anterior quando uma gravação diferente substitui a principal |
| `SMW.TMP` | 2.056 bytes, com assinatura `SMWJ`, CRC32 e dados | Permite recuperar uma gravação interrompida, se o conteúdo estiver completo |

Antes de substituir o arquivo principal, o código conclui uma gravação pendente válida, grava e relê o temporário e, quando aplicável, atualiza o backup. Depois grava a SRAM principal, relê e compara os bytes e remove o temporário. Na abertura, a prioridade é temporário com CRC válido, arquivo principal com tamanho correto e, por último, backup. O formato bruto dos dois últimos permite detectar um corte no tamanho, mas uma alteração que mantenha os 2.048 bytes pode passar por essa verificação.

Se o dispositivo recusar a gravação, aparece **ERRO AO SALVAR**. Uma gravação posterior bem-sucedida limpa a mensagem. Se houver um `SMW.TMP` após uma interrupção, deixe o arquivo na pasta para que o carregador possa verificá-lo. O memory card e os save states não fazem parte do sistema de progresso da v0.1.

## 5. Correções específicas da adaptação

- **Acesso à memória no R5900.** O jogo original lê palavras em endereços ímpares. O port marca os tipos usados nesses acessos com alinhamento de um byte e ajusta estruturas de coordenadas, como `PointU16`. A limpeza do fundo em `ppu.c` usa `memcpy` para suportar endereços sem alinhamento de 64 bits.
- **Mapa do mundo.** A rotina de sprites ignora posições vazias antes de consultar uma tabela que começa no identificador 1. A movimentação no mapa consulta a tabela do cartucho no endereço LoROM `0x049FEB`, preservando entradas que ultrapassavam a tabela parcial representada em C.
- **Canais de cor.** A geração da textura coloca o vermelho no byte baixo, na ordem esperada pelo Graphics Synthesizer. O teste de imagem descrito a seguir também acusa alterações nos bytes finais dos pixels.

## 6. Resultados e reprodução

| Ensaio | Procedimento e resultado observado |
| --- | --- |
| Compilação limpa | Linux x86-64, ps2dev v2.0.0 e GCC 15.2.0 para R5900. O ELF final tem 1.037.876 bytes, é ligado estaticamente, usa little-endian e tem ponto de entrada `0x00101930`. A comparação com o executável da v0.1 foi idêntica byte a byte. |
| Inicialização do PS2 emulado | ISO recém-gerada com pycdlib 1.22.0, ParaLLEl PS2 `v2.0.0-2793f43`, OpenPS2BIOS 0.6. O `SYSTEM.CNF` iniciou `cdrom0:\SMWG_000.01;1`. O teste chegou a Yoshi’s Island 1 (modo `0x14`, fase `0x29`), recebeu comandos de corrida e salto e entregou 1.627 blocos com áudio diferente de zero. Isso confirma saída de amostras nessa execução. |
| Percurso automatizado no PC | Duas execuções de 1.930 passos usaram as mesmas entradas. Uma desenhou todos os passos e a outra desenhou passos alternados. Ambas terminaram com Mario em `x = 253`, atingiram `y = 277` no salto, produziram som em 1.594 passos e deram o mesmo CRC32 de imagem, `D1AC4CE4`. O percurso passou com AddressSanitizer e verificação de alinhamento. |
| Carregamento e gravação no PC | Foram exercitados ZIP, SFC bruto, SMC com cabeçalho, entradas truncadas e CRC incorreto. A recuperação passou com arquivo principal ausente ou parcial, temporário íntegro ou corrompido, backup e falha na abertura do temporário. Pastas locais representaram as rotas de CD e USB. |

O SHA-256 do **ELF do pacote jogável de referência da v0.1**, reproduzido a partir destes fontes, é `6b6f470861a913b7114ddd7426912b822a50a0d8f9d9f8e5b283c9a206e27a2e`. A comparação usa o executável porque o ZIP do cartucho e os metadados de criação do CD podem mudar os bytes da ISO. Os comandos para recompilar o ELF, gerar a ISO local e repetir o percurso no PC estão no [README do projeto](README.md).

## 7. Validação de bancada em aberto

O emulador iniciou pelo dispositivo de CD e estava sem USB gravável. A montagem de uma ISO pelo OPL, a gravação física, a estabilidade em sessões longas, a campanha completa e o modo de dois jogadores precisam de testes no console. A v0.1 configura saída NTSC. Compatibilidade com a instalação específica do OPL e com a tela usada na bancada deve ser registrada em cada teste.

| Etapa no PS2 físico | Evidência a registrar |
| --- | --- |
| Iniciar `SMW.ELF` pelo wLaunchELF e depois por Aplicativos no OPL | Modelo SCPH, versão ou fork do OPL, identificação do pendrive e resultado de cada abertura |
| Iniciar a ISO na lista USB/BDM do OPL | Última mensagem exibida, entrada no mapa e chegada a uma fase |
| Completar um ponto com opção de salvar e reiniciar o jogo | Mesmo arquivo de jogo reabre com o progresso esperado. Conferir `SMW.SRM` com 2.048 bytes e a mensagem de erro, caso apareça |
| Jogar com dois controles e avançar por fases diferentes | Troca entre Mario e Luigi, som contínuo, defeitos de imagem e locais de lentidão |

## 8. Créditos e referências

A origem do motor é snesrev/smw, com componentes provenientes de LakeSnes. O aviso do motor preserva os créditos de snesrev e elzo_d. A pasta de licenças reúne também os textos do PS2SDK, gsKit, newlib, zlib, MiniZip, FatFs e audsrv. A ROM, a BIOS e a imagem de CD pessoal ficam fora do pacote de fontes. [Avisos incluídos](Licencas/) · [snesrev/smw](https://github.com/snesrev/smw) · [LakeSnes](https://github.com/elzo-d/LakeSnes)

As versões e interfaces de desenvolvimento podem ser consultadas nos projetos correspondentes. [ps2dev v2.0.0](https://github.com/ps2dev/ps2dev/releases/tag/v2.0.0) · [PS2SDK](https://github.com/ps2dev/ps2sdk) · [gsKit](https://github.com/ps2dev/gsKit) · [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader)
