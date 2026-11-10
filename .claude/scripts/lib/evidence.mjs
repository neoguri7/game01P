import fs from 'node:fs';
import path from 'node:path';

export const ERROR_RE = /\b(?:fatal )?error [A-Z]+\d+\s*:|: error:|LINK : fatal error/;

/** Deduplicated compiler/linker error lines, first 40. */
export function filterBuildOutput(text) {
  const seen = new Set();
  const out = [];
  for (const raw of String(text).split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || !ERROR_RE.test(line) || seen.has(line)) continue;
    seen.add(line);
    out.push(line);
    if (out.length >= 40) break;
  }
  return out;
}

const unescapeXml = (s) =>
  s
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(Number(n)))
    .replace(/&#x([0-9a-fA-F]+);/g, (_, n) => String.fromCodePoint(parseInt(n, 16)))
    .replace(/&amp;/g, '&');

/** Parses ctest --output-junit XML. */
export function parseJUnit(xml) {
  const res = { passed: [], failed: [], notRun: [], casesPassed: [], outputs: {} };
  const re = /<testcase\b([^>]*?)(?:\/>|>([\s\S]*?)<\/testcase>)/g;
  let m;
  while ((m = re.exec(xml))) {
    const attrs = m[1];
    const body = m[2] ?? '';
    const name = unescapeXml(/\bname="([^"]*)"/.exec(attrs)?.[1] ?? '');
    const status = /\bstatus="([^"]*)"/.exec(attrs)?.[1] ?? 'run';
    const so = /<system-out>([\s\S]*?)<\/system-out>/.exec(body);
    const output = so ? unescapeXml(so[1].replace(/^<!\[CDATA\[|\]\]>$/g, '')) : '';
    res.outputs[name] = output;
    if (status === 'notrun' || /<skipped\b/.test(body)) {
      res.notRun.push(name);
    } else if (/<failure\b/.test(body) || /<error\b/.test(body)) {
      res.failed.push(name);
    } else {
      res.passed.push(name);
    }
    for (const line of output.split(/\r?\n/)) {
      const c = /^\s*\[ ok \] (.+?)\s*$/.exec(line);
      if (c) res.casesPassed.push(`${name}::${c[1]}`);
    }
  }
  return res;
}

/** Sorted file names of tests/features/*_tests.cpp. */
export function featureSet(repoDir) {
  const dir = path.join(repoDir, 'tests', 'features');
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir).filter((f) => f.endsWith('_tests.cpp')).sort();
}
