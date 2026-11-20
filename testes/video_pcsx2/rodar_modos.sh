#!/bin/sh
# Roda o port no PCSX2 em cada modo de vídeo e guarda as capturas.
# Uso: sh rodar_modos.sh ISO PASTA_SAIDA [modos...]
# modos: 240p 240p_169 480i 480i_169 480p 480p_169
set -u
ISO="$1"; SAIDA="$2"; shift 2
AQUI="$(dirname "$0")"
[ $# -eq 0 ] && set -- 240p 240p_169 480i 480i_169 480p 480p_169
for modo in "$@"; do
  case "$modo" in
    240p|240p_169|480i|480i_169|480p|480p_169) ;;
    *) echo "modo desconhecido: $modo" >&2; exit 2 ;;
  esac
done
mkdir -p "$SAIDA"
falhas=0
for modo in "$@"; do
  case "$modo" in
    240p*) video="" ;;
    480i*) video="tecla:direita," ;;
    480p*) video="tecla:direita,tecla:direita," ;;
  esac
  tela=""; case "$modo" in *_169) tela="tecla:baixo,tecla:direita," ;; esac
  roteiro="esperar:8,${video}${tela}esperar:1,foto:1_ajustes,tecla:x,esperar:3,foto:2_confirma,tecla:x,filme:3_abertura:8:1.5,tecla:start,esperar:1,filme:4_jogo:12:1.5"
  echo "== $modo"
  python3 -u "$AQUI/pcsx2_auto.py" --iso "$ISO" --saida "$SAIDA/$modo" --roteiro "$roteiro" \
    > "$SAIDA/$modo.txt" 2>&1 || { falhas=$((falhas + 1)); echo "   FALHOU (veja $SAIDA/$modo.txt)"; }
done
if [ "$falhas" -gt 0 ]; then
  echo "$falhas modo(s) falharam" >&2
  exit 1
fi
