#!/usr/bin/env node
// Mechanical fidelity check for a doc digest (zero LLM tokens).
// usage: node check-digest.mjs <digest.json> <source.txt>
// Exit 0 = pass, 1 = failures found, 2 = bad input. Warnings never fail the check.
import { readFileSync } from 'node:fs';

const [, , digestPath, sourcePath] = process.argv;
if (!digestPath || !sourcePath) {
  console.error('usage: node check-digest.mjs <digest.json> <source.txt>');
  process.exit(2);
}

const TYPES = new Set(['number', 'definition', 'condition', 'method', 'result', 'limitation', 'counter', 'procedure']);
const norm = (s) => s
  .replace(/-\s*\n\s*/g, '')                 // de-hyphenate line breaks
  .replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
  .replace(/[‐-―−]/g, '-')
  .replace(/ /g, ' ')
  .replace(/\s+/g, ' ')
  .trim()
  .toLowerCase();
const numbers = (s) => (s.match(/\d+(?:[.,]\d+)*/g) || []).map((n) => n.replace(/,/g, ''));

let digest, source;
try {
  digest = JSON.parse(readFileSync(digestPath, 'utf8'));
  source = readFileSync(sourcePath, 'utf8');
} catch (e) {
  console.error(`cannot read input: ${e.message}`);
  process.exit(2);
}

const fail = [];
const warn = [];
const sections = Array.isArray(digest.sections) ? digest.sections : [];
if (!sections.length) fail.push('digest has no sections[]');

// 1. Coverage ledger: section spans must cover the source.
const spans = sections
  .map((s) => s.span)
  .filter((sp) => Array.isArray(sp) && sp.length === 2)
  .sort((a, b) => a[0] - b[0]);
let covered = 0;
let cursor = 0;
for (const [a, b] of spans) {
  if (a - cursor > 2000) fail.push(`uncovered source chars ${cursor}..${a} (no section entry)`);
  covered += Math.max(0, b - Math.max(a, cursor));
  cursor = Math.max(cursor, b);
}
if (source.length - cursor > 2000) fail.push(`uncovered source tail ${cursor}..${source.length}`);
const coverage = source.length ? covered / source.length : 0;
if (coverage < 0.98) fail.push(`span coverage ${(coverage * 100).toFixed(1)}% < 98%`);

const normSource = norm(source);
let recordCount = 0;
for (const s of sections) {
  const sid = s.id ?? '(no id)';
  if (!Array.isArray(s.span)) fail.push(`${sid}: missing span [start,end]`);
  for (const f of ['limitations', 'contradictions']) {
    if (typeof s[f] !== 'string' || !s[f].trim()) fail.push(`${sid}: field "${f}" empty (write "none found" if so)`);
  }
  const sectionText = Array.isArray(s.span) ? norm(source.slice(s.span[0], s.span[1])) : normSource;
  const records = Array.isArray(s.records) ? s.records : [];
  let numericRecords = 0;
  for (const [i, r] of records.entries()) {
    recordCount++;
    const rid = `${sid}#${i}`;
    if (!TYPES.has(r.type)) fail.push(`${rid}: unknown type "${r.type}"`);
    const q = norm(r.quote ?? '');
    if (!q) { fail.push(`${rid}: empty quote`); continue; }
    if (!sectionText.includes(q)) {
      fail.push(normSource.includes(q) ? `${rid}: quote found outside its section span` : `${rid}: quote is not verbatim source text`);
    }
    const missing = numbers(r.claim ?? '').filter((n) => !numbers(r.quote).includes(n));
    if (missing.length) fail.push(`${rid}: claim numbers not in quote: ${missing.join(', ')}`);
    if (numbers(r.quote).length) numericRecords++;
  }
  // 2. Omission heuristic: number-dense sections with no numeric record.
  const srcNums = numbers(sectionText).length;
  if (srcNums >= 8 && numericRecords === 0) warn.push(`${sid}: source section has ${srcNums} numbers but no numeric record — possible omission`);
}

console.log(`sections=${sections.length} records=${recordCount} coverage=${(coverage * 100).toFixed(1)}% failures=${fail.length} warnings=${warn.length}`);
for (const f of fail) console.log(`FAIL ${f}`);
for (const w of warn) console.log(`WARN ${w}`);
process.exit(fail.length ? 1 : 0);
