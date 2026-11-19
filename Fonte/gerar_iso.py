"""Gera a imagem de CD do PS2. Requer Python 3 e pycdlib.

    python3 Fonte/gerar_iso.py [SMW.ELF] [jogo.zip] [saida.iso]

Sem argumentos usa APPS/SMW/SMW.ELF, APPS/SMW/Super Mario World (USA).zip e
grava CD/SMWG_000.01.Super Mario World.iso. Com a pasta do projeto
organizada (Codigo, ROM, "ELF OU ISO TESTE"), rodando de dentro de Codigo:

    python3 Fonte/gerar_iso.py APPS/SMW/SMW.ELF "../ROM/Super Mario World (USA).zip" \
        "../ELF OU ISO TESTE/SMWG_000.01.Super Mario World.iso"
"""
from io import BytesIO
import sys
from pathlib import Path
from tempfile import NamedTemporaryFile

import pycdlib

def main():
    raiz = Path(__file__).resolve().parent.parent
    app = raiz / "APPS" / "SMW"
    destino = raiz / "CD" / "SMWG_000.01.Super Mario World.iso"
    elf = app / "SMW.ELF"
    jogo = app / "Super Mario World (USA).zip"
    args = sys.argv[1:]
    if len(args) > 3:
        raise SystemExit("Uso: gerar_iso.py [SMW.ELF] [jogo.zip] [saida.iso]")
    if len(args) > 0: elf = Path(args[0])
    if len(args) > 1: jogo = Path(args[1])
    if len(args) > 2: destino = Path(args[2])
    if not elf.is_file() or not jogo.is_file():
        raise SystemExit(f"Não achei {elf} ou {jogo}.")
    boot = b"BOOT2 = cdrom0:\\SMWG_000.01;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"
    destino.parent.mkdir(parents=True, exist_ok=True)
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, vol_ident="SUPER_MARIO_WORLD", sys_ident="PLAYSTATION")
    temporario = None
    try:
        iso.add_fp(BytesIO(boot), len(boot), iso_path="/SYSTEM.CNF;1")
        iso.add_file(str(elf), iso_path="/SMWG_000.01;1")
        iso.add_file(str(jogo), iso_path="/SMW.ZIP;1")
        with NamedTemporaryFile(dir=destino.parent, delete=False) as saida:
            temporario = Path(saida.name)
            iso.write_fp(saida)
        temporario.replace(destino)
    finally:
        iso.close()
        if temporario is not None:
            temporario.unlink(missing_ok=True)
    print(destino)

if __name__ == "__main__":
    main()
