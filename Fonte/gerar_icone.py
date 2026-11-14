"""Gera ps2/smw_icon.h: o icone 3D (.icn) e o icon.sys do save no Memory Card.

Sem icon.sys e icone, o navegador do PS2 mostra a pasta como "dados
corrompidos". Arte original (sem personagens da Nintendo): um cartucho cinza
generico, levemente girado, com rotulo de um gatinho pulando e a faixa
"SMW PS2". Formato conferido com saves comerciais: cabecalho de 20 bytes,
coordenadas em 1/4096 (Y negativo para cima), quadros de animacao de 8 bytes
e textura 128x128 ABGR1555 sem compressao (tipo 0x07).
"""
import math
import struct
from pathlib import Path

W = H = 128
ICON_VERSION = b"SMWICON3"  # muda quando o desenho muda: o jogo regrava o icone
BODY, DARK = (150, 150, 158), (95, 95, 105)
PAL = {'K': (30, 24, 40), 'O': (245, 140, 40), 'W': (250, 245, 235), 'P': (240, 120, 150),
       'T': (30, 170, 170), 't': (20, 120, 125), 'Y': (255, 230, 80)}
CAT = [
    "....K...........K.....",
    "...KOK.........KOK....",
    "...KOOK.......KOOK....",
    "...KOPOKKKKKKKOPOK....",
    "..KOOOOOOOOOOOOOOOK...",
    "..KOOOOOOOOOOOOOOOK...",
    ".KOOOOKKOOOOOKKOOOOK..",
    ".KOOOOKWOOOOOKWOOOOK..",
    ".KOOOOKKOOOOOKKOOOOK..",
    "KKOOWWOOOOPPOOOOWWOKK.",
    ".KOOWWOOOKWWKOOOWWOK..",
    "..KOOOOOOOKKOOOOOOK...",
    "...KKOOOOOOOOOOOKK....",
    "KK...KKKKKKKKKKK...KK.",
    "KOK.KTTTTTTTTTTTK.KOK.",
    "KOOKTTTTTTTTTTTTTKOOK.",
    ".KOKTTTTTTTTTTTTTKOK..",
    "..KKTTTTWWWWWTTTTKK...",
    "....KTTTWWWWWTTTK.....",
    "....KtTTTTTTTTTtK.....",
    "....KttttKKKttttK.....",
    "...KOOK.......KOOK....",
    "..KOOK.........KOOK...",
    "..KWWK.........KWWK...",
    "...KK...........KK....",
]
FONT = {
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'W': ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    '2': ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    ' ': ["00000"] * 7,
}
LABEL_V = 125 / 128          # o rotulo usa as linhas 0-124; abaixo ficam as cores do cartucho
UV_BODY = (124 / 128, 126.5 / 128)
UV_DARK = (116 / 128, 126.5 / 128)

