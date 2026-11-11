#!/usr/bin/env node
// CLI: node .claude/scripts/baseline.mjs --label <name> --seeds <a>-<b> [--max-rooms <n>] [--policy <text>]
//        [--exe <path>] [--timeout-s <n>] [--no-fast]
// Runs the game windowless once per seed (one after another) and prints the run-log report.
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { buildRunEnv } from './lib/runlog.mjs';
import { collectLogs, reportFor } from './runlog-report.mjs';

const USAGE =
  'usage: node .claude/scripts/baseline.mjs --label <name> --seeds <a>-<b>|<a> [--max-rooms <n>] [--policy <text>] [--exe <path>] [--timeout-s <n>] [--no-fast]';
const usageExit = () => {
  console.error(USAGE);
  process.exit(2);
};

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

const opts = {
  label: '',
  seeds: '',
  maxRooms: 9,
  policy: 'default',
  exe: 'out/build/windows-msvc-agent/Debug/game01P.exe',
  timeoutS: 600,
  fast: true,
};
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '--label') opts.label = argv[++i] ?? '';
  else if (a === '--seeds') opts.seeds = argv[++i] ?? '';
  else if (a === '--max-rooms') opts.maxRooms = Number(argv[++i]);
  else if (a === '--policy') opts.policy = argv[++i] ?? 'default';
  else if (a === '--exe') opts.exe = argv[++i] ?? '';
  else if (a === '--timeout-s') opts.timeoutS = Number(argv[++i]);
  else if (a === '--no-fast') opts.fast = false;
  else usageExit();
}

function parseSeeds(text) {
  const m = /^(\d+)(?:-(\d+))?$/.exec(text);
  if (!m) return null;
  const a = Number(m[1]);
  const b = m[2] === undefined ? a : Number(m[2]);
  if (a > b) return null;
  const seeds = [];
  for (let s = a; s <= b; s++) seeds.push(s);
  return seeds;
}

const seeds = parseSeeds(opts.seeds);
if (opts.label === '' || seeds === null || !Number.isFinite(opts.maxRooms) || !Number.isFinite(opts.timeoutS)) usageExit();

if (!/^[A-Za-z0-9_-]+$/.test(opts.label)) usageExit();
const exe = path.resolve(repoRoot, opts.exe);
const baselineDir = path.join(repoRoot, 'out', 'baseline', opts.label);
fs.rmSync(baselineDir, { recursive: true, force: true });

function runSeed(seed) {
  const base = Object.fromEntries(Object.entries(process.env).filter(([k]) => !k.startsWith('GAME01P_')));
  const env = buildRunEnv({ label: opts.label, seed, maxRooms: opts.maxRooms, policy: opts.policy, fast: opts.fast });
  env.GAME01P_SAVE_DIR = path.resolve(repoRoot, env.GAME01P_SAVE_DIR);
  fs.mkdirSync(env.GAME01P_SAVE_DIR, { recursive: true });
  return new Promise((resolve) => {
    const started = Date.now();
    const child = spawn(exe, [], { cwd: path.dirname(exe), env: { ...base, ...env }, windowsHide: true, stdio: 'ignore' });
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill();
    }, opts.timeoutS * 1000);
    let finished = false;
    const done = (code) => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      resolve({ code: timedOut ? 'timeout' : code, seconds: (Date.now() - started) / 1000 });
    };
    child.on('error', () => done(127));
    child.on('close', (code) => done(code ?? 1));
  });
}

let allOk = true;
for (const seed of seeds) {
  const { code, seconds } = await runSeed(seed);
  if (code !== 0) allOk = false;
  console.log(`seed ${seed}: exit ${code} ${seconds.toFixed(1)}s`);
}

const files = collectLogs([baselineDir]);
if (files.length === 0) console.error('no run logs found');
else console.log(reportFor(files));
process.exit(allOk ? 0 : 1);
