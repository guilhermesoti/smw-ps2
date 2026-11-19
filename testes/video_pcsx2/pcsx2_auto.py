#!/usr/bin/env python3
"""Roda o port no PCSX2 sem tela (Xvfb), aperta botões por script e tira capturas.

Usa uma configuração do PCSX2 só para o teste (não mexe na do usuário):
renderizador por software, captura na resolução interna sem correção de
proporção (a imagem que o GS do PS2 manda para a TV), Memory Card novo a
cada rodada (o jogo sempre começa na tela AJUSTES).

    python3 pcsx2_auto.py --iso JOGO.iso --saida PASTA --roteiro ROTEIRO

ROTEIRO é uma lista de passos separados por vírgula:
    esperar:SEG    espera SEG segundos
    tecla:NOME     aperta e solta (NOME = cima baixo esquerda direita x
                   circulo quadrado triangulo start select l2 r2)
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
          "foto": "F8"}


def preparar_config(base: Path) -> Path:
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
        "EmuCore": {"EnableFastBoot": "true", "HostFs": "false"},
        "EmuCore/GS": {"Renderer": "13", "ScreenshotSize": "2", "ScreenshotFormat": "0",
                       "deinterlace_mode": "1", "linear_present_mode": "0",
                       "upscale_multiplier": "1", "OrganizeScreenshotsByGame": "false"},
        "MemoryCards": {"Slot1_Enable": "true", "Slot1_Filename": "Mcd001.ps2"},
        "Hotkeys": {"Screenshot": "Keyboard/F8"},
        # Sem som: no Xvfb não há saída de áudio e o aviso de erro roubaria o foco.
        "SPU2/Output": {"Backend": "Null"},
    }
    for secao, chaves in trocas.items():
        if not ini.has_section(secao):
            ini.add_section(secao)
        for k, v in chaves.items():
            ini.set(secao, k, v)
    with open(inis / "PCSX2.ini", "w", encoding="utf-8") as f:
        ini.write(f, space_around_delimiters=True)
    return cfg


def abrir_tela():
    """Abre um Xvfb num número de display livre (cada rodada tem o seu)."""
    for n in range(77, 120):
        if not Path(f"/tmp/.X11-unix/X{n}").exists() and not Path(f"/tmp/.X{n}-lock").exists():
            tela = f":{n}"
            p = subprocess.Popen(["Xvfb", tela, "-screen", "0", "1600x1000x24", "-nolisten", "tcp"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(50):
                time.sleep(0.1)
                try:
                    display.Display(tela).close()
                    return tela, p
                except Exception:
                    if p.poll() is not None:
                        break
            p.kill()
            p.wait()
    raise SystemExit("Não consegui abrir o Xvfb.")


def janela_pcsx2(d):
    """A janela de jogo do PCSX2: a de título "SMWG..." (o código do disco)."""
    for w in d.screen().root.query_tree().children:
        try:
            nome = w.get_wm_name() or ""
            if nome.startswith("SMWG") and w.get_attributes().map_state == X.IsViewable:
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
    codigo = d.keysym_to_keycode(XK.string_to_keysym(TECLAS[nome]))
    xtest.fake_input(d, X.KeyPress, codigo)
    d.sync()
    time.sleep(segura)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True, type=Path)
    ap.add_argument("--saida", required=True, type=Path)
    ap.add_argument("--roteiro", required=True)
    ap.add_argument("--trabalho", type=Path, default=Path("/tmp/smw_teste_video"))
    a = ap.parse_args()
    if not a.iso.is_file():
        raise SystemExit(f"ISO não encontrada: {a.iso}")
    passos = [passo.strip().split(":") for passo in a.roteiro.split(",") if passo.strip()]
    for partes in passos:
        if partes[0] not in ("esperar", "tecla", "foto", "raiz", "janelas", "filme"):
            raise SystemExit(f"Passo desconhecido no roteiro: {':'.join(partes)}")
        if partes[0] == "tecla" and partes[1] not in TECLAS:
            raise SystemExit(f"Tecla desconhecida: {partes[1]}")
    # Pasta de trabalho exclusiva desta rodada (config, Memory Card, capturas).
    a.trabalho.mkdir(parents=True, exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix="rodada_", dir=a.trabalho))
    # As capturas desta rodada vão para uma pasta nova; capturas velhas não se misturam.
    if a.saida.exists() and any(a.saida.iterdir()):
        raise SystemExit(f"A pasta de saída já tem arquivos: {a.saida}")
    a.saida.mkdir(parents=True, exist_ok=True)
    cfg = preparar_config(base)
    falhas = 0
    tela, xvfb = abrir_tela()
    p = None
    try:
        # Só o Vulkan por software (lavapipe): o Xvfb não tem DRI3 para a GPU, e com
        # o lavapipe o PCSX2 apresenta o renderizador por software pelo OpenGL do Mesa.
        env = dict(os.environ, DISPLAY=tela, XDG_CONFIG_HOME=str(cfg), QT_QPA_PLATFORM="xcb",
                   LANG="C.UTF-8", LC_ALL="C.UTF-8",
                   VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/lvp_icd.json",
                   VK_DRIVER_FILES="/usr/share/vulkan/icd.d/lvp_icd.json")
        with open(a.saida / "pcsx2.log", "w") as log:
            p = subprocess.Popen([str(PCSX2), "-batch", "-nogui", "-fastboot", "--", str(a.iso)],
                                 env=env, stdout=log, stderr=subprocess.STDOUT)
            d = display.Display(tela)
            t0 = time.time()
            for partes in passos:
                if p.poll() is not None:
                    print("PCSX2 fechou antes do fim do roteiro", file=sys.stderr)
                    falhas += 1
                    break
                if partes[0] == "esperar":
                    time.sleep(float(partes[1]))
                elif partes[0] == "tecla":
                    apertar(d, partes[1])
                    time.sleep(0.35)
                elif partes[0] == "foto":
                    falhas += not foto(d, base / "snaps", a.saida, partes[1])
                elif partes[0] == "raiz":
                    subprocess.run(["import", "-display", tela, "-window", "root",
                                    str(a.saida / f"{partes[1]}.png")])
                elif partes[0] == "janelas":
                    listar_janelas(d)
                elif partes[0] == "filme":
                    for i in range(int(partes[2])):
                        falhas += not foto(d, base / "snaps", a.saida, f"{partes[1]}_{i:02d}")
                        time.sleep(float(partes[3]))
                print(f"{time.time() - t0:6.1f}s  {':'.join(partes)}", flush=True)
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
        shutil.rmtree(base, ignore_errors=True)
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
