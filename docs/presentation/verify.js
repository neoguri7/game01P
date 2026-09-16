/**
 * Checks docs/presentation/mid-presentation.html without a build.
 *
 * Static checks (always): diagram source parses, slide/diagram counts, tag balance,
 * offline asset, keyboard routing, print stylesheet, escaped arrows, and labels that
 * mermaid would read as markdown (a label like "1. 엔진 구조" renders as an ordered list
 * and mermaid bails out with an error blob).
 *
 * Render check (when a Chromium browser is found): loads the deck head-less and asserts
 * that every diagram really rendered. This is the check that catches the failure mode
 * where mermaid renders into a hidden element, measures 0x0 and emits its 16x16 fallback.
 *
 * Usage (jsdom is not a repo dependency, keep it out of the project):
 *   mkdir -p /tmp/mmd && cd /tmp/mmd && npm install jsdom
 *   NODE_PATH=/tmp/mmd/node_modules node docs/presentation/verify.js
 *   DECK_BROWSER="/c/Program Files/.../msedge.exe" NODE_PATH=... node verify.js   # force a browser
 */
const fs = require("fs");
const os = require("os");
const path = require("path");
const vm = require("vm");
const { execFileSync } = require("child_process");
const { JSDOM } = require("jsdom");

const HTML = path.join(__dirname, "mid-presentation.html");
const MERMAID = path.join(__dirname, "vendor/mermaid.min.js");
const html = fs.readFileSync(HTML, "utf8");

const MERMAID_FALLBACK_VIEWBOX = 'viewBox="-8 -8 16 16"';

const dom = new JSDOM(
  '<!doctype html><html><body><div id="c"></div></body></html>',
  { runScripts: "outside-only", pretendToBeVisual: true },
);
vm.runInContext(fs.readFileSync(MERMAID, "utf8"), dom.getInternalVMContext());
const mermaid = dom.window.mermaid;
if (!mermaid) throw new Error("the vendored bundle did not define window.mermaid");
mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "base" });

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

const sources = [...html.matchAll(/<pre class="mermaid">([\s\S]*?)<\/pre>/g)].map(
  (m) => unescapeHtml(m[1]).trim(),
);

/// Labels: everything quoted in the diagram source is rendered through markdown by mermaid.
function markdownLabelProblems() {
  const problems = [];
  for (const [n, source] of sources.entries()) {
    const labels = [...source.matchAll(/"([^"\n]+)"/g)].map((m) => m[1].trim());
    for (const label of labels) {
      if (/^\d+[.)]\s/.test(label) || /^[-*+]\s/.test(label)) {
        problems.push(
          `diagram ${n + 1}: label "${label}" reads as a markdown list — mermaid renders an error blob`,
        );
      }
    }
  }
  return problems;
}

function findBrowser() {
  const candidates = [
    process.env.DECK_BROWSER,
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
  ].filter(Boolean);
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) return candidate;
  }
  for (const name of ["chrome", "chromium", "msedge"]) {
    try {
      return execFileSync(process.platform === "win32" ? "where" : "which", [name], {
        stdio: ["ignore", "pipe", "ignore"],
      })
        .toString()
        .split(/\r?\n/)[0]
        .trim();
    } catch {
      /* not on PATH */
    }
  }
  return null;
}

/// Loads the deck in a real browser and reports how many diagrams produced a usable <svg>.
function renderCheck() {
  const browser = findBrowser();
  if (!browser) return { skipped: "no Chromium browser found (set DECK_BROWSER to check rendering)" };
  const url = "file:///" + HTML.replace(/\\/g, "/");
  const profile = path.join(os.tmpdir(), "deck-verify-profile");
  let dumped;
  try {
    dumped = execFileSync(
      browser,
      [
        "--headless=new",
        "--disable-gpu",
        "--no-sandbox",
        "--no-first-run",
        `--user-data-dir=${profile}`,
        "--virtual-time-budget=15000",
        "--dump-dom",
        url,
      ],
      { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"], maxBuffer: 64 * 1024 * 1024 },
    );
  } catch (err) {
    return { error: `browser run failed: ${String(err.message || err).split("\n")[0]}` };
  }
  const rendered = /data-rendered="(\d+)"/.exec(dumped);
  return {
    expected: sources.length,
    rendered: rendered ? Number(rendered[1]) : 0,
    fallbacks: dumped.split(MERMAID_FALLBACK_VIEWBOX).length - 1,
    failedPanels: dumped.split('class="diagram failed"').length - 1,
  };
}

(async () => {
  let failed = 0;
  for (const [n, text] of sources.entries()) {
    const head = text.split("\n")[0];
    try {
      const r = await mermaid.parse(text);
      console.log(`OK   #${n + 1} ${r && r.diagramType ? r.diagramType : "?"} — ${head}`);
    } catch (e) {
      failed++;
      console.log(
        `FAIL #${n + 1} — ${head}\n     ${String(e.message || e).split("\n")[0]}`,
      );
    }
  }

  const slides = (html.match(/<section class="slide/g) || []).length;
  const balance = tagBalance(html);
  const labelProblems = markdownLabelProblems();

  console.log("");
  console.log("slides:            ", slides);
  console.log(
    "diagrams:          ",
    sources.length,
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
    "markdown labels:   ",
    labelProblems.length === 0 ? "ok" : `PROBLEM — ${labelProblems.join("; ")}`,
  );
  console.log("render mode:       ", /\bmermaid\.render\(/.test(html) ? "render()" : "run()/startOnLoad");
  console.log(
    "keyboard routing:  ",
    /ArrowRight/.test(html) && /ArrowLeft/.test(html) ? "yes" : "NO",
  );
  console.log("print stylesheet:  ", /@media print/.test(html) ? "yes" : "NO");

  const rendered = renderCheck();
  if (rendered.skipped) {
    console.log("headless render:    skipped —", rendered.skipped);
  } else if (rendered.error) {
    failed++;
    console.log("headless render:    FAILED —", rendered.error);
  } else {
    const ok =
      rendered.rendered === rendered.expected &&
      rendered.fallbacks === 0 &&
      rendered.failedPanels === 0;
    if (!ok) failed++;
    console.log(
      "headless render:    ",
      ok
        ? `ok — ${rendered.rendered}/${rendered.expected} rendered, 0 fallbacks`
        : `FAILED — ${rendered.rendered}/${rendered.expected} rendered, ` +
            `${rendered.fallbacks} mermaid fallbacks, ${rendered.failedPanels} error panels`,
    );
  }

  process.exitCode = failed === 0 && balance.ok && labelProblems.length === 0 ? 0 : 1;
})();

/// Stack scan over real tags: the naive open/close count is fooled by tag-like text in CSS/JS.
function tagBalance(markup) {
  const clean = markup
    .replace(/<!--[\s\S]*?-->/g, "")
    .replace(/<style>[\s\S]*?<\/style>/g, "<style></style>")
    .replace(/<script>[\s\S]*?<\/script>/g, "<script></script>");
  const voidTags = new Set([
    "br", "meta", "link", "hr", "img", "input", "source", "col", "wbr", "!doctype",
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
  for (const open of stack) problems.push(`<${open.name}> at line ${open.line} never closed`);
  return { ok: problems.length === 0, problems };
}
