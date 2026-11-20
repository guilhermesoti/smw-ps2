"""Roda todos os testes do PC (Linux x86-64) de uma vez e mostra um resumo.

    python3 testes/rodar_todos.py "/caminho/Super Mario World (USA).zip"
    python3 testes/rodar_todos.py ROM --rapido          # sem as 235 salas e sem as telas do Gravador
    python3 testes/rodar_todos.py ROM --so saves,largo_169
    python3 testes/rodar_todos.py --lista

Cada teste roda numa pasta temporária nova, com uma cópia do jogo (também os
scripts testar_percurso.py, testar_salas.py e os do Gravador): nada é gravado
ao lado da sua ROM, e testes que precisam de "pasta sem save" (câmera do mapa,
opções) sempre começam limpos. Os programas compilados ficam numa pasta
temporária que é apagada no fim. Os mesmos comandos estão no README.

Resultado de cada teste: OK, FALHOU (com o fim da saída; a saída inteira fica
em <pasta dos registros>/<nome>.log) ou PULADO (falta uma ferramenta, dita na
linha). Sai com código 1 se algum teste falhar.

Os testes do Gravador usam APPS/SMW/SMW.ELF (compile antes) e precisam do
Node; o núcleo também do python3 com o pycdlib; as telas também do
puppeteer-core (NODE_PATH) e de um Chrome (CHROME ou google-chrome/chromium no
PATH)."""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TESTES = Path(__file__).resolve().parent
RAIZ = TESTES.parent
FONTE = RAIZ / "Fonte"
NOMES_JOGO = {".zip": "SMW.ZIP", ".sfc": "SMW.SFC", ".smc": "SMW.SMC"}

