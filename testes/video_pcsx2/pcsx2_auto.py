#!/usr/bin/env python3
"""Roda o port no PCSX2 sem tela (Xvfb), aperta botões por script e tira capturas.

Usa uma configuração do PCSX2 só para o teste (não mexe na do usuário):
renderizador por software, captura na resolução interna sem correção de
proporção (a imagem que o GS do PS2 manda para a TV), Memory Card novo a
cada rodada (o jogo sempre começa na tela AJUSTES).

    python3 pcsx2_auto.py --iso JOGO.iso --saida PASTA --roteiro ROTEIRO
    python3 pcsx2_auto.py --elf SMW.ELF --saida PASTA --roteiro ROTEIRO

--elf roda o ELF direto, com o HostFS ligado (host: = pasta do ELF; o jogo
precisa estar nessa pasta). --cartao ARQUIVO copia o Memory Card 1
(Mcd001.ps2) para ARQUIVO no fim, para ler o que o jogo gravou (por exemplo
com o mymcplus: SMWPS2/SMW_LOG.TXT). --cartao-inicial ARQUIVO usa uma cópia
desse cartão no lugar do novo (o PCSX2 cria o cartão novo sem formatação;
um formatado sai com "mymcplus cartao.ps2 format"). --audio grava o som que
o SPU2 gera em WAV: "tecla:gravar" liga e desliga a captura, e cada trecho
sai como audio_NN.wav na pasta de saída.

ROTEIRO é uma lista de passos separados por vírgula:
    esperar:SEG    espera SEG segundos
    tecla:NOME     aperta e solta (NOME = cima baixo esquerda direita x
                   circulo quadrado triangulo start select l2 r2, e gravar
                   com --audio); juntas com "+": tecla:l2+r2+x
    foto:NOME      captura a tela como NOME.png (tecla de captura do PCSX2)
    raiz:NOME      captura a tela inteira do Xvfb (para diagnóstico)
    janelas        lista as janelas (para diagnóstico)
    filme:NOME:N:SEG  N capturas, uma a cada SEG segundos (NOME_00.png ...)
"""
import argparse
import configparser
import os
import shutil
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

from Xlib import X, XK, display
from Xlib.ext import xtest

HOME = Path.home()
PCSX2 = HOME / "work/squashfs-root/AppRun"
INI_USUARIO = HOME / ".config/PCSX2/inis/PCSX2.ini"
BIOS = HOME / ".config/PCSX2/bios"

# Botões do controle -> teclas do Pad1 do PCSX2 do usuário.
TECLAS = {"cima": "Up", "baixo": "Down", "esquerda": "Left", "direita": "Right",
          "x": "k", "circulo": "l", "quadrado": "j", "triangulo": "i",
          "start": "Return", "select": "BackSpace", "l2": "1", "r2": "3",
          "foto": "F8", "gravar": "F9"}


def preparar_config(base: Path, hostfs=False, audio=False) -> Path:
    """Copia o PCSX2.ini do usuário e troca só o necessário para o teste."""
    cfg = base / "config"
    inis = cfg / "PCSX2/inis"
    inis.mkdir(parents=True, exist_ok=True)
    (base / "memcards").mkdir(exist_ok=True)
    (base / "snaps").mkdir(exist_ok=True)
    texto = INI_USUARIO.read_text(encoding="utf-8")
    ini = configparser.RawConfigParser(strict=False, interpolation=None)
    ini.optionxform = str
    ini.read_string(texto)
    trocas = {
        "UI": {"ConfirmShutdown": "false", "PauseOnFocusLoss": "false", "StartFullscreen": "false",
               "SetupWizardIncomplete": "false", "StartPaused": "false"},
        "Folders": {"Bios": str(BIOS), "MemoryCards": str(base / "memcards"),
                    "Snapshots": str(base / "snaps")},
        "EmuCore": {"EnableFastBoot": "true", "HostFs": "true" if hostfs else "false"},
        "EmuCore/GS": {"Renderer": "13", "ScreenshotSize": "2", "ScreenshotFormat": "0",
                       "deinterlace_mode": "1", "linear_present_mode": "0",
                       "upscale_multiplier": "1", "OrganizeScreenshotsByGame": "false"},
        "MemoryCards": {"Slot1_Enable": "true", "Slot1_Filename": "Mcd001.ps2"},
        "Hotkeys": {"Screenshot": "Keyboard/F8"},
        # Sem som: no Xvfb não há saída de áudio e o aviso de erro roubaria o foco.
        "SPU2/Output": {"Backend": "Null"},
    }
    if audio:
        # Captura só do som (o que o SPU2 do PS2 gera, antes da saída Null), em WAV,
        # numa pasta da rodada. Sem --audio a tecla de captura nem é configurada.
        (base / "capturas").mkdir(exist_ok=True)
        trocas["Hotkeys"]["ToggleVideoCapture"] = "Keyboard/F9"
        trocas["Folders"]["Videos"] = str(base / "capturas")
        trocas["EmuCore/GS"].update({"EnableVideoCapture": "false", "EnableAudioCapture": "true",
                                     "CaptureContainer": "wav", "AudioCaptureCodec": "pcm_s16le",
                                     "EnableAudioCaptureParameters": "false",
                                     "OrganizeVideoCaptureByGame": "false"})
    for secao, chaves in trocas.items():
        if not ini.has_section(secao):
            ini.add_section(secao)
        for k, v in chaves.items():
            ini.set(secao, k, v)
    with open(inis / "PCSX2.ini", "w", encoding="utf-8") as f:
        ini.write(f, space_around_delimiters=True)
    return cfg


