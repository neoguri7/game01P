/**
 * Checks docs/presentation/mid-presentation.html without a browser:
 *   - every <pre class="mermaid"> block parses (real mermaid, vendored copy)
 *   - slide / diagram counts, tag balance, keyboard routing, offline asset
 *
 * Usage (jsdom is not a repo dependency, keep it out of the project):
 *   mkdir -p /tmp/mmd && cd /tmp/mmd && npm install jsdom
 *   NODE_PATH=/tmp/mmd/node_modules node docs/presentation/verify.js
 */
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const { JSDOM } = require("jsdom");

const ROOT = path.resolve(__dirname, "../..");
const HTML = path.join(__dirname, "mid-presentation.html");
const MERMAID = path.join(__dirname, "vendor/mermaid.min.js");

const html = fs.readFileSync(HTML, "utf8");

const dom = new JSDOM(
  '<!doctype html><html><body><div id="c"></div></body></html>',
  {
    runScripts: "outside-only",
    pretendToBeVisual: true,
  },
);
vm.runInContext(fs.readFileSync(MERMAID, "utf8"), dom.getInternalVMContext());
const mermaid = dom.window.mermaid;
if (!mermaid)
  throw new Error("the vendored bundle did not define window.mermaid");

mermaid.initialize({
  startOnLoad: false,
  securityLevel: "loose",
  theme: "base",
});

const blocks = [
  ...html.matchAll(/<pre class="mermaid">([\s\S]*?)<\/pre>/g),
].map((m) => unescapeHtml(m[1]).trim());

/// Mirrors what the HTML parser does to text nodes: mermaid reads `--&gt;` in the source as `-->`.
/// (Arrows are escaped in the markup so the unescaped-`>` lint rule stays quiet.)
function unescapeHtml(text) {
  return text
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&nbsp;/g, " ")
    .replace(/&amp;/g, "&");
}

(async () => {
  let failed = 0;
  for (const [n, text] of blocks.entries()) {
    const head = text.split("\n")[0];
    try {
      const r = await mermaid.parse(text);
      console.log(
        `OK   #${n + 1} ${r && r.diagramType ? r.diagramType : "?"} — ${head}`,
      );
    } catch (e) {
      failed++;
      console.log(
        `FAIL #${n + 1} — ${head}\n     ${String(e.message || e).split("\n")[0]}`,
      );
    }
  }

  const slides = (html.match(/<section class="slide/g) || []).length;
  const balance = tagBalance(html);

  console.log("");
  console.log("slides:            ", slides);
  console.log(
    "diagrams:          ",
    blocks.length,
    failed === 0 ? "(all parse)" : `(${failed} FAILED)`,
  );
  console.log(
    "tag balance:       ",
    balance.ok ? "ok" : `MISMATCH — ${balance.problems.join("; ")}`,
  );
  console.log(
    "offline mermaid:   ",
    /vendor\/mermaid\.min\.js/.test(html) ? "yes" : "NO",
  );
  console.log(
    "escaped arrows:    ",
    `${(html.match(/&gt;/g) || []).length} (&gt; inside mermaid blocks)`,
  );
  console.log(
    "keyboard routing:  ",
    /ArrowRight/.test(html) && /ArrowLeft/.test(html) ? "yes" : "NO",
  );
  console.log("print stylesheet:  ", /@media print/.test(html) ? "yes" : "NO");
  process.exitCode = failed === 0 && balance.ok ? 0 : 1;
})();

/// Stack scan over real tags: the naive open/close count is fooled by tag-like text in CSS/JS.
function tagBalance(markup) {
  const clean = markup
    .replace(/<!--[\s\S]*?-->/g, "")
    .replace(/<style>[\s\S]*?<\/style>/g, "<style></style>")
    .replace(/<script>[\s\S]*?<\/script>/g, "<script></script>");
  const voidTags = new Set([
    "br",
    "meta",
    "link",
    "hr",
    "img",
    "input",
    "source",
    "col",
    "wbr",
    "!doctype",
  ]);
  const re = /<(\/?)([a-zA-Z!][a-zA-Z0-9]*)[^>]*?(\/?)>/g;
  const stack = [];
  const problems = [];
  for (let m = re.exec(clean); m !== null; m = re.exec(clean)) {
    const name = m[2].toLowerCase();
    if (voidTags.has(name) || m[3] === "/") continue;
    if (m[1] === "") {
      stack.push({ name, line: clean.slice(0, m.index).split("\n").length });
      continue;
    }
    const top = stack.pop();
    if (top === undefined || top.name !== name) {
      problems.push(
        `</${name}> at line ${clean.slice(0, m.index).split("\n").length} closes ${top ? `<${top.name}> from line ${top.line}` : "nothing"}`,
      );
    }
  }
  for (const open of stack)
    problems.push(`<${open.name}> at line ${open.line} never closed`);
  return { ok: problems.length === 0, problems };
}
