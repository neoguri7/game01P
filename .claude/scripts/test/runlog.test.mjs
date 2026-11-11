import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseRunLog, aggregate, formatReport, buildRunEnv, REPORT_HEADINGS } from '../lib/runlog.mjs';

const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-9, `${actual} !~ ${expected}`);

test('parse skips broken lines', () => {
  const text = '{"t":"a","n":1}\n{"t":"b", broken\n{"t":"c","n":3}\n';
  const lines = parseRunLog(text);
  assert.equal(lines.length, 2);
  assert.deepEqual(lines.map((l) => l.t), ['a', 'c']);
});

const battleLines = (extra = []) => [
  { t: 'log_session' },
  { t: 'room_start', depth: 1, room: 0, ms: 1000, frame: 0 },
  { t: 'room_end', depth: 1, room: 0, result: 'win', rounds: 4, first_damage_round: 2, elite: false, ms: 5000, frame: 120 },
  { t: 'room_start', depth: 1, room: 1, ms: 6000, frame: 200 },
  ...extra,
  { t: 'room_end', depth: 1, room: 1, result: 'defeat', rounds: 6, first_damage_round: 0, elite: true, ms: 12000, frame: 560 },
];

test('battle metrics', () => {
  const m = aggregate([battleLines()]).battles;
  assert.equal(m.all.count, 2);
  assert.equal(m.all.wins, 1);
  assert.equal(m.all.defeats, 1);
  assert.equal(m.all.meanRounds, 5);
  assert.equal(m.all.meanFirstDamageRound, 2);
  assert.equal(m.all.meanWallSeconds, 5);
  assert.equal(m.all.meanSimSeconds, 4);
  assert.equal(m.elite.count, 1);
  assert.equal(m.elite.meanWallSeconds, 6);
  assert.equal(m.normal.meanSimSeconds, 2);

  const resumed = aggregate([battleLines([{ t: 'log_session' }])]).battles;
  assert.equal(resumed.all.count, 2);
  assert.equal(resumed.all.meanWallSeconds, 4);
  assert.equal(resumed.all.meanSimSeconds, 2);

  assert.equal(aggregate([]).battles.all.meanRounds, null);
});

test('skill shares per member', () => {
  const lines = [
    { t: 'room_end', skills_by_member: { 'party.warrior': { 'skill.a': 14, 'skill.b': 3 }, 'party.mage': { 'skill.x': 10 } } },
    { t: 'room_end', skills_by_member: { 'party.warrior': { 'skill.a': 10, 'skill.b': 2, 'skill.c': 1 } } },
    { t: 'room_end', skills_used: { 'skill.old': 5 } },
  ];
  const s = aggregate([lines]).skills;
  assert.equal(s['party.warrior']['skill.a'].count, 24);
  near(s['party.warrior']['skill.a'].share, 0.8);
  assert.equal(s['party.warrior']['skill.a'].flag, 'high');
  near(s['party.warrior']['skill.b'].share, 5 / 30);
  assert.equal(s['party.warrior']['skill.b'].flag, '');
  near(s['party.warrior']['skill.c'].share, 1 / 30);
  assert.equal(s['party.warrior']['skill.c'].flag, 'low');
  assert.equal(s['party.mage']['skill.x'].share, 1);
  assert.equal(s['party.mage']['skill.x'].flag, '');
  assert.equal(s['?']['skill.old'].count, 5);
  assert.equal(s['?']['skill.old'].flag, '');
});

test('rune pick rates', () => {
  const lines = [
    { t: 'offer', candidates: ['rune.a', 'rune.b'], rarities: ['common', 'rare'], picked: 'rune.a', picked_rarity: 'common', skipped: false },
    { t: 'offer', candidates: ['rune.a', 'rune.c'], rarities: ['common', 'common'], picked: '', picked_rarity: '', skipped: true },
    { t: 'offer', rarities: ['rare'], picked: 'rune.z', picked_rarity: 'rare', skipped: false },
  ];
  const r = aggregate([lines]).runes;
  assert.deepEqual(r.byRune['rune.a'], { offered: 2, picked: 1, rate: 0.5 });
  assert.deepEqual(r.byRune['rune.b'], { offered: 1, picked: 0, rate: 0 });
  assert.equal(r.byRune['rune.z'], undefined);
  assert.deepEqual(r.byRarity.common, { offered: 3, picked: 1 });
  assert.deepEqual(r.byRarity.rare, { offered: 2, picked: 1 });
  assert.equal(r.offers, 3);
  assert.equal(r.skipped, 1);
  near(r.skipRate, 1 / 3);
});

