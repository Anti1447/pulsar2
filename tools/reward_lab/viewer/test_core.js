// node tools/reward_lab/viewer/test_core.js [exported.lab.html ...]
// Tests viewer_core.js on a hand-built run, and (when given exported pages) on the real runs embedded in them.
const fs = require('fs');
const C = require('./viewer_core.js');

let pass = 0, fail = 0;
function check(name, cond, extra) { if (cond) pass++; else { fail++; console.log('FAIL: ' + name + (extra ? '  ' + extra : '')); } }
function throws(name, fn, needle) { try { fn(); check(name, false, 'did not throw'); } catch (e) { check(name, !needle || String(e.message).includes(needle), e.message); } }
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// ---- a small run: 2 cars, 3 traced frames, 2 rewards (one ZeroSum) ---------------------------------------------------
function player(car, team, pos) { return { car, team, pos, vel: [0, 0, 0], forward: [0, 1, 0], up: [0, 0, 1], boost: 33, on_ground: true, flipping: false, jumped: false, double_jumped: false, flipped: false, demoed: false, touched: false }; }
function trace(k, a, b) {
  // reward A (not ZeroSum, weight 2): raw=wrapped=a[car]; reward B (ZeroSum, weight 10): wrapped = raw[c] - raw[other]
  const rawB = b, wrB = [b[0] - b[1], b[1] - b[0]];
  const wA = a.map(v => v * 2), wB = wrB.map(v => v * 10);
  return { schema: 1, step: k - 1, cars: [7, 9], total: [wA[0] + wB[0], wA[1] + wB[1]],
    rewards: [
      { name: 'A', wandb_key: 'kA', weight: 2, zero_sum: false, raw: a.slice(), wrapped: a.slice(), weighted: wA,
        players: [{ car: 7, components: [{ name: 'x', value: 0.5 }], gates: [{ name: 'g', passed: k === 2, reason: k === 2 ? '' : 'nope' }], events: k === 2 ? ['paid'] : [], prims: [{ kind: 'line', role: 'threshold', a: [0, 0, 0], b: [1, 1, 1], radius: 0, text: '' }], prims_dropped: 0 },
                  { car: 9, components: [], gates: [], events: [], prims: [{ kind: 'point', role: 'target', a: [5, 5, 5], b: [0, 0, 0], radius: 0, text: '' }], prims_dropped: 0 }] },
      { name: 'B', wandb_key: 'kB', weight: 10, zero_sum: true, raw: rawB, wrapped: wrB, weighted: wB, players: [] },
    ] };
}
function makeRun() {
  const frames = [{ i: 0, terminal: 0, goal: false, ball: { pos: [0, 0, 93], vel: [0, 0, 0] }, players: [player(7, 0, [0, -100, 17]), player(9, 1, [0, 100, 17])], actions: [], trace: null }];
  [[[1, 0], [0.5, 0.1]], [[0, 2], [0.25, 0.25]], [[3, 0], [0, 1]]].forEach(([a, b], idx) => {
    frames.push({ i: idx + 1, terminal: idx === 2 ? 1 : 0, goal: false, ball: { pos: [0, 0, 93], vel: [0, 0, 0] }, players: [player(7, 0, [0, -100, 17]), player(9, 1, [0, 100, 17])], actions: [[1, 0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0]], trace: trace(idx + 1, a, b) });
  });
  return { schema: 1, kind: 'lab_run', result: 'PASS', scenario: { name: 't' }, frames, expectations: [{ kind: 'reward_sum', intent: 'x', ok: true, detail: 'd', reward: 'A', car: 0, from: 1, to: 99 }] };
}

