#!/usr/bin/env node
// node .claude/scripts/wp-merge.mjs <id> --reviewed <sha> [--build-cmd "<cmd>"]
// Squash-merges wp/<id> into main after the gate passes, rebuilds on main, commits, removes the worktree.
import { spawnSync } from 'node:child_process';
import path from 'node:path';

const argv = process.argv.slice(2);
const id = argv[0];
let reviewed;
let buildCmd = 'node .claude/scripts/build.mjs';
for (let i = 1; i < argv.length; i++) {
  if (argv[i] === '--reviewed') reviewed = argv[++i];
  else if (argv[i] === '--build-cmd') buildCmd = argv[++i];
}

const die = (msg) => {
  console.error(`wp-merge: ${msg}`);
  process.exit(1);
};
if (!id || !reviewed) {
  console.error('usage: wp-merge.mjs <id> --reviewed <sha> [--build-cmd "<cmd>"]');
  process.exit(64);
}

const git = (args, cwd = repo) => spawnSync('git', args, { cwd, encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
const gitOut = (args, cwd) => {
  const r = git(args, cwd);
  if (r.status !== 0) die(`git ${args.join(' ')} failed: ${r.stderr.trim()}`);
  return r.stdout.trim();
};

const top = spawnSync('git', ['rev-parse', '--show-toplevel'], { cwd: process.cwd(), encoding: 'utf8' });
if (top.status !== 0) die('not inside a git repository');
const repo = path.resolve(top.stdout.trim());
const branch = `wp/${id}`;

// --- F11 refusals (nothing is changed before all pass)
if (gitOut(['rev-parse', '--abbrev-ref', 'HEAD']) !== 'main') die('current branch is not main');
if (gitOut(['status', '--porcelain', '--untracked-files=no'])) die('main has uncommitted tracked changes');

let worktree = null;
{
  let cur = null;
  for (const line of gitOut(['worktree', 'list', '--porcelain']).split(/\r?\n/)) {
    if (line.startsWith('worktree ')) cur = line.slice(9);
    else if (line === `branch refs/heads/${branch}`) worktree = cur;
  }
}
if (!worktree) die(`no worktree has branch ${branch}`);

const branchHead = gitOut(['rev-parse', branch]);
if (branchHead !== reviewed && !(reviewed.length >= 7 && branchHead.startsWith(reviewed))) {
  die(`${branch} is at ${branchHead}, not the reviewed ${reviewed}`);
}

const base = gitOut(['merge-base', 'main', branch]);
const env = { ...process.env };
delete env.NODE_TEST_CONTEXT;
const gate = spawnSync(process.execPath, [path.join(repo, '.claude', 'scripts', 'wp-gate.mjs'), id, '--worktree', worktree, '--base', base], {
  cwd: repo,
  env,
  encoding: 'utf8',
  maxBuffer: 256 * 1024 * 1024,
});
if (gate.status !== 0) die(`gate failed:\n${gate.stdout}${gate.stderr}`);

// --- F12 / F13
const pre = gitOut(['rev-parse', 'HEAD']);
const mainMoved = () => git(['rev-parse', 'HEAD']).stdout.trim() !== pre;
const rollback = (why) => {
  if (mainMoved()) die(`${why}; main HEAD moved away from ${pre}, NOT resetting`);
  git(['reset', '--hard', pre]);
  die(`${why}; main reset to ${pre}, worktree and branch kept`);
};

const squash = git(['merge', '--squash', branchHead]);
if (squash.status !== 0) rollback(`squash failed:\n${squash.stdout}${squash.stderr}`);

const build = spawnSync(buildCmd, { cwd: repo, shell: true, stdio: 'inherit' });
if (build.status !== 0) rollback(`build command exited ${build.status}`);

if (mainMoved()) die(`main HEAD moved away from ${pre} during the build, NOT committing or resetting`);
const specText = gitOut(['show', `${branch}:docs/wp/${id}/spec.md`]);
const h1 = specText.split(/\r?\n/).find((l) => l.startsWith('# '));
if (!h1) rollback(`spec.md of ${branch} has no H1`);
const commit = git(['commit', '-m', h1.slice(2).trim()]);
if (commit.status !== 0) rollback(`commit failed: ${commit.stdout}${commit.stderr}`);

const newSha = gitOut(['rev-parse', 'HEAD']);
// cleanup only when the branch is still exactly what was squashed and the worktree holds nothing unmerged
const nowHead = git(['rev-parse', branch]).stdout.trim();
const wtDirty = git(['status', '--porcelain', '--untracked-files=no'], worktree).stdout.trim();
if (nowHead !== branchHead || wtDirty) {
  console.error(`wp-merge: WARNING ${branch} changed after review (head ${nowHead}${wtDirty ? ', uncommitted changes' : ''}); worktree and branch kept`);
} else {
  const rm = git(['worktree', 'remove', '--force', worktree]);
  if (rm.status !== 0) console.error(`wp-merge: worktree remove failed: ${rm.stderr.trim()}`);
  const del = git(['branch', '-D', branch]);
  if (del.status !== 0) console.error(`wp-merge: branch delete failed: ${del.stderr.trim()}`);
}
console.log(newSha);
process.exit(0);
