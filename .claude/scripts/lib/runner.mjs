import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { acquireLock, LockTimeout } from './locks.mjs';
import { filterBuildOutput, parseJUnit, featureSet } from './evidence.mjs';

const normPath = (s) => s.replace(/\\/g, '/').toLowerCase();
const readOr = (file, fallback = '') => {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch {
    return fallback;
  }
};
const tail = (text, n) => String(text).split(/\r?\n/).filter((l) => l.length).slice(-n);

/** <lockRoot>/vcpkg_installed/<12 hex of sha1 over the vcpkg manifest files>. */
export function computedVcpkgDir(cwd, lockRoot) {
  const body = readOr(path.join(cwd, 'vcpkg.json')) + readOr(path.join(cwd, 'vcpkg-configuration.json'));
  const h = crypto.createHash('sha1').update(body).digest('hex').slice(0, 12);
  return path.join(lockRoot, 'vcpkg_installed', h);
}

export async function runBuild({
  cwd,
  preset = 'windows-msvc-agent',
  noTest = false,
  tests,
  run,
  lockRoot = path.join(process.env.LOCALAPPDATA ?? path.join(cwd, 'out'), 'game01P'),
  lockTimeoutMs = 1800000,
  priority = 'below-normal',
  setPriority = os.setPriority,
  cpuCount = os.availableParallelism(),
}) {
  if (priority !== 'normal') {
    try {
      setPriority(0, os.constants.priority.PRIORITY_BELOW_NORMAL);
    } catch {}
  }
  const toolOpts = {
    env: { MSBUILDDISABLENODEREUSE: '1', VCPKG_MAX_CONCURRENCY: String(Math.max(1, Math.floor(cpuCount / 4))) },
  };
  const evidence = {
    sha: '',
    dirty: false,
    preset,
    configured: false,
    build: { ok: false, errors: [] },
    ctest: { ran: false, passed: [], failed: [], notRun: [] },
    cases: { passed: [] },
  };
  const reportParts = [];
  let exitCode = 0;
  const lockOpts = { timeoutMs: lockTimeoutMs };

  const buildDir = path.join(cwd, 'out', 'build', preset);
  const cachePath = path.join(buildDir, 'CMakeCache.txt');
  const featurePath = path.join(buildDir, 'feature-set.json');
  const vcpkgDir = computedVcpkgDir(cwd, lockRoot);

  let releaseBuild;
  try {
    releaseBuild = await acquireLock(path.join(cwd, 'out', 'build.lock'), lockOpts);
  } catch (e) {
    if (e instanceof LockTimeout) return { exitCode: 4, evidence, report: `lock timeout: ${e.message}` };
    throw e;
  }

  try {
    // --- configure decision (F6)
    const features = featureSet(cwd);
    let needConfigure = false;
    if (!fs.existsSync(cachePath)) {
      needConfigure = true;
    } else {
      let cached = [];
      try {
        cached = JSON.parse(readOr(featurePath, 'null'));
      } catch {
        cached = null;
      }
      if (!Array.isArray(cached) || JSON.stringify(cached) !== JSON.stringify(features)) needConfigure = true;
      const m = /^VCPKG_INSTALLED_DIR:[A-Z]+=(.*)$/m.exec(readOr(cachePath));
      if (!m || normPath(m[1].trim()) !== normPath(vcpkgDir)) {
        fs.rmSync(cachePath, { force: true });
        needConfigure = true;
      }
    }

    if (needConfigure) {
      evidence.configured = true;
      const releaseConfigure = await acquireLock(path.join(lockRoot, 'configure.lock'), lockOpts);
      let res;
      try {
        res = await run('cmake', ['--preset', preset, `-DVCPKG_INSTALLED_DIR=${vcpkgDir}`], toolOpts);
      } finally {
        releaseConfigure();
      }
      if (res.code !== 0) {
        exitCode = 1;
        const errs = filterBuildOutput(res.output);
        evidence.build.errors = errs;
        reportParts.push('configure failed', ...(errs.length ? errs : tail(res.output, 30)));
      } else {
        fs.mkdirSync(buildDir, { recursive: true });
        fs.writeFileSync(featurePath, JSON.stringify(features));
      }
    }

    // --- build (F7: one retry on transient DLL-copy lock)
    if (exitCode === 0) {
      const buildArgs = ['--build', '--preset', preset, '--parallel', String(Math.max(1, cpuCount - 2))];
      let res = await run('cmake', buildArgs, toolOpts);
      if (res.code !== 0 && /MSB3073|MSB3075/.test(res.output)) res = await run('cmake', buildArgs, toolOpts);
      if (res.code !== 0) {
        exitCode = 2;
        const errs = filterBuildOutput(res.output);
        evidence.build.errors = errs;
        reportParts.push('build failed', ...(errs.length ? errs : tail(res.output, 30)));
      } else {
        evidence.build.ok = true;
      }
    }

    // --- ctest
    if (exitCode === 0 && !noTest) {
      const junitPath = path.resolve(cwd, 'out', 'ctest-junit.xml');
      fs.mkdirSync(path.dirname(junitPath), { recursive: true });
      fs.rmSync(junitPath, { force: true });
      const args = [
        '--preset', preset,
        '--output-junit', junitPath,
        '--test-output-size-passed', '1048576',
        '--test-output-size-failed', '1048576',
      ];
      if (tests) args.push('-R', tests);
      const releaseCtest = await acquireLock(path.join(lockRoot, 'ctest.lock'), lockOpts);
      let res;
      try {
        res = await run('ctest', args, toolOpts);
      } finally {
        releaseCtest();
      }
      const j = parseJUnit(readOr(junitPath));
      evidence.ctest = { ran: true, passed: j.passed, failed: j.failed, notRun: j.notRun };
      evidence.cases.passed = j.casesPassed;
      if (res.code !== 0 || j.failed.length || j.notRun.length) {
        exitCode = 3;
        for (const name of j.failed) reportParts.push(`FAILED ${name}`, ...tail(j.outputs[name] ?? '', 30));
        for (const name of j.notRun) reportParts.push(`NOT RUN ${name}`);
        if (!j.failed.length && !j.notRun.length) reportParts.push('ctest failed', ...tail(res.output, 30));
      }
    }

    // --- evidence
    evidence.sha = (await run('git', ['rev-parse', 'HEAD'])).output.trim();
    evidence.dirty = (await run('git', ['status', '--porcelain', '--untracked-files=no'])).output.trim().length > 0;
    fs.mkdirSync(path.join(cwd, 'out'), { recursive: true });
    fs.writeFileSync(path.join(cwd, 'out', 'evidence.json'), JSON.stringify(evidence, null, 2));
  } catch (e) {
    if (e instanceof LockTimeout) return { exitCode: 4, evidence, report: `lock timeout: ${e.file}` };
    throw e;
  } finally {
    releaseBuild();
  }
  return { exitCode, evidence, report: reportParts.join('\n') };
}
