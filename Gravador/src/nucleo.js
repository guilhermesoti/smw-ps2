/* Gravador SMW PS2 — núcleo: confere a ROM, monta a ISO, o ZIP e o SMW.CFG.
 * Roda no navegador (dentro do Gravador_SMW_PS2.html) e no Node (testes).
 * Nada sai do computador: tudo é feito na memória. */
const Nucleo = (() => {
  'use strict';

  const ROM_BYTES = 512 * 1024;
  const ROM_CRC = 0xB19ED489;

  /* ---------------------------------------------------------------- CRC32 */
  const tabelaCrc = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();

  function crc32(dados) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < dados.length; i++) c = tabelaCrc[(c ^ dados[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }

  const hex8 = (n) => (n >>> 0).toString(16).toUpperCase().padStart(8, '0');

  class ErroRom extends Error {
    constructor(codigo, detalhe) {
      super(codigo);
      this.codigo = codigo;
      Object.assign(this, detalhe || {});
    }
  }

  /* ------------------------------------------------------- leitura do ZIP */
  const u16 = (d, i) => d[i] | (d[i + 1] << 8);
  const u32 = (d, i) => (d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | (d[i + 3] << 24)) >>> 0;

  function entradasZip(d) {
    let fim = -1;
    for (let i = d.length - 22; i >= 0 && i >= d.length - 22 - 65535; i--)
      if (u32(d, i) === 0x06054B50) { fim = i; break; }
    if (fim < 0) throw new ErroRom('ZIP_INVALIDO');
    const total = u16(d, fim + 10);
    let p = u32(d, fim + 16);
    const lista = [];
    for (let k = 0; k < total; k++) {
      if (p + 46 > d.length || u32(d, p) !== 0x02014B50) throw new ErroRom('ZIP_INVALIDO');
      const metodo = u16(d, p + 10), comprimido = u32(d, p + 20), tamanho = u32(d, p + 24);
      const nLen = u16(d, p + 28), xLen = u16(d, p + 30), cLen = u16(d, p + 32);
      const local = u32(d, p + 42);
      const nome = new TextDecoder().decode(d.subarray(p + 46, p + 46 + nLen));
      lista.push({ nome, metodo, comprimido, tamanho, local });
      p += 46 + nLen + xLen + cLen;
    }
    return lista;
  }

  async function extrairZip(d, e, inflar) {
    if (u32(d, e.local) !== 0x04034B50) throw new ErroRom('ZIP_INVALIDO');
    const inicio = e.local + 30 + u16(d, e.local + 26) + u16(d, e.local + 28);
    const bruto = d.subarray(inicio, inicio + e.comprimido);
    if (e.metodo === 0) return bruto.slice();
    if (e.metodo === 8) return await inflar(bruto);
    throw new ErroRom('ZIP_METODO', { metodo: e.metodo });
  }

  /* --------------------------------------------------------- conferir ROM
   * Aceita .zip (com .sfc/.smc dentro), .sfc e .smc, com ou sem o cabeçalho
   * de copiador de 512 bytes, como o próprio jogo no PS2. Devolve a ROM de
   * 512 KB sem cabeçalho. inflar: função async (bytes deflate -> bytes). */
  async function conferirRom(nomeArquivo, dados, inflar) {
    let rom = dados, interno = null;
    const ehZip = dados.length >= 4 && u32(dados, 0) === 0x04034B50;
    if (ehZip) {
      const candidatas = entradasZip(dados).filter((e) => /\.(sfc|smc)$/i.test(e.nome) && !e.nome.endsWith('/'));
      if (!candidatas.length) throw new ErroRom('ZIP_SEM_ROM');
      let ultimoErro = null;
      for (const e of candidatas) {
        try {
          const r = normalizar(await extrairZip(dados, e, inflar));
          return { ...r, interno: e.nome, zip: true };
        } catch (erro) {
          if (!(erro instanceof ErroRom)) throw erro;
          ultimoErro = ultimoErro || erro;
        }
      }
      throw ultimoErro;
    }
    if (!/\.(sfc|smc|bin)$/i.test(nomeArquivo || '') && dados.length !== ROM_BYTES && dados.length !== ROM_BYTES + 512)
      throw new ErroRom('FORMATO');
    return { ...normalizar(rom), interno, zip: false };
  }

  function normalizar(rom) {
    let cabecalho = false;
    if (rom.length === ROM_BYTES + 512) rom = rom.subarray(512), cabecalho = true;
    if (rom.length !== ROM_BYTES) throw new ErroRom('TAMANHO', { tamanho: rom.length });
    const crc = crc32(rom);
    if (crc !== ROM_CRC) throw new ErroRom('CRC', { crc: hex8(crc) });
    return { rom: rom.slice(), crc: hex8(crc), cabecalho };
  }

  /* ------------------------------------------------------------- SMW.CFG */
  const OPCOES = {
    tela: ['perguntar', '4:3', '16:9'],
    video: ['perguntar', '240p', '480p'],
    idioma: ['pt', 'en'],
  };

  function gerarCfg(ajustes) {
    const v = (chave) => (OPCOES[chave].includes(ajustes[chave]) ? ajustes[chave] : OPCOES[chave][0]);
    return [
      '# Gravador SMW PS2 - como o jogo abre',
      '# tela: perguntar, 4:3 ou 16:9 | video: perguntar, 240p ou 480p | idioma: pt ou en',
      'tela=' + v('tela'),
      'video=' + v('video'),
      'idioma=' + v('idioma'),
      '',
    ].join('\r\n');
  }

  const texto = (s) => new TextEncoder().encode(s);
  const TITLE_CFG = 'title=Super Mario World\nboot=SMW.ELF\n';
  const SYSTEM_CNF = 'BOOT2 = cdrom0:\\SMWG_000.01;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n';
  const NOME_ISO = 'SMWG_000.01.Super Mario World.iso';
  const APP_ISO = 'GRAVADOR SMW PS2 - RETROGAMES';

  /* --------------------------------------------------------------- datas
   * quando: {ano, mes, dia, hora, min, seg, fuso} — fuso em minutos a leste
   * de UTC (Brasília = -180). Sem quando, usa o relógio do computador. */
  function agora(d) {
    d = d || new Date();
    return { ano: d.getFullYear(), mes: d.getMonth() + 1, dia: d.getDate(), hora: d.getHours(),
             min: d.getMinutes(), seg: d.getSeconds(), fuso: -d.getTimezoneOffset() };
  }

  /* ----------------------------------------------------------------- ISO
   * ISO9660 nível 1, só a pasta raiz, com a mesma arrumação do pycdlib (que
   * gerava a ISO testada no OPL): setores 16 PVD, 17 terminador, 18 versão,
   * 19 e 21 tabelas de caminho (L e M), 23 a raiz, e os arquivos em ordem
   * de nome a partir do 24. arquivos: [{nome: 'SMW.CFG;1', dados}]. */
  function gerarIso(arquivos, quando, opcoes) {
    opcoes = opcoes || {};
    const S = 2048;
    const q = quando || agora();
    const lista = arquivos.map((a) => ({ nome: a.nome.toUpperCase(), dados: a.dados }))
      .sort((a, b) => (a.nome < b.nome ? -1 : a.nome > b.nome ? 1 : 0));
    let setor = 24;
    for (const a of lista) {
      a.setor = setor;
      setor += Math.max(1, Math.ceil(a.dados.length / S));
    }
    const total = setor;
    const iso = new Uint8Array(total * S);
    const dv = new DataView(iso.buffer);
    const ambos32 = (o, v) => { dv.setUint32(o, v, true); dv.setUint32(o + 4, v, false); };
    const ambos16 = (o, v) => { dv.setUint16(o, v, true); dv.setUint16(o + 2, v, false); };
    const escreve = (o, s, n, preenche) => {
      const b = texto(s);
      for (let i = 0; i < n; i++) iso[o + i] = i < b.length ? b[i] : preenche;
    };
    const fuso15 = Math.round(q.fuso / 15);
    const dataDir = (o) => {
      iso.set([q.ano - 1900, q.mes, q.dia, q.hora, q.min, q.seg, fuso15 & 0xFF], o);
    };
    const dataVol = (o) => {
      const p = (n, w) => String(n).padStart(w, '0');
      escreve(o, p(q.ano, 4) + p(q.mes, 2) + p(q.dia, 2) + p(q.hora, 2) + p(q.min, 2) + p(q.seg, 2) + '00', 16, 0);
      iso[o + 16] = fuso15 & 0xFF;
    };
    const registro = (o, setorDados, tamanho, flags, nome) => {
      const n = nome.length, len = 33 + n + (n % 2 === 0 ? 1 : 0);
      iso[o] = len;
      ambos32(o + 2, setorDados);
      ambos32(o + 10, tamanho);
      dataDir(o + 18);
      iso[o + 25] = flags;
      ambos16(o + 28, 1);
      iso[o + 32] = n;
      iso.set(nome, o + 33);
      return len;
    };

    /* PVD */
    const v = 16 * S;
    iso[v] = 1;
    escreve(v + 1, 'CD001', 5, 0);
    iso[v + 6] = 1;
    escreve(v + 8, opcoes.sistema || 'PLAYSTATION', 32, 0x20);
    escreve(v + 40, opcoes.volume || 'SUPER_MARIO_WORLD', 32, 0x20);
    ambos32(v + 80, total);
    ambos16(v + 120, 1);
    ambos16(v + 124, 1);
    ambos16(v + 128, S);
    ambos32(v + 132, 10);
    dv.setUint32(v + 140, 19, true);
    dv.setUint32(v + 148, 21, false);
    registro(v + 156, 23, S, 2, [0]);
    escreve(v + 190, '', 128, 0x20);
    escreve(v + 318, '', 128, 0x20);
    escreve(v + 446, '', 128, 0x20);
    escreve(v + 574, opcoes.aplicativo || APP_ISO, 128, 0x20);
    escreve(v + 702, '', 37, 0x20);
    escreve(v + 739, '', 37, 0x20);
    escreve(v + 776, '', 37, 0x20);
    dataVol(v + 813);
    dataVol(v + 830);
    escreve(v + 847, '0000000000000000', 16, 0);
    dataVol(v + 864);
    iso[v + 881] = 1;
    escreve(v + 883, '', 512, 0x20);

    /* terminador */
    const t = 17 * S;
    iso[t] = 0xFF;
    escreve(t + 1, 'CD001', 5, 0);
    iso[t + 6] = 1;

    /* tabelas de caminho: só a raiz */
    iso.set([1, 0, 23, 0, 0, 0, 1, 0, 0, 0], 19 * S);
    iso.set([1, 0, 0, 0, 0, 23, 0, 1, 0, 0], 21 * S);

    /* raiz */
    let o = 23 * S;
    o += registro(o, 23, S, 2, [0]);
    o += registro(o, 23, S, 2, [1]);
    for (const a of lista) o += registro(o, a.setor, a.dados.length, 0, texto(a.nome));
    if (o > 24 * S) throw new Error('Pasta raiz grande demais');

    for (const a of lista) iso.set(a.dados, a.setor * S);
    return iso;
  }

  /* ----------------------------------------------------------------- ZIP
   * ZIP sem compressão (os arquivos já são pequenos ou já vêm comprimidos),
   * para o download quando o navegador não grava direto na pasta. */
  function gerarZip(arquivos, quando) {
    const q = quando || agora();
    const hora = (q.hora << 11) | (q.min << 5) | (q.seg >> 1);
    const dia = ((q.ano - 1980) << 9) | (q.mes << 5) | q.dia;
    const partes = [], central = [];
    let pos = 0;
    for (const a of arquivos) {
      const nome = texto(a.caminho), crc = crc32(a.dados), n = a.dados.length;
      const l = new Uint8Array(30 + nome.length), dl = new DataView(l.buffer);
      dl.setUint32(0, 0x04034B50, true);
      dl.setUint16(4, 20, true);
      dl.setUint16(6, 0x0800, true);
      dl.setUint16(10, hora, true);
      dl.setUint16(12, dia, true);
      dl.setUint32(14, crc, true);
      dl.setUint32(18, n, true);
      dl.setUint32(22, n, true);
      dl.setUint16(26, nome.length, true);
      l.set(nome, 30);
      const c = new Uint8Array(46 + nome.length), dc = new DataView(c.buffer);
      dc.setUint32(0, 0x02014B50, true);
      dc.setUint16(4, 20, true);
      dc.setUint16(6, 20, true);
      dc.setUint16(8, 0x0800, true);
      dc.setUint16(12, hora, true);
      dc.setUint16(14, dia, true);
      dc.setUint32(16, crc, true);
      dc.setUint32(20, n, true);
      dc.setUint32(24, n, true);
      dc.setUint16(28, nome.length, true);
      dc.setUint32(42, pos, true);
      c.set(nome, 46);
      partes.push(l, a.dados);
      central.push(c);
      pos += l.length + n;
    }
    const tamCentral = central.reduce((s, c) => s + c.length, 0);
    const fim = new Uint8Array(22), df = new DataView(fim.buffer);
    df.setUint32(0, 0x06054B50, true);
    df.setUint16(8, arquivos.length, true);
    df.setUint16(10, arquivos.length, true);
    df.setUint32(12, tamCentral, true);
    df.setUint32(16, pos, true);
    const saida = new Uint8Array(pos + tamCentral + 22);
    let k = 0;
    for (const p of [...partes, ...central, fim]) saida.set(p, k), (k += p.length);
    return saida;
  }

  /* ------------------------------------------------------ o que gravar
   * modo: 'app' | 'iso' | 'dois'. Devolve [{caminho, dados}] relativo à
   * raiz do pendrive (ou pasta escolhida). */
  function arquivosParaGravar({ modo, rom, elf, ajustes, quando }) {
    const cfg = texto(gerarCfg(ajustes || {}));
    /* A ROM vai num ZIP, o mesmo caminho de leitura já usado pelo jogo no
       pendrive e na ISO. */
    const romZip = gerarZip([{ caminho: 'Super Mario World (USA).sfc', dados: rom }], quando);
    const lista = [];
    if (modo === 'app' || modo === 'dois') {
      lista.push({ caminho: 'APPS/SMW/SMW.ELF', dados: elf });
      lista.push({ caminho: 'APPS/SMW/title.cfg', dados: texto(TITLE_CFG) });
      lista.push({ caminho: 'APPS/SMW/SMW.CFG', dados: cfg });
      lista.push({ caminho: 'APPS/SMW/SMW.ZIP', dados: romZip });
    }
    if (modo === 'iso' || modo === 'dois') {
      const iso = gerarIso([
        { nome: 'SYSTEM.CNF;1', dados: texto(SYSTEM_CNF) },
        { nome: 'SMWG_000.01;1', dados: elf },
        { nome: 'SMW.ZIP;1', dados: romZip },
        { nome: 'SMW.CFG;1', dados: cfg },
      ], quando);
      lista.push({ caminho: 'CD/' + NOME_ISO, dados: iso });
    }
    return lista;
  }

  return { ROM_BYTES, ROM_CRC, crc32, hex8, ErroRom, entradasZip, conferirRom, gerarCfg, gerarIso,
           gerarZip, arquivosParaGravar, agora, TITLE_CFG, SYSTEM_CNF, NOME_ISO, APP_ISO, OPCOES };
})();

if (typeof module !== 'undefined') module.exports = Nucleo;
