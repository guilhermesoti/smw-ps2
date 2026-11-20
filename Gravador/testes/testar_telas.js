/* Testa o Gravador_SMW_PS2.html num Chrome de verdade (sem janela).
 * Precisa de: npm install puppeteer-core, Google Chrome/Chromium e Python 3
 * (para servir a página em localhost). O pendrive é simulado com uma pasta
 * do próprio navegador (OPFS) que já tem um save antigo.
 *
 * Uso: CHROME=/usr/bin/google-chrome node testar_telas.js Gravador_SMW_PS2.html "jogo.zip" pasta_saida
 * Em pasta_saida ficam as fotos das telas e os arquivos gravados. */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
const puppeteer = require('puppeteer-core');

const [pagina, jogo, saida] = process.argv.slice(2);
let falhas = 0;
const confere = (ok, oque) => { console.log((ok ? 'ok    ' : 'FALHOU') + ' ' + oque); if (!ok) falhas++; };
const espera = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  fs.mkdirSync(saida, { recursive: true });
  const porta = 8700 + Math.floor(Math.random() * 200);
  const servidor = spawn('python3', ['-m', 'http.server', String(porta), '--bind', '127.0.0.1'],
    { cwd: path.dirname(path.resolve(pagina)), stdio: 'ignore' });
  await espera(800);
  const url = `http://127.0.0.1:${porta}/${path.basename(pagina)}`;
  const chrome = await puppeteer.launch({ executablePath: process.env.CHROME || '/usr/bin/google-chrome',
    headless: true, args: ['--no-sandbox', '--force-device-scale-factor=1'] });
  const erros = [];
  const nova = async (simularPendrive) => {
    const p = await chrome.newPage();
    p.on('pageerror', (e) => erros.push(String(e)));
    p.on('console', (m) => { if (m.type() === 'error') erros.push(m.text()); });
    await p.setViewport({ width: 1000, height: 700 });
    await p.evaluateOnNewDocument((simular) => {
      if (!simular) { delete window.showDirectoryPicker; window.showDirectoryPicker = undefined; return; }
      window.showDirectoryPicker = async () => {
        const raiz = await navigator.storage.getDirectory();
        return raiz.getDirectoryHandle('PENDRIVE', { create: true });
      };
    }, simularPendrive);
    await p.goto(url, { waitUntil: 'load' });
    await p.evaluate(() => document.fonts.ready);
    return p;
  };
  const foto = (p, nome) => p.screenshot({ path: path.join(saida, nome + '.png') });
  const visivel = (p, sel) => p.$eval(sel, (e) => !e.hidden && e.offsetParent !== null).catch(() => false);
  const texto = (p, sel) => p.$eval(sel, (e) => e.textContent.trim());
  const clica = async (p, sel) => { await p.click(sel); await espera(150); };

  /* ---------- 1. pendrive simulado, modo "os dois", 16:9 e 480p ---------- */
  const p = await nova(true);
  await p.evaluate(async () => {
    const raiz = await navigator.storage.getDirectory();
    try { await raiz.removeEntry('PENDRIVE', { recursive: true }); } catch (e) { /* novo */ }
    const smw = await (await (await raiz.getDirectoryHandle('PENDRIVE', { create: true }))
      .getDirectoryHandle('APPS', { create: true })).getDirectoryHandle('SMW', { create: true });
    for (const [nome, conteudo] of [['SMW.SRM', 'SAVE-ANTIGO'], ['SMW.OPT', 'tela=4:3\n'], ['TRAPACAS.TXT', 'Antiga = 7E0DBE:01\n']]) {
      const w = await (await smw.getFileHandle(nome, { create: true })).createWritable();
      await w.write(conteudo);
      await w.close();
    }
  });
  await foto(p, '1_vazio');
  confere(await p.$eval('#avancar', (b) => b.disabled), 'passo 1: CONTINUAR desligado sem cartucho');

  /* arquivo errado */
  const ruim = path.join(saida, 'ruim.sfc');
  fs.writeFileSync(ruim, Buffer.alloc(524288, 7));
  await (await p.$('#arquivo')).uploadFile(ruim);
  await p.waitForFunction(() => !document.getElementById('c-erro').hidden, { timeout: 10000 });
  await espera(500);
  await foto(p, '1_erro');
  confere((await texto(p, '#c-erro-caixa')).includes('CRC32 do arquivo'), 'cartucho errado: mostra o CRC');

  /* arquivo certo */
  await (await p.$('#arquivo')).uploadFile(jogo);
  await p.waitForFunction(() => !document.getElementById('c-ok').hidden, { timeout: 10000 });
  await espera(600);
  await foto(p, '1_ok');
  confere(!(await p.$eval('#avancar', (b) => b.disabled)), 'cartucho certo: CONTINUAR ligado');

  await clica(p, '#avancar');
  await clica(p, '.opcao[data-modo="dois"]');
  await foto(p, '2_formato');
  confere(await visivel(p, '#p2'), 'passo 2 aparece');
  await clica(p, '#avancar');
  await clica(p, '.grupo[data-ajuste="tela"] .seg[data-valor="16:9"]');
  await clica(p, '.grupo[data-ajuste="video"] .seg[data-valor="480p"]');
  await foto(p, '3_ajustes');
  confere((await texto(p, '#resumo')).includes('tela 16:9') && (await texto(p, '#resumo')).includes('480p'), 'resumo segue os ajustes');
  confere((await texto(p, '#tr-estado')) === '13 trapaças, todas certas. No PS2 começam desligadas.', 'trapaças: lista do pacote conferida (' + (await texto(p, '#tr-estado')) + ')');
  confere((await texto(p, '#resumo')).includes('13 trapaças no TRAPACAS.TXT, desligadas.'), 'resumo: 13 trapaças');
  const padrao = await p.$eval('#tr-texto', (t) => t.value);
  confere(padrao === fs.readFileSync(path.join(__dirname, '..', '..', 'APPS', 'SMW', 'TRAPACAS.TXT'), 'utf8'), 'trapaças: o editor começa com o TRAPACAS.TXT do pacote');
  await clica(p, '#tr-editor summary');
  await p.focus('#tr-texto');
  await p.keyboard.down('Control'); await p.keyboard.press('End'); await p.keyboard.up('Control');
  await p.keyboard.type('Teste = 7E0DBE:63\nRuim = 008000:EA\nMeio = 7E0DBE:62+xyz\n');
  await espera(150);
  const estadoEditado = await texto(p, '#tr-estado');
  confere(estadoEditado === '16 trapaças; 1 sem código que funcione (linha 29); código recusado (linha 30).', 'trapaças: erros com o número da linha (' + estadoEditado + ')');
  confere((await texto(p, '#resumo')).includes('15 trapaças no TRAPACAS.TXT'), 'resumo: só conta as que funcionam');
  await foto(p, '3_trapacas');
  const editado = await p.$eval('#tr-texto', (t) => t.value);
  await clica(p, '#avancar');
  confere(await p.$eval('#avancar', (b) => b.disabled), 'passo 4: GRAVAR desligado sem pendrive');
  await clica(p, '#escolher-pasta');
  await p.waitForFunction(() => !document.getElementById('d-info').hidden, { timeout: 5000 });
  await foto(p, '4_destino');
  const info = await texto(p, '#d-info');
  confere(info.includes('Save encontrado'), 'destino: acha o save que já existe');
  confere(info.includes('vão ser trocados pelos daqui'), 'destino: avisa da escolha antiga do PS2');
  confere(info.includes('O TRAPACAS.TXT desse pendrive é diferente'), 'destino: avisa do TRAPACAS.TXT diferente');
  confere((await texto(p, '#arvore')).includes('PENDRIVE\\'), 'destino: árvore com o nome do pendrive');
  confere((await texto(p, '#arvore')).includes('TRAPACAS.TXT  (trapaças)'), 'destino: TRAPACAS.TXT na árvore');

  await clica(p, '#avancar');
  await p.waitForFunction(() => document.getElementById('t5').textContent !== 'GRAVANDO...', { timeout: 30000 });
  await espera(300);
  await foto(p, '5_gravado');
  confere((await texto(p, '#t5')) === 'GRAVADO!', 'gravação terminou bem (' + (await texto(p, '#registro')).slice(0, 300) + ')');
  await clica(p, '#avancar');
  await foto(p, '6_pronto');
  confere((await texto(p, '#r-passos')).includes('Aplicativos ou na lista de jogos'), 'pronto: passos do modo os dois');

  /* o que ficou no "pendrive" */
  const gravados = await p.evaluate(async () => {
    const raiz = await (await navigator.storage.getDirectory()).getDirectoryHandle('PENDRIVE');
    const lista = {};
    const anda = async (d, prefixo) => {
      for await (const [nome, h] of d.entries()) {
        if (h.kind === 'directory') await anda(h, prefixo + nome + '/');
        else {
          const b = new Uint8Array(await (await h.getFile()).arrayBuffer());
          let s = '';
          for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode.apply(null, b.subarray(i, i + 0x8000));
          lista[prefixo + nome] = btoa(s);
        }
      }
    };
    await anda(raiz, '');
    return lista;
  });
  const pend = path.join(saida, 'pendrive');
  fs.rmSync(pend, { recursive: true, force: true });
  for (const [nome, b64] of Object.entries(gravados)) {
    fs.mkdirSync(path.join(pend, path.dirname(nome)), { recursive: true });
    fs.writeFileSync(path.join(pend, nome), Buffer.from(b64, 'base64'));
  }
  const nomes = Object.keys(gravados).sort().join(',');
  confere(nomes === 'APPS/SMW/SMW.CFG,APPS/SMW/SMW.ELF,APPS/SMW/SMW.SRM,APPS/SMW/SMW.ZIP,APPS/SMW/TRAPACAS.TXT,APPS/SMW/title.cfg,CD/SMWG_000.01.Super Mario World.iso',
    'pendrive: arquivos certos, SMW.OPT antigo removido (' + nomes + ')');
  const trGravado = fs.readFileSync(path.join(pend, 'APPS/SMW/TRAPACAS.TXT'), 'utf8');
  confere(trGravado === editado && trGravado.endsWith('Meio = 7E0DBE:62+xyz\n'), 'pendrive: TRAPACAS.TXT com a lista editada');
  const isoGravada = fs.readFileSync(path.join(pend, 'CD/SMWG_000.01.Super Mario World.iso'));
  confere(isoGravada.indexOf('TRAPACAS.TXT;1', 0, 'latin1') > 0 && isoGravada.indexOf(Buffer.from(editado, 'utf8')) > 0,
    'ISO: TRAPACAS.TXT editado dentro');
  confere(fs.readFileSync(path.join(pend, 'APPS/SMW/SMW.SRM'), 'latin1') === 'SAVE-ANTIGO', 'pendrive: save antigo intacto');
  confere(/tela=16:9\r\nvideo=480p\r\nidioma=pt/.test(fs.readFileSync(path.join(pend, 'APPS/SMW/SMW.CFG'), 'latin1')), 'pendrive: SMW.CFG com os ajustes');
  await p.close();

  /* ---------- 2. navegador sem gravação direta: download ---------- */
  const q = await nova(false);
  const baixados = path.join(saida, 'downloads');
  fs.rmSync(baixados, { recursive: true, force: true });
  fs.mkdirSync(baixados, { recursive: true });
  const cdp = await q.target().createCDPSession();
  await cdp.send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: baixados });
  await (await q.$('#arquivo')).uploadFile(jogo);
  await q.waitForFunction(() => !document.getElementById('c-ok').hidden, { timeout: 10000 });
  await clica(q, '#avancar');
  await clica(q, '#avancar');
  await clica(q, '.grupo[data-ajuste="tela"] .seg[data-valor="16:9"]');
  await clica(q, '.grupo[data-ajuste="video"] .seg[data-valor="240p"]');
  confere((await texto(q, '#resumo')).includes('o 16:9 só vale no 480p: com o 240p o jogo fica em 4:3'), 'resumo: 16:9 só no 480p');
  await clica(q, '.grupo[data-ajuste="trapacas"] .seg[data-valor="nao"]');
  confere(!(await visivel(q, '#tr-editor')) && (await texto(q, '#tr-estado')) === 'O Gravador não grava o TRAPACAS.TXT.', 'trapaças: "Não gravar" esconde o editor');
  confere((await texto(q, '#resumo')).includes('Sem TRAPACAS.TXT novo.'), 'resumo: sem TRAPACAS.TXT');
  await clica(q, '#avancar');
  await foto(q, '4_baixar');
  confere((await texto(q, '#avancar')) === 'BAIXAR ▶', 'sem gravação direta: botão BAIXAR');
  await clica(q, '#avancar');
  for (let i = 0; i < 50 && !fs.existsSync(path.join(baixados, 'SMW_PS2_pendrive.zip')); i++) await espera(200);
  confere(fs.existsSync(path.join(baixados, 'SMW_PS2_pendrive.zip')), 'download do SMW_PS2_pendrive.zip');
  await espera(500);
  const noZip = require('child_process').execFileSync('python3', ['-c',
    'import zipfile, sys; print(",".join(sorted(zipfile.ZipFile(sys.argv[1]).namelist())))',
    path.join(baixados, 'SMW_PS2_pendrive.zip')]).toString().trim();
  confere(noZip === 'APPS/SMW/SMW.CFG,APPS/SMW/SMW.ELF,APPS/SMW/SMW.ZIP,APPS/SMW/title.cfg', 'download sem TRAPACAS.TXT (' + noZip + ')');
  await q.setViewport({ width: 390, height: 844 });
  await clica(q, '#avancar');
  await foto(q, '6_celular');
  await q.close();

  confere(erros.length === 0, 'sem erros no console' + (erros.length ? ': ' + erros.join(' | ') : ''));
  await chrome.close();
  servidor.kill();
  console.log(falhas ? `TELAS: ${falhas} FALHA(S)` : 'TELAS: OK');
  process.exit(falhas ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
