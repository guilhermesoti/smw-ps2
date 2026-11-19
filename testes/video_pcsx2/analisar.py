#!/usr/bin/env python3
"""Analisa as capturas do rodar_modos.sh: tamanho do quadro, largura dos pixels
do SNES (em pontos do PS2), bordas pretas e linhas repetidas.

    python3 analisar.py PASTA_RESULTADOS

Para cada modo, olha a tela "A IMAGEM APARECEU?" e os quadros do jogo
(4_jogo_*): os dois últimos quadros do jogo que tenham conteúdo (não pretos),
quando a tela de título já está parada.
"""
import sys
from collections import Counter
from pathlib import Path

from PIL import Image


def larguras(img):
    """Distâncias entre as colunas onde a cor muda em alguma linha."""
    w, h = img.size
    px = img.load()
    fronteiras = []
    for x in range(1, w):
        for y in range(0, h, 1):
            if px[x, y] != px[x - 1, y]:
                fronteiras.append(x)
                break
    gaps = Counter(b - a for a, b in zip(fronteiras, fronteiras[1:]))
    return fronteiras, gaps


def caixa(img):
    """Retângulo com conteúdo (cor diferente de preto)."""
    return img.convert("L").point(lambda v: 255 if v > 8 else 0).getbbox()


def linhas_iguais(img):
    """Quantos pares de linhas (2k, 2k+1) são idênticos."""
    w, h = img.size
    px = img.load()
    iguais = 0
    for y in range(0, h - 1, 2):
        if all(px[x, y] == px[x, y + 1] for x in range(w)):
            iguais += 1
    return iguais, h // 2


def resumo(nome, arq):
    img = Image.open(arq).convert("RGB")
    w, h = img.size
    fr, gaps = larguras(img)
    top = ", ".join(f"{g}:{n}" for g, n in sorted(gaps.items(), key=lambda t: -t[1])[:6])
    li = linhas_iguais(img) if h > 300 else None
    linha = f"  {nome:<14} {w}x{h}  conteúdo {caixa(img)}  larguras (pontos:vezes) {top}"
    if li:
        linha += f"  linhas repetidas {li[0]}/{li[1]}"
    print(linha)


def main():
    raiz = Path(sys.argv[1])
    for modo in sorted(p for p in raiz.iterdir() if p.is_dir()):
        print(f"== {modo.name}")
        for nome in ["1_ajustes", "2_confirma"]:
            arq = modo / f"{nome}.png"
            if arq.exists():
                resumo(nome, arq)
        jogo = sorted(modo.glob("4_jogo_*.png"))
        com_conteudo = [j for j in jogo if caixa(Image.open(j).convert("RGB"))]
        for j in com_conteudo[-2:]:
            resumo(j.stem, j)


if __name__ == "__main__":
    main()
