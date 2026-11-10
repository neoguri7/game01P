#!/usr/bin/env node
// CLI: node .claude/scripts/build.mjs [--preset <p>] [--no-test] [--tests <regex>]
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runBuild } from './lib/runner.mjs';

const cwd = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const argv = process.argv.slice(2);
const opts = { cwd, noTest: false };
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--preset') opts.preset = argv[++i];
  else if (argv[i] === '--no-test') opts.noTest = true;
  else if (argv[i] === '--tests') opts.tests = argv[++i];
  else {
    console.error(`unknown argument: ${argv[i]}`);
    process.exit(64);
  }
}

const run = (cmd, args) =>
  new Promise((resolve) => {
    const child = spawn(cmd, args, { cwd, windowsHide: true });
    let output = '';
    child.stdout.on('data', (d) => (output += d));
    child.stderr.on('data', (d) => (output += d));
    child.on('error', (e) => resolve({ code: 127, output: output + String(e) }));
    child.on('close', (code) => resolve({ code: code ?? 1, output }));
  });

const { exitCode, report } = await runBuild({ ...opts, run });
if (report) console.log(report);
console.log(exitCode === 0 ? 'BUILD OK' : `BUILD FAILED (exit ${exitCode})`);
process.exit(exitCode);
