"""Gera o Gravador_SMW_PS2.html: um arquivo só, que abre em qualquer navegador,
com as telas, o núcleo, as fontes e o SMW.ELF embutidos. Não usa a ROM.

Uso (na raiz do repositório, depois de compilar o ELF):
    python3 Gravador/gerar_gravador.py [saida.html]
"""
import base64
import hashlib
import re
import sys
from pathlib import Path

PASTA = Path(__file__).resolve().parent
RAIZ = PASTA.parent


def fonte(nome, familia):
    dados = base64.b64encode((PASTA / "fontes" / nome).read_bytes()).decode()
    return (f"@font-face {{ font-family: '{familia}'; font-style: normal; font-weight: 400; "
            f"font-display: block; src: url(data:font/woff2;base64,{dados}) format('woff2'); }}")


def main():
    saida = Path(sys.argv[1]) if len(sys.argv) > 1 else RAIZ / "Gravador_SMW_PS2.html"
    elf = (RAIZ / "APPS" / "SMW" / "SMW.ELF").read_bytes()
    main_c = (RAIZ / "Fonte" / "ps2" / "main.c").read_text(encoding="utf-8")
    versao = re.search(r'smw_intro\(gs, video\.p480, sound, "([^"]+)"', main_c).group(1)
    sha = hashlib.sha256(elf).hexdigest()
    b64 = base64.b64encode(elf).decode()
    b64 = "\n".join(b64[i:i + 120] for i in range(0, len(b64), 120))

    fontes = "\n".join([
        "/* Press Start 2P e VT323: SIL Open Font License 1.1 (Gravador/fontes) */",
        fonte("PressStart2P.woff2", "Press Start 2P"),
        fonte("VT323.woff2", "VT323"),
    ])
    html = (PASTA / "src" / "gravador.html").read_text(encoding="utf-8")
    trocas = {
        "/*@@FONTES@@*/": fontes,
        "/*@@CSS@@*/": (PASTA / "src" / "gravador.css").read_text(encoding="utf-8"),
        "/*@@NUCLEO@@*/": (PASTA / "src" / "nucleo.js").read_text(encoding="utf-8"),
        "/*@@TELA@@*/": (PASTA / "src" / "tela.js").read_text(encoding="utf-8"),
        "@@ELF_B64@@": b64,
        "@@ELF_SHA@@": sha,
        "@@VERSAO@@": versao,
    }
    for marca, valor in trocas.items():
        if marca not in html:
            raise SystemExit(f"marca {marca} não encontrada no modelo")
        html = html.replace(marca, valor)
    if "@@" in html.replace("@@ELF", ""):
        raise SystemExit("sobrou marca sem trocar")
    saida.write_text(html, encoding="utf-8", newline="\n")
    print(f"{saida} ({saida.stat().st_size // 1024} KB, jogo v{versao}, ELF {sha[:16]}...)")


if __name__ == "__main__":
    main()
