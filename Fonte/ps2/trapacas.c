#include "trapacas.h"
#include <stdio.h>
#include <string.h>

static int hex(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool espaco(int c) { return c == ' ' || c == '\t' || c == '\r'; }

/* Endereco do SNES -> posicao em g_ram, ou -1. */
static long posicao_ram(uint32_t endereco) {
  unsigned banco = endereco >> 16, resto = endereco & 0xFFFF;
  if (banco == 0x7E) return (long)resto;
  if (banco == 0x7F) return 0x10000L + resto;
  if ((banco <= 0x3F || (banco >= 0x80 && banco <= 0xBF)) && resto < 0x2000) return (long)resto;
  return -1;
}

/* Um codigo em s[0..n): "AAAAAA:VV", "AAAAAA=VV" ou "AAAAAAVV" (um ':' ou
   '=' so, depois do sexto algarismo). */
static bool codigo(const char *s, size_t n, TrapacaCodigo *c) {
  char d[8];
  size_t k = 0;
  bool separou = false;
  for (size_t i = 0; i < n; ++i) {
    if ((s[i] == ':' || s[i] == '=') && k == 6 && !separou) {
      separou = true;
      continue;
    }
    if (hex((unsigned char)s[i]) < 0 || k == 8) return false;
    d[k++] = s[i];
  }
  if (k != 8) return false;
  uint32_t endereco = 0;
  for (int i = 0; i < 6; ++i) endereco = endereco << 4 | (uint32_t)hex(d[i]);
  long p = posicao_ram(endereco);
  if (p < 0) return false;
  c->ram = (uint32_t)p;
  c->valor = (uint8_t)(hex(d[6]) << 4 | hex(d[7]));
  return true;
}

/* Copia o nome (sem espacos nas pontas), ate TRAPACA_NOME letras. Fica so o
   que a fonte dos menus mostra: ASCII e as letras com acento do portugues
   (0xC3 xx em UTF-8); o resto vira '?'. */
static void copia_nome(char *dst, const char *s, size_t n) {
  while (n && espaco((unsigned char)*s)) ++s, --n;
  while (n && espaco((unsigned char)s[n - 1])) --n;
  size_t o = 0;
  int letras = 0;
  for (size_t i = 0; i < n && letras < TRAPACA_NOME;) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 0x20 && c < 0x7F) {
      dst[o++] = (char)c, ++i;
    } else if (c == 0xC3 && i + 1 < n && ((unsigned char)s[i + 1] & 0xC0) == 0x80) {
      dst[o++] = (char)c, dst[o++] = s[i + 1], i += 2;
    } else {
      dst[o++] = '?', ++i;
      while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;  /* resto do caractere */
    }
    ++letras;
  }
  /* O corte em TRAPACA_NOME pode deixar um espaco no fim: sai, como o do
     comeco (o nome volta igual do TRAPACAS.LIG; revisao do Codex). */
  while (o && dst[o - 1] == ' ') --o;
  dst[o] = '\0';
}

size_t trapacas_corte(const char *texto, size_t n) {
  if (n <= TRAPACAS_BYTES) return n;
  size_t fim = TRAPACAS_BYTES;
  while (fim && texto[fim - 1] != '\n') --fim;
  return fim;
}

void trapacas_le(Trapacas *t, const char *texto, size_t n) {
  memset(t, 0, sizeof(*t));
  size_t i = 0;
  int numero = 0;
  /* Marca de ordem de bytes do UTF-8 no comeco (Bloco de Notas). */
  if (n >= 3 && (unsigned char)texto[0] == 0xEF && (unsigned char)texto[1] == 0xBB &&
      (unsigned char)texto[2] == 0xBF)
    i = 3;
  while (i < n) {
    size_t ini = i;
    while (i < n && texto[i] != '\n') ++i;
    size_t fim = i;
    ++i;
    ++numero;
    const char *l = texto + ini;
    size_t tam = fim - ini;
    while (tam && espaco((unsigned char)*l)) ++l, --tam;
    if (!tam || *l == '#' || *l == ';') continue;
    const char *igual = memchr(l, '=', tam);
    if (!igual) continue;
    if (t->n == TRAPACA_MAX) {
      ++t->ignoradas;
      continue;
    }
    Trapaca *tr = &t->t[t->n];
    memset(tr, 0, sizeof(*tr));
    tr->linha = numero;
    copia_nome(tr->nome, l, (size_t)(igual - l));
    if (!tr->nome[0]) snprintf(tr->nome, sizeof(tr->nome), "Linha %d", numero);
    const char *s = igual + 1, *e = l + tam;
    while (s < e) {
      while (s < e && (espaco((unsigned char)*s) || *s == '+' || *s == ',')) ++s;
      const char *c = s;
      while (s < e && !espaco((unsigned char)*s) && *s != '+' && *s != ',') ++s;
      if (s == c) continue;
      TrapacaCodigo cod;
      if (tr->codigos < TRAPACA_CODIGOS && codigo(c, (size_t)(s - c), &cod))
        tr->codigo[tr->codigos++] = cod;
      else
        ++tr->recusados;
    }
    ++t->n;
  }
}

