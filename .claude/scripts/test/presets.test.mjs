import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const read = (rel) => fs.readFileSync(path.join(REPO, rel), 'utf8');
const norm = (s) => s.replace(/\\/g, '/').toLowerCase();
const lockRoot = () => path.join(process.env.LOCALAPPDATA ?? '', 'game01P');
const computedDir = () => {
  const body = ['vcpkg.json', 'vcpkg-configuration.json'].map((f) => (fs.existsSync(path.join(REPO, f)) ? read(f) : '')).join('');
  return path.join(lockRoot(), 'vcpkg_installed', crypto.createHash('sha1').update(body).digest('hex').slice(0, 12));
};

test('AgentPresetCache', () => {
  const cachePath = path.join(REPO, 'out', 'build', 'windows-msvc-agent', 'CMakeCache.txt');
  assert.ok(fs.existsSync(cachePath), 'run build.mjs first: no CMakeCache.txt');
  const cache = fs.readFileSync(cachePath, 'utf8');
  assert.match(cache, /^CMAKE_MSVC_DEBUG_INFORMATION_FORMAT:STRING=Embedded$/m);
  assert.match(cache, /^GAME01P_ENABLE_TRACY:BOOL=OFF$/m);
  const m = /^VCPKG_INSTALLED_DIR:[A-Z]+=(.*)$/m.exec(cache);
  assert.ok(m, 'VCPKG_INSTALLED_DIR missing from cache');
  assert.equal(norm(m[1].trim()), norm(computedDir()));
});

test('DefaultPresetKeepsFormat', () => {
  const cmake = read('CMakeLists.txt');
  assert.match(
    cmake,
    /if\s*\(\s*NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT\s*\)\s*\n\s*set\(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT[^\n]*EditAndContinue[^\n]*\)\s*\n\s*endif\(\)/,
  );
  const presets = JSON.parse(read('CMakePresets.json')).configurePresets;
  const byName = new Map(presets.map((p) => [p.name, p]));
  const chain = (name, seen = new Set()) => {
    if (seen.has(name)) return seen;
    seen.add(name);
    const p = byName.get(name);
    const inh = p?.inherits === undefined ? [] : [].concat(p.inherits);
    inh.forEach((n) => chain(n, seen));
    return seen;
  };
  for (const name of ['windows-msvc-debug', 'windows-msvc-release']) {
    for (const n of chain(name)) {
      const vars = byName.get(n)?.cacheVariables ?? {};
      assert.ok(!('CMAKE_MSVC_DEBUG_INFORMATION_FORMAT' in vars), `${n} defines the format`);
    }
  }
});

test('SmokeEnvUsesSdl3Names', () => {
  const cmake = read('CMakeLists.txt');
  const m = /set\(SMOKE_TESTS([^)]*)\)/.exec(cmake);
  assert.ok(m, 'SMOKE_TESTS not found');
  const names = m[1].trim().split(/\s+/);
  assert.ok(names.length > 0);
  const r = spawnSync('ctest', ['--preset', 'windows-msvc-agent', '--show-only=json-v1'], { cwd: REPO, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  assert.equal(r.status, 0, r.stderr);
  const tests = JSON.parse(r.stdout).tests;
  for (const name of names) {
    const t = tests.find((x) => x.name === name);
    assert.ok(t, `ctest has no test ${name}`);
    const env = (t.properties.find((p) => p.name === 'ENVIRONMENT')?.value ?? []).join('\n');
    for (const want of ['SDL_VIDEO_DRIVER=dummy', 'SDL_AUDIO_DRIVER=dummy', 'SDL_RENDER_DRIVER=software']) {
      assert.ok(env.split('\n').includes(want), `${name} lacks ${want}`);
    }
  }
});
