/* Testes do núcleo do Gravador (Node 18+ e Python 3 com pycdlib).
 * Uso: node Gravador/testes/testar_nucleo.js "/caminho/Super Mario World (USA).zip" APPS/SMW/SMW.ELF
 * A ISO do Gravador tem de sair byte a byte igual à do pycdlib (a que o
 * Fonte/gerar_iso.py faz e que já foi testada no OPL). */
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const { execFileSync } = require('child_process');
const N = require('../src/nucleo.js');

let falhas = 0;
const confere = (ok, oque) => { console.log((ok ? 'ok    ' : 'FALHOU') + ' ' + oque); if (!ok) falhas++; };
const inflar = async (b) => new Uint8Array(zlib.inflateRawSync(b));
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
  const cfg = N.gerarCfg({ tela: '16:9', video: '480p', idioma: 'en' });
  confere(/\r\ntela=16:9\r\nvideo=480p\r\nidioma=en\r\n$/.test(cfg), 'SMW.CFG com os ajustes');
  confere(/tela=perguntar\r\nvideo=perguntar\r\nidioma=pt/.test(N.gerarCfg({ tela: 'x' })), 'SMW.CFG com valores inválidos volta ao padrão');

  /* ISO igual à do pycdlib */
  const arquivosIso = [
    ['SYSTEM.CNF;1', new TextEncoder().encode(N.SYSTEM_CNF)],
    ['SMWG_000.01;1', elf], ['SMW.ZIP;1', N.gerarZip([{ caminho: 'Super Mario World (USA).sfc', dados: r.rom }])],
    ['SMW.CFG;1', new TextEncoder().encode(cfg)]];
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
for nome in ['/SYSTEM.CNF;1', '/SMWG_000.01;1', '/SMW.ZIP;1', '/SMW.CFG;1']:
    b = io.BytesIO(); iso.get_file_from_iso_fp(b, iso_path=nome); print(nome, len(b.getvalue()), '%08X' % zlib.crc32(b.getvalue()))
    if nome == '/SMW.ZIP;1':
        z = zipfile.ZipFile(b); d = z.read(z.namelist()[0]); print('ROM', z.namelist()[0], len(d), '%08X' % zlib.crc32(d))
iso.close()`, path.join(tmp, 'gravador.iso')]).toString();
  confere(lidos.includes('ROM Super Mario World (USA).sfc 524288 B19ED489'), 'ISO lida de volta: ROM inteira no SMW.ZIP');
  confere(lidos.includes('/SMWG_000.01;1 ' + elf.length + ' ' + N.hex8(N.crc32(elf))), 'ISO lida de volta: ELF inteiro');

  /* arquivos para gravar */
  const app = N.arquivosParaGravar({ modo: 'app', rom: r.rom, elf, ajustes: { tela: '4:3' } });
  confere(app.map((a) => a.caminho).join() === 'APPS/SMW/SMW.ELF,APPS/SMW/title.cfg,APPS/SMW/SMW.CFG,APPS/SMW/SMW.ZIP', 'modo pendrive: arquivos de APPS/SMW');
  confere(new TextDecoder().decode(app[1].dados) === 'title=Super Mario World\nboot=SMW.ELF\n', 'title.cfg igual ao do pacote');
  const dois = N.arquivosParaGravar({ modo: 'dois', rom: r.rom, elf, ajustes: {}, quando });
  confere(dois.length === 5 && dois[4].caminho === 'CD/SMWG_000.01.Super Mario World.iso', 'modo os dois: APPS e CD');

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