def abrir_tela():
    """Abre um Xvfb e deixa ele mesmo escolher um display livre (-displayfd):
    duas rodadas ao mesmo tempo nunca dividem a mesma tela."""
    ler, escrever = os.pipe()
    p = subprocess.Popen(["Xvfb", "-displayfd", str(escrever), "-screen", "0", "1600x1000x24",
                          "-nolisten", "tcp"], pass_fds=(escrever,),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(escrever)
    with os.fdopen(ler) as f:
        numero = f.readline().strip()
    if not numero.isdigit():
        p.kill()
        p.wait()
        raise SystemExit("Não consegui abrir o Xvfb.")
    return f":{numero}", p


# Começo do título da janela de jogo: "SMWG..." (código do disco) ou o nome do ELF.
TITULOS = ["SMWG"]


def janela_pcsx2(d):
    """A janela de jogo do PCSX2 (título em TITULOS)."""
    for w in d.screen().root.query_tree().children:
        try:
            nome = w.get_wm_name() or ""
            if nome.startswith(tuple(TITULOS)) and w.get_attributes().map_state == X.IsViewable:
                return w
        except Exception:
            pass
    return None


def apertar(d, nome, segura=0.12):
    w = janela_pcsx2(d)
    if w is not None:
        w.set_input_focus(X.RevertToParent, X.CurrentTime)
        g = w.get_geometry()
        xtest.fake_input(d, X.MotionNotify, x=g.x + g.width // 2, y=g.y + g.height // 2)
        d.sync()
    codigos = [d.keysym_to_keycode(XK.string_to_keysym(TECLAS[n])) for n in nome.split("+")]
    for codigo in codigos:  # várias juntas (l2+r2+x): aperta na ordem, solta ao contrário
        xtest.fake_input(d, X.KeyPress, codigo)
        d.sync()
        time.sleep(0.05 if len(codigos) > 1 else 0)
    time.sleep(segura)
    for codigo in reversed(codigos):
        xtest.fake_input(d, X.KeyRelease, codigo)
        d.sync()


def foto(d, snaps: Path, saida: Path, nome: str):
    antes = set(snaps.glob("*.png"))
    apertar(d, "foto")
    for _ in range(40):
        time.sleep(0.1)
        novos = set(snaps.glob("*.png")) - antes
        if novos:
            time.sleep(0.3)
            shutil.move(str(novos.pop()), saida / f"{nome}.png")
            return True
    print(f"  ! sem captura para {nome}", file=sys.stderr)
    return False


# Quantos argumentos cada passo do roteiro leva.
ARGUMENTOS = {"esperar": 1, "tecla": 1, "foto": 1, "raiz": 1, "janelas": 0, "filme": 3}


def inicio_dos_dados(arq: Path) -> int:
    """Posição do primeiro byte de som do WAV (logo depois do cabeçalho do bloco "data")."""
    with open(arq, "rb") as f:
        f.seek(12)  # "RIFF", tamanho, "WAVE"
        while True:
            cab = f.read(8)
            if len(cab) < 8:
                return f.tell()
            nome, tamanho = cab[:4], int.from_bytes(cab[4:], "little")
            if nome == b"data":
                return f.tell()
            f.seek(tamanho + (tamanho & 1), os.SEEK_CUR)


def conferir_wav(arq: Path) -> str:
    """"som", "vazio" (só o cabeçalho) ou o problema do arquivo.

    Captura que não foi desligada com tecla:gravar antes do fim fica com dados,
    mas sem o tamanho no cabeçalho (0 amostras), ou nem abre; arquivo cortado
    tem menos dados que o cabeçalho diz. Ao desligar, o PCSX2 às vezes abre e
    fecha na hora um "_part002" só com o cabeçalho: esse é "vazio"."""
    try:
        with wave.open(str(arq), "rb") as w:
            amostras, quadro = w.getnframes(), w.getsampwidth() * w.getnchannels()
    except (wave.Error, EOFError) as erro:
        return f"não abre como WAV ({erro})"
    dados = arq.stat().st_size - inicio_dos_dados(arq)
    if amostras and dados < amostras * quadro:
        return f"cortado: o cabeçalho diz {amostras * quadro} bytes de som e o arquivo tem {dados}"
    if amostras:
        return "som"
    if dados > 0:
        return "com dados, mas sem o tamanho no cabeçalho (desligue com tecla:gravar antes do fim)"
    return "vazio"


def ler_roteiro(texto):
    passos = [passo.strip().split(":") for passo in texto.split(",") if passo.strip()]
    for partes in passos:
        nome, args = partes[0], partes[1:]
        if nome not in ARGUMENTOS or len(args) != ARGUMENTOS[nome]:
            raise SystemExit(f"Passo inválido no roteiro: {':'.join(partes)}")
        if nome == "tecla" and (any(t not in TECLAS for t in args[0].split("+"))
                                or ("+" in args[0] and "gravar" in args[0].split("+"))):
            raise SystemExit(f"Tecla desconhecida: {args[0]}")
        try:
            if nome == "esperar":
                ok = 0 <= float(args[0]) < 3600
            elif nome == "filme":
                ok = 0 < int(args[1]) <= 1000 and 0 <= float(args[2]) < 3600
            else:
                ok = all(args) and not any("/" in a or a.startswith(".") for a in args)
        except ValueError:
            ok = False
        if not ok:
            raise SystemExit(f"Passo inválido no roteiro: {':'.join(partes)}")
    return passos


def main():
    ap = argparse.ArgumentParser()
    origem = ap.add_mutually_exclusive_group(required=True)
    origem.add_argument("--iso", type=Path)
    origem.add_argument("--elf", type=Path)
    ap.add_argument("--cartao", type=Path)
    ap.add_argument("--cartao-inicial", type=Path)
    ap.add_argument("--audio", action="store_true")
    ap.add_argument("--saida", required=True, type=Path)
    ap.add_argument("--roteiro", required=True)
    ap.add_argument("--trabalho", type=Path, default=Path("/tmp/smw_teste_video"))
    a = ap.parse_args()
    jogo = (a.iso or a.elf).resolve()
    if not jogo.is_file():
        raise SystemExit(f"Arquivo não encontrado: {jogo}")
    if a.elf:
        TITULOS.append(jogo.name)
    if a.cartao and a.cartao.exists():
        raise SystemExit(f"O arquivo do cartão já existe: {a.cartao}")
    if a.cartao and not a.cartao.resolve().parent.is_dir():
        raise SystemExit(f"A pasta do arquivo do cartão não existe: {a.cartao.parent}")
    if a.cartao_inicial and not a.cartao_inicial.is_file():
        raise SystemExit(f"Cartão inicial não encontrado: {a.cartao_inicial}")
    passos = ler_roteiro(a.roteiro)
    gravar = sum(1 for p in passos if p[0] == "tecla" and p[1] == "gravar")
    if not a.audio and gravar:
        raise SystemExit("tecla:gravar só funciona com --audio.")
    if a.audio and (gravar == 0 or gravar % 2):
        raise SystemExit("Com --audio, o roteiro liga e desliga a captura: tecla:gravar um número par de vezes.")
    # As capturas desta rodada vão para uma pasta nova; capturas velhas não se misturam.
    if a.saida.exists() and any(a.saida.iterdir()):
        raise SystemExit(f"A pasta de saída já tem arquivos: {a.saida}")
    a.saida.mkdir(parents=True, exist_ok=True)
    try:
        # Criação exclusiva: se outra rodada já pegou esta pasta, esta para aqui.
        log = open(a.saida / "pcsx2.log", "x")
    except FileExistsError:
        raise SystemExit(f"Outra rodada já usa a pasta de saída: {a.saida}")
    a.trabalho.mkdir(parents=True, exist_ok=True)
    falhas = 0
    # Pasta de trabalho exclusiva desta rodada (config, Memory Card, capturas);
    # o TemporaryDirectory apaga só ela, mesmo se algo der erro.
    with log, tempfile.TemporaryDirectory(prefix="rodada_", dir=a.trabalho) as tmp:
        base = Path(tmp)
        cfg = preparar_config(base, hostfs=a.elf is not None, audio=a.audio)
        if a.cartao_inicial:
            shutil.copyfile(a.cartao_inicial, base / "memcards" / "Mcd001.ps2")
        tela, xvfb = abrir_tela()
        p = None
        try:
            # Só o Vulkan por software (lavapipe): o Xvfb não tem DRI3 para a GPU, e com
            # o lavapipe o PCSX2 apresenta o renderizador por software pelo OpenGL do Mesa.
            env = dict(os.environ, DISPLAY=tela, XDG_CONFIG_HOME=str(cfg), QT_QPA_PLATFORM="xcb",
                       LANG="C.UTF-8", LC_ALL="C.UTF-8",
                       VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/lvp_icd.json",
                       VK_DRIVER_FILES="/usr/share/vulkan/icd.d/lvp_icd.json")
            origem = ["-elf", str(jogo)] if a.elf else ["--", str(jogo)]
            p = subprocess.Popen([str(PCSX2), "-batch", "-nogui", "-fastboot", *origem],
                                 env=env, stdout=log, stderr=subprocess.STDOUT)
            d = display.Display(tela)
            t0 = time.time()
            for partes in passos:
                if partes[0] == "esperar":
                    time.sleep(float(partes[1]))
                elif partes[0] == "tecla":
                    apertar(d, partes[1])
                    time.sleep(0.35)
                elif partes[0] == "foto":
                    falhas += not foto(d, base / "snaps", a.saida, partes[1])
                elif partes[0] == "raiz":
                    r = subprocess.run(["import", "-display", tela, "-window", "root",
                                        str(a.saida / f"{partes[1]}.png")])
                    if r.returncode != 0:
                        print(f"  ! captura da tela inteira falhou: {partes[1]}", file=sys.stderr)
                        falhas += 1
                elif partes[0] == "janelas":
                    listar_janelas(d)
                elif partes[0] == "filme":
                    for i in range(int(partes[2])):
                        falhas += not foto(d, base / "snaps", a.saida, f"{partes[1]}_{i:02d}")
                        time.sleep(float(partes[3]))
                print(f"{time.time() - t0:6.1f}s  {':'.join(partes)}", flush=True)
                # Confere depois de cada passo: se o PCSX2 caiu, a rodada falhou.
                if p.poll() is not None:
                    print("PCSX2 fechou antes do fim do roteiro", file=sys.stderr)
                    falhas += 1
                    break
        finally:
            if p is not None:
                p.terminate()
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
            xvfb.terminate()
            try:
                xvfb.wait(5)
            except subprocess.TimeoutExpired:
                xvfb.kill()
                xvfb.wait()
            # Log do PCSX2 (inclui o printf do jogo: "SMW: ...").
            emulog = cfg / "PCSX2" / "logs" / "emulog.txt"
            if emulog.is_file():
                shutil.copyfile(emulog, a.saida / "emulog.txt")
            if a.audio:
                # "tecla:gravar" (F9) liga e desliga a captura do som.
                capturas = sorted(c for c in (base / "capturas").glob("*") if c.is_file())
                com_som = 0
                for i, arq in enumerate(capturas):
                    destino = a.saida / f"audio_{i:02d}{arq.suffix}"
                    shutil.copyfile(arq, destino)
                    situacao = conferir_wav(destino)
                    if situacao == "som":
                        com_som += 1
                    elif situacao == "vazio":
                        print(f"  {destino.name}: só o cabeçalho, sem som (ignorado)")
                    else:
                        print(f"  ! {destino.name}: {situacao}", file=sys.stderr)
                        falhas += 1
                if not com_som:
                    print("  ! o PCSX2 não gravou o som (falta tecla:gravar?)", file=sys.stderr)
                    falhas += 1
            cartao = base / "memcards" / "Mcd001.ps2"
            if a.cartao and cartao.is_file():
                # Com o PCSX2 fechado o cartão já foi gravado no disco.
                # Copia para um temporário na mesma pasta e só então publica com
                # os.link, que não sobrescreve: nem cópia pela metade, nem duas
                # rodadas gravando o mesmo arquivo.
                parcial = a.cartao.with_name(a.cartao.name + f".parcial{os.getpid()}")
                try:
                    with open(cartao, "rb") as origem_cartao, open(parcial, "xb") as destino:
                        shutil.copyfileobj(origem_cartao, destino)
                    os.link(parcial, a.cartao)
                except OSError as e:
                    print(f"  ! não consegui guardar o cartão em {a.cartao}: {e}", file=sys.stderr)
                    falhas += 1
                finally:
                    parcial.unlink(missing_ok=True)
            elif a.cartao:
                print(f"  ! o PCSX2 não criou {cartao.name}", file=sys.stderr)
                falhas += 1
    if falhas:
        raise SystemExit(f"{falhas} falha(s) nesta rodada")


def listar_janelas(d):
    pilha = [(d.screen().root, 0)]
    while pilha:
        w, nivel = pilha.pop()
        try:
            g, at = w.get_geometry(), w.get_attributes()
            print("  " * nivel + f"{w.id:#x} {g.width}x{g.height}+{g.x}+{g.y} "
                  f"visivel={at.map_state == X.IsViewable} {w.get_wm_name()!r}")
            pilha += [(f, nivel + 1) for f in w.query_tree().children]
        except Exception as e:
            print("  " * nivel + f"erro: {e}")


if __name__ == "__main__":
    main()
