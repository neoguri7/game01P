#!/usr/bin/env node
// Deterministic WP gate.
//   node .claude/scripts/wp-gate.mjs <id> --worktree <path> --base <sha>
//   node .claude/scripts/wp-gate.mjs --overlap <idA> <idB>
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { parseJUnit } from './lib/evidence.mjs';

function git(cwd, args, { allowFail = false } = {}) {
  const r = spawnSync('git', args, { cwd, encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
  if (r.status !== 0) {
    if (allowFail) return null;
    throw new Error(`git ${args.join(' ')} failed: ${r.stderr}`);
  }
  return r.stdout;
}

const argv = process.argv.slice(2);

if (argv[0] === '--overlap') {
  const [a, b] = [argv[1], argv[2]];
  const files = (id) => JSON.parse(git(process.cwd(), ['show', `main:docs/wp/${id}/features.json`])).files ?? [];
  const fb = new Set(files(b));
  const shared = [...new Set(files(a))].filter((f) => fb.has(f));
  console.log(JSON.stringify(shared));
  process.exit(shared.length ? 1 : 0);
}

const id = argv[0];
let worktree;
let base;
for (let i = 1; i < argv.length; i++) {
  if (argv[i] === '--worktree') worktree = path.resolve(argv[++i]);
  else if (argv[i] === '--base') base = argv[++i];
}
if (!id || !worktree || !base) {
  console.error('usage: wp-gate.mjs <id> --worktree <path> --base <sha> | --overlap <idA> <idB>');
  process.exit(64);
}

const rules = [];
const fail = (ruleId, detail) => rules.push({ id: ruleId, detail });

const spec = JSON.parse(git(worktree, ['show', `${base}:docs/wp/${id}/features.json`]));
const headSha = git(worktree, ['rev-parse', 'HEAD']).trim();

// --- branch / dirty
const branch = git(worktree, ['rev-parse', '--abbrev-ref', 'HEAD']).trim();
if (branch !== `wp/${id}`) fail('branch', `on ${branch}, expected wp/${id}`);
if (git(worktree, ['status', '--porcelain', '--untracked-files=no']).trim()) fail('dirty', 'tracked changes are uncommitted');

// --- changed paths
const changes = [];
for (const line of git(worktree, ['diff', '--name-status', '-M', `${base}..HEAD`]).split(/\r?\n/)) {
  if (!line) continue;
  const [status, ...paths] = line.split('\t');
  changes.push({ status: status[0], paths });
}
const allowed = new Set([...(spec.files ?? []), `docs/wp/${id}/progress.md`, `docs/wp/${id}/decisions.md`]);
const touched = changes.flatMap((c) => c.paths);
for (const p of touched) if (!allowed.has(p)) fail('scope', `${p} is outside the file map`);
for (const p of [`docs/wp/${id}/spec.md`, `docs/wp/${id}/features.json`]) {
  if (touched.includes(p)) fail('spec-edit', `${p} changed`);
}
for (const c of changes) {
  if (c.status === 'D' && c.paths[0].startsWith('tests/')) fail('test-deleted', `${c.paths[0]} deleted`);
}

// --- check-drop
const countChecks = (text) => (text.match(/\bCHECK\s*\(/g) ?? []).length;
const exempt = new Set(spec.allowCheckDrop ?? []);
for (const c of changes) {
  const p = c.paths[c.paths.length - 1];
  const oldP = c.paths[0];
  if (c.status === 'D' || !/^tests\/.*\.(cpp|h)$/.test(oldP) || exempt.has(oldP)) continue;
  const before = git(worktree, ['show', `${base}:${oldP}`], { allowFail: true });
  const after = git(worktree, ['show', `HEAD:${p}`], { allowFail: true });
  if (before === null || after === null) continue;
  if (countChecks(after) < countChecks(before)) fail('check-drop', `${oldP}: ${countChecks(before)} -> ${countChecks(after)} CHECKs`);
}

// --- timeout multiset
if (!spec.allowTimeoutChange) {
  const timeouts = (rev) => {
    const files = (git(worktree, ['ls-tree', '-r', '--name-only', rev]) ?? '').split(/\r?\n/).filter((f) => /(^|\/)CMakeLists\.txt$/.test(f));
    const all = [];
    for (const f of files) {
      for (const m of (git(worktree, ['show', `${rev}:${f}`]) ?? '').matchAll(/TIMEOUT\s+(\d+)/g)) all.push(m[1]);
    }
    return all.sort().join(',');
  };
  if (timeouts(base) !== timeouts('HEAD')) fail('timeout', 'TIMEOUT values in CMakeLists.txt changed');
}

// --- evidence
let evidence = null;
const evidencePath = path.join(worktree, 'out', 'evidence.json');
try {
  evidence = JSON.parse(fs.readFileSync(evidencePath, 'utf8'));
} catch {
  fail('evidence-missing', `${evidencePath} missing or unreadable`);
}
if (evidence) {
  if (evidence.sha !== headSha) fail('evidence-sha', `evidence ${String(evidence.sha).slice(0, 12)} != HEAD ${headSha.slice(0, 12)}`);
  if (evidence.dirty) fail('evidence-dirty', 'evidence was produced from a dirty tree');
  if (!evidence.build?.ok) fail('build-failed', (evidence.build?.errors ?? []).slice(0, 5).join(' | ') || 'build not ok');
  const failed = evidence.ctest?.failed ?? [];
  const notRun = evidence.ctest?.notRun ?? [];
  if (failed.length || notRun.length) fail('ctest-failed', `failed: ${failed.join(', ')}; not run: ${notRun.join(', ')}`);
}

// --- features
const features = spec.features ?? [];
const inHead = (file) => git(worktree, ['cat-file', '-e', `HEAD:${file}`], { allowFail: true }) !== null;
// prove from HEAD, not the working tree: an untracked or uncommitted test file proves nothing
const nodeFiles = [...new Set(features.filter((f) => f.test.startsWith('node:')).map((f) => f.test.slice(5).split('::')[0]))].filter(inHead);
let nodePassed = new Set();
if (nodeFiles.length) {
  const junit = path.join(worktree, 'out', 'node-junit.xml');
  fs.mkdirSync(path.dirname(junit), { recursive: true });
  fs.rmSync(junit, { force: true });
  const env = { ...process.env };
  delete env.NODE_TEST_CONTEXT;
  spawnSync(process.execPath, ['--test', '--test-reporter=junit', `--test-reporter-destination=${junit}`, ...nodeFiles], {
    cwd: worktree,
    env,
    encoding: 'utf8',
    maxBuffer: 256 * 1024 * 1024,
  });
  try {
    nodePassed = new Set(parseJUnit(fs.readFileSync(junit, 'utf8')).passed);
  } catch {
    /* no report: nothing proven */
  }
}

function proven(test) {
  if (test.startsWith('node:')) {
    const [file, ...nm] = test.slice(5).split('::');
    return inHead(file) && nodePassed.has(nm.join('::'));
  }
  if (!evidence) return false;
  if (test.startsWith('ctest::')) return (evidence.ctest?.passed ?? []).includes(test.slice(7));
  const [suite, ...rest] = test.split('::');
  const name = rest.join('::');
  if (!(evidence.cases?.passed ?? []).includes(test)) return false;
  const text = git(worktree, ['show', `HEAD:tests/features/${suite}.cpp`], { allowFail: true });
  return text !== null && text.includes(`"${name}"`);
}

const outFeatures = features.map((f) => ({ id: f.id, test: f.test, passes: proven(f.test) }));
for (const f of outFeatures) if (!f.passes) fail('feature-unproven', `${f.id}: ${f.test}`);

const ok = rules.length === 0;
console.log(JSON.stringify({ ok, rules, features: outFeatures }, null, 2));
process.exit(ok ? 0 : 1);
