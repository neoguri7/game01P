#!/usr/bin/env node
// CLI: node .claude/scripts/runlog-report.mjs <file-or-dir>... [--json]
// Prints the playtest metrics of the run logs (*.jsonl) under the given files and directories.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { aggregate, formatReport, parseRunLog } from './lib/runlog.mjs';

/** All `*.jsonl` files under the given paths (directories searched recursively), sorted. */
export function collectLogs(paths) {
  const found = [];
  const walk = (p) => {
    let stat;
    try {
      stat = fs.statSync(p);
    } catch {
      return;
    }
    if (stat.isDirectory()) {
      for (const name of fs.readdirSync(p)) walk(path.join(p, name));
    } else if (stat.isFile() && p.endsWith('.jsonl')) {
      found.push(p);
    }
  };
  for (const p of paths) walk(p);
  return found.sort();
}

/** The report text (or metrics object) for a list of log files. */
export function reportFor(files, { json = false } = {}) {
  const metrics = aggregate(files.map((f) => parseRunLog(fs.readFileSync(f, 'utf8'))));
  return json ? JSON.stringify(metrics, null, 2) : formatReport(metrics);
}

if (process.argv[1] && pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url) {
  const args = process.argv.slice(2);
  const json = args.includes('--json');
  const paths = args.filter((a) => a !== '--json');
  const files = collectLogs(paths);
  if (files.length === 0) {
    console.error('no run logs found');
    process.exit(1);
  }
  console.log(reportFor(files, { json }));
}
