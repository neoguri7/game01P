#!/usr/bin/env node
// CLI: node .claude/scripts/build.mjs [--preset <p>] [--no-test] [--tests <regex>] [--normal-priority]
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runBuild } from './lib/runner.mjs';
import { parseArgs, makeRun } from './lib/cli.mjs';

const cwd = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
let opts;
try {
  opts = parseArgs(process.argv.slice(2));
} catch (e) {
  console.error(e.message);
  process.exit(64);
}

const { exitCode, report } = await runBuild({ ...opts, cwd, run: makeRun(cwd) });
if (report) console.log(report);
console.log(exitCode === 0 ? 'BUILD OK' : `BUILD FAILED (exit ${exitCode})`);
process.exit(exitCode);
