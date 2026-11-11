// WP-42: run-log report library. Pure functions over parsed JSONL lines (no I/O, no dependencies).
// Spec: docs/wp/WP-42/spec.md section 5.

export const FLAG_HIGH_SHARE = 0.6;
export const FLAG_LOW_SHARE = 0.05;
export const FLAG_MIN_USES = 20;
export const FRAMES_PER_SECOND = 60;

/** Parses JSONL text. Lines that are not JSON objects are skipped; order is kept. */
export function parseRunLog(text) {
  const lines = [];
  for (const raw of String(text).split(/\r?\n/)) {
    const trimmed = raw.trim();
    if (trimmed === '') continue;
    try {
      const value = JSON.parse(trimmed);
      if (value !== null && typeof value === 'object' && !Array.isArray(value)) lines.push(value);
    } catch {
      // a torn or foreign line: skipped
    }
  }
  return lines;
}

const num = (v) => (typeof v === 'number' && Number.isFinite(v) ? v : 0);
const str = (v) => (typeof v === 'string' ? v : '');
const arr = (v) => (Array.isArray(v) ? v : []);
const obj = (v) => (v !== null && typeof v === 'object' && !Array.isArray(v) ? v : {});
const hasNum = (v) => typeof v === 'number' && Number.isFinite(v);

const mean = (values) => (values.length === 0 ? null : values.reduce((a, b) => a + b, 0) / values.length);
const rate = (count, denominator) => (denominator > 0 ? count / denominator : 0);
const bump = (map, key, by = 1) => {
  map[key] = (map[key] ?? 0) + by;
};

function newBattleBucket() {
  return { count: 0, wins: 0, defeats: 0, rounds: [], firstDamage: [], wall: [], sim: [] };
}

function finishBattleBucket(b) {
  return {
    count: b.count,
    wins: b.wins,
    defeats: b.defeats,
    meanRounds: mean(b.rounds),
    meanFirstDamageRound: mean(b.firstDamage),
    meanWallSeconds: mean(b.wall),
    meanSimSeconds: mean(b.sim),
  };
}

