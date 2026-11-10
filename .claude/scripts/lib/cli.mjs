import { spawn } from 'node:child_process';

/** Parse build.mjs arguments. Throws Error('unknown argument: <arg>') on anything unknown. */
export function parseArgs(argv) {
  const opts = { noTest: false };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--preset') opts.preset = argv[++i];
    else if (argv[i] === '--no-test') opts.noTest = true;
    else if (argv[i] === '--tests') opts.tests = argv[++i];
    else if (argv[i] === '--normal-priority') opts.priority = 'normal';
    else throw new Error(`unknown argument: ${argv[i]}`);
  }
  return opts;
}

/** Build a `run(cmd, args, opts)` that spawns in cwd; opts.env is merged over process.env. */
export function makeRun(cwd) {
  return (cmd, args, opts = {}) =>
    new Promise((resolve) => {
      const child = spawn(cmd, args, {
        cwd,
        windowsHide: true,
        env: opts.env ? { ...process.env, ...opts.env } : process.env,
      });
      let output = '';
      child.stdout.on('data', (d) => (output += d));
      child.stderr.on('data', (d) => (output += d));
      child.on('error', (e) => resolve({ code: 127, output: output + String(e) }));
      child.on('close', (code) => resolve({ code: code ?? 1, output }));
    });
}
