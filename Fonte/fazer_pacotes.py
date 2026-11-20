"""Monta os pacotes de uma versão nova na pasta do projeto, sem a ROM.

    cd Codigo
    python3 Fonte/fazer_pacotes.py --simular     # só confere e mostra o que faria
    python3 Fonte/fazer_pacotes.py

Usa a organização do projeto (a pasta acima de Codigo):
  ROM/Super Mario World (USA).zip     a cópia do jogo (só entra na ISO de teste)
  ELF OU ISO TESTE/                   ELF + title.cfg, Gravador e ISO da versão atual
  Github/                             pacote do jogo e do código; os da versão
                                      anterior vão para Github/versoes-anteriores

Antes de mexer em qualquer coisa, confere:
  - a ROM: um ZIP com o .sfc/.smc americano (CRC32 B19ED489, com ou sem o
    cabeçalho de copiador), com as mesmas regras do carregador do jogo (nome
    de 4 a 255 bytes, 524288 ou 524800 bytes);
  - a versão do Fonte/ps2/main.c (SMW_VERSAO), que tem de ser a mesma da
    linha 2 do LEIA-ME.txt, do README.md e da que está dentro do
    APPS/SMW/SMW.ELF, e o SHA-256 desse ELF, que tem de ser o que o README.md
    registra para a versão (compile antes);
  - o git sem mudanças nem arquivos novos fora do git (o pacote do código é o
    `git archive` do HEAD);
  - que os pacotes da versão anterior podem ir para versoes-anteriores sem
    substituir nada.
Depois monta tudo numa pasta temporária e confere que nenhum pacote (jogo,
código, Gravador) tem a ROM: nem arquivo com extensão de jogo, nem o conteúdo
da ROM (o ZIP, ou o .sfc com ou sem cabeçalho), nem um trecho dela, nem a ROM
em base64, nem dentro de outro arquivo compactado (ZIP, gzip, bzip2, xz) até 4
níveis. O que não der para conferir (formato que o script não abre, arquivo
grande demais) também impede a publicação.

Só então publica: cada arquivo é copiado para um nome temporário único na
pasta de destino e conferido; depois os arquivos são trocados (os.replace),
guardando os antigos até o fim, para voltar tudo como estava se uma troca
falhar. Os arquivos de ELF OU ISO TESTE são substituídos pelos da versão nova
(como a cada versão); os pacotes da versão anterior em Github só são movidos
para versoes-anteriores depois que os novos estão no lugar. Mostra o SHA-256
de tudo o que foi gerado."""
import argparse
import base64
import bz2
import hashlib
import io
import lzma
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

CODIGO = Path(__file__).resolve().parent.parent
PREFIXO = "RetroGames_Super_Mario_World_PS2_v"
ISO_TESTE = "SMWG_000.01.Super Mario World.iso"
EXTENSOES_JOGO = (".sfc", ".smc", ".zip", ".iso")
CRC_USA = 0xB19ED489
ROM_BYTES = 512 * 1024   # SMW_ROM_BYTES (Fonte/ps2/game.h)
CABECALHO = 512          # cabeçalho de copiador aceito pelo carregador
NOME_MAX = 255           # char name[256] em read_zip (Fonte/ps2/files.c)
LIMITE = 64 << 20        # maior arquivo (descompactado) que o script confere
NIVEIS = 4               # arquivos compactados uns dentro dos outros
TRECHO = 4096


def sha256(dados: bytes) -> str:
    return hashlib.sha256(dados).hexdigest()


