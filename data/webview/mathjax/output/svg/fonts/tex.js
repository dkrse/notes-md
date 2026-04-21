/* No-op stub. The bundled tex-chtml.js resolves a dependency path to
   output/svg/fonts/tex.js during startup (side-effect of loading the
   color/boldsymbol/cancel extensions under WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER).
   Without this file the HTTP fetch 404s, MathJax.loader.load rejects,
   mathjax.retryAfter() never resolves, and typesetPromise hangs forever.
   The bundle already did useOutput('chtml'); an empty script is enough. */
if (typeof MathJax !== 'undefined' && MathJax.loader && MathJax.loader.checkVersion) {
  try { MathJax.loader.checkVersion('output/svg/fonts/tex', '3.2.2', 'svg-font'); } catch (_) {}
}
