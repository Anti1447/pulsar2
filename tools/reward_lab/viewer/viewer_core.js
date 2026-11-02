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

  const api = { parseRun, validate, consistency, fmt, playerOf, rewardRows, totalAt, cumulative, series, eventMarks, gateFlips, FIELD, makeView, topViewHeight, sideViewHeight, prims, ROLES, roleStyle, expectationRange };
  if (typeof module !== 'undefined') module.exports = api;
  return api;
})();
