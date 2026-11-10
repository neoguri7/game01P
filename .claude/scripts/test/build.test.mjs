import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { filterBuildOutput, parseJUnit } from '../lib/evidence.mjs';
import { runBuild } from '../lib/runner.mjs';
import { acquireLock, LockTimeout } from '../lib/locks.mjs';

const tmp = (p) => fs.mkdtempSync(path.join(os.tmpdir(), p));

const MSVC_LOG = [
  'Build started...',
  "a.cpp(10,5): error C2065: 'x': undeclared identifier [proj.vcxproj]",
  "a.cpp(10,5): error C2065: 'x': undeclared identifier [proj.vcxproj]",
  'b.cpp(3): warning C4100: unused',
  "LINK : fatal error LNK1104: cannot open file 'x.lib' [proj.vcxproj]",
  'foo.cpp:12: error: something bad',
  'all fine here',
].join('\n');

const JUNIT = `<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="(empty)" tests="3" failures="1" disabled="0" skipped="0">
  <testcase name="alpha_tests" classname="alpha_tests" time="1" status="run">
    <system-out>[ ok ] A
[ ok ] B C
</system-out>
  </testcase>
  <testcase name="beta_tests" classname="beta_tests" time="1" status="run">
    <failure message="Failed"/>
    <system-out>line1
line2
</system-out>
  </testcase>
  <testcase name="gamma_tests" classname="gamma_tests" time="0" status="notrun"/>
</testsuite>`;

function makeRepo() {
  const cwd = tmp('wp35-repo-');
  fs.writeFileSync(path.join(cwd, 'vcpkg.json'), '{"name":"x"}');
  fs.mkdirSync(path.join(cwd, 'tests', 'features'), { recursive: true });
  fs.writeFileSync(path.join(cwd, 'tests', 'features', 'a_tests.cpp'), '');
  return cwd;
}
const vcpkgDir = (cwd, lockRoot) => {
  const h = crypto.createHash('sha1').update(fs.readFileSync(path.join(cwd, 'vcpkg.json'), 'utf8')).digest('hex').slice(0, 12);
  return path.join(lockRoot, 'vcpkg_installed', h);
};
function cacheWrite(cwd, preset, dir) {
  const b = path.join(cwd, 'out', 'build', preset);
  fs.mkdirSync(b, { recursive: true });
  fs.writeFileSync(path.join(b, 'CMakeCache.txt'), `FOO:STRING=1\nVCPKG_INSTALLED_DIR:PATH=${dir}\n`);
}
function fakeRun(calls, script = {}) {
  return async (cmd, args) => {
    calls.push([cmd, ...args]);
    if (cmd === 'git' && args[0] === 'rev-parse') return { code: 0, output: 'abc123\n' };
    if (cmd === 'git' && args[0] === 'status') return { code: 0, output: '' };
    if (cmd === 'cmake' && args[0] === '--preset') return script.configure ? script.configure(args) : { code: 0, output: '' };
    if (cmd === 'cmake' && args[0] === '--build') return script.build ? script.build() : { code: 0, output: 'ok' };
    if (cmd === 'ctest') {
      const i = args.indexOf('--output-junit');
      fs.mkdirSync(path.dirname(args[i + 1]), { recursive: true });
      fs.writeFileSync(args[i + 1], script.junit ?? JUNIT);
      return script.ctest ? script.ctest() : { code: 0, output: '' };
    }
    return { code: 0, output: '' };
  };
}

