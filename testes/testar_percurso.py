"""Repete, em Linux x86-64, o percurso de regressão do motor da v0.1."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jogo", type=Path, help="Cópia americana em ZIP, SFC ou SMC")
    parser.add_argument("--sanitizar", action="store_true",
                        help="Ativa AddressSanitizer e a verificação de alinhamento")
    args = parser.parse_args()
    nomes = {".zip": "SMW.ZIP", ".sfc": "SMW.SFC", ".smc": "SMW.SMC"}
    nome = nomes.get(args.jogo.suffix.lower())
    if nome is None or not args.jogo.is_file():
        parser.error("Informe um arquivo .zip, .sfc ou .smc existente.")
    if sys.byteorder != "little":
        parser.error("O CRC de referência foi medido com pixels em little-endian.")

    testes = Path(__file__).resolve().parent
    fonte = testes.parent / "Fonte"
    engine = fonte / "engine"
    integracao = fonte / "ps2"
    unidades = sorted((engine / "src").rglob("*.c"))
    unidades += [integracao / "files.c", integracao / "game.c", testes / "percurso.c"]
    cc = os.environ.get("CC", "cc")
    cflags = ["-O1", "-g", "-std=gnu11", "-fno-strict-aliasing", "-fwrapv",
              "-ffunction-sections", "-fdata-sections"]
    if args.sanitizar:
        cflags += ["-fsanitize=address,alignment", "-fno-sanitize-recover=all"]
    pkg = ["pkg-config", "minizip", "zlib"]
    includes = shlex.split(subprocess.check_output([*pkg, "--cflags"], text=True))
    libs = shlex.split(subprocess.check_output([*pkg, "--libs"], text=True))
    env = os.environ.copy()
    if args.sanitizar:
        # O motor mantém suas alocações até a saída, como no loop do console.
        env["ASAN_OPTIONS"] = ":".join(filter(None, [env.get("ASAN_OPTIONS"), "detect_leaks=0"]))

    with TemporaryDirectory(prefix="smw-ps2-") as temp:
        pasta = Path(temp)
        binario = pasta / "percurso"
        subprocess.run([cc, *cflags, *includes, "-I", engine, "-I", integracao,
                        *unidades, "-Wl,--gc-sections", *libs, "-lm", "-o", binario],
                       check=True)
        for modo in ("completo", "alternado"):
            rodada = pasta / modo
            rodada.mkdir()
            shutil.copyfile(args.jogo, rodada / nome)
            subprocess.run([binario, rodada, modo], check=True, env=env)

if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as erro:
        raise SystemExit(str(erro))
