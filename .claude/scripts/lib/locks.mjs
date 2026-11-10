import fs from 'node:fs';
import path from 'node:path';

export class LockTimeout extends Error {
  constructor(file) {
    super(`timed out waiting for lock ${file}`);
    this.name = 'LockTimeout';
    this.file = file;
  }
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/** Returns the lock text when its owner is gone, else null. Empty or unparsable content counts as alive (owner may be mid-write). */
function deadRecord(file) {
  let text;
  try {
    text = fs.readFileSync(file, 'utf8');
  } catch {
    return null; // vanished: the next create attempt decides
  }
  let pid;
  try {
    pid = JSON.parse(text).pid;
  } catch {
    return null;
  }
  if (!Number.isInteger(pid)) return null;
  try {
    process.kill(pid, 0);
    return null;
  } catch (e) {
    return e.code === 'ESRCH' ? text : null;
  }
}

/** Takes an exclusive lock file; resolves to a release() function. */
export async function acquireLock(file, { timeoutMs = 1800000, pollMs = 2000 } = {}) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    try {
      const fd = fs.openSync(file, 'wx');
      try {
        fs.writeSync(fd, JSON.stringify({ pid: process.pid, started: Date.now() }));
      } finally {
        fs.closeSync(fd);
      }
      let released = false;
      return () => {
        if (released) return;
        released = true;
        try {
          fs.unlinkSync(file);
        } catch {
          /* already gone */
        }
      };
    } catch (e) {
      // win32 reports a pending-delete file as EPERM/EACCES: treat like EEXIST and keep polling
      const busy = e.code === 'EEXIST' || (process.platform === 'win32' && (e.code === 'EPERM' || e.code === 'EACCES'));
      if (!busy) throw e;
    }
    const dead = deadRecord(file);
    if (dead !== null) {
      try {
        // only remove the very record we judged dead; a fresh lock written meanwhile stays
        if (fs.readFileSync(file, 'utf8') === dead) {
          fs.unlinkSync(file);
          continue;
        }
      } catch {
        /* someone else took it over first, or the file is pending delete: fall through and poll */
      }
    }
    if (Date.now() >= deadline) throw new LockTimeout(file);
    await sleep(Math.min(pollMs, Math.max(1, deadline - Date.now())));
  }
}