/** `runs` = array of line arrays, one per log file. Returns the metrics object of section 5. */
export function aggregate(runs) {
  const results = {};
  const startDepths = {};
  const dungeonsCleared = [];
  let finalBosses = 0;

  const battles = { all: newBattleBucket(), normal: newBattleBucket(), elite: newBattleBucket() };

  const skillCounts = {}; // member -> skill -> count

  const byRune = {};
  const byRarity = {};
  let offers = 0;
  let skipped = 0;

  const forkAcc = {}; // kind -> {count, variants, fractions}
  const events = {};

  let visits = 0;
  const campBefore = [];
  const campAfter = [];
  const campMaterials = [];
  const campSpend = {};
  const campStay = [];

  let committed = 0;
  let invalidated = 0;
  const whiffCounts = {};

  for (const lines of runs) {
    let epoch = 0;
    const starts = new Map(); // "depth/room" -> {ms, frame, epoch}
    const openCamps = []; // unpaired camp_enter, nearest last
    let runEnd = null;

    for (const line of lines) {
      switch (line.t) {
        case 'log_session':
          ++epoch;
          break;
        case 'run_end':
          if (runEnd === null) runEnd = line;
          break;
        case 'room_start':
          starts.set(`${num(line.depth)}/${num(line.room)}`, {
            ms: hasNum(line.ms) ? line.ms : null,
            frame: hasNum(line.frame) ? line.frame : null,
            epoch,
          });
          break;
        case 'room_end': {
          const elite = line.elite === true;
          const buckets = [battles.all, elite ? battles.elite : battles.normal];
          const start = starts.get(`${num(line.depth)}/${num(line.room)}`);
          for (const b of buckets) {
            ++b.count;
            if (line.result === 'win') ++b.wins;
            else if (line.result === 'defeat') ++b.defeats;
            b.rounds.push(num(line.rounds));
            if (num(line.first_damage_round) > 0) b.firstDamage.push(num(line.first_damage_round));
            if (start !== undefined && start.epoch === epoch) {
              if (start.ms !== null && hasNum(line.ms)) b.wall.push((line.ms - start.ms) / 1000);
              if (start.frame !== null && hasNum(line.frame)) {
                b.sim.push((line.frame - start.frame) / FRAMES_PER_SECOND);
              }
            }
          }
          if (Object.hasOwn(line, 'skills_by_member')) {
            for (const [member, skills] of Object.entries(obj(line.skills_by_member))) {
              for (const [skill, count] of Object.entries(obj(skills))) {
                skillCounts[member] ??= {};
                bump(skillCounts[member], skill, num(count));
              }
            }
          } else {
            for (const [skill, count] of Object.entries(obj(line.skills_used))) {
              skillCounts['?'] ??= {};
              bump(skillCounts['?'], skill, num(count));
            }
          }
          const intents = obj(line.intents);
          committed += num(intents.committed);
          invalidated += num(intents.invalidated);
          for (const [reason, count] of Object.entries(obj(intents.whiff_reasons))) {
            bump(whiffCounts, reason, num(count));
          }
          break;
        }
        case 'offer': {
          ++offers;
          if (line.skipped === true) ++skipped;
          const candidates = arr(line.candidates);
          if (Object.hasOwn(line, 'candidates')) {
            for (const id of new Set(candidates.map(str))) {
              byRune[id] ??= { offered: 0, picked: 0 };
              ++byRune[id].offered;
            }
            const picked = str(line.picked);
            if (picked !== '') {
              byRune[picked] ??= { offered: 0, picked: 0 };
              ++byRune[picked].picked;
            }
          }
          for (const rarity of arr(line.rarities).map(str)) {
            byRarity[rarity] ??= { offered: 0, picked: 0 };
            ++byRarity[rarity].offered;
          }
          const pickedRarity = str(line.picked_rarity);
          if (pickedRarity !== '') {
            byRarity[pickedRarity] ??= { offered: 0, picked: 0 };
            ++byRarity[pickedRarity].picked;
          }
          break;
        }
        case 'fork': {
          const kind = str(line.kind);
          forkAcc[kind] ??= { count: 0, variants: 0, fractions: [] };
          const acc = forkAcc[kind];
          ++acc.count;
          if (line.choice === 1) ++acc.variants;
          if (num(line.party_hp_max) > 0) acc.fractions.push(num(line.party_hp) / num(line.party_hp_max));
          break;
        }
        case 'event': {
          const id = str(line.event);
          events[id] ??= {};
          bump(events[id], String(num(line.choice)));
          break;
        }
        case 'camp_enter': {
          ++visits;
          const max = num(line.party_hp_max);
          if (max > 0) {
            campBefore.push(num(line.party_hp_before_heal) / max);
            campAfter.push(num(line.party_hp) / max);
          }
          campMaterials.push(num(line.materials));
          openCamps.push({ ms: hasNum(line.ms) ? line.ms : null, epoch });
          break;
        }
        case 'camp_leave': {
          const enter = openCamps.pop();
          if (enter !== undefined && enter.epoch === epoch && enter.ms !== null && hasNum(line.ms)) {
            campStay.push((line.ms - enter.ms) / 1000);
          }
          break;
        }
        case 'camp': {
          const id = str(line.id);
          campSpend[id] ??= { count: 0, total: 0 };
          ++campSpend[id].count;
          campSpend[id].total += num(line.amount);
          break;
        }
        default:
          break;
      }
    }

    if (runEnd === null) {
      bump(results, 'open');
    } else {
      bump(results, str(runEnd.result));
      dungeonsCleared.push(num(runEnd.dungeons_cleared));
      finalBosses += num(runEnd.final_bosses);
      if (hasNum(runEnd.start_depth)) bump(startDepths, String(runEnd.start_depth));
    }
  }

  const skills = {};
  for (const [member, perSkill] of Object.entries(skillCounts)) {
    const total = Object.values(perSkill).reduce((a, b) => a + b, 0);
    skills[member] = {};
    for (const [skill, count] of Object.entries(perSkill)) {
      const share = rate(count, total);
      let flag = '';
      if (member !== '?' && total >= FLAG_MIN_USES) {
        if (share >= FLAG_HIGH_SHARE) flag = 'high';
        else if (share <= FLAG_LOW_SHARE) flag = 'low';
      }
      skills[member][skill] = { count, share, flag };
    }
  }

  const runeStats = {};
  for (const [id, v] of Object.entries(byRune)) {
    runeStats[id] = { offered: v.offered, picked: v.picked, rate: rate(v.picked, v.offered) };
  }

  const forks = {};
  for (const [kind, acc] of Object.entries(forkAcc)) {
    forks[kind] = {
      count: acc.count,
      variantRate: rate(acc.variants, acc.count),
      meanHpFraction: mean(acc.fractions),
    };
  }

  const denominator = committed - invalidated;
  const byReason = {};
  for (const [reason, count] of Object.entries(whiffCounts)) {
    byReason[reason] = { count, rate: rate(count, denominator) };
  }

  return {
    runs: {
      count: runs.length,
      results,
      meanDungeonsCleared: mean(dungeonsCleared),
      finalBosses,
      startDepths,
    },
    battles: {
      all: finishBattleBucket(battles.all),
      normal: finishBattleBucket(battles.normal),
      elite: finishBattleBucket(battles.elite),
    },
    skills,
    runes: { byRune: runeStats, byRarity, offers, skipped, skipRate: rate(skipped, offers) },
    forks,
    events,
    camps: {
      visits,
      meanHpFractionBeforeHeal: mean(campBefore),
      meanHpFraction: mean(campAfter),
      meanMaterials: mean(campMaterials),
      spend: campSpend,
      meanStaySeconds: mean(campStay),
    },
    whiffs: { denominator, byReason },
  };
}