test('fork and event choices', () => {
  const lines = [
    { t: 'fork', kind: 'elite', choice: 0, party_hp: 50, party_hp_max: 100 },
    { t: 'fork', kind: 'elite', choice: 1, party_hp: 300, party_hp_max: 300 },
    { t: 'fork', kind: 'elite', choice: 1, party_hp: 0, party_hp_max: 0 },
    { t: 'event', event: 'event.shrine', choice: 0 },
    { t: 'event', event: 'event.shrine', choice: 1 },
    { t: 'event', event: 'event.shrine', choice: 1 },
  ];
  const m = aggregate([lines]);
  assert.equal(m.forks.elite.count, 3);
  near(m.forks.elite.variantRate, 2 / 3);
  near(m.forks.elite.meanHpFraction, 0.75);
  assert.deepEqual(m.events['event.shrine'], { 0: 1, 1: 2 });

  const two = aggregate([lines.slice(0, 2)]);
  assert.equal(two.forks.elite.variantRate, 0.5);
  assert.equal(two.forks.elite.meanHpFraction, 0.75);
});

test('camp metrics', () => {
  const lines = [
    { t: 'log_session' },
    { t: 'camp_enter', depth: 1, party_hp_before_heal: 50, party_hp: 80, party_hp_max: 100, materials: 4, ms: 10000, frame: 0 },
    { t: 'camp', id: 'upgrade.attack', amount: 3 },
    { t: 'camp', id: 'upgrade.attack', amount: 5 },
    { t: 'camp_leave', depth: 1, materials: 1, ms: 40000, frame: 0 },
    { t: 'camp_enter', depth: 2, party_hp_before_heal: 100, party_hp: 100, party_hp_max: 100, materials: 8, ms: 90000, frame: 0 },
    { t: 'log_session' },
    { t: 'camp_leave', depth: 2, materials: 8, ms: 500, frame: 0 },
  ];
  const c = aggregate([lines]).camps;
  assert.equal(c.visits, 2);
  near(c.meanHpFractionBeforeHeal, 0.75);
  near(c.meanHpFraction, 0.9);
  assert.equal(c.meanMaterials, 6);
  assert.deepEqual(c.spend['upgrade.attack'], { count: 2, total: 8 });
  assert.equal(c.meanStaySeconds, 30);
});

test('whiff rates by reason', () => {
  const lines = [
    { t: 'room_end', intents: { committed: 6, invalidated: 1, whiff_reasons: { displaced: 1, blocked: 1 } } },
    { t: 'room_end', intents: { committed: 4, invalidated: 1, whiff_reasons: { displaced: 1 } } },
  ];
  const w = aggregate([lines]).whiffs;
  assert.equal(w.denominator, 8);
  assert.equal(w.byReason.displaced.count, 2);
  assert.equal(w.byReason.displaced.rate, 0.25);
  assert.equal(w.byReason.blocked.rate, 0.125);

  const none = aggregate([[{ t: 'room_end', intents: { committed: 0, invalidated: 0, whiff_reasons: { displaced: 2 } } }]]).whiffs;
  assert.equal(none.byReason.displaced.rate, 0);
});

test('report sections', () => {
  const text = formatReport(aggregate([]));
  let from = 0;
  for (const heading of REPORT_HEADINGS) {
    const at = text.split('\n').indexOf(heading, from);
    assert.ok(at >= 0, `heading ${heading} missing or out of order`);
    from = at + 1;
  }
  assert.deepEqual(REPORT_HEADINGS, ['RUNS', 'BATTLES', 'SKILLS', 'RUNES', 'FORKS', 'EVENTS', 'CAMPS', 'WHIFFS']);
  const filled = formatReport(aggregate([battleLines(), [{ t: 'run_end', result: 'abandon', dungeons_cleared: 1, final_bosses: 0, start_depth: 1 }]]));
  assert.ok(filled.includes('meanRounds'));
});

test('run results and final bosses', () => {
  const runs = [
    [{ t: 'run_end', result: 'defeat', dungeons_cleared: 2, final_bosses: 1, start_depth: 1 }],
    [{ t: 'run_end', result: 'abandon', dungeons_cleared: 0, final_bosses: 0, start_depth: 3 }],
    [{ t: 'run_start' }],
  ];
  const r = aggregate(runs).runs;
  assert.equal(r.count, 3);
  assert.deepEqual(r.results, { defeat: 1, abandon: 1, open: 1 });
  assert.equal(r.meanDungeonsCleared, 1);
  assert.equal(r.finalBosses, 1);
  assert.deepEqual(r.startDepths, { 1: 1, 3: 1 });
});

test('baseline environment', () => {
  const env = buildRunEnv({ label: 'x', seed: 3 });
  assert.deepEqual(env, {
    SDL_VIDEO_DRIVER: 'dummy',
    SDL_AUDIO_DRIVER: 'dummy',
    SDL_RENDER_DRIVER: 'software',
    GAME01P_SAVE_DIR: 'out/baseline/x/seed-3',
    GAME01P_SEED: '3',
    GAME01P_AUTOPLAY: '1',
    GAME01P_AUTOPILOT: 'default',
    GAME01P_FAST: '1',
  });
  assert.equal(buildRunEnv({ label: 'x', seed: 3, maxRooms: 5 }).GAME01P_AUTOPILOT_MAX_ROOMS, '5');
  assert.ok(!('GAME01P_FAST' in buildRunEnv({ label: 'x', seed: 3, fast: false })));
});