# Opções de compilação dos testes que usam o motor (as mesmas do README).
MOTOR = ["-O1", "-std=gnu11", "-fno-strict-aliasing", "-fwrapv",
         "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
# Para no primeiro erro do UBSan: um aviso não pode virar OK.
SANITIZAR = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
# Variáveis que mudam o modo dos testes: cada teste define as suas.
VARIAVEIS_DO_JOGO = ("SMW_LARGO", "SMW_INGLES")
ASAN = {"ASAN_OPTIONS": "detect_leaks=0"}


class Pulado(Exception):
    """O teste não pode rodar nesta máquina (falta uma ferramenta)."""


class Contexto:
    def __init__(self, jogo: Path, elf: Path, registros: Path, binarios: Path):
        self.jogo, self.elf, self.registros = jogo, elf, registros
        self.cc = os.environ.get("CC", "cc")
        self._pkg = None
        self.binarios = binarios  # compilados uma vez por rodada (largo_169 e largo_43); apagada no fim

    def pkg(self):
        """Opções do pkg-config para minizip e zlib (ou Pulado)."""
        if self._pkg is None:
            try:
                cflags = subprocess.check_output(["pkg-config", "minizip", "zlib", "--cflags"], text=True,
                                                 stderr=subprocess.DEVNULL)
                libs = subprocess.check_output(["pkg-config", "minizip", "zlib", "--libs"], text=True,
                                               stderr=subprocess.DEVNULL)
            except (OSError, subprocess.CalledProcessError):
                self._pkg = False
            else:
                self._pkg = (shlex.split(cflags), shlex.split(libs))
        if not self._pkg:
            raise Pulado("falta o pkg-config com minizip e zlib (libminizip-dev, zlib1g-dev)")
        if shutil.which(self.cc) is None:
            raise Pulado(f"falta o compilador C ({self.cc})")
        return self._pkg

    def pasta_com_jogo(self, base: Path, nome="jogo") -> Path:
        pasta = base / nome
        pasta.mkdir()
        shutil.copyfile(self.jogo, pasta / NOMES_JOGO[self.jogo.suffix.lower()])
        return pasta

    def copia_do_jogo(self, base: Path) -> Path:
        """Cópia do jogo com o nome original, para os scripts que recebem o arquivo."""
        pasta = base / "rom"
        pasta.mkdir()
        copia = pasta / self.jogo.name
        shutil.copyfile(self.jogo, copia)
        return copia


class TempoEsgotado(Exception):
    def __init__(self, saida):
        super().__init__("passou do tempo limite")
        self.saida = saida


def rodar(cmd, cwd, env_extra=None, timeout=1800):
    """(código de saída, saída e erros na ordem em que saíram)."""
    env = {k: v for k, v in os.environ.items() if k not in VARIAVEIS_DO_JOGO}
    env.update(env_extra or {})
    try:
        r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as erro:
        parcial = erro.stdout or ""
        if isinstance(parcial, bytes):
            parcial = parcial.decode(errors="replace")
        raise TempoEsgotado(parcial + "\n(passou do tempo limite)") from None
    return r.returncode, r.stdout


def compilar(ctx, base, saida, fontes, extra=(), sanitizar=False, wrap_fopen=False, mocks=False,
             motor=False, cwd=None, reaproveitar=False):
    cflags, libs = ctx.pkg()
    if reaproveitar:
        base = ctx.binarios
        if (base / saida).exists():
            return base / saida
    cmd = [ctx.cc, "-std=gnu11"]
    if mocks:  # os simulados do PS2 vêm antes, como no README
        cmd += ["-D_EE", "-I", TESTES / "mocks", "-I", FONTE / "ps2"]
    if motor:
        cmd += MOTOR + ["-I", FONTE / "engine", "-I", FONTE / "ps2"]
    if sanitizar:
        cmd += SANITIZAR
    cmd += [*extra, *cflags, *fontes]
    if wrap_fopen:
        cmd += ["-Wl,--wrap=fopen"]
    cmd += [*libs, "-lm", "-o", base / saida]
    rc, texto = rodar(cmd, cwd or RAIZ)
    if rc:
        raise RuntimeError("não compilou:\n" + texto)
    return base / saida


def motor_c():
    return sorted((FONTE / "engine" / "src").rglob("*.c"))


def todas_as_salas():
    return ["0x" + s["hex"] for s in json.loads((TESTES / "salas.json").read_text())["salas"]]


# ---- os testes: cada um devolve (ok, saída) ou levanta Pulado ----

def t_opcoes(ctx, base):
    b = compilar(ctx, base, "opcoes", [FONTE / "ps2/files.c", TESTES / "opcoes.c"],
                 extra=["-I", FONTE / "ps2"], sanitizar=True)
    rc, out = rodar([b, str(ctx.pasta_com_jogo(base)) + "/"], base, ASAN)
    return rc == 0 and "OPCOES: OK" in out, out


def t_ritmo(ctx, base):
    b = compilar(ctx, base, "ritmo", [FONTE / "ps2/ritmo.c", TESTES / "ritmo.c"],
                 extra=["-I", FONTE / "ps2"], sanitizar=True)
    rc, out = rodar([b], base, ASAN)
    return rc == 0 and "RITMO: OK" in out, out


def _mock(nome, arquivo, wrap=True, gc=False):
    def teste(ctx, base):
        extra = ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"] if gc else []
        b = compilar(ctx, base, nome, [TESTES / arquivo], extra=extra, mocks=True, wrap_fopen=wrap)
        rc, out = rodar([b], base)
        return rc == 0, out
    return teste


def t_palacio(ctx, base):
    b = compilar(ctx, base, "palacio", [*motor_c(), FONTE / "ps2/game.c", FONTE / "ps2/registro.c",
                                        TESTES / "palacio_amarelo.c"],
                 extra=["-I", FONTE / "engine/src"], mocks=True, motor=True, wrap_fopen=True)
    rc, out = rodar([b, str(ctx.pasta_com_jogo(base)) + "/"], base)
    return rc == 0, out


def _com_motor(nome, arquivo, sanitizar=False):
    def compila(ctx, base):
        return compilar(ctx, base, nome, [*motor_c(), FONTE / "ps2/files.c", FONTE / "ps2/game.c",
                                          FONTE / "ps2/registro.c", TESTES / arquivo],
                        motor=True, sanitizar=sanitizar, reaproveitar=True)
    return compila


def t_camera(ctx, base):
    b = _com_motor("camera", "camera_mapa.c", sanitizar=True)(ctx, base)
    rc, out = rodar([b, str(ctx.pasta_com_jogo(base)) + "/"], base, ASAN)
    return rc == 0, out


def t_aparecer(ctx, base):
    b = _com_motor("aparecer", "aparecer.c")(ctx, base)
    rc, out = rodar([b, str(ctx.pasta_com_jogo(base)) + "/", "1", "600", *todas_as_salas()], base)
    m = re.search(r"TOTAL surge=(\d+) some=(\d+)", out)
    # Esperado (README): surge=0; some=2 são dois casos normais do jogo.
    ok = rc == 0 and m is not None and int(m.group(1)) == 0 and int(m.group(2)) <= 2
    return ok, out


def _largo(largo):
    def teste(ctx, base):
        b = _com_motor("largo_mario", "largo_mario.c")(ctx, base)
        env = {"SMW_LARGO": "1"} if largo else {}
        rc, out = rodar([b, str(ctx.pasta_com_jogo(base)) + "/", "1500", "-", *todas_as_salas()], base, env)
        return rc == 0, out
    return teste


def t_percurso(ctx, base):
    ctx.pkg()
    rc, out = rodar([sys.executable, TESTES / "testar_percurso.py", "--sanitizar", ctx.copia_do_jogo(base)], RAIZ)
    return rc == 0, out


def _salas(largo):
    def teste(ctx, base):
        ctx.pkg()
        cmd = [sys.executable, TESTES / "testar_salas.py", ctx.copia_do_jogo(base)] + (["--largo"] if largo else [])
        rc, out = rodar(cmd, RAIZ, timeout=3600)
        return rc == 0 and re.search(r"salas jogadas, 0 sem chegar ao modo de fase", out) is not None, out
    return teste


def _node():
    if shutil.which("node") is None:
        raise Pulado("falta o Node")


def t_gravador_nucleo(ctx, base):
    _node()
    if not ctx.elf.is_file():
        raise Pulado(f"falta o ELF ({ctx.elf}); compile antes")
    # testar_nucleo.js compara a ISO com a do pycdlib chamando "python3".
    if shutil.which("python3") is None or rodar(["python3", "-c", "import pycdlib"], RAIZ)[0]:
        raise Pulado("falta o python3 com o pycdlib (pip install pycdlib)")
    rc, out = rodar(["node", RAIZ / "Gravador/testes/testar_nucleo.js", ctx.copia_do_jogo(base), ctx.elf], RAIZ)
    return rc == 0 and "NÚCLEO: OK" in out, out


def _chrome():
    pedido = os.environ.get("CHROME")
    if pedido:  # caminho ou nome de um programa no PATH
        achado = pedido if Path(pedido).is_file() else shutil.which(pedido)
        if achado is None:
            raise Pulado(f"CHROME={pedido} não existe")
        return achado
    for nome in ("google-chrome", "chromium", "chromium-browser"):
        achado = shutil.which(nome)
        if achado:
            return achado
    raise Pulado("falta um Chrome (CHROME=/caminho/do/chrome)")


def t_gravador_telas(ctx, base):
    _node()
    chrome = _chrome()
    elf_repo = RAIZ / "APPS/SMW/SMW.ELF"
    if not elf_repo.is_file():
        raise Pulado("falta APPS/SMW/SMW.ELF; compile antes")
    rc, _ = rodar(["node", "-e", "require('puppeteer-core')"], RAIZ)
    if rc:
        raise Pulado("falta o puppeteer-core (npm install puppeteer-core; NODE_PATH)")
    html = base / "Gravador_SMW_PS2.html"
    rc, out = rodar([sys.executable, RAIZ / "Gravador/gerar_gravador.py", html], RAIZ)
    if rc:
        return False, out
    rc, out2 = rodar(["node", RAIZ / "Gravador/testes/testar_telas.js", html, ctx.copia_do_jogo(base), base / "telas"],
                     RAIZ, {"CHROME": chrome})
    return rc == 0 and "TELAS: OK" in out2, out + out2


# Nome, descrição, função, se é demorado (fica fora com --rapido).
LISTA = [
    ("opcoes", "SMW.CFG / SMW.OPT", t_opcoes, False),
    ("ritmo", "ritmo do jogo (60/30 quadros, simulado)", t_ritmo, False),
    ("saves", "saves entre pendrive e Memory Cards", _mock("save_midias", "save_midias.c"), False),
    ("log", "SMW_LOG.TXT", _mock("registro", "registro.c"), False),
    ("icone", "ícone do save no Memory Card", _mock("icone", "memory_card_icon.c", wrap=False), False),
    ("memory_card", "sequência de chamadas do MCSERV", _mock("memory_card", "memory_card.c", wrap=False, gc=True), False),
    ("palacio", "Palácio Amarelo com o motor", t_palacio, False),
    ("camera", "câmera do mapa", t_camera, False),
    ("percurso", "percurso de regressão (CRC d1ac4ce4)", t_percurso, False),
    ("aparecer", "objetos surgindo/sumindo em 16:9", t_aparecer, False),
    ("largo_169", "Mario na borda e PPU em 16:9", _largo(True), False),
    ("largo_43", "Mario na borda em 4:3", _largo(False), False),
    ("gravador_nucleo", "núcleo do Gravador", t_gravador_nucleo, False),
    ("gravador_telas", "telas do Gravador no Chrome", t_gravador_telas, True),
    ("salas_43", "235 salas com ASan/UBSan em 4:3", _salas(False), True),
    ("salas_169", "235 salas com ASan/UBSan em 16:9", _salas(True), True),
]


def ultimas_linhas(texto, n=12):
    linhas = [l for l in texto.splitlines() if l.strip()]
    return "\n".join("      " + l for l in linhas[-n:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("jogo", nargs="?", type=Path, help="Cópia americana em ZIP, SFC ou SMC")
    ap.add_argument("--elf", type=Path, default=RAIZ / "APPS/SMW/SMW.ELF",
                    help="ELF para o teste do núcleo do Gravador (padrão: APPS/SMW/SMW.ELF)")
    escolha = ap.add_mutually_exclusive_group()
    escolha.add_argument("--so", help="Só estes testes, separados por vírgula (veja --lista)")
    escolha.add_argument("--rapido", action="store_true", help="Pula os testes demorados (salas e telas)")
    ap.add_argument("--lista", action="store_true", help="Mostra os testes e sai")
    a = ap.parse_args()
    if a.lista:
        for nome, desc, _, demorado in LISTA:
            print(f"{nome:16s} {desc}{' (demorado)' if demorado else ''}")
        return 0
    if a.jogo is None or not a.jogo.is_file() or a.jogo.suffix.lower() not in NOMES_JOGO:
        ap.error("Informe a sua cópia do jogo (.zip, .sfc ou .smc).")
    escolhidos = LISTA
    if a.so:
        pedidos = [n.strip() for n in a.so.split(",") if n.strip()]
        if not pedidos:
            ap.error("--so sem nenhum teste (veja --lista)")
        desconhecidos = [n for n in pedidos if n not in {x[0] for x in LISTA}]
        if desconhecidos:
            ap.error("Teste desconhecido: " + ", ".join(desconhecidos) + " (veja --lista)")
        escolhidos = [x for x in LISTA if x[0] in pedidos]
    elif a.rapido:
        escolhidos = [x for x in LISTA if not x[3]]

    registros = Path(tempfile.mkdtemp(prefix="smw-testes-"))
    inicio = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="smw-binarios-") as binarios:
        ctx = Contexto(a.jogo.resolve(), a.elf.resolve(), registros, Path(binarios))
        resultado = rodar_escolhidos(ctx, escolhidos)
    contagem = {e: sum(1 for estado, _ in resultado.values() if estado == e) for e in ("OK", "FALHOU", "PULADO")}
    print(f"\n{contagem['OK']} OK, {contagem['FALHOU']} falharam, {contagem['PULADO']} pulados "
          f"em {time.monotonic() - inicio:.0f} s. Saídas completas em {registros}")
    return 1 if contagem["FALHOU"] else 0


def rodar_escolhidos(ctx, escolhidos) -> dict:
    """{nome: (estado, segundos)}, mostrando cada linha assim que o teste acaba."""
    resultado = {}
    for nome, desc, funcao, _ in escolhidos:
        t0 = time.monotonic()
        with tempfile.TemporaryDirectory(prefix=f"smw-{nome}-") as tmp:
            try:
                ok, saida = funcao(ctx, Path(tmp))
                estado = "OK" if ok else "FALHOU"
            except Pulado as motivo:
                estado, saida = "PULADO", str(motivo)
            except TempoEsgotado as erro:
                estado, saida = "FALHOU", erro.saida
            except (OSError, RuntimeError) as erro:
                estado, saida = "FALHOU", str(erro)
        (ctx.registros / f"{nome}.log").write_text(saida, encoding="utf-8")
        segundos = time.monotonic() - t0
        resultado[nome] = (estado, segundos)
        linha = f"{estado:7s}{nome:16s} {desc} ({segundos:.0f} s)"
        if estado == "PULADO":
            linha += f": {saida}"
        print(linha, flush=True)
        if estado == "FALHOU":
            print(ultimas_linhas(saida), flush=True)
    return resultado


if __name__ == "__main__":
    raise SystemExit(main())
