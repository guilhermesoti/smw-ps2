/* Testes do núcleo do Gravador (Node 18+ e Python 3 com pycdlib).
 * Uso: node Gravador/testes/testar_nucleo.js "/caminho/Super Mario World (USA).zip" APPS/SMW/SMW.ELF
 * A ISO do Gravador tem de sair byte a byte igual à do pycdlib (a que o
 * Fonte/gerar_iso.py faz e que já foi testada no OPL).
 * Trapaças: com TRAPACAS_REF=<trapacas_ref compilado> (o leitor do jogo, ver
 * trapacas_ref.c), o conferirTrapacas é comparado com ele em textos
 * sorteados; sem ela, essa comparação fica de fora (o rodar_todos.py sempre
 * passa). */
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const { execFileSync } = require('child_process');
const N = require('../src/nucleo.js');
const LISTA_TRAPACAS = path.join(__dirname, '..', '..', 'APPS', 'SMW', 'TRAPACAS.TXT');

let falhas = 0;
const confere = (ok, oque) => { console.log((ok ? 'ok    ' : 'FALHOU') + ' ' + oque); if (!ok) falhas++; };
const inflar = async (b) => new Uint8Array(zlib.inflateRawSync(b));
const texto = (s) => new TextEncoder().encode(s);
const igual = (a, b) => a.length === b.length && Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
const espera = async (promessa, codigo) => {
  try { await promessa; return false; } catch (e) { return e instanceof N.ErroRom && e.codigo === codigo ? e : false; }
};