// --- text report ---------------------------------------------------------------------------------

export const REPORT_HEADINGS = ['RUNS', 'BATTLES', 'SKILLS', 'RUNES', 'FORKS', 'EVENTS', 'CAMPS', 'WHIFFS'];

const fmtValue = (v) => {
  if (v === null || v === undefined) return '-';
  if (typeof v === 'number') return Number.isInteger(v) ? String(v) : v.toFixed(2);
  if (typeof v === 'boolean') return String(v);
  return String(v);
};

/** `label value, label value` over the sorted keys of a flat object. */
const fmtFlat = (o) =>
  Object.keys(o)
    .sort()
    .map((k) => `${k} ${fmtValue(o[k])}`)
    .join(', ');

const isPlain = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);

/** Rows for an object: primitives become `key: value`; flat objects `key: label value, ...`. */
function rowsOf(o, prefix = '') {
  const rows = [];
  for (const key of Object.keys(o)) {
    const value = o[key];
    if (isPlain(value)) {
      const inner = Object.values(value);
      if (inner.every((x) => !isPlain(x))) rows.push([prefix + key, fmtFlat(value)]);
      else {
        for (const [k, v] of rowsOf(value, `${prefix}${key}/`)) rows.push([k, v]);
      }
    } else {
      rows.push([prefix + key, fmtValue(value)]);
    }
  }
  return rows;
}

function sectionRows(heading, m) {
  switch (heading) {
    case 'RUNS':
      return rowsOf(m.runs);
    case 'BATTLES':
      return rowsOf(m.battles);
    case 'SKILLS': {
      const rows = [];
      for (const member of Object.keys(m.skills)) {
        for (const skill of Object.keys(m.skills[member])) {
          const s = m.skills[member][skill];
          const flag = s.flag === '' ? '' : ` ${s.flag}`;
          rows.push([`${member}/${skill}`, `count ${fmtValue(s.count)}, share ${fmtValue(s.share)}${flag}`]);
        }
      }
      return rows;
    }
    case 'RUNES': {
      const rows = [
        ['offers', fmtValue(m.runes.offers)],
        ['skipRate', fmtValue(m.runes.skipRate)],
        ['skipped', fmtValue(m.runes.skipped)],
      ];
      for (const [id, v] of Object.entries(m.runes.byRune)) rows.push([`rune/${id}`, fmtFlat(v)]);
      for (const [id, v] of Object.entries(m.runes.byRarity)) rows.push([`rarity/${id}`, fmtFlat(v)]);
      return rows;
    }
    case 'FORKS':
      return rowsOf(m.forks);
    case 'EVENTS': {
      const rows = [];
      for (const [id, choices] of Object.entries(m.events)) {
        const labelled = {};
        for (const [choice, n] of Object.entries(choices)) labelled[`choice${choice}`] = n;
        rows.push([id, fmtFlat(labelled)]);
      }
      return rows;
    }
    case 'CAMPS':
      return rowsOf(m.camps);
    case 'WHIFFS': {
      const rows = [['denominator', fmtValue(m.whiffs.denominator)]];
      for (const [reason, v] of Object.entries(m.whiffs.byReason)) rows.push([`reason/${reason}`, fmtFlat(v)]);
      return rows;
    }
    default:
      return [];
  }
}

/** Plain-text report: eight headings in order, one row per key (sorted ascending) under each. */
export function formatReport(metrics) {
  const out = [];
  for (const heading of REPORT_HEADINGS) {
    out.push(heading);
    const rows = sectionRows(heading, metrics).sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
    for (const [key, value] of rows) out.push(`  ${key}: ${value}`);
    out.push('');
  }
  return out.join('\n');
}

// --- baseline runner environment -----------------------------------------------------------------

/** The environment of one headless baseline run (F18). Values are strings; the save dir is repo-relative. */
export function buildRunEnv({ label, seed, maxRooms = 0, policy = 'default', fast = true }) {
  const env = {
    SDL_VIDEO_DRIVER: 'dummy',
    SDL_AUDIO_DRIVER: 'dummy',
    SDL_RENDER_DRIVER: 'software',
    GAME01P_SAVE_DIR: `out/baseline/${label}/seed-${seed}`,
    GAME01P_SEED: String(seed),
    GAME01P_AUTOPLAY: '1',
    GAME01P_AUTOPILOT: String(policy),
  };
  if (maxRooms !== 0) env.GAME01P_AUTOPILOT_MAX_ROOMS = String(maxRooms);
  if (fast) env.GAME01P_FAST = '1';
  return env;
}