test('FiltersAndEvidence', async () => {
  const lines = filterBuildOutput(MSVC_LOG);
  assert.equal(lines.filter((l) => l.includes('C2065')).length, 1);
  assert.ok(lines.some((l) => l.includes('LNK1104')));
  assert.ok(lines.some((l) => l.includes(': error:')));
  assert.ok(!lines.some((l) => l.includes('C4100')));
  const many = Array.from({ length: 100 }, (_, i) => `f.cpp(${i}): error C1000: bad ${i}`).join('\n');
  assert.equal(filterBuildOutput(many).length, 40);

  const j = parseJUnit(JUNIT);
  assert.deepEqual(j.passed, ['alpha_tests']);
  assert.deepEqual(j.failed, ['beta_tests']);
  assert.deepEqual(j.notRun, ['gamma_tests']);
  assert.deepEqual(j.casesPassed, ['alpha_tests::A', 'alpha_tests::B C']);
  assert.ok(j.outputs.beta_tests.includes('line2'));

  // green run writes evidence
  let cwd = makeRepo();
  let lockRoot = tmp('wp35-lock-');
  cacheWrite(cwd, 'windows-msvc-agent', vcpkgDir(cwd, lockRoot));
  fs.writeFileSync(path.join(cwd, 'out', 'build', 'windows-msvc-agent', 'feature-set.json'), JSON.stringify(['a_tests.cpp']));
  let calls = [];
  const green = JUNIT.replace(/<failure[^>]*\/>/, '').replace('status="notrun"', 'status="run"');
  let r = await runBuild({ cwd, lockRoot, run: fakeRun(calls, { junit: green }) });
  assert.equal(r.exitCode, 0);
  const ev = JSON.parse(fs.readFileSync(path.join(cwd, 'out', 'evidence.json'), 'utf8'));
  assert.equal(ev.sha, 'abc123');
  assert.equal(ev.dirty, false);
  assert.equal(ev.preset, 'windows-msvc-agent');
  assert.equal(ev.configured, false);
  assert.equal(ev.build.ok, true);
  assert.equal(ev.ctest.ran, true);
  assert.deepEqual(ev.ctest.failed, []);
  assert.deepEqual(ev.cases.passed, ['alpha_tests::A', 'alpha_tests::B C']);

  // failing tests -> exit 3, report has name and output tail
  calls = [];
  r = await runBuild({ cwd, lockRoot, run: fakeRun(calls, { ctest: () => ({ code: 8, output: '' }) }) });
  assert.equal(r.exitCode, 3);
  assert.ok(r.report.includes('beta_tests') && r.report.includes('line2'));

  // build failure -> exit 2, evidence build.ok false, errors in report
  calls = [];
  r = await runBuild({ cwd, lockRoot, run: fakeRun(calls, { build: () => ({ code: 1, output: MSVC_LOG }) }) });
  assert.equal(r.exitCode, 2);
  assert.ok(r.report.includes('C2065'));
  const ev2 = JSON.parse(fs.readFileSync(path.join(cwd, 'out', 'evidence.json'), 'utf8'));
  assert.equal(ev2.build.ok, false);
  assert.ok(!calls.some((c) => c[0] === 'ctest'));

  // configure failure -> exit 1, evidence written
  cwd = makeRepo();
  lockRoot = tmp('wp35-lock-');
  r = await runBuild({ cwd, lockRoot, run: fakeRun([], { configure: () => ({ code: 1, output: 'CMake Error' }) }) });
  assert.equal(r.exitCode, 1);
  const ev3 = JSON.parse(fs.readFileSync(path.join(cwd, 'out', 'evidence.json'), 'utf8'));
  assert.equal(ev3.build.ok, false);
  assert.equal(ev3.configured, true);

  // --no-test
  cwd = makeRepo();
  lockRoot = tmp('wp35-lock-');
  calls = [];
  r = await runBuild({ cwd, lockRoot, noTest: true, run: fakeRun(calls) });
  assert.equal(r.exitCode, 0);
  assert.ok(!calls.some((c) => c[0] === 'ctest'));
  const ev4 = JSON.parse(fs.readFileSync(path.join(cwd, 'out', 'evidence.json'), 'utf8'));
  assert.equal(ev4.ctest.ran, false);
  assert.deepEqual(ev4.ctest.passed, []);
});

test('Locks', async () => {
  const dir = tmp('wp35-locks-');
  const f = path.join(dir, 'sub', 'a.lock');
  const rel = await acquireLock(f);
  const t0 = Date.now();
  await assert.rejects(acquireLock(f, { timeoutMs: 300, pollMs: 50 }), LockTimeout);
  assert.ok(Date.now() - t0 >= 250);
  // blocks until release
  const p = acquireLock(f, { timeoutMs: 5000, pollMs: 50 });
  setTimeout(() => rel(), 200);
  const rel2 = await p;
  rel2();
  assert.ok(!fs.existsSync(f));

  // empty / invalid JSON = alive
  fs.writeFileSync(f, '');
  await assert.rejects(acquireLock(f, { timeoutMs: 200, pollMs: 50 }), LockTimeout);
  fs.writeFileSync(f, '{not json');
  await assert.rejects(acquireLock(f, { timeoutMs: 200, pollMs: 50 }), LockTimeout);
  fs.unlinkSync(f);

  // dead pid taken over
  const child = spawn(process.execPath, ['-e', '']);
  const pid = child.pid;
  await new Promise((res) => child.on('exit', res));
  fs.writeFileSync(f, JSON.stringify({ pid, started: 1 }));
  const rel3 = await acquireLock(f, { timeoutMs: 2000, pollMs: 50 });
  assert.equal(JSON.parse(fs.readFileSync(f, 'utf8')).pid, process.pid);
  rel3();
});

