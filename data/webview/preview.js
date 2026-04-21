(function () {
  'use strict';

  // Marked extensions to protect LaTeX from markdown parser.
  // We convert $$...$$ and $...$ into \[...\] / \(...\) tokens that MathJax picks up,
  // and make sure marked does not touch their content (no _emph_, no \\ collapsing).
  const mathBlock = {
    name: 'mathBlock',
    level: 'block',
    start(src) { const i = src.indexOf('$$'); return i < 0 ? undefined : i; },
    tokenizer(src) {
      const m = /^\$\$([\s\S]+?)\$\$(?:\n|$)/.exec(src);
      if (m) return { type: 'mathBlock', raw: m[0], text: m[1] };
    },
    renderer(tok) {
      return '<div class="math-display">\\[' + escapeHtml(tok.text) + '\\]</div>\n';
    }
  };
  const mathInline = {
    name: 'mathInline',
    level: 'inline',
    start(src) { const i = src.indexOf('$'); return i < 0 ? undefined : i; },
    tokenizer(src) {
      const m = /^\$(?!\s)([^\$\n]+?)(?<!\s)\$/.exec(src);
      if (m) return { type: 'mathInline', raw: m[0], text: m[1] };
    },
    renderer(tok) {
      return '<span class="math-inline">\\(' + escapeHtml(tok.text) + '\\)</span>';
    }
  };

  function escapeHtml(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  marked.use({ extensions: [mathBlock, mathInline] });

  // Custom renderer: fenced ```mermaid becomes a <div class="mermaid"> placeholder.
  const renderer = new marked.Renderer();
  const origCode = renderer.code.bind(renderer);
  renderer.code = function (code, lang) {
    const c = (typeof code === 'object' && code.text) ? code.text : code;
    const l = (typeof code === 'object' && code.lang) ? code.lang : lang;
    if (l === 'mermaid') {
      return '<div class="mermaid">' + escapeHtml(c) + '</div>\n';
    }
    return origCode(code, lang);
  };
  marked.use({ renderer });

  mermaid.initialize({ startOnLoad: false, securityLevel: 'loose', theme: 'default' });

  const el = document.getElementById('content');
  let mermaidSeq = 0;
  let pending = null;
  let rendering = false;

  async function render(md) {
    if (rendering) { pending = md; return; }
    rendering = true;
    try {
      const html = marked.parse(md || '', { gfm: true, breaks: false });
      el.innerHTML = html;

      // Mermaid
      const blocks = el.querySelectorAll('div.mermaid');
      for (const node of blocks) {
        const raw = node.textContent;
        const id = 'mmd-' + (++mermaidSeq);
        try {
          const { svg } = await mermaid.render(id, raw);
          node.innerHTML = svg;
        } catch (e) {
          node.innerHTML = '<span class="math-error">Mermaid: ' + escapeHtml(String(e && e.message || e)) + '</span>';
        }
      }

      // MathJax — last, so it sees final DOM
      if (window.MathJax && MathJax.typesetPromise) {
        if (MathJax.typesetClear) MathJax.typesetClear([el]);
        try { await MathJax.typesetPromise([el]); }
        catch (e) { /* swallow; leave raw */ }
        // Force full repaint after typeset — software-rendered WebKit otherwise
        // leaves off-screen math blocks unpainted until scrolled into view.
        el.style.display = 'none';
        void el.offsetHeight;
        el.style.display = '';
      }
    } finally {
      rendering = false;
      if (pending !== null) {
        const next = pending; pending = null;
        render(next);
      }
    }
  }

  window.nmdRender = render;
  window.nmdSetTheme = function (dark) {
    document.documentElement.classList.toggle('dark', !!dark);
  };
  window.nmdSetLayout = function (full) {
    document.documentElement.classList.toggle('full', !!full);
  };
  window.nmdReady = true;
})();