(async () => {
  const [arquivoJogo, arquivoElf] = process.argv.slice(2);
  if (!arquivoJogo || !arquivoElf) { console.log('uso: testar_nucleo.js <jogo.zip> <SMW.ELF>'); process.exit(2); }
  const jogo = new Uint8Array(fs.readFileSync(arquivoJogo));
  const elf = new Uint8Array(fs.readFileSync(arquivoElf));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gravador-'));

  confere(N.hex8(N.crc32(new TextEncoder().encode('123456789'))) === 'CBF43926', 'CRC32 de referência');

  /* ROM */
  const r = await N.conferirRom(path.basename(arquivoJogo), jogo, inflar);
  confere(r.rom.length === 524288 && r.crc === 'B19ED489', 'ROM do ZIP conferida (' + r.interno + ')');
  const sfc = await N.conferirRom('SMW.SFC', r.rom, inflar);
  confere(!sfc.cabecalho && igual(sfc.rom, r.rom), '.sfc sem cabeçalho');
  const comCab = new Uint8Array(524288 + 512);
  comCab.set(r.rom, 512);
  const smc = await N.conferirRom('jogo.smc', comCab, inflar);
  confere(smc.cabecalho && igual(smc.rom, r.rom), '.smc com cabeçalho de copiador');
  const ruim = r.rom.slice();
  ruim[0x7FC0] ^= 1;
  const eCrc = await espera(N.conferirRom('x.sfc', ruim, inflar), 'CRC');
  confere(eCrc && /^[0-9A-F]{8}$/.test(eCrc.crc), 'CRC errado é recusado (' + (eCrc && eCrc.crc) + ')');
  confere(!!(await espera(N.conferirRom('x.sfc', r.rom.subarray(0, 1000), inflar), 'TAMANHO')), 'tamanho errado é recusado');
  const zipTxt = N.gerarZip([{ caminho: 'leia.txt', dados: new TextEncoder().encode('oi') }]);
  confere(!!(await espera(N.conferirRom('x.zip', zipTxt, inflar), 'ZIP_SEM_ROM')), 'ZIP sem .sfc é recusado');
  const zipRom = N.gerarZip([{ caminho: 'pasta/Super Mario World (USA).sfc', dados: r.rom }]);
  confere((await N.conferirRom('y.zip', zipRom, inflar)).crc === 'B19ED489', 'ZIP sem compressão também serve');
  const zipDois = N.gerarZip([{ caminho: 'hack.sfc', dados: ruim }, { caminho: 'smw.smc', dados: comCab }]);
  confere((await N.conferirRom('z.zip', zipDois, inflar)).crc === 'B19ED489', 'ZIP com uma ROM ruim e uma boa: acha a boa');
  confere(!!(await espera(N.conferirRom('foto.png', new Uint8Array(3000), inflar), 'FORMATO')), 'arquivo qualquer é recusado');

  /* SMW.CFG */
  const cfg = N.gerarCfg({ tela: '16:9', video: '480p', suavizacao: 'media', idioma: 'en' });
  confere(/\r\ntela=16:9\r\nvideo=480p\r\nidioma=en\r\n$/.test(cfg) && !/suaviz/.test(cfg), 'SMW.CFG com os ajustes (sem suavização desde a v0.13.7)');
  confere(/video=480i\r\n/.test(N.gerarCfg({ video: '480i' })), 'SMW.CFG com 480i');
  confere(/tela=perguntar\r\nvideo=perguntar\r\nidioma=pt/.test(N.gerarCfg({ tela: 'x', idioma: 'y' })), 'SMW.CFG com valores inválidos volta ao padrão');

  /* TRAPACAS.TXT: os mesmos casos do testes/trapacas.c (leitor do jogo) */
  const casos = '\uFEFF# comentario\n  ; outro comentario\n\nVidas = 7E0DBE:62\r\nMinusculas = 7e0dbf=63\n'
    + 'Oito juntos = 7E0F3109 + 7E0F32:09,7E0F33:09\nEspelho = 000019:02\nEspelho alto = 80:0019:02\n'
    + 'Banco 7F = 7F0010:AA\nROM = 008000:EA\nSRAM = 700000:01\nRegistro = 002100:0F\nEspelho fora = 002000:01\n'
    + 'Curto = 7E0DBE:6\nSem codigo =\n = 7E0DC2:04\nSem igual 7E0DBE:62\n'
    + 'Nome muito comprido com mais de vinte e oito letras = 7E0DBE:01\nAção Ünica € = 7E0DBE:02\n';
  const tc = N.conferirTrapacas(casos);
  const t = tc.trapacas;
  const cod = (x) => x.codigos.map((c) => c.ram.toString(16) + ':' + c.valor.toString(16)).join(' ');
  confere(t.length === 15 && tc.ignoradas === 0 && !tc.cortado, 'trapaças: 15 linhas com "=" (' + t.length + ')');
  confere(t[0].nome === 'Vidas' && cod(t[0]) === 'dbe:62' && t[0].linha === 4, 'trapaças: BOM, comentários e \\r\\n');
  confere(cod(t[1]) === 'dbf:63', 'trapaças: minúsculas e "=" no código');
  confere(cod(t[2]) === 'f31:9 f32:9 f33:9' && t[2].recusados === 0, 'trapaças: 8 juntos, "+" e ","');
  confere(cod(t[3]) === '19:2' && cod(t[5]) === '10010:aa', 'trapaças: espelho do banco 00 e banco 7F');
  confere(t[4].codigos.length === 0 && t[4].recusados === 1, 'trapaças: 80:0019:02 recusado');
  confere([6, 7, 8, 9, 10].every((i) => t[i].codigos.length === 0 && t[i].recusados === 1), 'trapaças: ROM, SRAM, registro, fora do espelho e curto recusados');
  confere(t[11].codigos.length === 0 && t[11].recusados === 0, 'trapaças: sem código');
  confere(t[12].nome === 'Linha 16' && t[12].codigos.length === 1, 'trapaças: sem nome vira "Linha 16"');
  confere(t[13].nome === 'Nome muito comprido com mais', 'trapaças: nome cortado em 28 letras');
  confere(t[14].nome === 'Ação Ünica ?', 'trapaças: acentos ficam, € vira "?"');
  const muitas = Array.from({ length: 70 }, (_, i) => 'T' + i + ' = 7E0DBE:01\n').join('');
  const tm = N.conferirTrapacas(muitas);
  confere(tm.trapacas.length === 64 && tm.ignoradas === 6, 'trapaças: até 64 (as outras 6 ficam de fora)');
  const tsep = N.conferirTrapacas('A = 7E0DBE::62\nB = 7E0DBE:=62\nC = 7E0DBE=62\n').trapacas;
  confere(tsep.length === 3 && !tsep[0].codigos.length && tsep[0].recusados === 1 && !tsep[1].codigos.length
    && tsep[1].recusados === 1 && cod(tsep[2]) === 'dbe:62', 'trapaças: um ":" ou "=" só');
  const tesp = N.conferirTrapacas('AAAAAAAAAAAAAAAAAAAAAAAAAAA B = 7E0DBE:01\n').trapacas;
  confere(tesp[0].nome === 'A'.repeat(27), 'trapaças: o corte em 28 não deixa espaço no fim');
  const tcod = N.conferirTrapacas('Muitos =' + Array.from({ length: 20 }, (_, i) => ' 7E00' + N.hex8(i).slice(6) + ':01').join(''));
  confere(tcod.trapacas[0].codigos.length === 16 && tcod.trapacas[0].recusados === 4, 'trapaças: até 16 códigos em cada');
  const grandeTxt = '# ' + 'x'.repeat(16400) + '\nDepois = 7E0DBE:01\n';
  const tg = N.conferirTrapacas(grandeTxt);
  confere(tg.cortado && tg.trapacas.length === 0 && tg.bytes === grandeTxt.length, 'trapaças: o jogo só lê 16 KB');
  const corteTxt = '# ' + 'x'.repeat(16368) + '\nY = 7E0DBE:01+7E0DBF:02\n';
  const tcorte = N.conferirTrapacas(corteTxt);
  confere(tcorte.cortado && tcorte.trapacas.length === 0, 'trapaças: a linha cortada nos 16 KB sai inteira');
  const pacote = new Uint8Array(fs.readFileSync(LISTA_TRAPACAS));
  const tp = N.conferirTrapacas(pacote);
  confere(tp.trapacas.length === 13 && tp.ignoradas === 0 && !tp.cortado
    && tp.trapacas.every((x) => x.codigos.length > 0 && x.recusados === 0), 'trapaças: lista do pacote, 13 certas');

  /* comparação com o leitor do jogo, em textos sorteados */
  if (process.env.TRAPACAS_REF) {
    let semente = 0x5eed;
    const sorteia = (n) => { semente = (semente * 1103515245 + 12345) >>> 0; return (semente >>> 8) % n; };
    const pedacos = ['7E0DBE:62', '7e0f31=09', '7F0010AA', '000019:02', '8F1FFF:01', '81:0019:02', '002000:01',
      '700000:01', '7E0DBE:6', '7E0DBE::62', 'Vidas', 'Ação', 'Ünica', '€', 'ç', ' ', ' ', '  ', '\t', '\r', '=', '=',
      ' = ', '+', ',', '#', ';', '\n', '\n', '\n', 'abc', 'X'.repeat(30), '?', '\u0000', '\u0001'];
    const brutos = [[0xEF, 0xBB, 0xBF], [0xC3], [0x80], [0xC3, 0x28], [0xE2, 0x82], [0xFF], [0x7F], [0x1F],
      [0xC3, 0xC3, 0xA7], [0xC3, 0xFF]];
    const codigos = ['7E0DBE:62', '7e0f31=09', '7F0010AA', '7FFFFF:FF', '000019:02', '3F1FFF:01', '8F1FFF:01', 'BF0000:7f',
      '81:0019:02', '002000:01', 'C00000:01', '700000:01', '7E0DBE:6', '7E0DBE::62', '7E0DBE:=62', '7E0DBE:620', 'G00000:01'];
    const separadores = [' + ', '+', ',', ' ', '\t', ' ,+ '];
    const peca = (lista, partes) => partes.push(...texto(lista[sorteia(lista.length)]));
    const textos = [texto(casos), texto(muitas), pacote, texto(grandeTxt), texto(corteTxt)];
    for (let k = 0; k < 220; k++) {
      const partes = [];
      if (sorteia(8) === 0) partes.push(0xEF, 0xBB, 0xBF);
      const linhas = sorteia(k < 200 ? 30 : 500);
      for (let l = 0; l < linhas; l++) {
        if (sorteia(4) === 0) {  /* linha com pedaços quaisquer */
          for (let i = sorteia(12); i > 0; i--) {
            if (sorteia(8) === 0) partes.push(...brutos[sorteia(brutos.length)]);
            else peca(pedacos, partes);
          }
        } else {  /* nome = códigos, às vezes com sujeira no meio */
          for (let i = sorteia(5); i > 0; i--) {
            if (sorteia(5) === 0) partes.push(...brutos[sorteia(brutos.length)]);
            else peca(pedacos.filter((x) => x !== '\n'), partes);
          }
          if (sorteia(10)) partes.push(...texto(' = '));
          for (let i = 1 + sorteia(20); i > 0; i--) {
            peca(codigos, partes);
            if (sorteia(15) === 0) peca(pedacos.filter((x) => x !== '\n'), partes);
            peca(separadores, partes);
          }
        }
        partes.push(...(sorteia(5) === 0 ? [13, 10] : [10]));
      }
      textos.push(new Uint8Array(partes));
    }
    let diferentes = 0, primeira = '', lidas = 0, aceitos = 0;
    textos.forEach((x, k) => {
      const arq = path.join(tmp, 'trapacas' + k + '.txt');
      fs.writeFileSync(arq, x);
      const doJogo = execFileSync(process.env.TRAPACAS_REF, [arq]).toString();
      const lido = N.conferirTrapacas(new Uint8Array(fs.readFileSync(arq)));
      lidas += lido.trapacas.length;
      aceitos += lido.trapacas.reduce((a, y) => a + y.codigos.length, 0);
      const doGravador = lido.trapacas.map((y) => ['T', y.linha, Buffer.from(texto(y.nome)).toString('hex'), y.recusados,
        ...y.codigos.map((c) => c.ram.toString(16) + ':' + c.valor.toString(16))].join(' ') + '\n').join('')
        + 'N ' + lido.trapacas.length + ' ' + lido.ignoradas + '\n';
      if (doJogo !== doGravador) { diferentes++; if (!primeira) primeira = arq + '\n' + doJogo + '---\n' + doGravador; }
    });
    confere(diferentes === 0 && lidas > 1000 && aceitos > 1000, 'trapaças: Gravador lê igual ao jogo em ' + textos.length
      + ' textos (' + lidas + ' trapaças, ' + aceitos + ' códigos aceitos)'
      + (primeira ? '\n' + primeira.slice(0, 1500) : ''));
  } else {
    console.log('-      trapaças: comparação com o leitor do jogo pulada (sem TRAPACAS_REF)');
  }

  /* ISO igual à do pycdlib */
  const arquivosIso = [
    ['SYSTEM.CNF;1', new TextEncoder().encode(N.SYSTEM_CNF)],
    ['SMWG_000.01;1', elf], ['SMW.ZIP;1', N.gerarZip([{ caminho: 'Super Mario World (USA).sfc', dados: r.rom }])],
    ['SMW.CFG;1', new TextEncoder().encode(cfg)], ['TRAPACAS.TXT;1', pacote]];
  arquivosIso.forEach(([nome, dados], i) => fs.writeFileSync(path.join(tmp, 'f' + i), dados));
  const py = `
import os, sys, time, json
os.environ['TZ'] = 'America/Sao_Paulo'; time.tzset()
import pycdlib
tm = 1795013400.0
time.time = lambda: tm
iso = pycdlib.PyCdlib()
iso.new(interchange_level=1, vol_ident='SUPER_MARIO_WORLD', sys_ident='PLAYSTATION', app_ident_str=${JSON.stringify(N.APP_ISO)})
for i, nome in enumerate(${JSON.stringify(arquivosIso.map((a) => a[0]))}):
    iso.add_file(os.path.join(sys.argv[1], 'f%d' % i), iso_path='/' + nome)
iso.write(os.path.join(sys.argv[1], 'ref.iso'))
iso.close()
l = time.localtime(tm)
print(json.dumps(dict(ano=l.tm_year, mes=l.tm_mon, dia=l.tm_mday, hora=l.tm_hour, min=l.tm_min, seg=l.tm_sec, fuso=-time.timezone // 60 if not l.tm_isdst else -time.altzone // 60)))
`;
  const quando = JSON.parse(execFileSync('python3', ['-c', py, tmp]).toString());
  const ref = new Uint8Array(fs.readFileSync(path.join(tmp, 'ref.iso')));
  const iso = N.gerarIso(arquivosIso.map(([nome, dados]) => ({ nome, dados })), quando);
  let dif = -1;
  for (let i = 0; i < Math.max(iso.length, ref.length); i++) if (iso[i] !== ref[i]) { dif = i; break; }
  confere(dif < 0, `ISO igual à do pycdlib (${iso.length} bytes${dif >= 0 ? ', difere no byte ' + dif + ' setor ' + (dif >> 11) : ''})`);

  /* ISO lida de volta pelo pycdlib */
  fs.writeFileSync(path.join(tmp, 'gravador.iso'), iso);
  const lidos = execFileSync('python3', ['-c', `
import pycdlib, sys, io, zlib
iso = pycdlib.PyCdlib(); iso.open(sys.argv[1])
import zipfile
for nome in ['/SYSTEM.CNF;1', '/SMWG_000.01;1', '/SMW.ZIP;1', '/SMW.CFG;1', '/TRAPACAS.TXT;1']:
    b = io.BytesIO(); iso.get_file_from_iso_fp(b, iso_path=nome); print(nome, len(b.getvalue()), '%08X' % zlib.crc32(b.getvalue()))
    if nome == '/SMW.ZIP;1':
        z = zipfile.ZipFile(b); d = z.read(z.namelist()[0]); print('ROM', z.namelist()[0], len(d), '%08X' % zlib.crc32(d))
iso.close()`, path.join(tmp, 'gravador.iso')]).toString();
  confere(lidos.includes('ROM Super Mario World (USA).sfc 524288 B19ED489'), 'ISO lida de volta: ROM inteira no SMW.ZIP');
  confere(lidos.includes('/SMWG_000.01;1 ' + elf.length + ' ' + N.hex8(N.crc32(elf))), 'ISO lida de volta: ELF inteiro');
  confere(lidos.includes('/TRAPACAS.TXT;1 ' + pacote.length + ' ' + N.hex8(N.crc32(pacote))), 'ISO lida de volta: TRAPACAS.TXT inteiro');

  /* arquivos para gravar */
  const textoPacote = new TextDecoder().decode(pacote);
  const app = N.arquivosParaGravar({ modo: 'app', rom: r.rom, elf, ajustes: { tela: '4:3' }, trapacas: textoPacote });
  confere(app.map((a) => a.caminho).join() === 'APPS/SMW/SMW.ELF,APPS/SMW/title.cfg,APPS/SMW/SMW.CFG,APPS/SMW/TRAPACAS.TXT,APPS/SMW/SMW.ZIP', 'modo pendrive: arquivos de APPS/SMW');
  confere(new TextDecoder().decode(app[1].dados) === 'title=Super Mario World\nboot=SMW.ELF\n', 'title.cfg igual ao do pacote');
  confere(igual(app[3].dados, pacote), 'TRAPACAS.TXT sem mudança sai igual ao do pacote');
  const semTr = N.arquivosParaGravar({ modo: 'app', rom: r.rom, elf, ajustes: {} });
  confere(semTr.map((a) => a.caminho).join() === 'APPS/SMW/SMW.ELF,APPS/SMW/title.cfg,APPS/SMW/SMW.CFG,APPS/SMW/SMW.ZIP', 'sem trapaças: não grava o TRAPACAS.TXT');
  const dois = N.arquivosParaGravar({ modo: 'dois', rom: r.rom, elf, ajustes: {}, trapacas: textoPacote, quando });
  confere(dois.length === 6 && dois[5].caminho === 'CD/SMWG_000.01.Super Mario World.iso', 'modo os dois: APPS e CD');
  const isoSo = N.arquivosParaGravar({ modo: 'iso', rom: r.rom, elf, ajustes: {}, trapacas: textoPacote, quando });
  const isoSem = N.arquivosParaGravar({ modo: 'iso', rom: r.rom, elf, ajustes: {}, quando });
  confere(isoSo.length === 1 && isoSo[0].dados.length === isoSem[0].dados.length + 2048, 'modo ISO: TRAPACAS.TXT dentro da ISO (um setor a mais)');

  /* ZIP do download, aberto pelo Python */
  const zip = N.gerarZip(dois, quando);
  fs.writeFileSync(path.join(tmp, 'pendrive.zip'), zip);
  const z = execFileSync('python3', ['-c', `
import zipfile, sys
z = zipfile.ZipFile(sys.argv[1]); print(z.testzip()); print(','.join(z.namelist()))`, path.join(tmp, 'pendrive.zip')]).toString();
  confere(z.startsWith('None\n') && z.includes('APPS/SMW/SMW.ZIP') && z.includes('CD/SMWG_000.01.Super Mario World.iso'), 'ZIP do download íntegro');

  /* pasta do pendrive gravada pelo Gravador, para o teste do jogo no PC */
  if (process.env.PASTA_APP) for (const a of app) {
    const destino = path.join(process.env.PASTA_APP, a.caminho);
    fs.mkdirSync(path.dirname(destino), { recursive: true });
    fs.writeFileSync(destino, a.dados);
  }
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(falhas ? `NÚCLEO: ${falhas} FALHA(S)` : 'NÚCLEO: OK');
  process.exit(falhas ? 1 : 0);
})();
