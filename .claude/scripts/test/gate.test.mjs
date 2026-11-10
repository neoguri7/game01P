import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const GATE = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'wp-gate.mjs');

function git(cwd, ...args) {
  const r = spawnSync('git', args, { cwd, encoding: 'utf8' });
  if (r.status !== 0) throw new Error(`git ${args.join(' ')} failed: ${r.stderr}`);
  return r.stdout.trim();
}
const write = (root, rel, text) => {
  fs.mkdirSync(path.dirname(path.join(root, rel)), { recursive: true });
  fs.writeFileSync(path.join(root, rel), text);
};

const BASE_FEATURES = {
  wp: 'X',
  files: ['a.txt', 't.test.mjs'],
  features: [
    { id: 'F1', behavior: 'alpha', test: 'node:t.test.mjs::Alpha', passes: false },
    { id: 'F2', behavior: 'beta', test: 'suite_tests::Beta', passes: false },
  ],
};

/** Temp repo on main with the base commit, plus worktree <root>-wt on `branch`. */
function fixture({ features = BASE_FEATURES, branch = 'wp/X' } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wp35-gate-'));
  git(root, 'init', '-b', 'main');
  git(root, 'config', 'user.name', 't');
  git(root, 'config', 'user.email', 't@t');
  write(root, 'docs/wp/X/spec.md', '# X: Title\n');
  write(root, 'docs/wp/X/features.json', JSON.stringify(features, null, 1));
  write(root, 'tests/features/suite_tests.cpp', 'CHECK(1); CHECK(2); CHECK(3); {"Beta", Beta}\n');
  write(root, 'CMakeLists.txt', 'set_tests_properties(x PROPERTIES TIMEOUT 30)\n');
  write(root, 'a.txt', 'one\n');
  write(root, '.gitignore', 'out/\n');
  git(root, 'add', '-A');
  git(root, 'commit', '-m', 'base');
  const base = git(root, 'rev-parse', 'HEAD');
  const wt = root + '-wt';
  git(root, 'worktree', 'add', '-b', branch, wt);
  return { root, wt, base };
}

function evidence(wt, over = {}) {
  const ev = {
    sha: git(wt, 'rev-parse', 'HEAD'),
    dirty: false,
    preset: 'windows-msvc-agent',
    configured: false,
    build: { ok: true, errors: [] },
    ctest: { ran: true, passed: ['suite_tests'], failed: [], notRun: [] },
    cases: { passed: ['suite_tests::Beta'] },
    ...over,
  };
  write(wt, 'out/evidence.json', JSON.stringify(ev));
}

const PASSING_NODE_TEST = "import { test } from 'node:test';\ntest('Alpha', () => {});\n";

function commitAll(wt, msg = 'wp') {
  git(wt, 'add', '-A');
  git(wt, 'commit', '-m', msg);
}

function runGate(f, id = 'X') {
  const r = spawnSync(process.execPath, [GATE, id, '--worktree', f.wt, '--base', f.base], { cwd: f.wt, encoding: 'utf8' });
  let out = null;
  try {
    out = JSON.parse(r.stdout);
  } catch {
    /* leave null */
  }
  return { code: r.status, out, raw: r.stdout + r.stderr };
}
const ruleIds = (g) => (g.out?.rules ?? []).map((r) => r.id);

/** Clean passing branch with optional mutation; returns gate result. */
function scenario({ fixtureOpts, mutate, ev, skipCommit = false, skipTest = false, afterCommit } = {}) {
  const f = fixture(fixtureOpts);
  if (!skipTest) write(f.wt, 't.test.mjs', PASSING_NODE_TEST);
  if (mutate) mutate(f);
  if (!skipCommit) commitAll(f.wt);
  if (afterCommit) afterCommit(f);
  if (ev !== null) evidence(f.wt, ev);
  return { f, g: runGate(f) };
}

test('DerivesPasses', () => {
  let { g } = scenario();
  assert.equal(g.code, 0, g.raw);
  assert.equal(g.out.ok, true);
  assert.deepEqual(g.out.rules, []);
  assert.deepEqual(g.out.features.map((x) => x.passes), [true, true]);

  // Beta missing from evidence -> unproven
  ({ g } = scenario({ ev: { cases: { passed: [] } } }));
  assert.equal(g.code, 1);
  assert.ok(ruleIds(g).includes('feature-unproven'));
  assert.deepEqual(g.out.features.map((x) => x.passes), [true, false]);

  // Beta in evidence but the C++ file lacks the case text -> unproven
  ({ g } = scenario({
    fixtureOpts: { features: { ...BASE_FEATURES, files: [...BASE_FEATURES.files, 'tests/features/suite_tests.cpp'] } },
    mutate: (f) => write(f.wt, 'tests/features/suite_tests.cpp', 'CHECK(1); CHECK(2); CHECK(3);\n'),
  }));
  assert.ok(ruleIds(g).includes('feature-unproven'));

  // failing node test -> unproven
  ({ g } = scenario({ mutate: (f) => write(f.wt, 't.test.mjs', "import { test } from 'node:test';\ntest('Alpha', () => { throw new Error('x'); });\n") }));
  assert.ok(ruleIds(g).includes('feature-unproven'));
  assert.deepEqual(g.out.features.map((x) => x.passes), [false, true]);
});