// ---- validate / parse -----------------------------------------------------------------------------------------------
check('valid run has no errors', C.validate(makeRun()).length === 0);
throws('parseRun rejects another schema', () => C.parseRun(JSON.stringify(Object.assign(makeRun(), { schema: 2 }))), 'unsupported schema');
throws('parseRun rejects another kind', () => C.parseRun(JSON.stringify(Object.assign(makeRun(), { kind: 'x' }))), 'kind is not lab_run');
throws('parseRun rejects bad JSON', () => C.parseRun('{nope'));
{ const r = makeRun(); r.frames[2].i = 5; check('validate: frame index must equal position', C.validate(r).some(e => e.includes('has index 5'))); }
{ const r = makeRun(); r.frames[1].trace = null; check('validate: frames after 0 need a trace', C.validate(r).some(e => e.includes('no schema-1 trace'))); }
{ const r = makeRun(); r.frames[0].trace = trace(1, [0, 0], [0, 0]); check('validate: frame 0 must have no trace', C.validate(r).some(e => e.includes('frame 0 must have no trace'))); }
{ const r = makeRun(); r.frames[1].trace.rewards[0].raw = [1]; check('validate: per-car arrays match the car count', C.validate(r).some(e => e.includes('A.raw has 1 values'))); }
{ const r = makeRun(); r.frames[2].players.pop(); check('validate: player count is constant', C.validate(r).some(e => e.includes('has 1 players'))); }

// ---- consistency (the page checks its own embedded arithmetic) -------------------------------------------------------
check('consistency: clean run', C.consistency(makeRun()).length === 0);
{ const r = makeRun(); r.frames[1].trace.total[0] += 1; check('consistency: wrong total is reported', C.consistency(r).some(e => e.includes('total'))); }
{ const r = makeRun(); r.frames[1].trace.rewards[0].weighted[1] += 1; check('consistency: wrong weighted is reported', C.consistency(r).some(e => e.includes('weighted'))); }
{ const r = makeRun(); r.frames[1].trace.rewards[0].raw[0] = 9; check('consistency: raw != wrapped on a non-ZeroSum reward is reported', C.consistency(r).some(e => e.includes('not ZeroSum'))); }
{ const r = makeRun(); r.frames[1].trace.rewards[0].weighted[0] += 1e-7; check('consistency: float noise is tolerated', C.consistency(r).length === 0); }

// ---- numbers ---------------------------------------------------------------------------------------------------------
check('fmt trims to 6 significant digits', C.fmt(0.1234567891) === '0.123457' && C.fmt(2.8) === '2.8' && C.fmt(-10) === '-10' && C.fmt(0) === '0');
check('fmt handles null and NaN', C.fmt(null) === 'n/a' && C.fmt(NaN) === 'n/a' && C.fmt(undefined) === 'n/a');
check('fmt keeps small numbers readable', C.fmt(0.000123456789) === '0.000123457');

// ---- per-frame views -------------------------------------------------------------------------------------------------
{
  const r = makeRun();
  check('rewardRows: frame 0 has none', C.rewardRows(r, 0, 0).length === 0);
  const rows = C.rewardRows(r, 2, 0);
  check('rewardRows: one row per reward, in stack order', eq(rows.map(x => x.name), ['A', 'B']));
  check('rewardRows: raw/wrapped/weighted are the selected car\'s', rows[0].raw === 0 && rows[1].raw === 0.25 && rows[1].wrapped === 0 && rows[1].weighted === 0);
  check('rewardRows: gates/components/events come from the selected car', rows[0].gates[0].passed === true && rows[0].events[0] === 'paid' && rows[0].components[0].value === 0.5);
  const r1 = C.rewardRows(r, 2, 1);
  check('rewardRows: the other car sees its own raw value', r1[0].raw === 2 && r1[0].gates.length === 0);
  check('rewardRows: a reward with no players block yields empty details', rows[1].gates.length === 0 && rows[1].events.length === 0);
  check('totalAt', C.totalAt(r, 1, 0) === r.frames[1].trace.total[0] && C.totalAt(r, 0, 0) === null);
  check('totalAt is per car', C.totalAt(r, 1, 1) === r.frames[1].trace.total[1] && C.totalAt(r, 1, 0) !== C.totalAt(r, 1, 1));
}
{
  const r = makeRun();
  const c = C.cumulative(r, 3, 0);
  check('cumulative: raw sums over frames 1..F', c.rewards[0].raw === 4 && c.rewards[1].raw === 0.75);
  check('cumulative: weighted and total', Math.abs(c.rewards[0].weighted - 8) < 1e-9 && Math.abs(c.total - (8 + 10 * (0.4 + 0 + (-1)))) < 1e-9, JSON.stringify(c));
  check('cumulative: stops at the requested frame', C.cumulative(r, 1, 0).rewards[0].raw === 1 && C.cumulative(r, 0, 0).total === 0);
  const s = C.series(r, 1, 'raw');
  check('series: one per reward, frame 0 is null', s.length === 2 && s[0].values[0] === null && eq(s[0].values.slice(1), [0, 2, 0]));
  check('series: wrapped differs from raw for a ZeroSum reward', eq(C.series(r, 0, 'wrapped')[1].values.slice(1), [0.4, 0, -1]));
  const ev = C.eventMarks(r, 0);
  check('eventMarks: frame, reward index, event name', eq(ev, [{ frame: 2, reward: 0, name: 'A', event: 'paid' }]));
  check('eventMarks: another car sees none', C.eventMarks(r, 1).length === 0);
  check('gateFlips: reports the frame where a gate changes', eq(C.gateFlips(r, 0), [{ frame: 2, reward: 0, name: 'A', gate: 'g', passed: true }, { frame: 3, reward: 0, name: 'A', gate: 'g', passed: false }]));
}