/* Endereco do SNES de uma posicao em g_ram (o banco 7E ou 7F). */
static uint32_t endereco_snes(uint32_t ram) { return (ram < 0x10000 ? 0x7E0000u : 0x7F0000u) | (ram & 0xFFFF); }

static bool mesmos_codigos(const Trapaca *a, const Trapaca *b) {
  if (a->codigos != b->codigos) return false;
  for (int c = 0; c < a->codigos; ++c)
    if (a->codigo[c].ram != b->codigo[c].ram || a->codigo[c].valor != b->codigo[c].valor) return false;
  return true;
}

int trapacas_liga_salvas(Trapacas *t, const char *texto, size_t n) {
  static Trapacas salvas;  /* o TRAPACAS.LIG lido com o mesmo leitor (BOM, comentarios) */
  trapacas_le(&salvas, texto, n);
  int ligou = 0;
  for (int s = 0; s < salvas.n; ++s) {
    const Trapaca *e = &salvas.t[s];
    if (!e->codigos) continue;
    for (int k = 0; k < t->n; ++k) {
      Trapaca *x = &t->t[k];
      if (!x->ligada && strcmp(x->nome, e->nome) == 0 && mesmos_codigos(x, e)) {
        x->ligada = true;
        ++ligou;
        break;
      }
    }
  }
  return ligou;
}

int trapacas_texto_ligadas(const Trapacas *t, char *texto, size_t tamanho) {
  if (!tamanho) return 0;
  size_t n = 0;
  int w = snprintf(texto, tamanho, "# Trapacas ligadas no PS2 (o jogo refaz este arquivo)\n");
  if (w < 0) return 0;
  n = (size_t)w;
  for (int k = 0; k < t->n && n < tamanho; ++k) {
    const Trapaca *tr = &t->t[k];
    if (!tr->ligada || tr->codigos <= 0) continue;
    w = snprintf(texto + n, tamanho - n, "%s =", tr->nome);
    for (int c = 0; c < tr->codigos && w >= 0 && n + (size_t)w < tamanho; ++c) {
      int m = snprintf(texto + n + w, tamanho - n - (size_t)w, "%s%06X:%02X", c ? "+" : " ",
                       (unsigned)endereco_snes(tr->codigo[c].ram), tr->codigo[c].valor);
      w = m < 0 ? -1 : w + m;
    }
    if (w < 0) break;
    if (n + (size_t)w < tamanho) {
      int m = snprintf(texto + n + w, tamanho - n - (size_t)w, "\n");
      w = m < 0 ? -1 : w + m;
    }
    if (w < 0 || n + (size_t)w >= tamanho) break;  /* a linha nao coube: fica de fora inteira */
    n += (size_t)w;
  }
  texto[n < tamanho ? n : tamanho - 1] = '\0';
  return (int)(n < tamanho ? n : tamanho - 1);
}

int trapacas_ligadas(const Trapacas *t) {
  int n = 0;
  for (int k = 0; k < t->n; ++k) n += t->t[k].ligada && t->t[k].codigos > 0;
  return n;
}

void trapacas_aplica(const Trapacas *t, uint8_t *ram) {
  for (int k = 0; k < t->n; ++k) {
    const Trapaca *tr = &t->t[k];
    if (!tr->ligada) continue;
    for (int c = 0; c < tr->codigos; ++c) ram[tr->codigo[c].ram] = tr->codigo[c].valor;
  }
}
