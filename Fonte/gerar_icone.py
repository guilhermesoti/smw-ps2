"""Gera ps2/smw_icon.h: o icone 3D (.icn) e o icon.sys do save no Memory Card.

O navegador do PS2 mostra uma pasta de save como "dados corrompidos" quando
falta o icon.sys ou o icone. Arte original (sem personagens): um cartucho com
o rotulo SMW / PS2 / RETROGAMES. Formato conferido com saves comerciais:
cabecalho de 20 bytes, vertices com coordenadas em 1/4096 (Y negativo para
cima), animacao com quadros de 8 bytes e textura 128x128 ABGR1555 sem
compressao (tipo 0x07).
"""
import struct
from pathlib import Path

W = H = 128
NAVY, RED, WHITE, GOLD, SIDE = (20, 32, 90), (200, 30, 40), (250, 250, 250), (250, 200, 40), (70, 80, 110)
FONT = {  # 5x7
    'S': ["01111","10000","10000","01110","00001","00001","11110"],
    'M': ["10001","11011","10101","10101","10001","10001","10001"],
    'W': ["10001","10001","10001","10101","10101","11011","10001"],
    'P': ["11110","10001","10001","11110","10000","10000","10000"],
    '2': ["01110","10001","00001","00010","00100","01000","11111"],
    'R': ["11110","10001","10001","11110","10100","10010","10001"],
    'E': ["11111","10000","10000","11110","10000","10000","11111"],
    'T': ["11111","00100","00100","00100","00100","00100","00100"],
    'O': ["01110","10001","10001","10001","10001","10001","01110"],
    'G': ["01110","10001","10000","10111","10001","10001","01111"],
    'A': ["01110","10001","10001","11111","10001","10001","10001"],
    ' ': ["00000"] * 7,
}

def texture():
    px = [[NAVY] * W for _ in range(H)]
    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[y][x] = c
    def text(s, cx, y0, scale, c):
        width = len(s) * 6 * scale - scale
        x = cx - width // 2
        for ch in s:
            for r, row in enumerate(FONT[ch]):
                for col, bit in enumerate(row):
                    if bit == '1':
                        rect(x + col * scale, y0 + r * scale, x + (col + 1) * scale, y0 + (r + 1) * scale, c)
            x += 6 * scale
    rect(0, 0, W, 4, GOLD)
    rect(0, 8, W, 52, RED)
    text("SMW", 64, 16, 4, WHITE)
    text("PS2", 64, 62, 4, GOLD)
    text("RETROGAMES", 64, 104, 1, WHITE)
    rect(0, 120, W, 124, GOLD)
    rect(120, 124, 128, 128, SIDE)  # cor das laterais
    out = bytearray()
    for row in px:
        for r, g, b in row:
            out += struct.pack('<H', (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000)
    return bytes(out)

def box():
    """Caixa x +-1.0, y 0..-2.6 (topo), z +-0.3; rotulo na frente e atras."""
    x0, x1, yb, yt, zf, zb = -1.0, 1.0, 0.0, -2.6, -0.3, 0.3
    side_uv = (124 / 128, 126 / 128)
    faces = []  # (4 cantos, normal, uvs)
    vb = 124 / 128  # o rotulo termina antes do bloco de cor das laterais
    lab = [(0, 0), (1, 0), (1, vb), (0, vb)]          # topo-esq, topo-dir, baixo-dir, baixo-esq
    faces.append(([(x0, yt, zf), (x1, yt, zf), (x1, yb, zf), (x0, yb, zf)], (0, 0, -1), lab))
    faces.append(([(x1, yt, zb), (x0, yt, zb), (x0, yb, zb), (x1, yb, zb)], (0, 0, 1), lab))
    s = [side_uv] * 4
    faces.append(([(x0, yt, zb), (x0, yt, zf), (x0, yb, zf), (x0, yb, zb)], (-1, 0, 0), s))
    faces.append(([(x1, yt, zf), (x1, yt, zb), (x1, yb, zb), (x1, yb, zf)], (1, 0, 0), s))
    faces.append(([(x0, yt, zb), (x1, yt, zb), (x1, yt, zf), (x0, yt, zf)], (0, -1, 0), s))
    faces.append(([(x0, yb, zf), (x1, yb, zf), (x1, yb, zb), (x0, yb, zb)], (0, 1, 0), s))
    tris = []
    for corners, n, uv in faces:
        for a, b, c in ((0, 1, 2), (0, 2, 3)):
            tri = [(corners[i], uv[i]) for i in (a, b, c)]
            tris.append((tri, n))
            # mesma face com a outra orientacao: visivel mesmo se o navegador descartar faces de tras
            tris.append(([tri[0], tri[2], tri[1]], tuple(-v for v in n)))
    return tris

def icn():
    f16 = lambda v: max(-32768, min(32767, int(round(v * 4096))))
    tris = box()
    out = bytearray(struct.pack('<IIIfI', 0x10000, 1, 0x07, 1.0, len(tris) * 3))
    for tri, n in tris:
        for (x, y, z), (u, v) in tri:
            out += struct.pack('<hhhH', f16(x), f16(y), f16(z), 0x400)
            out += struct.pack('<hhhH', f16(n[0]), f16(n[1]), f16(n[2]), 0x400)
            out += struct.pack('<hh', f16(u), f16(v))
            out += bytes((255, 255, 255, 255))
    out += struct.pack('<IIfII', 1, 16, 1.0, 0, 1)   # cabecalho da animacao
    out += struct.pack('<II', 0, 1) + struct.pack('<ff', 1.0, 1.0)  # 1 quadro, 1 chave
    out += texture()
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
    bg = [(20, 32, 90, 0), (120, 20, 30, 0), (120, 20, 30, 0), (20, 32, 90, 0)]
    for i, c in enumerate(bg): struct.pack_into('<4i', d, 16 + 16 * i, *c)
    for i, v in enumerate([(0.5, 0.5, 0.5, 0.0), (0.0, -0.4, -0.1, 0.0), (-0.5, -0.5, 0.5, 0.0)]):
        struct.pack_into('<4f', d, 80 + 16 * i, *v)
    for i, v in enumerate([(0.3, 0.3, 0.3, 0.0), (0.4, 0.4, 0.4, 0.0), (0.5, 0.5, 0.5, 0.0)]):
        struct.pack_into('<4f', d, 128 + 16 * i, *v)
    struct.pack_into('<4f', d, 176, 0.5, 0.5, 0.5, 0.0)
    d[192:192 + len(title)] = title
    for off in (260, 324, 388):
        d[off:off + len(icon_name)] = icon_name.encode()
    return bytes(d)

def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] __attribute__((aligned(64))) = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)

if __name__ == '__main__':
    here = Path(__file__).resolve().parent
    icon = icn()
    sysf = icon_sys("smwps2.icn")
    (here / "ps2" / "smw_icon.h").write_text(
        "/* Gerado por Fonte/gerar_icone.py: icone do save no Memory Card. */\n"
        "#define SMW_ICON_NAME \"smwps2.icn\"\n" + c_array("kSmwIcon", icon) + "\n" + c_array("kSmwIconSys", sysf) + "\n")
    (here.parent / "testes" / "smwps2.icn").write_bytes(icon)
    (here.parent / "testes" / "icon.sys").write_bytes(sysf)
    print(len(icon), len(sysf))
