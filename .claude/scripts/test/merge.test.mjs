import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCRIPTS = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const MERGE = path.join(SCRIPTS, 'wp-merge.mjs');
const GATE = path.join(SCRIPTS, 'wp-gate.mjs');

function git(cwd, ...args) {
  const r = spawnSync('git', args, { cwd, encoding: 'utf8' });
  if (r.status !== 0) throw new Error(`git ${args.join(' ')} failed: ${r.stderr}`);
  return r.stdout.trim();
}
const write = (root, rel, text) => {
  fs.mkdirSync(path.dirname(path.join(root, rel)), { recursive: true });
  fs.writeFileSync(path.join(root, rel), text);
};

/** Main repo with a wp/X worktree holding one committed change and fresh passing evidence. */
function fixture() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wp35-merge-'));
  git(root, 'init', '-b', 'main');
  git(root, 'config', 'user.name', 't');
  git(root, 'config', 'user.email', 't@t');
  write(root, 'docs/wp/X/spec.md', '# X: Title\n');
  write(root, 'docs/wp/X/features.json', JSON.stringify({ wp: 'X', files: ['a.txt'], features: [] }));
  write(root, 'a.txt', 'one\ntwo\nthree\n');
  write(root, '.gitignore', 'out/\n');
  // the merge script runs the gate from <repo>/.claude/scripts, so the fixture carries a copy
  fs.mkdirSync(path.join(root, '.claude', 'scripts', 'lib'), { recursive: true });
  fs.copyFileSync(GATE, path.join(root, '.claude', 'scripts', 'wp-gate.mjs'));
  for (const f of fs.readdirSync(path.join(SCRIPTS, 'lib'))) {
    fs.copyFileSync(path.join(SCRIPTS, 'lib', f), path.join(root, '.claude', 'scripts', 'lib', f));
  }
  git(root, 'add', '-A');
  git(root, 'commit', '-m', 'base');
  const wt = root + '-wt';
  git(root, 'worktree', 'add', '-b', 'wp/X', wt);
  write(wt, 'a.txt', 'one\nTWO-wp\nthree\n');
  git(wt, 'add', '-A');
  git(wt, 'commit', '-m', 'wp change');
  const sha = git(wt, 'rev-parse', 'HEAD');
  write(wt, 'out/evidence.json', JSON.stringify({
    sha, dirty: false, preset: 'p', configured: false,
    build: { ok: true, errors: [] },
    ctest: { ran: true, passed: [], failed: [], notRun: [] },
    cases: { passed: [] },
  }));
  return { root, wt, sha };
}

const merge = (f, args) => spawnSync(process.execPath, [MERGE, 'X', ...args], { cwd: f.root, encoding: 'utf8' });
const GREEN = ['--build-cmd', 'node -e process.exit(0)'];
const RED = ['--build-cmd', 'node -e process.exit(1)'];
const branches = (root) => git(root, 'branch', '--list', 'wp/X');
const head = (root) => git(root, 'rev-parse', 'HEAD');

test('Refusals', () => {
  const unchanged = (f, before) => {
    assert.equal(head(f.root), before);
    assert.ok(branches(f.root).includes('wp/X'));
    assert.ok(fs.existsSync(f.wt));
    assert.equal(git(f.root, 'status', '--porcelain', '--untracked-files=no'), '');
  };

  // wrong reviewed sha
  let f = fixture();
  let before = head(f.root);
  let r = merge(f, ['--reviewed', 'deadbeef', ...GREEN]);
  assert.equal(r.status, 1);
  unchanged(f, before);

  // not on main
  f = fixture();
  git(f.root, 'checkout', '-b', 'side');
  before = head(f.root);
  r = merge(f, ['--reviewed', f.sha, ...GREEN]);
  assert.equal(r.status, 1);
  assert.equal(head(f.root), before);
  assert.ok(branches(f.root).includes('wp/X'));

  // dirty main
  f = fixture();
  before = head(f.root);
  write(f.root, 'a.txt', 'dirty\n');
  r = merge(f, ['--reviewed', f.sha, ...GREEN]);
  assert.equal(r.status, 1);
  assert.equal(head(f.root), before);
  assert.ok(branches(f.root).includes('wp/X'));

  // no worktree for the branch
  f = fixture();
  git(f.root, 'worktree', 'remove', '--force', f.wt);
  before = head(f.root);
  r = merge(f, ['--reviewed', f.sha, ...GREEN]);
  assert.equal(r.status, 1);
  assert.equal(head(f.root), before);
  assert.ok(branches(f.root).includes('wp/X'));

  // gate fails (evidence sha stale)
  f = fixture();
  write(f.wt, 'out/evidence.json', JSON.stringify({
    sha: '0'.repeat(40), dirty: false, preset: 'p', configured: false,
    build: { ok: true, errors: [] }, ctest: { ran: true, passed: [], failed: [], notRun: [] }, cases: { passed: [] },
  }));
  before = head(f.root);
  r = merge(f, ['--reviewed', f.sha, ...GREEN]);
  assert.equal(r.status, 1);
  unchanged(f, before);
});

test('SquashBuildCommit', () => {
  const f = fixture();
  const before = head(f.root);
  const r = merge(f, ['--reviewed', f.sha, ...GREEN]);
  assert.equal(r.status, 0, r.stdout + r.stderr);
  const after = head(f.root);
  assert.notEqual(after, before);
  assert.equal(git(f.root, 'log', '-1', '--format=%s'), 'X: Title');
  assert.equal(git(f.root, 'rev-parse', 'HEAD~1'), before);
  assert.equal(fs.readFileSync(path.join(f.root, 'a.txt'), 'utf8').replace(/\r/g, ''), 'one\nTWO-wp\nthree\n');
  assert.ok(r.stdout.includes(after));
  assert.equal(branches(f.root), '');
  assert.ok(!fs.existsSync(f.wt));
});

test('RollbackOnRedBuild', () => {
  // red build
  let f = fixture();
  let before = head(f.root);
  let r = merge(f, ['--reviewed', f.sha, ...RED]);
  assert.equal(r.status, 1);
  assert.equal(head(f.root), before);
  assert.equal(git(f.root, 'status', '--porcelain', '--untracked-files=no'), '');
  assert.ok(branches(f.root).includes('wp/X'));
  assert.ok(fs.existsSync(f.wt));
  assert.equal(fs.readFileSync(path.join(f.root, 'a.txt'), 'utf8').replace(/\r/g, ''), 'one\ntwo\nthree\n');

  // squash conflict: main edits the same line
  f = fixture();
  write(f.root, 'a.txt', 'one\nTWO-main\nthree\n');
  git(f.root, 'add', '-A');
  git(f.root, 'commit', '-m', 'main edit');
  before = head(f.root);
  r = merge(f, ['--reviewed', f.sha, ...GREEN]);
  assert.equal(r.status, 1);
  assert.ok(r.stderr.includes('squash failed'), r.stderr);
  assert.equal(head(f.root), before);
  assert.equal(git(f.root, 'status', '--porcelain', '--untracked-files=no'), '');
  assert.ok(branches(f.root).includes('wp/X'));
  assert.ok(fs.existsSync(f.wt));
});
