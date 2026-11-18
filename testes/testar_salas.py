"""Carrega todas as salas do jogo com AddressSanitizer e UBSan (Linux x86-64).

Cada sala é carregada pelo próprio motor e jogada alguns segundos com
entradas fixas. Uma sala que derruba o processo é anotada e a varredura
continua nas seguintes. Não prova que o jogo inteiro pode ser terminado."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
from tempfile import TemporaryDirectory

def compilar(pasta, testes, cc, sanitizar):
    fonte = testes.parent / "Fonte"
    unidades = sorted((fonte / "engine" / "src").rglob("*.c"))
    unidades += [fonte / "ps2" / "files.c", fonte / "ps2" / "game.c", testes / "salas.c"]
    cflags = ["-O1", "-g", "-std=gnu11", "-fno-strict-aliasing", "-fwrapv",
              "-ffunction-sections", "-fdata-sections"]
    if sanitizar:
        cflags += ["-fsanitize=address,undefined", "-fsanitize-recover=address",
                   "-fno-omit-frame-pointer"]
    pkg = ["pkg-config", "minizip", "zlib"]
    inc = shlex.split(subprocess.check_output([*pkg, "--cflags"], text=True))
    libs = shlex.split(subprocess.check_output([*pkg, "--libs"], text=True))
    binario = pasta / "salas"
    subprocess.run([cc, *cflags, *inc, "-I", fonte / "engine", "-I", fonte / "ps2",
                    *unidades, "-Wl,--gc-sections", *libs, "-lm", "-o", binario], check=True)
    return binario

def locais(stderr):
    """Um item por lugar do código: arquivo:linha, função e o que aconteceu."""
    achados, linhas, sala = set(), stderr.splitlines(), "?"
    for i, linha in enumerate(linhas):
        if linha.startswith("SALA_INICIO "):
            sala = f"{int(linha.split()[1]):03X}"
        m = re.search(r"([\w/.-]+\.c:\d+):\d+: runtime error: (.*)", linha)
        if m:
            texto = re.sub(r"0x[0-9a-f]+", "...", m.group(2))
            achados.add(f"UBSan {Path(m.group(1)).name}: {texto} [sala {sala}]")
        if "ERROR: AddressSanitizer:" in linha:
            tipo = linha.split("AddressSanitizer:")[1].split(" on ")[0].strip()
            pilha = next((l for l in linhas[i:i + 12] if re.search(r"#0 ", l)), "")
            local = re.search(r"in (\w+) ([\w/.-]+\.c:\d+)", pilha)
            perto = next((l.strip() for l in linhas[i:i + 40] if "is located" in l), "")
            global_ = re.search(r"global variable '(\w+)'", perto)
            onde = f"{Path(local.group(2)).name} {local.group(1)}" if local else "?"
            achados.add(f"ASan {onde}: {tipo}" + (f" (perto de {global_.group(1)})" if global_ else "")
                        + f" [sala {sala}]")
    return sorted(achados)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("jogo", type=Path, help="Cópia americana em ZIP, SFC ou SMC")
    p.add_argument("--quadros", type=int, default=240)
    p.add_argument("--lote", type=int, default=24)
    p.add_argument("--processos", type=int, default=os.cpu_count() or 2)
    p.add_argument("--sem-sanitizar", action="store_true")
    p.add_argument("--ingles", action="store_true", help="Textos originais em inglês")
    p.add_argument("--largo", action="store_true", help="Tela larga 16:9")
    p.add_argument("--so", help="Lista de salas (ex.: 0x105,0x106)")
    p.add_argument("--relatorio", type=Path, help="Grava o resultado em JSON")
    a = p.parse_args()
    nomes = {".zip": "SMW.ZIP", ".sfc": "SMW.SFC", ".smc": "SMW.SMC"}
    nome = nomes.get(a.jogo.suffix.lower())
    if nome is None or not a.jogo.is_file():
        p.error("Informe um arquivo .zip, .sfc ou .smc existente.")
    testes = Path(__file__).resolve().parent
    salas = json.loads((testes / "salas.json").read_text())["salas"]
    if a.so:
        pedidas = {int(x, 0) for x in a.so.split(",")}
        salas = [s for s in salas if s["id"] in pedidas]
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=0"
    if a.ingles:
        env["SMW_INGLES"] = "1"
    if a.largo:
        env["SMW_LARGO"] = "1"

    with TemporaryDirectory(prefix="smw-salas-") as temp:
        pasta = Path(temp)
        binario = compilar(pasta, testes, os.environ.get("CC", "cc"), not a.sem_sanitizar)

        def rodar(grupo):
            pendentes, resultados = list(grupo), []
            while pendentes:
                # Pasta nova a cada execução: o jogo grava o progresso ao lado
                # da cópia, e um save mudaria o roteiro inicial.
                with TemporaryDirectory(dir=pasta) as rodada:
                    shutil.copyfile(a.jogo, Path(rodada) / nome)
                    r = subprocess.run([binario, rodada, str(a.quadros),
                                        *[str(s["id"]) for s in pendentes]],
                                       env=env, capture_output=True, text=True,
                                       errors="replace", timeout=900)
                saida = r.stdout + r.stderr
                ok, ultima = {}, None
                for linha in r.stdout.splitlines():
                    if linha.startswith("SALA_INICIO "):
                        ultima = int(linha.split()[1])
                    elif linha.startswith("SALA_OK "):
                        _, sala, chegou, quadros, modo = linha.split()
                        ok[int(sala)] = (chegou == "1", int(quadros), modo)
                avisos = locais(r.stderr)
                erros = re.findall(r"ERROR: AddressSanitizer: .*", r.stderr)
                for s in pendentes:
                    if s["id"] in ok:
                        chegou, quadros, modo = ok[s["id"]]
                        resultados.append(dict(s, estado="ok" if chegou else "nao_jogou",
                                               quadros=quadros, modo_final=modo))
                restantes = [s for s in pendentes if s["id"] not in ok]
                if avisos:
                    resultados.append({"id": -1, "estado": "aviso",
                                       "salas": [s["hex"] for s in pendentes],
                                       "avisos": avisos, "pilha": r.stderr[-6000:]})
                if not restantes:
                    break
                ruim = next((s for s in restantes if s["id"] == ultima), restantes[0])
                resultados.append(dict(ruim, estado="falhou", saida=r.returncode,
                                       erro=erros[0] if erros else saida[-300:],
                                       pilha=r.stderr[-6000:]))
                pendentes = [s for s in restantes if s is not ruim]
            return resultados

        grupos = [salas[i:i + a.lote] for i in range(0, len(salas), a.lote)]
        resultados = []
        with ThreadPoolExecutor(max_workers=a.processos) as pool:
            for parte in pool.map(rodar, grupos):
                resultados += parte

    falhas = [r for r in resultados if r["estado"] == "falhou"]
    por_local = {}
    for r in resultados:
        if r["estado"] == "aviso":
            for x in r["avisos"]:
                m = re.match(r"(.*) \[sala (\w+|\?)\]$", x)
                texto = re.sub(r"0x[0-9a-f]+", "...", m.group(1) if m else x)
                if texto.startswith("UBSan") and "insufficient space" in texto:
                    continue  # repete o aviso de índice/ASan do mesmo lugar
                por_local.setdefault(texto, set()).add(m.group(2) if m else "?")
    ubsan = sorted(por_local)
    nao_jogou = [r["hex"] for r in resultados if r["estado"] == "nao_jogou"]
    jogadas = sum(r["estado"] == "ok" for r in resultados)
    print(f"{jogadas} salas jogadas, {len(nao_jogou)} sem chegar ao modo de fase, "
          f"{len(falhas)} que derrubaram o processo, {len(ubsan)} problemas distintos no código")
    for r in falhas:
        print(f"  ERRO sala {r['hex']} ({r['label']}): {r['erro']}")
    for aviso in ubsan:
        print(f"  {aviso} — salas {' '.join(sorted(por_local[aviso]))}")
    if nao_jogou:
        print("  Sem modo de fase:", " ".join(nao_jogou))
    if a.relatorio:
        a.relatorio.write_text(json.dumps(resultados, indent=1, ensure_ascii=False) + "\n")
    return 1 if falhas or ubsan else 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError) as erro:
        raise SystemExit(str(erro))