def texture():
    px = [[(0, 0, 0)] * W for _ in range(H)]
    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            px[y][x] = c
    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                put(x, y, c)
    for y in range(H):  # ceu em degrade
        t = y / 96
        for x in range(W):
            px[y][x] = (int(90 + 80 * t), int(160 + 60 * t), 255)
    for cx, cy in ((10, 18), (84, 30)):  # nuvens
        for dx, dy, r in ((0, 0, 7), (8, -3, 8), (16, 0, 6)):
            for y in range(-r, r + 1):
                for x in range(-r, r + 1):
                    if x * x + y * y <= r * r:
                        put(cx + dx + x, cy + dy + y, (255, 255, 255))
    for y in range(96, 104):  # chao de tijolos
        for x in range(W):
            mortar = y == 103 or (x + (8 if (y // 8) % 2 else 0)) % 16 == 15
            put(x, y, (110, 60, 40) if mortar else (190, 100, 60))
    for cx, cy in ((22, 44), (104, 56), (96, 14), (30, 80)):  # estrelinhas
        for d in range(-2, 3):
            put(cx + d, cy, PAL['Y'])
            put(cx, cy + d, PAL['Y'])
        put(cx, cy, (255, 255, 255))
    s = 3
    ox, oy = 64 - len(CAT[0]) * s // 2, 20
    for r, row in enumerate(CAT):
        for c, ch in enumerate(row):
            if ch != '.':
                rect(ox + c * s, oy + r * s, ox + (c + 1) * s, oy + (r + 1) * s, PAL[ch])
    rect(0, 103, W, 104, PAL['Y'])  # faixa com o nome
    rect(0, 104, W, 124, (200, 30, 40))
    rect(0, 124, W, 125, PAL['Y'])
    text, scale = "SMW PS2", 2
    x = 64 - (len(text) * 6 * scale - scale) // 2
    for ch in text:
        for r, row in enumerate(FONT[ch]):
            for c, bit in enumerate(row):
                if bit == '1':
                    rect(x + c * scale, 107 + r * scale, x + (c + 1) * scale, 107 + (r + 1) * scale, (255, 255, 255))
        x += 6 * scale
    rect(0, 125, W, 128, BODY)       # cores do corpo do cartucho
    rect(112, 125, 120, 128, DARK)
    out = bytearray()
    for row in px:
        for r, g, b in row:
            out += struct.pack('<H', (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000)
    return bytes(out), px

def quad(corners, normal, uvs):
    """Dois triangulos, mais as copias viradas (visiveis dos dois lados)."""
    tris = []
    for a, b, c in ((0, 1, 2), (0, 2, 3)):
        tri = [(corners[i], uvs[i]) for i in (a, b, c)]
        tris.append((tri, normal))
        tris.append(([tri[0], tri[2], tri[1]], tuple(-v for v in normal)))
    return tris

def box(x0, x1, yt, yb, zf, zb, uv):
    u = [uv] * 4
    return (quad([(x0, yt, zf), (x1, yt, zf), (x1, yb, zf), (x0, yb, zf)], (0, 0, -1), u) +
            quad([(x1, yt, zb), (x0, yt, zb), (x0, yb, zb), (x1, yb, zb)], (0, 0, 1), u) +
            quad([(x0, yt, zb), (x0, yt, zf), (x0, yb, zf), (x0, yb, zb)], (-1, 0, 0), u) +
            quad([(x1, yt, zf), (x1, yt, zb), (x1, yb, zb), (x1, yb, zf)], (1, 0, 0), u) +
            quad([(x0, yt, zb), (x1, yt, zb), (x1, yt, zf), (x0, yt, zf)], (0, -1, 0), u) +
            quad([(x0, yb, zf), (x1, yb, zf), (x1, yb, zb), (x0, yb, zb)], (0, 1, 0), u))

def cartridge():
    """Cartucho generico: corpo cinza, ressalto no topo, ranhuras de pegada e rotulo."""
    tris = box(-1.1, 1.1, -2.7, 0.0, -0.25, 0.25, UV_BODY)              # corpo
    tris += box(-0.95, 0.95, -2.9, -2.7, -0.2, 0.2, UV_DARK)            # ressalto de cima
    for i in range(4):                                                  # ranhuras de pegada
        y = -0.18 - i * 0.12
        tris += box(-0.7, 0.7, y - 0.05, y, -0.29, -0.25, UV_DARK)
    lab = [(0, 0), (1, 0), (1, LABEL_V), (0, LABEL_V)]
    zl = -0.27
    tris += quad([(-0.9, -2.55, zl), (0.9, -2.55, zl), (0.9, -0.7, zl), (-0.9, -0.7, zl)], (0, 0, -1), lab)
    return tris

def rotate(tris, yaw):
    c, s = math.cos(yaw), math.sin(yaw)
    rot = lambda v: (v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c)
    return [([(rot(p), uv) for p, uv in tri], rot(n)) for tri, n in tris]

def icn():
    f16 = lambda v: max(-32768, min(32767, int(round(v * 4096))))
    tris = rotate(cartridge(), math.radians(-18))   # levemente girado
    out = bytearray(struct.pack('<IIIfI', 0x10000, 1, 0x07, 1.0, len(tris) * 3))
    for tri, n in tris:
        for (x, y, z), (u, v) in tri:
            out += struct.pack('<hhhH', f16(x), f16(y), f16(z), 0x400)
            out += struct.pack('<hhhH', f16(n[0]), f16(n[1]), f16(n[2]), 0x400)
            out += struct.pack('<hh', f16(u), f16(v))
            out += bytes((255, 255, 255, 255))
    out += struct.pack('<IIfII', 1, 16, 1.0, 0, 1)                  # cabecalho da animacao
    out += struct.pack('<II', 0, 1) + struct.pack('<ff', 1.0, 1.0)  # 1 quadro, 1 chave
    out += texture()[0]
    return bytes(out)

def sjis(s):
    out = bytearray()
    for ch in s:
        o = ord(ch)
        if ch == ' ': out += b'\x81\x40'
        elif '0' <= ch <= '9': out += struct.pack('>H', 0x824F + o - 48)
        elif 'A' <= ch <= 'Z': out += struct.pack('>H', 0x8260 + o - 65)
        elif 'a' <= ch <= 'z': out += struct.pack('>H', 0x8281 + o - 97)
        else: raise ValueError(ch)
    return bytes(out)

def icon_sys(icon_name):
    line1, line2 = "Super Mario World", "PS2 Retrogames"
    title = sjis(line1) + sjis(line2)
    assert len(title) <= 68
    d = bytearray(964)
    d[0:4] = b'PS2D'
    struct.pack_into('<HHII', d, 4, 0, len(sjis(line1)), 0, 0x60)
    for i, c in enumerate([(20, 32, 90, 0), (120, 20, 30, 0), (120, 20, 30, 0), (20, 32, 90, 0)]):
        struct.pack_into('<4i', d, 16 + 16 * i, *c)
    for i, v in enumerate([(0.5, 0.5, 0.5, 0.0), (0.0, -0.4, -0.1, 0.0), (-0.5, -0.5, 0.5, 0.0)]):
        struct.pack_into('<4f', d, 80 + 16 * i, *v)
    for i, v in enumerate([(0.3, 0.3, 0.3, 0.0), (0.4, 0.4, 0.4, 0.0), (0.5, 0.5, 0.5, 0.0)]):
        struct.pack_into('<4f', d, 128 + 16 * i, *v)
    struct.pack_into('<4f', d, 176, 0.5, 0.5, 0.5, 0.0)
    d[192:192 + len(title)] = title
    for off in (260, 324, 388):
        d[off:off + len(icon_name)] = icon_name.encode()
    d[452:452 + len(ICON_VERSION)] = ICON_VERSION   # area reservada: marca a versao do desenho
    return bytes(d)

def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] __attribute__((aligned(64))) = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)

if __name__ == '__main__':
    here = Path(__file__).resolve().parent
    icon, sysf = icn(), icon_sys("smwps2.icn")
    (here / "ps2" / "smw_icon.h").write_text(
        "/* Gerado por Fonte/gerar_icone.py: icone do save no Memory Card. */\n"
        "#define SMW_ICON_NAME \"smwps2.icn\"\n" + c_array("kSmwIcon", icon) + "\n" + c_array("kSmwIconSys", sysf) + "\n")
    print(len(icon), len(sysf))