// ---- geometry --------------------------------------------------------------------------------------------------------
{
  const top = C.makeView(900, C.topViewHeight(900), 'top'), side = C.makeView(900, C.sideViewHeight(900), 'side');
  const blueGoal = top.map([0, -5120, 0]), orangeGoal = top.map([0, 5120, 0]), centre = top.map([0, 0, 0]);
  check('top view: blue goal left of centre, orange right', blueGoal[0] < centre[0] && orangeGoal[0] > centre[0]);
  check('top view: +x is up the screen', top.map([1000, 0, 0])[1] < centre[1] && top.map([-1000, 0, 0])[1] > centre[1]);
  check('top view: field fits inside the canvas with its padding', top.map([-4096, -5120 - 880, 0])[0] >= 13 && top.map([4096, 5120 + 880, 0])[0] <= 900 - 13 && top.map([4096, 0, 0])[1] >= 13 && top.map([-4096, 0, 0])[1] <= top.h - 13);
  check('top view: the same uu span has the same scale both ways', Math.abs((top.map([0, 1000, 0])[0] - top.map([0, 0, 0])[0]) - (top.map([0, 0, 0])[1] - top.map([1000, 0, 0])[1])) < 1e-9);
  check('side view: shares the horizontal axis with the top view', top.map([123, 777, 0])[0] === side.map([123, 777, 0])[0]);
  check('side view: higher z is higher on the screen, floor at the bottom', side.map([0, 0, 500])[1] < side.map([0, 0, 0])[1] && side.map([0, 0, 0])[1] <= side.h - 13);
  check('side view: the ceiling fits', side.map([0, 0, 2044])[1] >= 13);
}
{
  const r = makeRun();
  check('prims: selected car only', eq(C.prims(r, 1, 0, false).map(p => p.kind + '/' + p.reward), ['line/0']));
  check('prims: all cars, tagged with the car index', eq(C.prims(r, 1, 0, true).map(p => p.kind + '/' + p.car), ['line/0', 'point/1']));
  check('prims: frame 0 has none', C.prims(r, 0, 0, true).length === 0);
}
// ---- live mode: merging host replies --------------------------------------------------------------------------------------
{
  const r = makeRun();                                   // frames 0..3
  const extra = JSON.parse(JSON.stringify(r.frames[3])); extra.i = 4;
  const exps = [{ kind: 'x', intent: 'i', ok: false, detail: 'd', reward: 'A', car: 0, from: 1, to: 4 }];
  const appended = C.applyDelta(r, { ok: true, keep: 4, frames: [extra], expectations: exps, result: 'FAIL' });
  check('applyDelta: appends after the kept frames', appended.frames.length === 5 && appended.frames[4].i === 4 && appended.frames[3] === r.frames[3]);
  check('applyDelta: takes the new expectations and result', appended.result === 'FAIL' && eq(appended.expectations, exps));
  check('applyDelta: does not change the old run', r.frames.length === 4 && r.result === 'PASS');
  const branched = C.applyDelta(r, { ok: true, keep: 2, frames: [], expectations: [], result: 'NONE' });
  check('applyDelta: keep < length truncates (a branch)', branched.frames.length === 2 && branched.frames[1] === r.frames[1] && branched.result === 'NONE');
  const rebuilt = C.applyDelta(branched, { ok: true, keep: 2, frames: [JSON.parse(JSON.stringify(r.frames[2]))], expectations: [], result: 'NONE' });
  check('applyDelta: a branch can grow again', rebuilt.frames.length === 3 && rebuilt.frames[2].i === 2);
  throws('applyDelta: refuses a failed reply', () => C.applyDelta(r, { ok: false, error: 'x' }), 'not a valid delta');
  throws('applyDelta: refuses an error reply even if it looks complete', () => C.applyDelta(r, { ok: false, keep: 4, frames: [], expectations: [], result: 'NONE' }), 'not a valid delta');
  throws('applyDelta: refuses keep 0', () => C.applyDelta(r, { ok: true, keep: 0, frames: [], expectations: [], result: 'NONE' }), 'keeps 0');
  throws('applyDelta: refuses keep past the run', () => C.applyDelta(r, { ok: true, keep: 5, frames: [], expectations: [], result: 'NONE' }), 'keeps 5');
  throws('applyDelta: refuses a non-integer keep', () => C.applyDelta(r, { ok: true, keep: 2.5, frames: [], expectations: [], result: 'NONE' }), 'keeps 2.5');
  const gap = JSON.parse(JSON.stringify(extra)); gap.i = 9;
  throws('applyDelta: refuses a frame with the wrong index', () => C.applyDelta(r, { ok: true, keep: 4, frames: [gap], expectations: [], result: 'NONE' }), 'has index 9');
  const noTrace = JSON.parse(JSON.stringify(extra)); noTrace.trace = null;
  throws('applyDelta: refuses a traced frame without a trace', () => C.applyDelta(r, { ok: true, keep: 4, frames: [noTrace], expectations: [], result: 'NONE' }), 'no schema-1 trace');
  throws('applyDelta: refuses missing expectations', () => C.applyDelta(r, { ok: true, keep: 4, frames: [] }), 'not a valid delta');
}
// ---- live mode: keyboard and script ---------------------------------------------------------------------------------------
{
  const RL = C.PRESETS.rocket_league.bindings;
  const h = (b, ...codes) => C.actionFromHeld(b, new Set(codes));
  const IDLE = [0, 0, 0, 0, 0, 0, 0, 0];
  check('controls: nothing held is the idle action', eq(h(RL), IDLE));
  check('controls RL: W is throttle forward AND pitch nose down (ground and air share the key)', eq(h(RL, 'KeyW'), [1, 0, -1, 0, 0, 0, 0, 0]));
  check('controls RL: S is reverse AND pitch nose up', eq(h(RL, 'KeyS'), [-1, 0, 1, 0, 0, 0, 0, 0]));
  check('controls RL: D is steer right AND yaw right, A the opposite', eq(h(RL, 'KeyD'), [0, 1, 0, 1, 0, 0, 0, 0]) && eq(h(RL, 'KeyA'), [0, -1, 0, -1, 0, 0, 0, 0]));
  check('controls RL: opposite keys cancel', eq(h(RL, 'KeyW', 'KeyS'), IDLE) && eq(h(RL, 'KeyA', 'KeyD'), IDLE));
  check('controls RL: Q / E are air roll left / right', h(RL, 'KeyQ')[4] === -1 && h(RL, 'KeyE')[4] === 1);
  check('controls RL: left mouse boosts, right mouse jumps, left shift powerslides', h(RL, 'Mouse0')[6] === 1 && h(RL, 'Mouse2')[5] === 1 && h(RL, 'ShiftLeft')[7] === 1);
  check('controls: unbound codes do nothing (Space, arrows, X, the step key)', eq(h(RL, 'Space', 'ArrowLeft', 'KeyX', 'Enter', 'Mouse1'), IDLE));
  check('controls: buttons are 0 or 1 even if two keys drive one', eq(h({ jump: ['KeyJ', 'KeyK'] }, 'KeyJ', 'KeyK').slice(5), [1, 0, 0]));
  check('controls: an action with no keys never fires', eq(h({}, 'KeyW'), IDLE) && eq(h({ throttle_fwd: [] }, 'KeyW'), IDLE));
  check('controls: two keys on one axis direction do not add up past 1', h({ throttle_fwd: ['KeyW', 'KeyT'] }, 'KeyW', 'KeyT')[0] === 1);
  check('controls: the separate-air preset has its own pitch keys', eq(h(C.PRESETS.lab_separate.bindings, 'ArrowDown'), [0, 0, 1, 0, 0, 0, 0, 0]) && eq(h(C.PRESETS.lab_separate.bindings, 'KeyW'), [1, 0, 0, 0, 0, 0, 0, 0]));
  check('controls: 8 slots, every input points at a slot in 0..7 (or is the step key)', C.ACTION_NAMES.length === 8 && C.ACTIONS.every(a => a.slot === null || (a.slot >= 0 && a.slot < 8)) && C.ACTIONS.filter(a => a.slot === null).map(a => a.id).join() === 'step');
  check('controls: every axis slot has both directions, every button slot one', [0, 1, 2, 3, 4].every(s => C.ACTIONS.filter(a => a.slot === s).map(a => a.value).sort().join() === '-1,1') && [5, 6, 7].every(s => C.ACTIONS.filter(a => a.slot === s).length === 1));
  check('presets: every preset is valid and covers every input', Object.values(C.PRESETS).every(p => C.validateBindings(p.bindings).length === 0 && C.ACTIONS.every(a => (p.bindings[a.id] || []).length > 0)) && !!C.PRESETS[C.DEFAULT_PRESET]);
  check('presets: the default is the Rocket League layout', C.DEFAULT_PRESET === 'rocket_league');
  const cp = C.copyBindings(RL); cp.jump.push('Space');
  check('copyBindings: a copy does not alias the preset', RL.jump.length === 1 && cp.jump.length === 2 && Object.keys(C.copyBindings({})).length === C.ACTIONS.length);
  check('validate: accepts keys and mouse buttons', C.validateBindings({ jump: ['Space', 'Mouse2', 'KeyJ', 'ShiftLeft', 'Digit1'] }).length === 0);
  check('validate: refuses an unknown input', C.validateBindings({ jumpp: ['KeyJ'] })[0].includes('unknown input'));
  check('validate: refuses a non-list and a non-string code', C.validateBindings({ jump: 'KeyJ' })[0].includes('list') && C.validateBindings({ jump: [5] })[0].includes('not a key code'));
  check('validate: refuses junk codes', C.validateBindings({ jump: ['Mouse9'] }).length === 1 && C.validateBindings({ jump: [''] }).length === 1 && C.validateBindings({ jump: ['a b'] }).length === 1);
  check('validate: refuses a non-object', C.validateBindings(null).length === 1 && C.validateBindings([]).length === 1);
  check('inputsForCode: W is two inputs in the RL layout', eq(C.inputsForCode(RL, 'KeyW').sort(), ['pitch_down', 'throttle_fwd']) && eq(C.inputsForCode(RL, 'KeyX'), []));
  check('warnings: the Rocket League layout has none', eq(C.bindingWarnings(RL), []));
  check('warnings: a key on both directions of one slot', C.bindingWarnings({ throttle_fwd: ['KeyW'], throttle_back: ['KeyW'] }).some(m => m.includes('cancels itself')));
  check('warnings: the step key doing double duty', C.bindingWarnings({ step: ['KeyJ'], jump: ['KeyJ'] }).some(m => m.includes('steps every time')));
  check('warnings: a playback key is taken over', C.bindingWarnings({ jump: ['Space'] }).some(m => m.includes('playback')) && C.PLAYBACK_CODES.includes('Space'));
  check('warnings: W on throttle and pitch is NOT a conflict (different slots)', eq(C.bindingWarnings({ throttle_fwd: ['KeyW'], pitch_down: ['KeyW'] }), []));
  check('codeLabel: readable names', C.codeLabel('KeyW') === 'W' && C.codeLabel('Mouse0') === 'Left mouse' && C.codeLabel('Mouse2') === 'Right mouse' && C.codeLabel('ShiftLeft') === 'Left Shift' && C.codeLabel('Digit3') === '3' && C.codeLabel('ArrowUp') === 'Up' && C.codeLabel('F5') === 'F5');
  const sc = { actions: [{ car: 0, from: 0, action: { throttle: 1, boost: 1 } }, { car: 0, from: 5, action: { jump: 1 } }, { car: 1, from: 3, action: { steer: -1 } }] };
  check('script: before the first entry a car idles', eq(C.scriptedAction(sc, 1, 2), [0, 0, 0, 0, 0, 0, 0, 0]));
  check('script: an entry holds until the next', eq(C.scriptedAction(sc, 0, 4), [1, 0, 0, 0, 0, 0, 1, 0]));
  check('script: the next entry replaces the whole action', eq(C.scriptedAction(sc, 0, 5), [0, 0, 0, 0, 0, 1, 0, 0]));
  check('script: entries of another car are ignored', eq(C.scriptedAction(sc, 1, 9), [0, -1, 0, 0, 0, 0, 0, 0]));
  check('script: a scenario with no actions idles', eq(C.scriptedAction({}, 0, 3), [0, 0, 0, 0, 0, 0, 0, 0]));
}
check('every role has a colour, a label, and a dash entry', Object.keys(C.ROLES).length === 7 && Object.values(C.ROLES).every(s => /^#[0-9a-f]{6}$/.test(s.color) && s.label && Array.isArray(s.dash)));
check('roles are told apart by more than colour', new Set(Object.values(C.ROLES).map(s => JSON.stringify(s.dash))).size >= 5);
check('unknown role falls back to info', C.roleStyle('mystery') === C.ROLES.info);
check('expectationRange clamps to the run', eq(C.expectationRange({ from: 1, to: 99 }, makeRun()), [1, 3]) && eq(C.expectationRange({ from: 0, to: 2 }, makeRun()), [1, 2]));

// ---- exported pages (real runs) ----------------------------------------------------------------------------------------
for (const file of process.argv.slice(2)) {
  const html = fs.readFileSync(file, 'utf8');
  const m = html.match(/<script type="application\/json" id="labdata">([\s\S]*?)<\/script>/);
  check(file + ': has an embedded run', !!m);
  if (!m) continue;
  check(file + ': the data block contains no literal "<"', !m[1].includes('<'));
  let run; try { run = C.parseRun(m[1]); } catch (e) { check(file + ': parses and validates', false, e.message); continue; }
  check(file + ': parses and validates', true);
  const bad = C.consistency(run);
  check(file + ': internally consistent', bad.length === 0, bad[0]);
  check(file + ': has frames and a verdict', run.frames.length > 1 && ['PASS', 'FAIL', 'NONE'].includes(run.result));
  check(file + ': no unresolved markers', !html.includes('<!--INLINE:') && !html.includes('<!--LAB_RUN_JSON-->') && !html.includes('<!--LAB_LIVE-->'));
}

console.log((fail ? 'FAILED' : 'PASSED') + ': ' + pass + ' checks passed' + (fail ? ', ' + fail + ' failed' : ''));
process.exit(fail ? 1 : 0);
