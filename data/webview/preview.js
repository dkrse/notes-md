(function () {
  'use strict';

  const engine = (window.nmdEngine || 'katex').toLowerCase();
  const useMathJax = (engine === 'mathjax');

  function escapeHtml(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  /* MathJax path needs marked extensions so $…$ / $$…$$ survive markdown
     parsing with their backslashes intact. KaTeX path uses placeholders
     (below) instead, so we must NOT register the extensions in that mode. */
  if (useMathJax) {
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
    marked.use({ extensions: [mathBlock, mathInline] });
  }

  /* Register the mermaid-aware renderer ONCE, globally, so that extension
     renderers (mathBlock/mathInline) stay active. Passing a fresh renderer
     via marked.parse() options would overwrite them. */
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

  /* Slugify heading text to GitHub-style anchor ids so in-document links
     like [text](#nadpis) resolve. Track collisions per render. */
  let slugCounts = {};
  function slugify(text) {
    let s = String(text)
      .toLowerCase()
      .replace(/[^\p{L}\p{N}\s-]/gu, '')
      .trim()
      .replace(/\s+/g, '-');
    if (!s) s = 'section';
    const n = slugCounts[s] || 0;
    slugCounts[s] = n + 1;
    return n === 0 ? s : s + '-' + n;
  }
  renderer.heading = function (text, level, raw) {
    const t = (typeof text === 'object') ? text : { text: text, depth: level, raw: raw };
    const depth = t.depth || level;
    const tokens = t.tokens;
    const inner = tokens ? this.parser.parseInline(tokens) : (t.text || text);
    const rawText = t.raw || raw || (typeof text === 'string' ? text : '');
    const id = slugify(rawText.replace(/<[^>]+>/g, ''));
    return '<h' + depth + ' id="' + id + '">' + inner + '</h' + depth + '>\n';
  };
  marked.use({ renderer });

  mermaid.initialize({ startOnLoad: false, securityLevel: 'loose', theme: 'default' });

  const el = document.getElementById('content');
  let mermaidSeq = 0;
  let pending = null;
  let rendering = false;

  async function renderMermaid() {
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
  }

  /* ── KaTeX path: synchronous math, single innerHTML write ── */
  async function renderKatex(md) {
    let src = md || '';
    const display = [], inline = [];
    src = src.replace(/\$\$([\s\S]+?)\$\$/g, (_, tex) => {
      display.push(tex); return '%%NMD_DISPLAY_' + (display.length - 1) + '%%';
    });
    src = src.replace(/\\\[([\s\S]+?)\\\]/g, (_, tex) => {
      display.push(tex); return '%%NMD_DISPLAY_' + (display.length - 1) + '%%';
    });
    src = src.replace(/(?<!\$)\$(?!\$)([^\$\n]+?)(?<!\s)\$/g, (_, tex) => {
      inline.push(tex); return '%%NMD_INLINE_' + (inline.length - 1) + '%%';
    });
    src = src.replace(/\\\(([\s\S]+?)\\\)/g, (_, tex) => {
      inline.push(tex); return '%%NMD_INLINE_' + (inline.length - 1) + '%%';
    });

    let html = marked.parse(src, { gfm: true, breaks: false });

    function renderMath(tex, displayMode) {
      try {
        return katex.renderToString(tex, { displayMode: displayMode, throwOnError: false });
      } catch (e) {
        return '<span class="math-error">' + escapeHtml(String(e && e.message || e)) + '</span>';
      }
    }
    html = html.replace(/%%NMD_DISPLAY_(\d+)%%/g, (_, i) => renderMath(display[+i].trim(), true));
    html = html.replace(/%%NMD_INLINE_(\d+)%%/g, (_, i) => renderMath(inline[+i].trim(), false));

    el.innerHTML = html;
    await renderMermaid();
  }

  /* ── MathJax path: marked (with mathBlock/mathInline extensions) + mermaid,
     then MathJax typesets the final DOM. ── */
  async function renderMathJax(md) {
    el.innerHTML = marked.parse(md || '', { gfm: true, breaks: false });

    await renderMermaid();

    if (window.MathJax && MathJax.typesetPromise) {
      if (MathJax.typesetClear) MathJax.typesetClear([el]);
      try { await MathJax.typesetPromise([el]); }
      catch (_) { /* swallow; raw \(…\) stays visible */ }
      /* Force repaint on software-rendered WebKit (nvidia/Wayland) — without this,
         async-typeset math blocks off-screen aren't invalidated until scrolled. */
      el.style.display = 'none';
      void el.offsetHeight;
      el.style.display = '';
    }
  }

  const renderImpl = useMathJax ? renderMathJax : renderKatex;

  async function render(md) {
    if (rendering) { pending = md; return; }
    rendering = true;
    try {
      slugCounts = {};
      await renderImpl(md);
    } finally {
      rendering = false;
      if (pending !== null) {
        const next = pending; pending = null;
        render(next);
      }
    }
  }

  /* Anchor link clicks (#slug) don't navigate cleanly because the page URL
     carries a query string — handle them manually with scrollIntoView. */
  document.addEventListener('click', function (ev) {
    let a = ev.target;
    while (a && a.tagName !== 'A') a = a.parentNode;
    if (!a) return;
    const href = a.getAttribute('href');
    if (!href || href.charAt(0) !== '#') return;
    const id = decodeURIComponent(href.slice(1));
    const target = id ? document.getElementById(id) : document.body;
    if (target) {
      ev.preventDefault();
      target.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  });

  window.nmdRender = render;
  window.nmdSetTheme = function (dark) {
    document.documentElement.classList.toggle('dark', !!dark);
    mermaid.initialize({
      startOnLoad: false, securityLevel: 'loose',
      theme: dark ? 'dark' : 'default'
    });
  };
  window.nmdSetLayout = function (full) {
    document.documentElement.classList.toggle('full', !!full);
  };
  window.nmdReady = true;
})();