def sha256_arquivo(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for bloco in iter(lambda: f.read(1 << 20), b""):
            h.update(bloco)
    return h.hexdigest()


def sem_cabecalho(dados: bytes) -> bytes:
    return dados[CABECALHO:] if len(dados) % 1024 == CABECALHO else dados


def executar(cmd: list, o_que: str) -> str:
    """Roda um programa; se faltar ou falhar, para com uma mensagem clara."""
    try:
        r = subprocess.run([str(c) for c in cmd], cwd=CODIGO, text=True, capture_output=True)
    except FileNotFoundError:
        raise SystemExit(f"Não achei o programa {cmd[0]} ({o_que}).")
    if r.returncode != 0:
        detalhe = (r.stderr or r.stdout).strip().splitlines()
        raise SystemExit(f"Falhou: {o_que} (código {r.returncode})" + (f"\n  {detalhe[-1]}" if detalhe else ""))
    return r.stdout


def git(*args) -> str:
    return executar(["git", *args], "git " + " ".join(args))


def nome_bruto(info: zipfile.ZipInfo) -> bytes:
    """O nome como está gravado no ZIP, que é o que o minizip do jogo lê."""
    return info.orig_filename.encode("utf-8" if info.flag_bits & 0x800 else "cp437", "replace")


def trechos(dados: bytes) -> list:
    """Os blocos de 4 KiB alinhados que não são preenchimento (32 valores de
    byte diferentes ou mais): um pedaço da ROM com 8 KiB ou mais contém um
    deles, a não ser que caia só em preenchimento."""
    bons = [dados[o:o + TRECHO] for o in range(0, len(dados) - TRECHO + 1, TRECHO)
            if len(set(dados[o:o + TRECHO])) >= 32]
    if len(bons) < 16:
        raise SystemExit("A ROM não tem blocos variados o bastante para a conferência.")
    return bons


class Rom:
    """A cópia do jogo e as marcas que a identificam dentro de um arquivo."""

    def __init__(self, caminho: Path):
        if not zipfile.is_zipfile(caminho):
            raise SystemExit(f"A ROM tem de ser o ZIP com o .sfc/.smc dentro (como o jogo usa): {caminho}")
        self.arquivo = caminho.read_bytes()
        self.membro = self.cartucho = None
        with zipfile.ZipFile(io.BytesIO(self.arquivo)) as z:
            for info in z.infolist():  # na ordem do ZIP, como read_zip
                nome = nome_bruto(info)
                if not 4 <= len(nome) <= NOME_MAX or nome[-4:].lower() not in (b".sfc", b".smc"):
                    continue
                if info.file_size not in (ROM_BYTES, ROM_BYTES + CABECALHO):
                    continue
                try:
                    dados = z.read(info)
                except (zipfile.BadZipFile, zlib.error, NotImplementedError, RuntimeError):
                    continue
                if zlib.crc32(dados[-ROM_BYTES:]) == CRC_USA:
                    self.membro, self.cartucho = dados, dados[-ROM_BYTES:]
                    break
        if self.cartucho is None:
            raise SystemExit(f"O ZIP não tem um .sfc/.smc do Super Mario World americano (CRC32 {CRC_USA:08X}) "
                             f"que o jogo aceite: {caminho}")
        self.marcas = {sha256(self.arquivo), sha256(self.membro), sha256(self.cartucho)}
        self.trechos = trechos(self.cartucho) + trechos(self.arquivo)


BASE64 = re.compile(rb"[A-Za-z0-9+/_-]{1600,}=*")  # 1200 bytes ou mais, em base64


def de_base64(dados: bytes):
    """Decodifica os trechos longos em base64 (comum ou de URL, com quebras de
    linha ou não), nos 4 começos possíveis, porque o trecho pode estar grudado
    em outras letras."""
    for m in BASE64.finditer(re.sub(rb"\s+", b"", dados)):
        texto = m.group().rstrip(b"=").replace(b"-", b"+").replace(b"_", b"/")
        if len(texto) > LIMITE // 3 * 4:
            yield None
            continue
        for inicio in range(4):
            pedaco = texto[inicio:]
            if len(pedaco) % 4 == 1:
                pedaco = pedaco[:-1]  # uma letra sozinha no fim não forma byte
            yield base64.b64decode(pedaco + b"=" * (-len(pedaco) % 4))


def descompactar(dados: bytes):
    """(formato, conteúdo) para gzip/bzip2/xz; (formato, None) se não abrir ou
    passar do LIMITE; None se não for nenhum desses."""
    if dados[:2] == b"\x1f\x8b":
        formato, d = "gzip", zlib.decompressobj(16 + zlib.MAX_WBITS)
    elif dados[:3] == b"BZh" and dados[3:4].isdigit():
        formato, d = "bzip2", bz2.BZ2Decompressor()
    elif dados[:6] == b"\xfd7zXZ\x00":
        formato, d = "xz", lzma.LZMADecompressor()
    else:
        return None
    try:
        saida = d.decompress(dados, LIMITE + 1)
    except (zlib.error, OSError, lzma.LZMAError, EOFError):
        return formato, None
    return formato, (saida if len(saida) <= LIMITE else None)


def dentro_do_zip(nome: str, dados: bytes, rom, nivel: int) -> list:
    problemas = []
    try:
        with zipfile.ZipFile(io.BytesIO(dados)) as z:
            for info in z.infolist():
                interno = f"{nome}/{info.filename}" if nivel else f"{nome}: {info.filename}"
                if info.is_dir():
                    continue
                if info.filename.lower().endswith(EXTENSOES_JOGO):
                    problemas.append(f"{interno} (arquivo de jogo)")
                    continue
                if info.file_size > LIMITE:
                    problemas.append(f"{interno} (passa de {LIMITE >> 20} MiB: não conferido)")
                    continue
                try:
                    with z.open(info) as f:
                        conteudo = f.read(LIMITE + 1)
                except (zipfile.BadZipFile, zlib.error, NotImplementedError, RuntimeError, OSError) as e:
                    problemas.append(f"{interno} (não abre: {e}; não conferido)")
                    continue
                if len(conteudo) > LIMITE:
                    problemas.append(f"{interno} (passa de {LIMITE >> 20} MiB: não conferido)")
                    continue
                problemas += procurar_rom(interno, conteudo, rom, nivel + 1)
    except (zipfile.BadZipFile, OSError) as e:
        problemas.append(f"{nome} (parece um ZIP mas não abre: {e}; não conferido)")
    return problemas


SEM_SUPORTE = (b"7z\xbc\xaf\x27\x1c", b"Rar!\x1a\x07")  # 7z e RAR


def procurar_rom(nome: str, dados: bytes, rom, nivel=0, decodificado=False) -> list:
    """Problemas: a ROM (inteira ou um pedaço de 8 KiB ou mais) em dados, num
    arquivo compactado dentro deles ou em base64 dentro deles; ou algo que não
    deu para conferir."""
    if sha256(dados) in rom.marcas or sha256(sem_cabecalho(dados)) in rom.marcas:
        return [f"{nome} (mesmo conteúdo da ROM)"]
    problemas = procurar_dentro(nome, dados, rom, nivel, decodificado)
    if problemas:
        return problemas
    if any(t in dados for t in rom.trechos):
        return [f"{nome} (um pedaço da ROM está dentro)"]
    if nivel < NIVEIS:
        for blob in de_base64(dados):
            if blob is None:
                return [f"{nome} (base64 de mais de {LIMITE >> 20} MiB: não conferido)"]
            problemas = procurar_rom(f"{nome} (base64)", blob, rom, nivel + 1, True)
            if problemas:
                return problemas
    return []


def procurar_dentro(nome: str, dados: bytes, rom, nivel: int, decodificado: bool) -> list:
    """Problemas dentro de um arquivo compactado ([] se dados não for um)."""
    e_zip = dados[:4] == b"PK\x03\x04" or (not decodificado and zipfile.is_zipfile(io.BytesIO(dados)))
    outro = None if e_zip else descompactar(dados)
    sem_suporte = dados[:6] in SEM_SUPORTE
    if not (e_zip or outro or sem_suporte):
        return []
    if nivel >= NIVEIS:
        problemas = [f"{nome} (compactado dentro de outros {NIVEIS} níveis: não conferido)"]
    elif sem_suporte:
        problemas = [f"{nome} (arquivo 7z/RAR, que o script não abre: não conferido)"]
    elif outro:
        formato, conteudo = outro
        if conteudo is None:
            problemas = [f"{nome} ({formato} que não abre ou passa de {LIMITE >> 20} MiB: não conferido)"]
        else:
            problemas = procurar_rom(f"{nome} ({formato})", conteudo, rom, nivel + 1, decodificado)
    else:
        problemas = dentro_do_zip(nome, dados, rom, nivel)
    if decodificado:
        # de_base64 tenta 4 começos e 3 são lixo: o lixo pode parecer um
        # arquivo compactado que não abre. Aqui só conta a ROM achada.
        problemas = [p for p in problemas if "não conferido" not in p]
    return problemas


def versao_do_codigo() -> str:
    m = re.search(r'#define SMW_VERSAO "([0-9.]+)"', (CODIGO / "Fonte/ps2/main.c").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("Não achei SMW_VERSAO em Fonte/ps2/main.c.")
    return m.group(1)


def anteriores_a_mover(github: Path, versao: str) -> list:
    novos = {f"{PREFIXO}{versao}.zip", f"{PREFIXO}{versao}_Codigo_Fonte_GitHub.zip"}
    return [p for p in sorted(github.glob(f"{PREFIXO}*.zip")) if p.name not in novos]


def destinos(raiz: Path, versao: str) -> dict:
    teste, github = raiz / "ELF OU ISO TESTE", raiz / "Github"
    return {"elf": teste / "APPS/SMW/SMW.ELF", "title": teste / "APPS/SMW/title.cfg",
            "gravador": teste / "Gravador_SMW_PS2.html", "iso": teste / ISO_TESTE,
            "jogo": github / f"{PREFIXO}{versao}.zip",
            "codigo": github / f"{PREFIXO}{versao}_Codigo_Fonte_GitHub.zip"}


def conferir(raiz: Path, rom: Path, versao: str) -> list:
    """Erros que impedem montar (lista vazia: pode montar)."""
    erros = []
    elf = CODIGO / "APPS/SMW/SMW.ELF"
    for p, o_que in ((rom, "a ROM"), (elf, "o ELF (compile antes)"), (CODIGO / "APPS/SMW/title.cfg", "o title.cfg"),
                     (CODIGO / "LEIA-ME.txt", "o LEIA-ME.txt"), (CODIGO / "README.md", "o README.md"),
                     (CODIGO / "CREDITOS.md", "o CREDITOS.md"), (CODIGO / "Licencas", "a pasta Licencas"),
                     (raiz / "ELF OU ISO TESTE", "a pasta ELF OU ISO TESTE"), (raiz / "Github", "a pasta Github")):
        if not p.exists():
            erros.append(f"Falta {o_que}: {p}")
    if erros:
        return erros
    m = re.search(rb"Super Mario World PS2 v([0-9.]+) - RetroGames", elf.read_bytes())
    if not m or m.group(1).decode() != versao:
        achada = m.group(1).decode() if m else "nenhuma"
        erros.append(f"O APPS/SMW/SMW.ELF é da versão {achada}, não da {versao} (compile de novo).")
    linhas = (CODIGO / "LEIA-ME.txt").read_text(encoding="utf-8").splitlines()
    if len(linhas) < 2 or not re.search(rf"\bv{re.escape(versao)}(?![0-9.])", linhas[1]):
        erros.append(f"A linha 2 do LEIA-ME.txt não diz v{versao}.")
    readme = (CODIGO / "README.md").read_text(encoding="utf-8")
    if not re.search(rf"Versão {re.escape(versao)}(?![0-9.])", readme):
        erros.append(f"O README.md não diz \"Versão {versao}\".")
    m = re.search(rf"SHA-256 do ELF da v{re.escape(versao)}(?![0-9.])[^`]*`([0-9a-f]{{64}})`", readme)
    if not m:
        erros.append(f"O README.md não registra o SHA-256 do ELF da v{versao}.")
    elif m.group(1) != sha256_arquivo(elf):
        erros.append(f"O APPS/SMW/SMW.ELF ({sha256_arquivo(elf)[:16]}…) não é o que o README.md registra "
                     f"({m.group(1)[:16]}…): compile de novo ou corrija o README.")
    if git("status", "--porcelain").strip():
        erros.append("O git tem mudanças sem commit (ou arquivo novo fora do git): o pacote do código "
                     "é o HEAD. Faça o commit antes.")
    github = raiz / "Github"
    for chave, destino in destinos(raiz, versao).items():
        if destino.is_dir() or (destino.is_symlink() and not destino.is_file()):
            erros.append(f"O destino não é um arquivo: {destino}")
        elif chave in ("jogo", "codigo") and destino.exists():
            erros.append(f"Já existe: {destino.name} (mova ou renomeie antes de montar de novo).")
    anteriores = github / "versoes-anteriores"
    if anteriores.exists() and not anteriores.is_dir():
        erros.append(f"versoes-anteriores não é uma pasta: {anteriores}")
    for p in anteriores_a_mover(github, versao):
        if (anteriores / p.name).exists():
            erros.append(f"Já existe em versoes-anteriores: {p.name}")
    return erros


def montar(tmp: Path, versao: str, caminho_rom: Path, rom: Rom) -> dict:
    """Gera tudo em tmp e confere. Devolve os arquivos prontos."""
    elf = CODIGO / "APPS/SMW/SMW.ELF"
    gravador = tmp / "Gravador_SMW_PS2.html"
    executar([sys.executable, CODIGO / "Gravador/gerar_gravador.py", gravador], "gerar o Gravador")
    jogo = tmp / f"{PREFIXO}{versao}.zip"
    with zipfile.ZipFile(jogo, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(gravador, "Gravador_SMW_PS2.html")
        z.write(CODIGO / "LEIA-ME.txt", "LEIA-ME.txt")
        z.write(elf, "APPS/SMW/SMW.ELF")
        z.write(CODIGO / "APPS/SMW/title.cfg", "APPS/SMW/title.cfg")
        for lic in sorted((CODIGO / "Licencas").iterdir()):
            if lic.is_file():
                z.write(lic, f"Licencas/{lic.name}")
        z.write(CODIGO / "CREDITOS.md", "CREDITOS.md")
    codigo = tmp / f"{PREFIXO}{versao}_Codigo_Fonte_GitHub.zip"
    git("archive", "--format=zip", "-o", str(codigo), "HEAD")

    problemas = []
    for p in (gravador, jogo, codigo):
        problemas += procurar_rom(p.name, p.read_bytes(), rom)
    if problemas:
        raise SystemExit("ROM encontrada nos pacotes (ou algo que não deu para conferir), nada foi publicado:\n  "
                         + "\n  ".join(problemas))

    iso = tmp / ISO_TESTE  # tem a ROM: fica só neste PC
    executar([sys.executable, CODIGO / "Fonte/gerar_iso.py", elf, caminho_rom, iso], "gerar a ISO de teste")
    return {"elf": elf, "title": CODIGO / "APPS/SMW/title.cfg", "gravador": gravador, "iso": iso,
            "jogo": jogo, "codigo": codigo}


def temporario(destino: Path, fim: str) -> Path:
    """Um nome que ninguém usa, na mesma pasta do destino (para o os.replace)."""
    fd, nome = tempfile.mkstemp(dir=destino.parent, prefix=f".{destino.name}.", suffix=fim)
    os.close(fd)
    return Path(nome)


def publicar(arquivos: list):
    """[(pronto, destino)]: copia cada um para um temporário único ao lado do
    destino e confere; depois troca todos, guardando os antigos. Se algo
    falhar, volta os antigos e apaga só os temporários deste script."""
    for _, destino in arquivos:
        if destino.is_dir() or (destino.exists() and not destino.is_file()):
            raise SystemExit(f"O destino não é um arquivo: {destino}")
    copias = []
    try:
        for pronto, destino in arquivos:
            destino.parent.mkdir(parents=True, exist_ok=True)
            copia = temporario(destino, ".novo")
            copias.append((copia, destino))
            shutil.copyfile(pronto, copia)
            if sha256_arquivo(copia) != sha256_arquivo(pronto):
                raise OSError(f"cópia diferente do original: {copia}")
    except BaseException:
        for copia, _ in copias:
            copia.unlink(missing_ok=True)
        raise

    trocados = []  # (destino, antigo guardado ou None)
    guardas = []
    try:
        for copia, destino in copias:
            antigo = None
            if destino.exists():
                antigo = temporario(destino, ".antigo")
                guardas.append(antigo)
                os.replace(destino, antigo)
            trocados.append((destino, antigo))
            os.replace(copia, destino)
    except BaseException:
        for destino, antigo in reversed(trocados):
            if antigo is not None and antigo.exists():
                os.replace(antigo, destino)
            elif antigo is None:
                destino.unlink(missing_ok=True)  # não existia antes: é o que este script pôs
        for copia, _ in copias:
            copia.unlink(missing_ok=True)
        for antigo in guardas:
            antigo.unlink(missing_ok=True)  # sobrou vazio (o antigo não chegou a ser movido)
        raise
    for antigo in guardas:
        antigo.unlink()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raiz", type=Path, default=CODIGO.parent,
                    help="Pasta do projeto (padrão: a pasta acima de Codigo)")
    ap.add_argument("--rom", type=Path, help="Cópia do jogo (padrão: <raiz>/ROM/Super Mario World (USA).zip)")
    ap.add_argument("--simular", action="store_true", help="Só confere e mostra o que faria")
    a = ap.parse_args()
    raiz = a.raiz.resolve()
    caminho_rom = (a.rom or raiz / "ROM" / "Super Mario World (USA).zip").resolve()
    github = raiz / "Github"
    anteriores = github / "versoes-anteriores"
    versao = versao_do_codigo()

    erros = conferir(raiz, caminho_rom, versao)
    if erros:
        raise SystemExit("Nada foi feito:\n  " + "\n  ".join(erros))
    rom = Rom(caminho_rom)
    mover = anteriores_a_mover(github, versao)
    para = destinos(raiz, versao)

    elf = CODIGO / "APPS/SMW/SMW.ELF"
    print(f"v{versao}, commit {git('rev-parse', '--short', 'HEAD').strip()}, ELF {sha256_arquivo(elf)[:16]}…")
    for chave in ("elf", "title", "gravador", "iso"):
        acao = "substituir" if para[chave].exists() else "criar"
        extra = " (com a ROM, só local)" if chave == "iso" else ""
        print(f"  {acao}: {para[chave].relative_to(raiz)}{extra}")
    for chave in ("jogo", "codigo"):
        print(f"  criar: {para[chave].relative_to(raiz)}")
    for p in mover:
        print(f"  depois, mover para versoes-anteriores: {p.name}")
    if a.simular:
        print("(simulação: nada foi feito)")
        return 0

    with tempfile.TemporaryDirectory(prefix="smw-pacotes-") as tmp:
        pronto = montar(Path(tmp), versao, caminho_rom, rom)
        saida = [(pronto[chave], para[chave]) for chave in ("elf", "title", "gravador", "iso", "jogo", "codigo")]
        publicar(saida)
    anteriores.mkdir(exist_ok=True)
    for p in mover:
        alvo = anteriores / p.name
        if alvo.exists():
            raise SystemExit(f"Já existe em versoes-anteriores: {p.name} (não movi; os pacotes novos já estão "
                             "no lugar)")
        shutil.move(str(p), str(alvo))

    print("Pronto. SHA-256:")
    for _, destino in saida:
        print(f"  {sha256_arquivo(destino)}  {destino.relative_to(raiz)}")
    print("Os pacotes em Github não têm a ROM (conferido). A ISO de teste tem e fica só neste PC.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