test('UntrackedNodeTestUnproven', () => {
  const { g } = scenario({ skipTest: true, mutate: (f) => write(f.wt, 'a.txt', 'two'), afterCommit: (f) => write(f.wt, 't.test.mjs', PASSING_NODE_TEST) });
  assert.equal(g.code, 1, g.raw);
  assert.ok(ruleIds(g).includes('feature-unproven'));
  assert.deepEqual(g.out.features.map((x) => x.passes), [false, true]);
});

test('Rejections', () => {
  const has = (g, id) => {
    assert.equal(g.code, 1, g.raw);
    assert.ok(ruleIds(g).includes(id), `expected ${id}, got ${ruleIds(g)}`);
  };

  has(scenario({ mutate: (f) => write(f.wt, 'b.txt', 'x') }).g, 'scope');
  has(scenario({ mutate: (f) => write(f.wt, 'docs/wp/X/spec.md', '# X: Changed\n') }).g, 'spec-edit');
  has(scenario({ mutate: (f) => write(f.wt, 'docs/wp/X/features.json', '{}') }).g, 'spec-edit');

  // test-deleted: file under tests/ removed
  const del = scenario({
    fixtureOpts: { features: { ...BASE_FEATURES, files: [...BASE_FEATURES.files, 'tests/features/suite_tests.cpp'] } },
    mutate: (f) => fs.unlinkSync(path.join(f.wt, 'tests/features/suite_tests.cpp')),
  });
  has(del.g, 'test-deleted');

  // check-drop, and the allowCheckDrop exemption
  const withTests = { ...BASE_FEATURES, files: [...BASE_FEATURES.files, 'tests/features/suite_tests.cpp'] };
  const drop = (f) => write(f.wt, 'tests/features/suite_tests.cpp', 'CHECK(1); CHECK(2); {"Beta", Beta}\n');
  has(scenario({ fixtureOpts: { features: withTests }, mutate: drop }).g, 'check-drop');
  const exempt = scenario({
    fixtureOpts: { features: { ...withTests, allowCheckDrop: ['tests/features/suite_tests.cpp'] } },
    mutate: drop,
  });
  assert.ok(!ruleIds(exempt.g).includes('check-drop'));

  // timeout changes
  const tmo = (f) => write(f.wt, 'CMakeLists.txt', 'set_tests_properties(x PROPERTIES TIMEOUT 60)\n');
  const withCmake = { ...BASE_FEATURES, files: [...BASE_FEATURES.files, 'CMakeLists.txt'] };
  has(scenario({ fixtureOpts: { features: withCmake }, mutate: tmo }).g, 'timeout');
  const tmoOk = scenario({ fixtureOpts: { features: { ...withCmake, allowTimeoutChange: true } }, mutate: tmo });
  assert.ok(!ruleIds(tmoOk.g).includes('timeout'));

  // evidence problems
  has(scenario({ ev: { sha: '0'.repeat(40) } }).g, 'evidence-sha');
  has(scenario({ ev: { dirty: true } }).g, 'evidence-dirty');
  has(scenario({ ev: { build: { ok: false, errors: ['e'] } } }).g, 'build-failed');
  has(scenario({ ev: { ctest: { ran: true, passed: [], failed: ['suite_tests'], notRun: [] } } }).g, 'ctest-failed');
  has(scenario({ ev: { ctest: { ran: true, passed: [], failed: [], notRun: ['suite_tests'] } } }).g, 'ctest-failed');

  // evidence missing
  const noEv = scenario({ ev: null });
  has(noEv.g, 'evidence-missing');

  // uncommitted tracked change
  has(
    scenario({ afterCommit: (f) => write(f.wt, 'a.txt', 'changed\n') }).g,
    'dirty',
  );

  // wrong branch
  has(scenario({ fixtureOpts: { branch: 'other' } }).g, 'branch');

  // a rename counts as old and new path: renaming a.txt to b.txt leaves scope violation on b.txt
  has(
    scenario({
      mutate: (f) => {
        git(f.wt, 'mv', 'a.txt', 'b.txt');
      },
    }).g,
    'scope',
  );
});

test('Overlap', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wp35-overlap-'));
  git(root, 'init', '-b', 'main');
  git(root, 'config', 'user.name', 't');
  git(root, 'config', 'user.email', 't@t');
  write(root, 'docs/wp/A/features.json', JSON.stringify({ wp: 'A', files: ['a.txt', 'shared.txt', 'z.txt'], features: [] }));
  write(root, 'docs/wp/B/features.json', JSON.stringify({ wp: 'B', files: ['b.txt', 'shared.txt', 'z.txt'], features: [] }));
  write(root, 'docs/wp/C/features.json', JSON.stringify({ wp: 'C', files: ['c.txt'], features: [] }));
  git(root, 'add', '-A');
  git(root, 'commit', '-m', 'wps');
  const run = (a, b) => spawnSync(process.execPath, [GATE, '--overlap', a, b], { cwd: root, encoding: 'utf8' });
  let r = run('A', 'B');
  assert.equal(r.status, 1, r.stderr);
  assert.deepEqual(JSON.parse(r.stdout).sort(), ['shared.txt', 'z.txt']);
  r = run('A', 'C');
  assert.equal(r.status, 0, r.stderr);
  assert.deepEqual(JSON.parse(r.stdout), []);
});
