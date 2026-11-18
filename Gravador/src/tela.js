/* Gravador SMW PS2 — as telas (os 5 passos do conceito).
 * Grava direto no pendrive no Chrome e no Edge (File System Access); nos
 * outros navegadores, baixa um ZIP (ou a ISO) para copiar. */
(() => {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const N = Nucleo;
  const gravaDireto = typeof window.showDirectoryPicker === 'function';

  const estado = {
    passo: 1, rom: null, modo: 'app', pasta: null, gravando: false, resultado: null,
    ajustes: { tela: 'perguntar', video: 'perguntar', suavizacao: 'perguntar', idioma: 'pt' },
  };
  const temApp = () => estado.modo === 'app' || estado.modo === 'dois';
  const temIso = () => estado.modo === 'iso' || estado.modo === 'dois';

  let elfBytes = null;
  function elf() {
    if (!elfBytes) {
      const bin = atob($('elf').textContent.replace(/\s+/g, ''));
      elfBytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) elfBytes[i] = bin.charCodeAt(i);
    }
    return elfBytes;
  }

  async function inflar(bytes) {
    let ds;
    try { ds = new DecompressionStream('deflate-raw'); } catch (e) { throw new N.ErroRom('NAVEGADOR_ZIP'); }
    const saida = new Blob([bytes]).stream().pipeThrough(ds);
    return new Uint8Array(await new Response(saida).arrayBuffer());
  }

  const anunciar = (t) => { $('anuncio').textContent = t; };
  const el = (tag, classe, texto) => {
    const e = document.createElement(tag);
    if (classe) e.className = classe;
    if (texto !== undefined) e.textContent = texto;
    return e;
  };

  /* ------------------------------------------------------------ navegação */
  function irPara(n) {
    estado.passo = n;
    for (let i = 1; i <= 6; i++) $('p' + i).hidden = i !== n;
    document.querySelectorAll('#passos li').forEach((li, i) => {
      const k = i + 1;
      li.className = k < n || n === 6 ? 'feito' : k === n ? 'atual' : '';
      li.querySelector('button').setAttribute('aria-current', k === n ? 'step' : 'false');
    });
    if (n === 3) atualizarResumo();
    if (n === 4) prepararDestino();
    atualizarRodape();
    const h = $('p' + n).querySelector('h1:not([hidden])');
    const visivel = [...$('p' + n).querySelectorAll('h1')].find((x) => x.offsetParent !== null) || h;
    if (visivel) visivel.focus({ preventScroll: true });
  }

  document.querySelectorAll('#passos button').forEach((b) => b.addEventListener('click', () => {
    const k = +b.dataset.ir;
    if (estado.gravando || k >= estado.passo && estado.passo !== 6) return;
    if (k <= 4) irPara(k);
  }));

  function atualizarRodape() {
    const v = $('voltar'), a = $('avancar'), p = estado.passo;
    v.hidden = p === 1 || estado.gravando;
    a.hidden = estado.gravando;
    v.textContent = '◀ VOLTAR';
    a.textContent = 'CONTINUAR ▶';
    a.disabled = false;
    if (p === 1) a.disabled = !estado.rom;
    if (p === 4) {
      a.textContent = gravaDireto ? 'GRAVAR ▶' : 'BAIXAR ▶';
      a.disabled = gravaDireto && !estado.pasta;
    }
    if (p === 5) {
      v.hidden = estado.gravando || estado.resultado === 'ok';
      a.hidden = estado.gravando;
      a.textContent = estado.resultado === 'ok' ? 'VER RESULTADO ▶' : 'TENTAR DE NOVO';
    }
    if (p === 6) {
      v.textContent = 'COMEÇAR DE NOVO';
      a.textContent = gravaDireto ? 'GRAVAR OUTRO PENDRIVE' : 'BAIXAR DE NOVO';
    }
  }

  $('voltar').addEventListener('click', () => {
    const p = estado.passo;
    if (p === 6) { estado.rom = null; estado.pasta = null; mostrarCartucho('vazio'); irPara(1); return; }
    if (p === 5) { irPara(4); return; }
    if (p > 1) irPara(p - 1);
  });
  $('avancar').addEventListener('click', () => {
    const p = estado.passo;
    if (p === 4 || (p === 5 && estado.resultado !== 'ok')) { gravar(); return; }
    if (p === 5) { irPara(6); return; }
    if (p === 6) { estado.pasta = null; irPara(4); return; }
    irPara(p + 1);
  });

  /* ------------------------------------------------------------- cartucho */
  function mostrarCartucho(fase) {
    for (const f of ['vazio', 'lendo', 'ok', 'erro']) $('c-' + f).hidden = f !== fase;
    const c = $('console');
    c.classList.toggle('com', fase === 'ok' || fase === 'erro');
    c.classList.toggle('ok', fase === 'ok');
    c.classList.toggle('erro', fase === 'erro');
    const h = $('c-' + fase).querySelector('h1');
    if (h && estado.passo === 1) h.focus({ preventScroll: true });
  }

  const MENSAGENS = {
    ZIP_SEM_ROM: ['Esse ZIP não tem nenhum arquivo .sfc ou .smc dentro.', null],
    ZIP_INVALIDO: ['Não deu para abrir esse ZIP.', 'Tente extrair o .sfc do ZIP e escolher ele.'],
    ZIP_METODO: ['Esse ZIP usa uma compressão que o Gravador não abre.', 'Extraia o .sfc do ZIP e escolha ele.'],
    NAVEGADOR_ZIP: ['Este navegador é antigo para abrir arquivos ZIP.', 'Extraia o .sfc do ZIP e escolha ele, ou atualize o navegador.'],
    FORMATO: ['Esse arquivo não parece uma ROM de Super Nintendo.', 'Use o .zip, .sfc ou .smc do seu cartucho.'],
  };

  function mostrarErro(e) {
    const caixa = $('c-erro-caixa');
    caixa.replaceChildren();
    $('c-erro-dica').hidden = false;
    if (e instanceof N.ErroRom && e.codigo === 'CRC') {
      $('c-erro-msg').textContent = 'O arquivo não é o Super Mario World americano original.';
      caixa.append(el('span', 'atencao', '✕ CRC32 do arquivo: ' + e.crc), el('span', 'dim', 'O certo é B19ED489 (versão americana, sem mudanças).'));
    } else if (e instanceof N.ErroRom && e.codigo === 'TAMANHO') {
      $('c-erro-msg').textContent = 'O tamanho não é o do cartucho americano.';
      caixa.append(el('span', 'atencao', '✕ Tamanho do arquivo: ' + e.tamanho.toLocaleString('pt-BR') + ' bytes'),
        el('span', 'dim', 'O certo é 524.288 bytes (512 KB), com ou sem cabeçalho de copiador.'));
    } else {
      const [msg, dica] = (e instanceof N.ErroRom && MENSAGENS[e.codigo]) || ['Não deu para ler esse arquivo.', String(e && e.message || e)];
      $('c-erro-msg').textContent = msg;
      if (dica) caixa.append(el('span', 'dim', dica));
      else caixa.hidden = true;
      $('c-erro-dica').hidden = e.codigo !== 'ZIP_SEM_ROM';
    }
    caixa.hidden = !caixa.childElementCount;
    mostrarCartucho('erro');
    anunciar('Esse cartucho não serve. ' + $('c-erro-msg').textContent);
  }

  async function receberArquivo(arquivo) {
    if (!arquivo || estado.gravando) return;
    if (estado.passo !== 1) irPara(1);
    estado.rom = null;
    $('c-lendo-nome').textContent = arquivo.name;
    mostrarCartucho('lendo');
    atualizarRodape();
    try {
      if (arquivo.size > 16 * 1024 * 1024) throw new N.ErroRom('FORMATO');
      const dados = new Uint8Array(await arquivo.arrayBuffer());
      const r = await N.conferirRom(arquivo.name, dados, inflar);
      estado.rom = r;
      $('c-ok-cab').textContent = r.cabecalho ? '✓ Tamanho certo: 512 KB (cabeçalho de copiador tirado)' : '✓ Tamanho certo: 512 KB';
      $('c-ok-arq').textContent = 'Arquivo: ' + arquivo.name + (r.interno ? ' → ' + r.interno : '');
      mostrarCartucho('ok');
      anunciar('Cartucho conferido. Pode continuar.');
    } catch (e) {
      mostrarErro(e);
    }
    atualizarRodape();
  }

  const abrirSeletor = () => { $('arquivo').value = ''; $('arquivo').click(); };
  for (const id of ['console', 'escolher', 'outro', 'trocar']) $(id).addEventListener('click', abrirSeletor);
  $('arquivo').addEventListener('change', (e) => receberArquivo(e.target.files[0]));

  let arrastos = 0;
  document.addEventListener('dragenter', (e) => { e.preventDefault(); if (++arrastos === 1) $('console').classList.add('arrastando'); });
  document.addEventListener('dragleave', () => { if (--arrastos <= 0) arrastos = 0, $('console').classList.remove('arrastando'); });
  document.addEventListener('dragover', (e) => e.preventDefault());
  document.addEventListener('drop', (e) => {
    e.preventDefault();
    arrastos = 0;
    $('console').classList.remove('arrastando');
    if (e.dataTransfer && e.dataTransfer.files.length) receberArquivo(e.dataTransfer.files[0]);
  });

  /* -------------------------------------------------------------- formato */
  document.querySelectorAll('.opcao').forEach((b) => b.addEventListener('click', () => {
    estado.modo = b.dataset.modo;
    document.querySelectorAll('.opcao').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
  }));

  /* -------------------------------------------------------------- ajustes */
  document.querySelectorAll('.grupo').forEach((g) => g.querySelectorAll('.seg').forEach((b) => b.addEventListener('click', () => {
    estado.ajustes[g.dataset.ajuste] = b.dataset.valor;
    g.querySelectorAll('.seg').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
    atualizarResumo();
  })));

  /* Algum ajuste marcado aqui: o SMW.OPT antigo (escolhas feitas no PS2) sai
     para o SMW.CFG valer. */
  function escolheuAjustes() {
    const a = estado.ajustes;
    return a.tela !== 'perguntar' || a.video !== 'perguntar' || a.suavizacao !== 'perguntar';
  }

  function atualizarResumo() {
    const a = estado.ajustes;
    const linhas = [
      'Tela AJUSTES: vídeo, tela, suavização e idioma.',
      a.video === 'perguntar' ? 'Você escolhe o vídeo (240p, 480i ou 480p).'
        : 'Já marcado: ' + a.video + (a.video === '240p' ? '.' : ' (se a TV não mostrar, volta para os ajustes).'),
      a.tela === 'perguntar' ? 'Você escolhe 4:3 ou 16:9.' : 'Já marcado: tela ' + a.tela + '.',
      a.suavizacao === 'perguntar' ? 'Suavização: desligada, se você não mudar.'
        : 'Suavização ' + (a.suavizacao === 'nao' ? 'desligada.' : a.suavizacao === 'media' ? 'média.' : 'forte.'),
      a.idioma === 'pt' ? 'Jogo em português.' : 'Jogo em inglês.',
      'Abertura Retrogames.',
    ];
    $('resumo').replaceChildren(...linhas.map((t) => el('li', '', t)));
  }

  /* -------------------------------------------------------------- destino */
  function nomeRaiz() {
    if (!gravaDireto) return null;
    if (!estado.pasta) return '[PENDRIVE]\\';
    const n = estado.pasta.name || 'PENDRIVE';
    return /[\\/]$/.test(n) ? n : n + '\\';
  }

  function desenharArvore() {
    const linhas = [];
    if (gravaDireto) {
      linhas.push(nomeRaiz());
      const app = ['   ├ SMW.ELF', '   ├ title.cfg', '   ├ SMW.CFG  (ajustes)', '   └ SMW.ZIP  (seu cartucho)'];
      if (temApp()) linhas.push((temIso() ? '├' : '└') + ' APPS\\SMW\\', ...(temIso() ? app.map((l) => '│' + l.slice(1)) : app));
      if (temIso()) linhas.push('└ CD\\', '   └ ' + N.NOME_ISO);
    } else if (estado.modo === 'iso') {
      linhas.push('Downloads\\', '└ ' + N.NOME_ISO, '', 'Copie para a pasta CD do pendrive.');
    } else {
      linhas.push('Downloads\\', '└ SMW_PS2_pendrive.zip', '   ' + (temIso() ? '├' : '└') + ' APPS\\SMW\\ (jogo, ajustes e cartucho)');
      if (temIso()) linhas.push('   └ CD\\' + N.NOME_ISO);
      linhas.push('', 'Descompacte na raiz do pendrive.');
    }
    $('arvore').textContent = linhas.join('\n');
  }

  async function existe(partes) {
    try {
      let d = estado.pasta;
      for (const p of partes.slice(0, -1)) d = await d.getDirectoryHandle(p);
      await d.getFileHandle(partes[partes.length - 1]);
      return true;
    } catch (e) {
      return false;
    }
  }

  async function conferirPasta() {
    const info = $('d-info');
    info.replaceChildren();
    if (!estado.pasta) { info.hidden = true; return; }
    const linhas = [];
    if (/^(APPS|SMW|CD)$/i.test(estado.pasta.name))
      linhas.push(['atencao', 'Você escolheu a pasta "' + estado.pasta.name + '". Escolha a raiz do pendrive: o Gravador cria APPS\\SMW e CD sozinho.']);
    if (await existe(['APPS', 'SMW', 'SMW.SRM'])) linhas.push(['ok', '✓ Save encontrado (SMW.SRM): fica intacto.']);
    if (temApp() && await existe(['APPS', 'SMW', 'SMW.ELF'])) linhas.push(['sub', 'O jogo já está nesse pendrive: vai ser atualizado.']);
    if (temApp() && escolheuAjustes() && await existe(['APPS', 'SMW', 'SMW.OPT']))
      linhas.push(['sub', 'Os ajustes escolhidos antes no PS2 vão ser trocados pelos daqui.']);
    if (temIso() && await existe(['CD', N.NOME_ISO])) linhas.push(['sub', 'A ISO que já está no pendrive vai ser substituída.']);
    for (const [c, t] of linhas) info.append(el('span', c, t));
    info.hidden = !linhas.length;
  }

  function prepararDestino() {
    $('d-pasta').hidden = !gravaDireto;
    $('d-baixar').hidden = gravaDireto;
    if (!gravaDireto) {
      const lista = $('d-baixar-lista');
      lista.replaceChildren();
      if (estado.modo === 'iso') lista.append(el('span', '', N.NOME_ISO), el('span', 'dim', 'Copie para a pasta CD do pendrive.'));
      else lista.append(el('span', '', 'SMW_PS2_pendrive.zip'),
        el('span', 'dim', 'Descompacte na raiz do pendrive: ele cria ' + (temIso() ? 'APPS\\SMW e CD.' : 'a pasta APPS\\SMW.')));
    }
    $('escolher-pasta').textContent = estado.pasta ? 'TROCAR O PENDRIVE...' : 'ESCOLHER O PENDRIVE...';
    desenharArvore();
    conferirPasta();
  }

  $('escolher-pasta').addEventListener('click', async () => {
    try {
      estado.pasta = await window.showDirectoryPicker({ id: 'smw-ps2', mode: 'readwrite' });
    } catch (e) {
      if (e && e.name === 'AbortError') return;
      anunciar('Não deu para abrir essa pasta: ' + (e && e.message));
      return;
    }
    prepararDestino();
    atualizarRodape();
    anunciar('Pendrive escolhido: ' + estado.pasta.name);
  });

  /* --------------------------------------------------------------- gravar */
  const BLOCOS = 20;
  function progresso(f) {
    const n = Math.round(Math.max(0, Math.min(1, f)) * BLOCOS);
    [...$('blocos').children].forEach((b, i) => b.classList.toggle('cheio', i < n));
    const pct = Math.round(f * 100);
    $('pct').textContent = pct + '%';
    $('blocos').setAttribute('aria-valuenow', String(pct));
  }
  function linha(texto, tipo) {
    const li = el('li');
    li.append(el('span', '', ''), el('span', '', texto));
    $('registro').append(li);
    marcar(li, tipo);
    return li;
  }
  function marcar(li, tipo) {
    const marca = { ok: '✓', atual: '▶', erro: '✕' }[tipo] || '·';
    const cor = { ok: 'var(--verde)', atual: 'var(--ouro)', erro: 'var(--laranja)' }[tipo] || 'var(--passo)';
    li.firstChild.textContent = marca;
    li.style.color = cor;
  }

  async function escrever(raiz, caminho, dados) {
    const partes = caminho.split('/');
    let d = raiz;
    for (const p of partes.slice(0, -1)) d = await d.getDirectoryHandle(p, { create: true });
    const fh = await d.getFileHandle(partes[partes.length - 1], { create: true });
    const w = await fh.createWritable();
    try {
      await w.write(dados);
      await w.close();
    } catch (e) {
      try { await w.abort(); } catch (e2) { /* nada */ }
      throw e;
    }
  }

  async function ler(raiz, caminho) {
    const partes = caminho.split('/');
    let d = raiz;
    for (const p of partes.slice(0, -1)) d = await d.getDirectoryHandle(p);
    const f = await (await d.getFileHandle(partes[partes.length - 1])).getFile();
    return new Uint8Array(await f.arrayBuffer());
  }

  function baixar(dados, nome) {
    const url = URL.createObjectURL(new Blob([dados], { type: 'application/octet-stream' }));
    const a = el('a');
    a.href = url;
    a.download = nome;
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 120000);
  }

  function explicarErro(e) {
    const n = e && e.name;
    if (n === 'QuotaExceededError') return 'O pendrive está cheio.';
    if (n === 'NotAllowedError' || n === 'SecurityError') return 'O navegador não recebeu permissão para gravar nessa pasta.';
    if (n === 'NoModificationAllowedError' || n === 'InvalidModificationError') return 'O pendrive está protegido contra gravação ou o arquivo está aberto em outro programa.';
    if (n === 'NotFoundError') return 'O pendrive foi tirado ou a pasta sumiu.';
    return (e && e.message) || String(e);
  }

  async function gravar() {
    if (estado.gravando || !estado.rom) return;
    const pasta = estado.pasta;
    if (gravaDireto) {
      if (!pasta) return;
      try {
        if (pasta.requestPermission && (await pasta.requestPermission({ mode: 'readwrite' })) !== 'granted') {
          anunciar('Sem permissão para gravar no pendrive.');
          return;
        }
      } catch (e) { /* segue: a gravação mostra o erro */ }
    }
    estado.gravando = true;
    estado.resultado = null;
    $('registro').replaceChildren();
    $('blocos').replaceChildren(...Array.from({ length: BLOCOS }, () => el('span')));
    $('t5').textContent = 'GRAVANDO...';
    $('g-sub').textContent = gravaDireto ? 'Não tire o pendrive agora.' : 'Preparando o download.';
    irPara(5);
    progresso(0);
    let atual = null;
    try {
      linha('Cartucho conferido (CRC32 B19ED489)', 'ok');
      await new Promise((r) => setTimeout(r, 30));
      const arquivos = N.arquivosParaGravar({ modo: estado.modo, rom: estado.rom.rom, elf: elf(), ajustes: estado.ajustes });
      if (gravaDireto) {
        const total = arquivos.reduce((s, a) => s + a.dados.length, 0);
        let feito = 0;
        for (const a of arquivos) {
          atual = linha('Gravando ' + a.caminho.replace(/\//g, '\\'), 'atual');
          await escrever(pasta, a.caminho, a.dados);
          feito += a.dados.length;
          progresso(0.05 + 0.8 * feito / total);
          marcar(atual, 'ok');
        }
        if (temApp() && escolheuAjustes()) {
          try {
            const smw = await (await pasta.getDirectoryHandle('APPS')).getDirectoryHandle('SMW');
            await smw.removeEntry('SMW.OPT');
          } catch (e) { /* não existia */ }
        }
        atual = linha('Conferindo o que foi gravado', 'atual');
        for (const a of arquivos) {
          const lido = await ler(pasta, a.caminho);
          if (lido.length !== a.dados.length || N.crc32(lido) !== N.crc32(a.dados))
            throw new Error('O arquivo ' + a.caminho + ' ficou diferente no pendrive. Tente de novo ou use outro pendrive.');
        }
        marcar(atual, 'ok');
      } else {
        atual = linha(estado.modo === 'iso' ? 'Montando a ISO' : 'Montando o SMW_PS2_pendrive.zip', 'atual');
        progresso(0.5);
        if (estado.modo === 'iso') baixar(arquivos[0].dados, N.NOME_ISO);
        else baixar(N.gerarZip(arquivos), 'SMW_PS2_pendrive.zip');
        marcar(atual, 'ok');
        linha('Download começou: confira a pasta de downloads', 'ok');
      }
      progresso(1);
      estado.resultado = 'ok';
      $('t5').textContent = 'GRAVADO!';
      $('g-sub').textContent = gravaDireto ? 'Tudo conferido.' : 'Agora é só copiar para o pendrive.';
      anunciar('Gravado. Tudo conferido.');
    } catch (e) {
      if (atual) marcar(atual, 'erro');
      linha(explicarErro(e), 'erro');
      estado.resultado = 'erro';
      $('t5').textContent = 'NÃO DEU CERTO';
      $('g-sub').textContent = 'Nada do que já estava no pendrive foi apagado.';
      anunciar('A gravação falhou: ' + explicarErro(e));
    }
    estado.gravando = false;
    montarPronto();
    atualizarRodape();
    $('avancar').focus();
  }

  window.addEventListener('beforeunload', (e) => { if (estado.gravando) { e.preventDefault(); e.returnValue = ''; } });

  /* ---------------------------------------------------------------- pronto */
  function montarPronto() {
    const passos = [];
    if (gravaDireto) passos.push('Tire o pendrive com segurança e ligue no PS2.');
    else if (estado.modo === 'iso') passos.push('Copie a ISO baixada para a pasta CD do pendrive.');
    else passos.push('Descompacte o SMW_PS2_pendrive.zip na raiz do pendrive e ligue no PS2.');
    if (estado.modo === 'app') passos.push('Abra o OPL e vá em Aplicativos → Super Mario World. Pelo wLaunchELF: mass:/APPS/SMW/SMW.ELF.');
    else if (estado.modo === 'iso') passos.push('Abra o OPL, atualize a lista de jogos USB e abra Super Mario World. O progresso vai para o Memory Card.');
    else passos.push('No OPL, abra Super Mario World em Aplicativos ou na lista de jogos USB.');
    passos.push('Na tela AJUSTES, confira vídeo, tela e suavização e aperte X. Bom jogo!');
    $('r-onde').textContent = 'NO PS2';
    $('r-aviso').textContent = (estado.modo === 'app' ? 'O SMW.ZIP tem' : estado.modo === 'iso' ? 'A ISO tem' : 'O SMW.ZIP e a ISO têm') +
      ' o seu jogo dentro. É só para você: não compartilhe.';
    $('r-passos').replaceChildren(...passos.map((t, i) => {
      const d = el('div', 'numerado');
      d.append(el('span', 'n', String(i + 1)), el('span', '', t));
      return d;
    }));
  }

  /* ---------------------------------------------------------------- início */
  mostrarCartucho('vazio');
  atualizarResumo();
  irPara(1);
})();
