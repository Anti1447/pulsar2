// Reward Lab viewer - pure logic (no DOM). Tested with node: node tools/reward_lab/viewer/test_core.js
// Reads the run JSON that LabExport.h writes (schema 1, kind "lab_run"; see docs/REWARD_VIEWER_SPEC.md).
// Every number the page shows comes from here, from the trace the reward itself reported; nothing is recomputed from the state.
const LabCore = (function () {
  'use strict';

  // ---- loading and checking -------------------------------------------------------------------------------------
  function parseRun(text) {
    const run = JSON.parse(text);
    const errs = validate(run);
    if (errs.length) throw new Error('invalid lab run: ' + errs.join('; '));
    return run;
  }

  function validate(run) {
    const e = [];
    if (!run || typeof run !== 'object') return ['not an object'];
    if (run.schema !== 1) e.push('unsupported schema ' + run.schema + ' (this viewer reads 1)');
    if (run.kind !== 'lab_run') e.push('kind is not lab_run');
    if (!Array.isArray(run.frames) || run.frames.length < 1) { e.push('no frames'); return e; }
    const nCars = run.frames[0].players ? run.frames[0].players.length : 0;
    run.frames.forEach((f, k) => {
      if (f.i !== k) e.push('frame ' + k + ' has index ' + f.i);
      if (!f.players || f.players.length !== nCars) e.push('frame ' + k + ' has ' + (f.players ? f.players.length : 0) + ' players, expected ' + nCars);
      if (k === 0 && f.trace !== null) e.push('frame 0 must have no trace');
      if (k > 0) {
        if (!f.trace || f.trace.schema !== 1) { e.push('frame ' + k + ' has no schema-1 trace'); return; }
        if (f.trace.cars.length !== nCars) e.push('frame ' + k + ' trace has ' + f.trace.cars.length + ' cars');
        f.trace.rewards.forEach(r => {
          ['raw', 'wrapped', 'weighted'].forEach(a => { if (r[a].length !== nCars) e.push('frame ' + k + ' ' + r.name + '.' + a + ' has ' + r[a].length + ' values'); });
        });
      }
    });
    return e;
  }

  // Arithmetic the page can check on its own data: weighted = wrapped * weight, total = sum of weighted (to float noise).
  // Returns a list of violations (empty when the trace is internally consistent).
  function consistency(run, tol) {
    tol = tol === undefined ? 1e-4 : tol;
    const bad = [];
    const close = (a, b) => Math.abs(a - b) <= tol * (1 + Math.max(Math.abs(a), Math.abs(b)));
    run.frames.forEach((f, k) => {
      if (!f.trace) return;
      const sum = f.trace.cars.map(() => 0);
      f.trace.rewards.forEach(r => {
        r.weighted.forEach((w, c) => {
          if (!close(w, r.wrapped[c] * r.weight)) bad.push('frame ' + k + ' ' + r.name + ' car ' + c + ': weighted ' + w + ' != wrapped*weight');
          sum[c] += w;
        });
        if (!r.zero_sum) r.raw.forEach((v, c) => { if (v !== r.wrapped[c]) bad.push('frame ' + k + ' ' + r.name + ' car ' + c + ': not ZeroSum but raw != wrapped'); });
      });
      f.trace.total.forEach((t, c) => { if (!close(t, sum[c])) bad.push('frame ' + k + ' car ' + c + ': total ' + t + ' != sum of weighted ' + sum[c]); });
    });
    return bad;
  }

  // ---- numbers ---------------------------------------------------------------------------------------------------
  // Display form: 6 significant digits, no trailing noise. The exact JSON value is kept in a data-* attribute next to it.
  function fmt(v) {
    if (v === null || v === undefined || Number.isNaN(v)) return 'n/a';
    if (v === 0) return '0';
    const s = Number(v).toPrecision(6);
    return s.indexOf('e') >= 0 ? s : String(parseFloat(s));
  }

  // ---- per-frame views -------------------------------------------------------------------------------------------
  function playerOf(run, frame, carIdx) { return run.frames[frame].players[carIdx]; }

  // One row per reward for the selected car at one frame. Frame 0 has no trace, so no rows.
  function rewardRows(run, frame, carIdx) {
    const f = run.frames[frame];
    if (!f.trace) return [];
    const carId = f.trace.cars[carIdx];
    return f.trace.rewards.map(r => {
      const pt = (r.players || []).find(p => p.car === carId) || { components: [], gates: [], events: [], prims: [], prims_dropped: 0 };
      return { name: r.name, wandbKey: r.wandb_key, weight: r.weight, zeroSum: r.zero_sum,
        raw: r.raw[carIdx], wrapped: r.wrapped[carIdx], weighted: r.weighted[carIdx],
        components: pt.components, gates: pt.gates, events: pt.events, primsDropped: pt.prims_dropped, prims: pt.prims };
    });
  }

  function totalAt(run, frame, carIdx) { const f = run.frames[frame]; return f.trace ? f.trace.total[carIdx] : null; }

  // Cumulative sums up to and including `frame`, per reward, for one car. Index = reward index in the stack.
  function cumulative(run, frame, carIdx) {
    const first = run.frames.find(f => f.trace);
    if (!first) return { rewards: [], total: 0 };
    const rewards = first.trace.rewards.map(r => ({ name: r.name, raw: 0, wrapped: 0, weighted: 0 }));
    let total = 0;
    for (let k = 1; k <= frame && k < run.frames.length; k++) {
      const t = run.frames[k].trace;
      t.rewards.forEach((r, i) => { rewards[i].raw += r.raw[carIdx]; rewards[i].wrapped += r.wrapped[carIdx]; rewards[i].weighted += r.weighted[carIdx]; });
      total += t.total[carIdx];
    }
    return { rewards, total };
  }

  // Series for the timeline: values[frame] for frames 1..N (frame 0 is null). `which` = raw | wrapped | weighted.
  function series(run, carIdx, which) {
    const first = run.frames.find(f => f.trace);
    if (!first) return [];
    return first.trace.rewards.map((r0, i) => ({
      name: r0.name,
      values: run.frames.map(f => (f.trace ? f.trace.rewards[i][which][carIdx] : null)),
    }));
  }

  // Every event the selected car's rewards emitted: {frame, reward (index), name, event}.
  function eventMarks(run, carIdx) {
    const out = [];
    run.frames.forEach((f, k) => {
      if (!f.trace) return;
      const carId = f.trace.cars[carIdx];
      f.trace.rewards.forEach((r, i) => {
        const pt = (r.players || []).find(p => p.car === carId);
        if (pt) pt.events.forEach(ev => out.push({ frame: k, reward: i, name: r.name, event: ev }));
      });
    });
    return out;
  }

  // Frames where a gate of the selected car's reward changed its pass/fail: {frame, reward, gate, passed}.
  function gateFlips(run, carIdx) {
    const out = [], last = {};
    run.frames.forEach((f, k) => {
      if (!f.trace) return;
      const carId = f.trace.cars[carIdx];
      f.trace.rewards.forEach((r, i) => {
        const pt = (r.players || []).find(p => p.car === carId);
        if (!pt) return;
        pt.gates.forEach(g => {
          const key = i + '/' + g.name;
          if (last[key] !== undefined && last[key] !== g.passed) out.push({ frame: k, reward: i, name: r.name, gate: g.name, passed: g.passed });
          last[key] = g.passed;
        });
      });
    });
    return out;
  }

  // ---- geometry --------------------------------------------------------------------------------------------------
  const FIELD = { halfX: 4096, halfY: 5120, height: 2044, goalHalfX: 892.755, goalDepth: 880, goalHeight: 642.775 };

  // Two aligned views share the horizontal axis (world y, blue goal on the left, orange on the right).
  //   top : screen x = y, screen y = -x   (looking down: +y right, +x up)
  //   side: screen x = y, screen y = -z
  function makeView(widthPx, heightPx, kind, pad) {
    pad = pad === undefined ? 14 : pad;
    const spanH = FIELD.halfY * 2 + 2 * FIELD.goalDepth;
    const s = (widthPx - 2 * pad) / spanH;
    const cx = widthPx / 2;
    if (kind === 'top') {
      const cy = heightPx / 2;
      return { kind, s, w: widthPx, h: heightPx, map: (p) => [cx + p[1] * s, cy - p[0] * s] };
    }
    const base = heightPx - pad;
    return { kind, s, w: widthPx, h: heightPx, map: (p) => [cx + p[1] * s, base - p[2] * s] };
  }

  // The size a view needs for the whole field at scale s=1px/(uu/k): height of the top view for a given width.
  function topViewHeight(widthPx, pad) { pad = pad === undefined ? 14 : pad; const s = (widthPx - 2 * pad) / (FIELD.halfY * 2 + 2 * FIELD.goalDepth); return Math.round(FIELD.halfX * 2 * s + 2 * pad); }
  function sideViewHeight(widthPx, pad) { pad = pad === undefined ? 14 : pad; const s = (widthPx - 2 * pad) / (FIELD.halfY * 2 + 2 * FIELD.goalDepth); return Math.round(FIELD.height * s + 2 * pad); }

  // Primitives of one frame for the selected car (or all cars), tagged with the reward that drew them.
  function prims(run, frame, carIdx, allCars) {
    const f = run.frames[frame];
    if (!f.trace) return [];
    const out = [];
    f.trace.rewards.forEach((r, i) => {
      (r.players || []).forEach(pt => {
        const idx = f.trace.cars.indexOf(pt.car);
        if (!allCars && idx !== carIdx) return;
        pt.prims.forEach(q => out.push(Object.assign({ reward: i, rewardName: r.name, car: idx }, q)));
      });
    });
    return out;
  }

  // Role -> how it is drawn. Colour AND dash AND a text label, so no role depends on colour alone.
  const ROLES = {
    info:      { color: '#9aa4b2', dash: [],        label: 'info (solid grey)' },
    target:    { color: '#34d399', dash: [],        label: 'target (solid green)' },
    corridor:  { color: '#22d3ee', dash: [8, 4],    label: 'corridor (dashed cyan)' },
    threshold: { color: '#facc15', dash: [2, 3, 10, 3], label: 'threshold (dash-dot yellow)' },
    ok:        { color: '#4ade80', dash: [],        label: 'ok (thin green)' },
    blocked:   { color: '#f87171', dash: [5, 5],    label: 'blocked (dashed red)' },
    phi:       { color: '#c084fc', dash: [1, 4],    label: 'potential (dotted purple)' },
  };
  function roleStyle(role) { return ROLES[role] || ROLES.info; }

  // The frames an expectation looks at, clamped to the run.
  function expectationRange(exp, run) { return [Math.max(1, exp.from), Math.min(exp.to, run.frames.length - 1)]; }

  // ---- live mode (P3-2): merging a host reply, keyboard controls, the scenario's own script ------------------------------
  // A delta says "keep your first `keep` frames, then append `frames`" (LabHost.h). Returns a NEW run; throws if the result is not a valid run.
  function applyDelta(run, d) {
    if (!d || d.ok !== true || !Array.isArray(d.frames) || !Array.isArray(d.expectations)) throw new Error('not a valid delta');
    if (!Number.isInteger(d.keep) || d.keep < 1 || d.keep > run.frames.length) throw new Error('delta keeps ' + d.keep + ' of ' + run.frames.length + ' frames');
    const next = Object.assign({}, run, { frames: run.frames.slice(0, d.keep).concat(d.frames), expectations: d.expectations, result: d.result });
    const errs = validate(next);
    if (errs.length) throw new Error('delta makes an invalid run: ' + errs.join('; '));
    return next;
  }

  const ACTION_NAMES = ['throttle', 'steer', 'pitch', 'yaw', 'roll', 'jump', 'boost', 'handbrake'];
  // ---- controls: what each input is, and which keys / mouse buttons drive it ----------------------------------------------
  // An input is one direction of one of the 8 action slots. Signs were MEASURED on the engine (a car facing +y in the air, 6 held steps):
  // pitch +1 = nose UP (forward.z +0.85), yaw +1 = turn RIGHT, roll +1 = roll RIGHT, steer +1 = steer right on the ground.
  const ACTIONS = [
    { id: 'throttle_fwd',  label: 'Throttle forward',  slot: 0, value: 1 },
    { id: 'throttle_back', label: 'Brake / reverse',   slot: 0, value: -1 },
    { id: 'steer_left',    label: 'Steer left',        slot: 1, value: -1 },
    { id: 'steer_right',   label: 'Steer right',       slot: 1, value: 1 },
    { id: 'pitch_down',    label: 'Pitch nose down',   slot: 2, value: -1 },
    { id: 'pitch_up',      label: 'Pitch nose up',     slot: 2, value: 1 },
    { id: 'yaw_left',      label: 'Yaw left',          slot: 3, value: -1 },
    { id: 'yaw_right',     label: 'Yaw right',         slot: 3, value: 1 },
    { id: 'roll_left',     label: 'Air roll left',     slot: 4, value: -1 },
    { id: 'roll_right',    label: 'Air roll right',    slot: 4, value: 1 },
    { id: 'jump',          label: 'Jump',              slot: 5, value: 1 },
    { id: 'boost',         label: 'Boost',             slot: 6, value: 1 },
    { id: 'handbrake',     label: 'Powerslide (handbrake)', slot: 7, value: 1 },
    { id: 'step',          label: 'Take one decision step', slot: null, value: 0 },
  ];
  // Codes are KeyboardEvent.code (the physical key, so layouts do not matter) or 'Mouse0' / 'Mouse1' / 'Mouse2' (left / middle / right).
  // In Rocket League the same W/A/S/D drive throttle+pitch and steer+yaw: the car uses throttle/steer on the ground and pitch/yaw in the air,
  // so the default binds each of those keys to both inputs. The default is Rocket League's keyboard-and-mouse layout as reported by Liquipedia's
  // control-settings table (via a search summary; the page itself could not be opened): UNVERIFIED in game, check Settings > Controls.
  const PRESETS = {
    rocket_league: { label: 'Rocket League default (keyboard + mouse)', bindings: {
      throttle_fwd: ['KeyW'], throttle_back: ['KeyS'], steer_left: ['KeyA'], steer_right: ['KeyD'],
      pitch_down: ['KeyW'], pitch_up: ['KeyS'], yaw_left: ['KeyA'], yaw_right: ['KeyD'],
      roll_left: ['KeyQ'], roll_right: ['KeyE'], jump: ['Mouse2'], boost: ['Mouse0'], handbrake: ['ShiftLeft'], step: ['Enter'] } },
    keyboard_only: { label: 'Keyboard only (no mouse)', bindings: {
      throttle_fwd: ['KeyW'], throttle_back: ['KeyS'], steer_left: ['KeyA'], steer_right: ['KeyD'],
      pitch_down: ['KeyW'], pitch_up: ['KeyS'], yaw_left: ['KeyA'], yaw_right: ['KeyD'],
      roll_left: ['KeyQ'], roll_right: ['KeyE'], jump: ['KeyJ'], boost: ['KeyK'], handbrake: ['ShiftLeft'], step: ['Enter'] } },
    lab_separate: { label: 'Lab: separate air keys', bindings: {
      throttle_fwd: ['KeyW'], throttle_back: ['KeyS'], steer_left: ['KeyA'], steer_right: ['KeyD'],
      pitch_down: ['ArrowUp'], pitch_up: ['ArrowDown'], yaw_left: ['KeyQ'], yaw_right: ['KeyE'],
      roll_left: ['KeyZ'], roll_right: ['KeyC'], jump: ['KeyJ'], boost: ['KeyB'], handbrake: ['KeyH'], step: ['Enter'] } },
  };
  const DEFAULT_PRESET = 'rocket_league';
  function copyBindings(b) { const o = {}; ACTIONS.forEach(a => { o[a.id] = (b && Array.isArray(b[a.id]) ? b[a.id] : []).slice(); }); return o; }
  // Problems that make a bindings object unusable (empty list = fine). Unknown actions are errors so a typo is not silently ignored.
  function validateBindings(b) {
    const e = [];
    if (!b || typeof b !== 'object' || Array.isArray(b)) return ['bindings must be an object'];
    const ids = new Set(ACTIONS.map(a => a.id));
    Object.keys(b).forEach(k => {
      if (!ids.has(k)) { e.push('unknown input "' + k + '"'); return; }
      if (!Array.isArray(b[k])) { e.push(k + ': expected a list of keys'); return; }
      b[k].forEach(c => { if (typeof c !== 'string' || !/^(Mouse[0-4]|(?!Mouse)[A-Za-z][A-Za-z0-9]*)$/.test(c)) e.push(k + ': "' + c + '" is not a key code'); });
    });
    return e;
  }
  // The 8-slot action for the codes held right now. Opposite directions of a slot cancel; buttons are 0 or 1.
  function actionFromHeld(bindings, held) {
    const a = [0, 0, 0, 0, 0, 0, 0, 0];
    ACTIONS.forEach(x => {
      if (x.slot === null) return;
      const codes = bindings[x.id] || [];
      if (codes.some(c => held.has(c))) a[x.slot] += x.value;
    });
    return a.map((v, i) => i < 5 ? Math.max(-1, Math.min(1, v)) : (v > 0 ? 1 : 0));
  }
  function inputsForCode(bindings, code) { return ACTIONS.filter(x => (bindings[x.id] || []).indexOf(code) >= 0).map(x => x.id); }
  // The viewer's playback keys; while live a bound code takes priority over them.
  const PLAYBACK_CODES = ['Space', 'ArrowLeft', 'ArrowRight', 'Home', 'End', 'BracketLeft', 'BracketRight'];
  // Things worth telling the person about (not errors): a key that always cancels itself, the step key doing double duty, a playback key taken over.
  function bindingWarnings(bindings) {
    const w = [], byCode = {};
    ACTIONS.forEach(x => (bindings[x.id] || []).forEach(c => { (byCode[c] = byCode[c] || []).push(x); }));
    Object.keys(byCode).forEach(c => {
      const xs = byCode[c], label = codeLabel(c);
      const slots = {};
      xs.forEach(x => { if (x.slot !== null) (slots[x.slot] = slots[x.slot] || []).push(x.value); });
      Object.keys(slots).forEach(s => { if (slots[s].indexOf(1) >= 0 && slots[s].indexOf(-1) >= 0) w.push(label + ' is bound to both directions of ' + ACTIONS.find(a => a.slot === Number(s)).id.replace(/_.*/, '') + ': it always cancels itself'); });
      if (xs.some(x => x.id === 'step') && xs.length > 1) w.push(label + ' is bound to "Take one decision step" and to an input: it steps every time you use it');
      if (PLAYBACK_CODES.indexOf(c) >= 0) w.push(label + ' also moves the playback (play / step / jump); while live, your binding wins');
    });
    return w;
  }
  function codeLabel(code) {
    const m = { Mouse0: 'Left mouse', Mouse1: 'Middle mouse', Mouse2: 'Right mouse', Mouse3: 'Mouse back', Mouse4: 'Mouse forward', ShiftLeft: 'Left Shift', ShiftRight: 'Right Shift',
      ControlLeft: 'Left Ctrl', ControlRight: 'Right Ctrl', AltLeft: 'Left Alt', AltRight: 'Right Alt', Space: 'Space', Enter: 'Enter', ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right', Tab: 'Tab', Backspace: 'Backspace' };
    if (m[code]) return m[code];
    let r = /^Key([A-Z])$/.exec(code); if (r) return r[1];
    r = /^Digit([0-9])$/.exec(code); if (r) return r[1];
    r = /^Numpad(.+)$/.exec(code); if (r) return 'Numpad ' + r[1];
    return code;
  }
  // The action the scenario's script gives a car at decision step `step` (the latest entry with from <= step, else idle) - Scenario.h ScriptedAction.
  function scriptedAction(scenario, car, step) {
    let best = -1, act = null;
    (scenario.actions || []).forEach(e => { if (e.car === car && e.from <= step && e.from >= best) { best = e.from; act = e.action; } });
    return ACTION_NAMES.map(n => (act && act[n] !== undefined) ? act[n] : 0);
  }

  // ---- stream mode (P4-3): follow a render run's reward trace, read-only ---------------------------------------------------
  // A stream run is a lab_run with a `stream` object: {episode, seq_first, seq_last, orig_first_i, orig_last_i, next_idx, ...}. Frames are numbered 0..n for
  // the viewer; the sender's own frame number is kept as `orig_i`. appendStreamMessages adds the next frames in order and STOPS at the first thing it cannot
  // append honestly: a gap in `seq`, a frame the sender refused, a restart of the sender, or a new episode. It reports which, so the page asks the server for a
  // fresh run instead of papering over it. Returns a NEW run (the old one is untouched); throws if the result is not a valid run.
  function appendStreamMessages(run, msgs) {
    const s = run.stream;
    if (!s) throw new Error('not a stream run');
    let frames = null, seqLast = s.seq_last, origLast = s.orig_last_i, added = 0;
    const events = [];
    for (const m of msgs) {
      if (!m || typeof m !== 'object') continue;
      if (m.kind === 'lab_stream_hello') { if (m.episode !== s.episode) { events.push(m.episode > s.episode ? 'new_episode' : 'other_episode'); if (m.episode > s.episode) break; } continue; }
      if (m.kind !== 'lab_stream_frame' && m.kind !== 'lab_stream_error') continue;
      if (m.episode !== s.episode) { events.push(m.episode > s.episode ? 'new_episode' : 'other_episode'); if (m.episode > s.episode) break; continue; }
      if (m.kind === 'lab_stream_error') { events.push('refused_frame'); break; }
      if (m.seq <= seqLast) { events.push('restart'); break; }
      if (m.seq !== seqLast + 1) { events.push('gap'); break; }
      frames = frames || run.frames.slice();
      frames.push(Object.assign({}, m.frame, { i: frames.length, orig_i: m.frame.i }));
      seqLast = m.seq; origLast = m.frame.i; added++;
    }
    const next = Object.assign({}, run, { frames: frames || run.frames, stream: Object.assign({}, s, { seq_last: seqLast, orig_last_i: origLast }) });
    const errs = validate(next);
    if (errs.length) throw new Error('stream makes an invalid run: ' + errs.join('; '));
    return { run: next, added, events, refetch: events.some(e => e !== 'other_episode') };
  }

  const api = { parseRun, validate, consistency, fmt, playerOf, rewardRows, totalAt, cumulative, series, eventMarks, gateFlips, FIELD, makeView, topViewHeight, sideViewHeight, prims, ROLES, roleStyle, expectationRange, appendStreamMessages, applyDelta, ACTION_NAMES, ACTIONS, PRESETS, DEFAULT_PRESET, copyBindings, validateBindings, actionFromHeld, inputsForCode, PLAYBACK_CODES, bindingWarnings, codeLabel, scriptedAction };
  if (typeof module !== 'undefined') module.exports = api;
  return api;
})();