test('ReconfigureOnFeatureSetChange', async () => {
  const preset = 'windows-msvc-agent';
  const cwd = makeRepo();
  const lockRoot = tmp('wp35-lock-');
  const dir = vcpkgDir(cwd, lockRoot);
  const fsPath = path.join(cwd, 'out', 'build', preset, 'feature-set.json');
  const cfgCalls = (c) => c.filter((x) => x[0] === 'cmake' && x[1] === '--preset');

  // cache missing -> configure with computed dir, feature-set written
  let calls = [];
  const r = await runBuild({ cwd, lockRoot, noTest: true, run: fakeRun(calls) });
  assert.equal(r.exitCode, 0);
  assert.equal(cfgCalls(calls).length, 1);
  assert.deepEqual(cfgCalls(calls)[0], ['cmake', '--preset', preset, `-DVCPKG_INSTALLED_DIR=${dir}`]);
  assert.deepEqual(JSON.parse(fs.readFileSync(fsPath, 'utf8')), ['a_tests.cpp']);

  // cache present (backslashes, upper case: normalized) + same set -> no configure
  cacheWrite(cwd, preset, dir.replace(/\//g, '\\').toUpperCase());
  calls = [];
  await runBuild({ cwd, lockRoot, noTest: true, run: fakeRun(calls) });
  assert.equal(cfgCalls(calls).length, 0);

  // feature set changed -> configure, feature-set rewritten
  fs.writeFileSync(path.join(cwd, 'tests', 'features', 'b_tests.cpp'), '');
  calls = [];
  await runBuild({ cwd, lockRoot, noTest: true, run: fakeRun(calls) });
  assert.equal(cfgCalls(calls).length, 1);
  assert.deepEqual(JSON.parse(fs.readFileSync(fsPath, 'utf8')), ['a_tests.cpp', 'b_tests.cpp']);

  // cached vcpkg dir differs -> cache deleted first, then configure
  cacheWrite(cwd, preset, path.join(cwd, 'vcpkg_installed'));
  const cachePath = path.join(cwd, 'out', 'build', preset, 'CMakeCache.txt');
  calls = [];
  let existedAtConfigure = null;
  await runBuild({
    cwd,
    lockRoot,
    noTest: true,
    run: fakeRun(calls, {
      configure: () => {
        existedAtConfigure = fs.existsSync(cachePath);
        return { code: 0, output: '' };
      },
    }),
  });
  assert.equal(cfgCalls(calls).length, 1);
  assert.equal(existedAtConfigure, false);
});

test('RetryTransientLock', async () => {
  for (const code of ['MSB3073', 'MSB3075']) {
    const cwd = makeRepo();
    const lockRoot = tmp('wp35-lock-');
    let n = 0;
    const calls = [];
    const r = await runBuild({
      cwd,
      lockRoot,
      noTest: true,
      run: fakeRun(calls, { build: () => (++n === 1 ? { code: 1, output: `error ${code}: copy failed` } : { code: 0, output: 'ok' }) }),
    });
    assert.equal(r.exitCode, 0);
    assert.equal(calls.filter((c) => c[0] === 'cmake' && c[1] === '--build').length, 2);
  }
  // persistent failure: retried once only
  const cwd = makeRepo();
  const lockRoot = tmp('wp35-lock-');
  const calls = [];
  const r = await runBuild({ cwd, lockRoot, noTest: true, run: fakeRun(calls, { build: () => ({ code: 1, output: 'error MSB3073: x' }) }) });
  assert.equal(r.exitCode, 2);
  assert.equal(calls.filter((c) => c[0] === 'cmake' && c[1] === '--build').length, 2);
});

test("LockTimeoutExitsFour", async () => {
  const cwd = makeRepo();
  const lockRoot = tmp("wp35-lock-");
  const release = await acquireLock(path.join(lockRoot, "configure.lock"));
  try {
    const r = await runBuild({ cwd, lockRoot, lockTimeoutMs: 200, run: fakeRun([]) });
    assert.equal(r.exitCode, 4);
    assert.ok(r.report.includes("lock timeout") && r.report.includes("configure.lock"));
    assert.ok(!fs.existsSync(path.join(cwd, "out", "build.lock")));
  } finally {
    release();
  }
  // ctest.lock held while the rest is green
  const cwd2 = makeRepo();
  const lockRoot2 = tmp("wp35-lock-");
  cacheWrite(cwd2, "windows-msvc-agent", vcpkgDir(cwd2, lockRoot2));
  fs.writeFileSync(path.join(cwd2, "out", "build", "windows-msvc-agent", "feature-set.json"), JSON.stringify(["a_tests.cpp"]));
  const rel2 = await acquireLock(path.join(lockRoot2, "ctest.lock"));
  try {
    const r = await runBuild({ cwd: cwd2, lockRoot: lockRoot2, lockTimeoutMs: 200, run: fakeRun([]) });
    assert.equal(r.exitCode, 4);
    assert.ok(!fs.existsSync(path.join(cwd2, "out", "build.lock")));
  } finally {
    rel2();
  }
});
