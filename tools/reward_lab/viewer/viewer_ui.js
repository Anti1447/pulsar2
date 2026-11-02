// Reward Lab viewer - DOM and canvas. Reads window.LAB (below) built from the embedded run; logic lives in viewer_core.js.
(function () {
  'use strict';
  const C = LabCore;
  const $ = (id) => document.getElementById(id);
  const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  const SVG = 'http://www.w3.org/2000/svg';

  let run;
  try { run = C.parseRun($('labdata').textContent); }
  catch (e) { document.body.innerHTML = '<pre class="warn" style="padding:16px">Reward Lab could not read this run: ' + String(e.message).replace(/</g, '&lt;') + '</pre>'; throw e; }

  let nFrames = run.frames.length - 1;              // frames 1..N carry a trace; frame 0 is the initial state
  let nCars = run.frames[0].players.length;
  const LIVE = !!window.LAB_LIVE;                   // set by tools/reward_lab/serve.py; a static export never has it
  const st = { frame: 0, car: 0, playing: false, speed: 1, allCars: false, vels: true, which: 'raw', expActive: -1 };

  // ---- header --------------------------------------------------------------------------------------------------
  function renderHeader() {
    const sc = run.scenario;
    $('title').textContent = sc.name;
    $('result').textContent = run.result; $('result').dataset.result = run.result;
    $('desc').textContent = sc.description || '';
    $('runinfo').textContent = nFrames + ' steps, tick_skip ' + sc.tick_skip + ', ' + nCars + ' cars, ' + run.expectations.filter(e => e.ok).length + '/' + run.expectations.length + ' expectations' + (LIVE ? '  [LIVE]' : '');
    document.title = 'Reward Lab - ' + sc.name + ' - ' + run.result;
    $('car').innerHTML = '';
    run.frames[0].players.forEach((p, i) => {
      const o = document.createElement('option'); o.value = i; o.textContent = 'car ' + i + ' (' + (p.team ? 'orange' : 'blue') + ')'; $('car').appendChild(o);
    });
    $('car').value = st.car;
  }
  renderHeader();
  $('legend').innerHTML = Object.keys(C.ROLES).map(k => '<span><i style="border-color:' + C.ROLES[k].color + ';border-top-style:' + (C.ROLES[k].dash.length ? 'dashed' : 'solid') + '"></i>' + C.ROLES[k].label + '</span>').join('');

  // ---- canvases ------------------------------------------------------------------------------------------------
  const dpr = window.devicePixelRatio || 1;
  function sizeCanvas(cv, wCss, hCss) { cv.width = Math.round(wCss * dpr); cv.height = Math.round(hCss * dpr); cv.style.width = '100%'; cv.style.height = 'auto'; cv.style.aspectRatio = wCss + ' / ' + hCss; }
  // Canvases are drawn at their CSS width (re-laid out on resize) so text stays a readable size.
  let W = 880, topView, sideView;
  function layout() {
    W = Math.max(420, Math.floor($('views').clientWidth - 20));
    const nRew = run.frames[1] ? run.frames[1].trace.rewards.length : 1;
    const topH = C.topViewHeight(W), sideH = C.sideViewHeight(W), tlH = 36 * Math.max(1, nRew) + 44;
    sizeCanvas($('top'), W, topH); sizeCanvas($('side'), W, sideH); sizeCanvas($('timeline'), W, tlH);
    topView = C.makeView(W, topH, 'top'); sideView = C.makeView(W, sideH, 'side');
  }
  layout();

  function dashed(ctx, d) { ctx.setLineDash(d.map(x => x)); }
  function line(ctx, a, b) { ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke(); }

  function drawField(ctx, v) {
    const F = C.FIELD, line_ = css('--line'), muted = css('--muted');
    ctx.strokeStyle = line_; ctx.lineWidth = 1.5; ctx.setLineDash([]);
    if (v.kind === 'top') {
      const [x0, y0] = v.map([F.halfX, -F.halfY]), [x1, y1] = v.map([-F.halfX, F.halfY]);
      ctx.strokeRect(x0, y0, x1 - x0, y1 - y0);
      line(ctx, v.map([F.halfX, 0]), v.map([-F.halfX, 0]));
      ctx.beginPath(); const c = v.map([0, 0]); ctx.arc(c[0], c[1], 1000 * v.s, 0, 6.2832); ctx.stroke();
      [[-1, css('--blue')], [1, css('--orange')]].forEach(([s, col]) => {
        ctx.strokeStyle = col; ctx.lineWidth = 3;
        const a = v.map([F.goalHalfX, s * F.halfY]), b = v.map([-F.goalHalfX, s * (F.halfY + F.goalDepth)]);
        ctx.strokeRect(Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.abs(b[0] - a[0]), Math.abs(b[1] - a[1]));
      });
    } else {
      const a = v.map([0, -F.halfY, 0]), b = v.map([0, F.halfY, F.height]);
      ctx.strokeRect(a[0], b[1], b[0] - a[0], a[1] - b[1]);
      [[-1, css('--blue')], [1, css('--orange')]].forEach(([s, col]) => {
        ctx.strokeStyle = col; ctx.lineWidth = 3;
        const p = v.map([0, s * F.halfY, 0]), q = v.map([0, s * F.halfY, F.goalHeight]);
        line(ctx, p, q);
      });
      ctx.fillStyle = muted; ctx.font = '11px system-ui'; ctx.fillText('height', 4, 12);
    }
  }

  function carColor(team) { return team ? css('--orange') : css('--blue'); }

  function drawPrim(ctx, v, q, dim) {
    const style = C.roleStyle(q.role);
    ctx.strokeStyle = style.color; ctx.fillStyle = style.color; ctx.lineWidth = q.role === 'ok' ? 1 : 2; dashed(ctx, style.dash);
    ctx.globalAlpha = dim ? 0.45 : 1;
    const A = v.map(q.a), B = v.map(q.b);
    if (q.kind === 'point') { ctx.setLineDash([]); ctx.beginPath(); ctx.moveTo(A[0], A[1] - 6); ctx.lineTo(A[0] + 6, A[1]); ctx.lineTo(A[0], A[1] + 6); ctx.lineTo(A[0] - 6, A[1]); ctx.closePath(); ctx.fill(); }
    else if (q.kind === 'line') line(ctx, A, B);
    else if (q.kind === 'arrow') {
      line(ctx, A, B); ctx.setLineDash([]);
      const ang = Math.atan2(B[1] - A[1], B[0] - A[0]); ctx.beginPath(); ctx.moveTo(B[0], B[1]);
      ctx.lineTo(B[0] - 9 * Math.cos(ang - 0.4), B[1] - 9 * Math.sin(ang - 0.4)); ctx.lineTo(B[0] - 9 * Math.cos(ang + 0.4), B[1] - 9 * Math.sin(ang + 0.4)); ctx.closePath(); ctx.fill();
    } else if (q.kind === 'ring') {
      if (v.kind === 'top') { ctx.beginPath(); ctx.arc(A[0], A[1], q.radius * v.s, 0, 6.2832); ctx.stroke(); }
      else { line(ctx, [A[0] - q.radius * v.s, A[1]], [A[0] + q.radius * v.s, A[1]]); }   // a horizontal ring is a segment from the side
    } else if (q.kind === 'box') {
      const lo = v.map([q.a[0] - q.b[0], q.a[1] - q.b[1], q.a[2] - q.b[2]]), hi = v.map([q.a[0] + q.b[0], q.a[1] + q.b[1], q.a[2] + q.b[2]]);
      ctx.strokeRect(Math.min(lo[0], hi[0]), Math.min(lo[1], hi[1]), Math.abs(hi[0] - lo[0]), Math.abs(hi[1] - lo[1]));
    } else if (q.kind === 'text') { ctx.setLineDash([]); ctx.font = '12px system-ui'; ctx.fillText(q.text || '', A[0] + 6, A[1] - 6); }
    ctx.globalAlpha = 1; ctx.setLineDash([]);
  }

  function drawView(cv, v) {
    const ctx = cv.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, v.w, v.h);
    drawField(ctx, v);
    const f = run.frames[st.frame];
    // reward geometry first (under the cars)
    C.prims(run, st.frame, st.car, st.allCars).forEach(q => drawPrim(ctx, v, q, st.allCars && q.car !== st.car));
    // ball
    const bp = v.map(f.ball.pos); ctx.fillStyle = css('--text'); ctx.beginPath(); ctx.arc(bp[0], bp[1], Math.max(3, 92.75 * 2.5 * v.s), 0, 6.2832); ctx.fill();
    if (st.vels) { ctx.strokeStyle = css('--text'); ctx.lineWidth = 1; line(ctx, bp, v.map([f.ball.pos[0] + f.ball.vel[0] / 3, f.ball.pos[1] + f.ball.vel[1] / 3, f.ball.pos[2] + f.ball.vel[2] / 3])); }
    // cars
    f.players.forEach((p, i) => {
      const c = v.map(p.pos), col = carColor(p.team), sel = i === st.car;
      ctx.save(); ctx.translate(c[0], c[1]);
      const fwd = v.kind === 'top' ? [p.forward[1], -p.forward[0]] : [p.forward[1], -p.forward[2]];
      const ang = Math.atan2(fwd[1], fwd[0]);
      ctx.rotate(ang); const L = 118 * 2.5 * v.s, Wd = 84 * 2.5 * v.s;
      ctx.fillStyle = col; ctx.globalAlpha = p.demoed ? 0.25 : 0.9; ctx.fillRect(-L / 2, v.kind === 'top' ? -Wd / 2 : -Wd / 3, L, v.kind === 'top' ? Wd : Wd * 2 / 3);
      ctx.globalAlpha = 1; ctx.fillStyle = css('--bg'); ctx.fillRect(L * 0.1, -Wd / 4, L * 0.4, Wd / 2);   // nose marker
      if (sel) { ctx.strokeStyle = css('--text'); ctx.lineWidth = 2; ctx.strokeRect(-L / 2 - 2, -Wd / 2 - 2, L + 4, Wd + 4); }
      ctx.restore();
      ctx.fillStyle = css('--text'); ctx.font = '11px system-ui'; ctx.fillText(String(i), c[0] + 7, c[1] - 7);
      if (st.vels) { ctx.strokeStyle = col; ctx.lineWidth = 1; line(ctx, c, v.map([p.pos[0] + p.vel[0] / 3, p.pos[1] + p.vel[1] / 3, p.pos[2] + p.vel[2] / 3])); }
    });
  }

  // ---- timeline ------------------------------------------------------------------------------------------------
  const TL = { left: 150, right: 12, top: 8, strip: 36 };
  function tlX(frame) { return TL.left + (frame - 0.5) / nFrames * (W - TL.left - TL.right); }
  function frameAtX(xCss) { return Math.max(1, Math.min(nFrames, Math.round((xCss - TL.left) / (W - TL.left - TL.right) * nFrames + 0.5))); }
  function drawTimeline() {
    const cv = $('timeline'), ctx = cv.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, W, cv.height / dpr);
    if (nFrames < 1) return;
    const ser = C.series(run, st.car, st.which), ev = C.eventMarks(run, st.car), flips = C.gateFlips(run, st.car);
    // expectation ranges along the bottom
    const baseY = TL.top + ser.length * TL.strip + 6;
    run.expectations.forEach((e, i) => {
      const [a, b] = C.expectationRange(e, run); if (b < a) return;
      ctx.fillStyle = e.ok ? css('--pass') : css('--fail'); ctx.globalAlpha = i === st.expActive ? 0.95 : 0.35;
      ctx.fillRect(tlX(a) - 3, baseY + (i % 3) * 7, tlX(b) - tlX(a) + 6, 5); ctx.globalAlpha = 1;
    });
    ser.forEach((s, r) => {
      const y0 = TL.top + r * TL.strip, vals = s.values.filter(v => v !== null);
      let lo = Math.min(0, ...vals), hi = Math.max(0, ...vals); if (hi === lo) { hi = lo + 1; }
      const yOf = (v) => y0 + TL.strip - 6 - (v - lo) / (hi - lo) * (TL.strip - 12);
      ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui'; ctx.fillText(s.name.length > 22 ? s.name.slice(0, 21) + '…' : s.name, 4, y0 + 15);
      ctx.fillText('[' + C.fmt(lo) + ', ' + C.fmt(hi) + ']', 4, y0 + 28);
      ctx.strokeStyle = css('--line'); ctx.lineWidth = 1; line(ctx, [TL.left, yOf(0)], [W - TL.right, yOf(0)]);
      ctx.strokeStyle = css('--accent'); ctx.lineWidth = 1.5; ctx.beginPath();
      s.values.forEach((v, k) => { if (v === null) return; const x = tlX(k), y = yOf(v); if (k === 1) ctx.moveTo(x, y); else ctx.lineTo(x, y); }); ctx.stroke();
      ev.filter(m => m.reward === r).forEach(m => { const x = tlX(m.frame); ctx.fillStyle = css('--text'); ctx.beginPath(); ctx.moveTo(x, y0 + 2); ctx.lineTo(x - 4, y0 + 10); ctx.lineTo(x + 4, y0 + 10); ctx.closePath(); ctx.fill(); });
      flips.filter(m => m.reward === r).forEach(m => { const x = tlX(m.frame); ctx.strokeStyle = m.passed ? css('--pass') : css('--fail'); ctx.lineWidth = 2; line(ctx, [x, y0 + TL.strip - 4], [x, y0 + TL.strip]); });
    });
    // playhead
    if (st.frame >= 1) { const x = tlX(st.frame); ctx.strokeStyle = css('--text'); ctx.lineWidth = 1.5; line(ctx, [x, 2], [x, baseY + 22]); }
    ctx.fillStyle = css('--muted'); ctx.font = '11px system-ui';
    ctx.fillText('1', tlX(1) - 3, cv.height / dpr - 3); ctx.fillText(String(nFrames), tlX(nFrames) - 8, cv.height / dpr - 3);
    $('tlnote').textContent = '▼ an event fired   | gate flipped (green: now passes, red: now fails)   bars at the bottom: the frames each expectation checks (green passed, red failed)';
  }

  // ---- HUD -----------------------------------------------------------------------------------------------------
  const ACT = ['throttle', 'steer', 'pitch', 'yaw', 'roll', 'jump', 'boost', 'handbrake'];
  function el(tag, attrs, text) { const e = document.createElement(tag); if (attrs) Object.keys(attrs).forEach(k => e.setAttribute(k, attrs[k])); if (text !== undefined) e.textContent = text; return e; }

  function updateHud() {
    const f = run.frames[st.frame], p = f.players[st.car];
    $('frameinfo').textContent = 'frame ' + st.frame + ' / ' + nFrames + (f.terminal ? '  (episode ended' + (f.goal ? ': goal' : '') + ')' : '');
    $('frameinfo').dataset.frame = st.frame; $('frameinfo').dataset.frames = nFrames;
    const a = st.frame > 0 ? f.actions[st.car] : null;
    $('controls').textContent = 'car ' + st.car + ': boost ' + C.fmt(p.boost) + ', speed ' + C.fmt(Math.hypot(p.vel[0], p.vel[1], p.vel[2])) + ' uu/s, z ' + C.fmt(p.pos[2]) + (p.on_ground ? ', on ground' : ', airborne') + (p.demoed ? ', demoed' : '')
      + (p.touched ? ', touched the ball' : '') + '.  Controls: ' + (a ? ACT.map((n, i) => n + ' ' + C.fmt(a[i])).join(', ') : '(initial state)');
    const rows = C.rewardRows(run, st.frame, st.car), cum = C.cumulative(run, st.frame, st.car);
    const tb = $('rewards').tBodies[0]; tb.innerHTML = '';
    rows.forEach((r, i) => {
      const tr = el('tr', { 'data-reward': r.name, 'data-car': st.car, 'data-weight': r.weight, 'data-raw': r.raw, 'data-wrapped': r.wrapped, 'data-weighted': r.weighted, 'data-sum-raw': cum.rewards[i].raw });
      tr.appendChild(el('td', null, r.name + (r.zeroSum ? ' [ZeroSum]' : '')));
      [r.weight, r.raw, r.wrapped, r.weighted, cum.rewards[i].raw].forEach(v => tr.appendChild(el('td', null, C.fmt(v))));
      tb.appendChild(tr);
    });
    const tot = C.totalAt(run, st.frame, st.car);
    $('total').textContent = tot === null ? 'n/a' : C.fmt(tot); $('total').dataset.total = tot === null ? '' : tot;
    $('sumtotal').textContent = C.fmt(cum.total); $('sumtotal').dataset.sumTotal = cum.total;
    if (!rows.length) { tb.appendChild(el('tr', null, '')); tb.lastChild.appendChild(el('td', { colspan: 6, class: 'muted' }, 'Initial state: no reward is computed on a reset.')); }
    const d = $('details'); d.innerHTML = '';
    rows.forEach(r => {
      const box = el('div', { class: 'reward', 'data-reward': r.name });
      const h = el('h3', null, r.name); h.appendChild(el('small', null, 'what it reported for car ' + st.car)); box.appendChild(h);
      if (r.events.length) { const evs = el('div'); r.events.forEach(e => evs.appendChild(el('span', { class: 'ev', 'data-event': e }, e))); box.appendChild(evs); }
      if (r.gates.length) {
        const ul = el('ul');
        r.gates.forEach(g => { const li = el('li', { class: 'gate', 'data-gate': g.name, 'data-passed': g.passed, 'data-mark': g.passed ? '✔' : '✘' }); li.appendChild(el('span', null, g.name + (g.passed ? ' passed' : ' failed'))); if (g.reason) li.appendChild(el('span', { class: 'why' }, ' - ' + g.reason)); ul.appendChild(li); });
        box.appendChild(ul);
      }
      if (r.components.length) {
        const c = el('div', { class: 'comps' });
        r.components.forEach((k, j) => { const s = el('span', { 'data-component': k.name, 'data-value': k.value }); s.appendChild(document.createTextNode((j ? ',  ' : '') + k.name + ' ')); s.appendChild(el('b', null, C.fmt(k.value))); c.appendChild(s); });
        box.appendChild(c);
      }
      if (r.primsDropped) box.appendChild(el('div', { class: 'warn' }, r.primsDropped + ' primitives dropped (over the per-car cap)'));
      d.appendChild(box);
    });
  }

  function buildExpectations() {
    const ul = $('expectations'); ul.innerHTML = '';
    $('expcount').textContent = run.expectations.filter(e => e.ok).length + ' of ' + run.expectations.length + ' pass';
    run.expectations.forEach((e, i) => {
      const li = el('li', { 'data-ok': e.ok, 'data-kind': e.kind, 'data-reward': e.reward, 'data-car': e.car, 'data-from': e.from, 'data-to': e.to, 'data-index': i });
      li.appendChild(el('span', { class: 'tag' }, e.ok ? 'PASS' : 'FAIL')); li.appendChild(document.createTextNode(e.intent));
      li.appendChild(el('span', { class: 'detail' }, 'car ' + e.car + ', frames ' + e.from + '-' + e.to + ': ' + e.detail));
      li.addEventListener('click', () => { st.expActive = i; st.car = e.car; $('car').value = e.car; setFrame(Math.max(1, Math.min(nFrames, e.from))); });
      ul.appendChild(li);
    });
    if (!run.expectations.length) ul.appendChild(el('li', { class: 'muted' }, 'This scenario has no expectations.'));
  }

  function buildChecks() {
    const bad = C.consistency(run), box = $('checks'); box.innerHTML = '<h2>Self-check of the embedded trace</h2>';
    box.appendChild(el('div', { 'data-consistency': bad.length ? 'fail' : 'ok', class: bad.length ? 'warn' : 'muted' },
      bad.length ? bad.length + ' inconsistencies, first: ' + bad[0] : 'weighted = wrapped x weight and the total = the sum of weighted, on every frame.'));
  }

  // ---- control -------------------------------------------------------------------------------------------------
  function render() {
    drawView($('top'), topView); drawView($('side'), sideView); drawTimeline(); updateHud();
    document.querySelectorAll('#expectations li').forEach(li => li.classList.toggle('active', Number(li.dataset.index) === st.expActive));
    $('play').textContent = st.playing ? 'Pause' : 'Play';
  }
  function setFrame(k) { st.frame = Math.max(0, Math.min(nFrames, k)); render(); }
  let last = 0;
  function tick(now) {
    if (st.playing) {
      if (now - last >= 1000 / (15 * st.speed)) { last = now; if (st.frame >= nFrames) st.playing = false; else st.frame++; render(); }
      requestAnimationFrame(tick);
    }
  }
  function play(on) { st.playing = on; if (on) { if (st.frame >= nFrames) st.frame = 0; last = 0; requestAnimationFrame(tick); } render(); }
  function jumpEvent(dir) {
    const marks = C.eventMarks(run, st.car).map(m => m.frame), uniq = Array.from(new Set(marks)).sort((a, b) => a - b);
    const next = dir > 0 ? uniq.find(f => f > st.frame) : uniq.slice().reverse().find(f => f < st.frame);
    if (next !== undefined) setFrame(next);
  }

  $('play').onclick = () => play(!st.playing);
  $('prev').onclick = () => { play(false); setFrame(st.frame - 1); }; $('next').onclick = () => { play(false); setFrame(st.frame + 1); };
  $('first').onclick = () => { play(false); setFrame(0); }; $('last').onclick = () => { play(false); setFrame(nFrames); };
  $('speed').onchange = (e) => { st.speed = Number(e.target.value); };
  $('car').onchange = (e) => { st.car = Number(e.target.value); render(); };
  $('allcars').onchange = (e) => { st.allCars = e.target.checked; render(); };
  $('vels').onchange = (e) => { st.vels = e.target.checked; render(); };
  $('which').onchange = (e) => { st.which = e.target.value; render(); };
  const tl = $('timeline'); let dragging = false;
  const tlScrub = (ev) => { const r = tl.getBoundingClientRect(); setFrame(frameAtX((ev.clientX - r.left) / r.width * W)); };
  tl.addEventListener('pointerdown', (ev) => { dragging = true; tl.setPointerCapture(ev.pointerId); play(false); tlScrub(ev); });
  tl.addEventListener('pointermove', (ev) => { if (dragging) tlScrub(ev); });
  tl.addEventListener('pointerup', () => { dragging = false; });
  function liveOwns(code) { return LIVE && (live.listening || C.inputsForCode(live.bindings, code).length > 0); }   // a live binding beats the playback key
  document.addEventListener('keydown', (e) => {
    if (e.target && /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
    if (liveOwns(e.code)) return;
    if (e.key === ' ') { e.preventDefault(); play(!st.playing); }
    else if (e.key === 'ArrowLeft') { play(false); setFrame(st.frame - 1); } else if (e.key === 'ArrowRight') { play(false); setFrame(st.frame + 1); }
    else if (e.key === 'Home') setFrame(0); else if (e.key === 'End') setFrame(nFrames);
    else if (e.key === '[') jumpEvent(-1); else if (e.key === ']') jumpEvent(1);
  });

  let resizeTimer = null;
  window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(() => { layout(); render(); }, 80); });
  buildExpectations(); buildChecks();
  // Start on the first frame where something happens (an event or a failed gate), else frame 1.
  const firstEvent = C.eventMarks(run, 0).map(m => m.frame).sort((a, b) => a - b)[0];
  setFrame(firstEvent !== undefined ? firstEvent : Math.min(1, nFrames));

  // ---- live mode (tools/reward_lab/serve.py): step with your own inputs, branch from any frame, record and play macros -------
  // The page only sends inputs and shows what the host's EnvSet/reward stack returned; nothing is simulated or scored here.
  function setRun(next, frame) {
    run = next; nFrames = run.frames.length - 1; nCars = run.frames[0].players.length;
    if (st.car >= nCars) st.car = 0;
    st.expActive = -1; renderHeader(); layout(); buildExpectations(); buildChecks();
    setFrame(frame === undefined ? st.frame : frame);
  }
  const live = { held: [], down: new Set(), bindings: C.copyBindings(C.PRESETS[C.DEFAULT_PRESET].bindings), preset: C.DEFAULT_PRESET, listening: null, others: 'script', busy: false };
  function heldFor(c) { if (!live.held[c]) live.held[c] = [0, 0, 0, 0, 0, 0, 0, 0]; return live.held[c]; }
  function setStatus(msg, bad) { const s = $('livestatus'); if (!s) return; s.textContent = msg; s.className = bad ? 'warn' : 'muted'; }

  async function hostCall(cmd) {
    if (live.busy) return null;
    live.busy = true;
    try {
      const resp = await fetch('/api', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Lab-Client': '1' }, body: JSON.stringify(cmd) });
      const r = await resp.json();
      if (!r.ok) { setStatus(String(r.error), true); return null; }
      return r;
    } catch (e) { setStatus('server unreachable: ' + e.message, true); return null; }
    finally { live.busy = false; }
  }
  function adopt(r, frame) {                  // a reply with a whole run (load, macro_play)
    const errs = C.validate(r.run);
    if (errs.length) { setStatus('host sent an invalid run: ' + errs[0], true); return false; }
    setRun(r.run, frame); return true;
  }
  function merge(d, frame) {                  // a reply with a delta (step, seek)
    try { setRun(C.applyDelta(run, d), frame); return true; } catch (e) { setStatus(e.message, true); return false; }
  }

  function syncWidgets() {
    const h = heldFor(st.car);
    document.querySelectorAll('#liveactions [data-i]').forEach(w => {
      const i = Number(w.dataset.i);
      if (w.type === 'checkbox') w.checked = h[i] === 1; else { w.value = h[i]; w.nextElementSibling.textContent = C.fmt(h[i]); }
    });
    $('livecar').textContent = 'car ' + st.car;
  }
  function buildLiveWidgets() {
    const box = $('liveactions'); box.innerHTML = '';
    C.ACTION_NAMES.forEach((n, i) => {
      const lab = el('label', { class: 'liverow' }); lab.appendChild(document.createTextNode(n + ' '));
      const w = el('input', i < 5 ? { type: 'range', min: -1, max: 1, step: 0.05, 'data-i': i } : { type: 'checkbox', 'data-i': i });
      lab.appendChild(w); if (i < 5) lab.appendChild(el('output', null, '0'));
      w.addEventListener('input', () => { heldFor(st.car)[i] = w.type === 'checkbox' ? (w.checked ? 1 : 0) : Number(w.value); syncWidgets(); });
      box.appendChild(lab);
    });
  }
  function actionsForStep(stepIdx) {
    const out = [];
    for (let c = 0; c < nCars; c++) {
      if (c === st.car || live.others === 'hold') out.push(heldFor(c).slice());
      else if (live.others === 'script') out.push(C.scriptedAction(run.scenario, c, stepIdx));
      else out.push([0, 0, 0, 0, 0, 0, 0, 0]);
    }
    return out;
  }
  async function liveStep(n) {
    play(false);
    if (st.frame < nFrames) {                 // viewing the past: stepping from here BRANCHES, the later frames are dropped
      const dropped = nFrames - st.frame;
      const s = await hostCall({ cmd: 'seek', frame: st.frame }); if (!s || !merge(s, st.frame)) return;
      setStatus('branched at frame ' + st.frame + '; ' + dropped + ' later frame(s) dropped', false);
    }
    // The scenario's script changes with the step number, so under "script" each step is its own request; otherwise one request repeats the held inputs.
    const requests = live.others === 'script' ? n : 1, repeat = live.others === 'script' ? 1 : n;
    for (let i = 0; i < requests; i++) {
      const d = await hostCall({ cmd: 'step', actions: actionsForStep(nFrames), repeat });
      if (!d) return;
      if (!merge(d, d.keep + d.frames.length - 1)) return;
      if (d.finished) { setStatus('the run is finished (' + d.max_steps + ' steps max or the episode ended); branch from an earlier frame or reset', false); return; }
    }
    setStatus('at step ' + nFrames + ' of ' + run.scenario.steps + '; car ' + st.car + ' is under your control', false);
  }
  function download(name, text) {
    const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' })); a.download = name; document.body.appendChild(a); a.click(); a.remove();
  }

  if (LIVE) {
    $('live').hidden = false;
    document.body.dataset.live = 'true';
    buildLiveWidgets(); syncWidgets();
    fetch('/api/scenarios').then(r => r.json()).then(j => {
      (j.scenarios || []).forEach(n => { const o = el('option', { value: n }, n); $('livescn').appendChild(o); });
      $('livescn').value = run.scenario.name;
    }).catch(() => {});
    $('liveothers').onchange = (e) => { live.others = e.target.value; };
    $('livestep').onclick = () => liveStep(1);
    $('livestepn').onclick = () => liveStep(Math.max(1, Math.min(600, Math.floor(Number($('livecount').value) || 1))));
    $('livereset').onclick = async () => { play(false); const r = await hostCall({ cmd: 'reset' }); if (r && merge(r, 0)) setStatus('reset to the start state', false); };
    $('liveload').onclick = async () => {
      play(false);
      const r = await hostCall({ cmd: 'load_named', name: $('livescn').value, steps: Math.max(1, Math.min(100000, Math.floor(Number($('livesteps').value) || 600))) });
      if (r && adopt(r, 0)) setStatus('loaded ' + run.scenario.name + ' as a sandbox of up to ' + run.scenario.steps + ' steps', false);
    };
    $('liverec').onclick = async () => {
      const r = await hostCall({ cmd: 'macro_record' });
      if (r) { download((run.scenario.name || 'run') + '.macro.json', JSON.stringify(r.macro, null, 1) + '\n'); setStatus('macro with ' + r.macro.ticks + ' ticks saved', false); }
    };
    $('livemacro').onchange = async (e) => {
      const f = e.target.files[0]; if (!f) return;
      let macro; try { macro = JSON.parse(await f.text()); } catch (x) { setStatus('that file is not JSON', true); return; }
      e.target.value = '';
      const skip = Math.max(1, Math.floor(Number($('liveskip').value) || run.scenario.tick_skip)), phase = Math.max(0, Math.floor(Number($('livephase').value) || 0));
      play(false);
      const r = await hostCall({ cmd: 'macro_play', macro, skip, phase });
      if (r && adopt(r, 1)) setStatus('played the macro at skip ' + skip + ', phase ' + phase + ' (' + (run.frames.length - 1) + ' steps)', false);
    };
    // Capture: save the frame being shown as a scenario file. The host does the work (Lab/Capture.h); a capture never carries expectations.
    async function capture(mode) {
      const name = $('capname').value.trim();
      if (mode === 'replay' && st.frame < 1) { setStatus('frame 0 has no actions to replay: use "Save state" for it, or step first', true); return; }
      const cmd = { cmd: 'capture', mode, frame: st.frame }; if (name) cmd.name = name;
      const r = await hostCall(cmd); if (!r) return;
      download(r.name + '.scenario.json', JSON.stringify(r.scenario, null, 1) + '\n');
      $('capwarn').textContent = r.warnings.length ? 'Lost in this capture: ' + r.warnings.join('; ') + '.' : '';
      setStatus('saved ' + r.name + '.scenario.json (' + (mode === 'replay' ? 'exact replay of frames 0..' + st.frame : 'approximate state at frame ' + st.frame) + '). It has no expectations: add them with an intent line.', false);
    }
    $('capreplay').onclick = () => capture('replay');
    $('capstate').onclick = () => capture('state');

    // ---- controls: configurable bindings (defaults: Rocket League keyboard + mouse), saved in this browser ----------------------------
    const STORE = 'rewardlab.controls.v1';
    function presetOf(b) {
      const key = (x) => JSON.stringify(C.ACTIONS.map(a => (x[a.id] || []).slice().sort()));
      const mine = key(b);
      return Object.keys(C.PRESETS).find(n => key(C.PRESETS[n].bindings) === mine) || 'custom';
    }
    function loadControls() {
      try {
        const j = JSON.parse(localStorage.getItem(STORE));
        if (j && C.validateBindings(j.bindings).length === 0) { live.bindings = C.copyBindings(j.bindings); live.preset = presetOf(live.bindings); }
      } catch (e) { /* no storage, or nothing saved: keep the default */ }
    }
    function saveControls() {
      try { localStorage.setItem(STORE, JSON.stringify({ bindings: live.bindings })); $('keysaved').textContent = 'saved in this browser'; }
      catch (e) { $('keysaved').textContent = 'could not save (browser storage unavailable): kept for this page only'; }
    }
    function setHeldFromDown() { heldFor(st.car).splice(0, 8, ...C.actionFromHeld(live.bindings, live.down)); syncWidgets(); }
    function bindingsChanged() {
      live.down.clear(); setHeldFromDown();
      live.preset = presetOf(live.bindings);
      buildKeyTable(); saveControls();
    }
    function stopListening() { live.listening = null; buildKeyTable(); }
    function buildKeyTable() {
      const tb = $('keytable').tBodies[0]; tb.innerHTML = '';
      C.ACTIONS.forEach(a => {
        const tr = el('tr', { 'data-input': a.id });
        tr.appendChild(el('td', null, a.label));
        const td = el('td');
        (live.bindings[a.id] || []).forEach(code => {
          const chip = el('span', { class: 'chip', 'data-code': code }); chip.appendChild(document.createTextNode(C.codeLabel(code)));
          const x = el('button', { title: 'remove ' + C.codeLabel(code), 'aria-label': 'remove ' + C.codeLabel(code) }, '×');
          x.onclick = () => { live.bindings[a.id] = live.bindings[a.id].filter(c => c !== code); bindingsChanged(); };
          chip.appendChild(x); td.appendChild(chip);
        });
        if (!(live.bindings[a.id] || []).length) td.appendChild(el('span', { class: 'muted' }, 'unbound'));
        tr.appendChild(td);
        const add = el('td'); const b = el('button', { 'data-add': a.id }, live.listening === a.id ? 'press a key or click… (Esc cancels)' : 'Add key');
        if (live.listening === a.id) b.className = 'listening';
        b.onclick = () => { live.listening = live.listening === a.id ? null : a.id; buildKeyTable(); };
        add.appendChild(b); tr.appendChild(add); tb.appendChild(tr);
      });
      const sel = $('keypreset'); sel.innerHTML = '';
      Object.keys(C.PRESETS).forEach(n => sel.appendChild(el('option', { value: n }, C.PRESETS[n].label)));
      if (live.preset === 'custom') sel.appendChild(el('option', { value: 'custom' }, 'Custom (edited)'));
      sel.value = live.preset;
      $('keywarn').textContent = C.bindingWarnings(live.bindings).join('  •  ');
      $('keynote').textContent = 'Click Add, then press a key or click a mouse button (Esc cancels). A key may drive several inputs: in Rocket League W/A/S/D are throttle+pitch and steer+yaw at once, because the car uses throttle/steer on the ground and pitch/yaw in the air. Engine signs (measured): pitch up = nose up, yaw/roll/steer right = right. The Rocket League layout is from Liquipedia\'s control-settings table as summarised by a search; I could not open the page, so check it against Settings > Controls in the game.';
    }
    $('keypreset').onchange = (e) => { if (C.PRESETS[e.target.value]) { live.bindings = C.copyBindings(C.PRESETS[e.target.value].bindings); bindingsChanged(); } };
    $('keyreset').onclick = () => { const n = C.PRESETS[live.preset] ? live.preset : C.DEFAULT_PRESET; live.bindings = C.copyBindings(C.PRESETS[n].bindings); bindingsChanged(); };
    loadControls(); buildKeyTable(); setHeldFromDown();

    // Rebinding: the next key or mouse button pressed anywhere is added to the input being edited (capture phase, so nothing else sees it).
    function bindCode(code) {
      const id = live.listening; if (!id) return;
      if ((live.bindings[id] || []).indexOf(code) < 0) live.bindings[id] = (live.bindings[id] || []).concat(code);
      live.listening = null; bindingsChanged();
    }
    document.addEventListener('keydown', (e) => {
      if (!live.listening) return;
      e.preventDefault(); e.stopImmediatePropagation();
      if (e.code === 'Escape') stopListening(); else if (!e.repeat) bindCode(e.code);
    }, true);
    let swallowContext = false;
    document.addEventListener('mousedown', (e) => {
      if (!live.listening) return;
      if (e.target && e.target.closest && e.target.closest('#keytable button')) return;   // clicking Add / × is not a binding
      e.preventDefault(); e.stopImmediatePropagation(); swallowContext = e.button === 2; bindCode('Mouse' + e.button);
    }, true);
    document.addEventListener('contextmenu', (e) => { if (swallowContext || (live.listening)) { e.preventDefault(); swallowContext = false; } }, true);

    // Playing: keys anywhere on the page, mouse buttons over the two field views.
    const typing = (e) => /^(INPUT|SELECT|TEXTAREA)$/.test((e.target && e.target.tagName) || '');
    const activates = (e) => /^(BUTTON|SUMMARY)$/.test((e.target && e.target.tagName) || '') && (e.code === 'Enter' || e.code === 'Space');
    function press(code, repeat) {
      const ids = C.inputsForCode(live.bindings, code);
      if (!ids.length) return false;
      if (ids.indexOf('step') >= 0 && !repeat) liveStep(1);
      if (ids.some(id => id !== 'step')) { live.down.add(code); setHeldFromDown(); }
      return true;
    }
    document.addEventListener('keydown', (e) => {
      if (live.listening || typing(e) || activates(e) || e.ctrlKey || e.metaKey || e.altKey) return;
      if (press(e.code, e.repeat)) e.preventDefault();
    });
    document.addEventListener('keyup', (e) => { if (live.down.delete(e.code)) setHeldFromDown(); });
    ['top', 'side'].forEach(id => {
      const cv = $(id);
      cv.addEventListener('mousedown', (e) => { if (live.listening) return; if (press('Mouse' + e.button, false)) e.preventDefault(); });
      cv.addEventListener('contextmenu', (e) => { if (C.inputsForCode(live.bindings, 'Mouse2').length) e.preventDefault(); });
    });
    document.addEventListener('mouseup', (e) => { if (live.down.delete('Mouse' + e.button)) setHeldFromDown(); });
    window.addEventListener('blur', () => { if (live.down.size) { live.down.clear(); setHeldFromDown(); } });
    $('car').addEventListener('change', syncWidgets);
  }

  // For tools (and Claude through the browser): the same data the page shows, by function.
  window.LAB = { get run() { return run; }, core: C, state: st, live, setFrame, setCar: (c) => { st.car = c; $('car').value = c; render(); if (LIVE) syncWidgets(); },
    liveStep, hostCall,
    snapshot: () => ({ frame: st.frame, car: st.car, rows: C.rewardRows(run, st.frame, st.car), total: C.totalAt(run, st.frame, st.car), cumulative: C.cumulative(run, st.frame, st.car) }) };
})();
