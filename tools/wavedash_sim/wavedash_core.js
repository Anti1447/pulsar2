// 2D (side view, pitch-only) Rocket League car wavedash simulator core.
// Calibrated against RocketSim v3 (see docs/WAVEDASH_FORMAL.md): flip impulse, flip rotation rate and duration, pitch control
// response, dodge availability. The CONTACT model (2 wheels + 4 hitbox corners, impulses) is a toy: it reproduces the qualitative
// wavedash / flip hop / nose crash behavior, not RocketSim's suspension and collision details.
const WD = (() => {
  const C = {
    G: 650, DT: 1 / 120,
    ALPHA: 12.46, WMAX: 5.6, DAMP: 2.8,          // pitch control (measured)
    WFLIP: 7.37, FLIP_TICKS: 79, FLIP_RAMP: 2,   // flip rotation: 7.37 rad/s for 79 ticks after a 1-tick delay and ~2-tick ramp (measured)
    ZDAMP_START: 17, ZDAMP: 0.65,                // dodge vertical damping (measured)
    VCAP: 2300, JFRONT: 500,                     // front flip adds 500 uu/s up to the 2300 cap (measured)
    BACK0: 533.3, BACKK: 0.348,                  // back flip dv = -(533.3 + 0.348 v) (measured, v >= 0)
    DODGE_WINDOW: 1.25,                          // seconds after a jump (measured); none after a no-jump fall
    REST_Z: 17, I: 1268, MU_BODY: 0.4, E_BODY: 0.15,
    wheels: [{ n: 'front', x: 51.25, z: -4.5, r: 12.5 }, { n: 'rear', x: -33.75, z: -2.0, r: 15.0 }],
    corners: [{ n: 'nose-bottom', x: 72.9, z: 2.67 }, { n: 'nose-top', x: 72.9, z: 38.83 }, { n: 'tail-bottom', x: -45.1, z: 2.67 }, { n: 'tail-top', x: -45.1, z: 38.83 }],
    SUSP_TICKS: 10, SUSP_REACH: 10,              // toy suspension: a wheel stays "in contact" for 10 ticks after touching while within 10 uu
    CEIL: 2044, TAU: 2, DELTA: 10, RHO: 0.7,     // formal-definition thresholds (ticks, ticks, fraction of J)
  };
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const DEG = Math.PI / 180;

  function newState(p) {
    return {
      tick: 0, x: 0, z: p.z0, vx: p.v0, vz: p.vz0 || 0, th: p.th0 * DEG, w: 0,
      ground: false, flipping: false, flipTick: 0, flipDir: 0, used: false, hasFlip: false,
      avail: p.avail, airSinceJump: p.airSinceJump0 || 0,
    };
  }

  function worldPt(s, lx, lz) {
    const c = Math.cos(s.th), n = Math.sin(s.th);
    return { x: s.x + c * lx - n * lz, z: s.z + n * lx + c * lz };
  }

  // One 120 Hz tick. input = {u: pitch in [-1,1], flip: bool}. ev collects events (mutated).
  function step(s, input, ev) {
    s.tick++;
    const u = clamp(input.u, -1, 1);
    // --- dodge availability
    if (!s.ground) {
      if (s.avail === 'jump') s.airSinceJump += C.DT;
      s.hasFlip = !s.used && (s.avail === 'nojump' || (s.avail === 'jump' && s.airSinceJump < C.DODGE_WINDOW));
    }
    // --- flip press
    if (input.flip && !s.ground) {
      if (s.flipping || s.used) { ev.rejected = ev.rejected || 'flip already used'; }
      else if (!s.hasFlip) { ev.rejected = ev.rejected || (s.avail === 'none' ? 'no flip available' : 'dodge window expired (' + s.airSinceJump.toFixed(2) + ' s since the jump)'); ev.rejectTick = ev.rejectTick ?? s.tick; }
      else if (Math.abs(u) < 0.5) { ev.rejected = ev.rejected || 'no direction input (pitch) - this sim has no double jump'; }
      else {
        const dir = Math.cos(s.th) >= 0 ? 1 : -1, front = u < 0;
        let J;
        if (front) { J = clamp(C.VCAP - s.vx * dir, 0, C.JFRONT); s.vx += dir * J; s.flipDir = -1; }
        else { J = -(C.BACK0 + C.BACKK * Math.max(0, s.vx * dir)); s.vx += dir * J; s.flipDir = 1; }
        s.used = true; s.flipping = true; s.flipTick = 0; s.flipHeading = dir; s.flipFront = front;
        ev.pressTick = s.tick; ev.J = J; ev.dir = dir; ev.front = front; ev.vx0AtPress = s.vx - dir * J;
        ev.contactAtPress = ev.anyContactNow || false;
      }
    }
    // --- rotation
    if (s.flipping) {
      s.w = s.flipDir * C.WFLIP * clamp(s.flipTick / C.FLIP_RAMP, 0, 1);   // torque starts the tick after the press, ~2 ticks to full rate (fits -16.1 deg at +6 ticks)
      // dodge vertical damping (measured): from the 18th tick after the press until the flip ends, vz is multiplied by 0.65 every
      // tick (gravity still applies afterwards), so vz converges to about -15.5 uu/s and the car floats instead of falling.
      if (s.flipTick >= C.ZDAMP_START && s.flipTick < C.FLIP_TICKS - 1) s.vz *= C.ZDAMP;
      s.flipTick++;
      if (s.flipTick >= C.FLIP_TICKS) { s.flipping = false; ev.flipEndedByTimer = s.tick; }
    } else {
      const wasIn = Math.abs(s.w) <= C.WMAX;
      s.w += C.ALPHA * u * C.DT;
      s.w -= C.DAMP * (1 - Math.abs(u)) * s.w * C.DT;
      if (wasIn) s.w = clamp(s.w, -C.WMAX, C.WMAX);
    }
    // --- integrate
    s.vz -= C.G * C.DT;
    s.th += s.w * C.DT; s.x += s.vx * C.DT; s.z += s.vz * C.DT;
    // --- contacts
    const pts = [];
    for (const wh of C.wheels) { const c = worldPt(s, wh.x, wh.z); pts.push({ n: wh.n, wheel: true, px: c.x, pz: c.z - wh.r, pen: wh.r - c.z }); }
    for (const co of C.corners) { const c = worldPt(s, co.x, co.z); pts.push({ n: co.n, wheel: false, px: c.x, pz: c.z, pen: -c.z }); }
    const touching = pts.filter(p => p.pen > 0);
    ev.anyContactNow = touching.length > 0;
    if (touching.length && ev.firstContactTick === undefined) { ev.firstContactTick = s.tick; ev.firstContactSet = touching.map(p => p.n); }
    if (touching.some(p => !p.wheel) && ev.bodyHitTick === undefined) { ev.bodyHitTick = s.tick; ev.speedAtBodyHit = s.vx; }
    let maxPen = 0;
    for (let it = 0; it < 8; it++) {
      for (const p of pts) {
        const c0 = Math.cos(s.th), n0 = Math.sin(s.th);
        // recompute the contact point (orientation does not change within the solve, position does through z correction only)
        if (p.pen <= 0) continue;
        const rx = p.px - s.x, rz = p.pz - s.z;
        const vpz = s.vz + s.w * rx, vpx = s.vx - s.w * rz;
        if (vpz < 0) {
          const e = (!p.wheel && vpz < -50) ? C.E_BODY : 0;
          const jn = -(1 + e) * vpz / (1 + rx * rx / C.I);
          let jt = 0;
          if (!p.wheel) jt = clamp(-vpx / (1 + rz * rz / C.I), -C.MU_BODY * jn, C.MU_BODY * jn);
          s.vz += jn; s.vx += jt; s.w += (rx * jn - rz * jt) / C.I;
        }
        maxPen = Math.max(maxPen, p.pen);
      }
    }
    if (maxPen > 0) s.z += maxPen;
    // --- ground mode: both wheels touching (the 2D analogue of >= 3 of 4 wheels)
    s.lastWheel = s.lastWheel || [-99, -99];
    const wc = C.wheels.map((wh, i) => {
      const c = worldPt(s, wh.x, wh.z), gap = c.z - wh.r;
      if (gap <= 0.75) s.lastWheel[i] = s.tick;
      return gap <= 0.75;
    });
    const susp = C.wheels.map((wh, i) => { const c = worldPt(s, wh.x, wh.z); return (s.tick - s.lastWheel[i] <= C.SUSP_TICKS) && (c.z - wh.r <= C.SUSP_REACH); });
    const bothWheels = susp[0] && susp[1];
    ev.wheelsNow = wc;
    if (bothWheels && !s.ground) {
      if (s.flipping) { ev.cancelTick = s.tick; }
      s.ground = true; s.flipping = false; s.th = 0; s.w = 0; s.z = C.REST_Z; s.vz = 0; s.used = false;
      ev.groundTick = s.tick; ev.vxAtGround = s.vx;
    }
  }

  // ---- controllers
  function pdInput(s, target) {   // pitch PD toward target angle (rad); critically damped near the target
    let e = target - s.th;
    e = Math.atan2(Math.sin(e), Math.cos(e));   // shortest way round (after a flip th is unwrapped)
    return clamp(3.0 * e - 1.0 * s.w, -1, 1);
  }

  // Run a scripted scenario. p = parameter object, press = null | {tick, front}. quantize: hold inputs G ticks (decision grid; G = p.G, default 8).
  function run(p, press) {
    const s = newState(p), ev = {}, frames = [];
    const MAXT = p.maxTicks || 900;
    let uHeld = 0, pressedYet = false;
    const snap = () => frames.push({
      tick: s.tick, x: s.x, z: s.z, vx: s.vx, vz: s.vz, th: s.th, w: s.w, ground: s.ground, flipping: s.flipping, hasFlip: s.hasFlip,
      wheels: ev.wheelsNow ? ev.wheelsNow.slice() : [false, false], contact: !!ev.anyContactNow, body: ev.bodyHitTick !== undefined && ev.bodyHitTick === s.tick,
    });
    ev.anyContactNow = false;
    snap();
    let after = 0;
    while (s.tick < MAXT) {
      const t = s.tick + 1;  // the tick being simulated
      const G = p.G || 8;
      const onGrid = !p.quantize || ((t - (p.phase || 0)) % G + G) % G === 0;
      // controller output
      let target = p.targetDeg * DEG;
      if (p.afterFlipTarget === 'level' && ev.flipEndedByTimer !== undefined) target = 0;
      let u = p.ctrl === 'hold' ? pdInput(s, target) : 0;
      if (p.quantize) { if (onGrid) uHeld = u; u = uHeld; }
      let doFlip = false;
      if (press && !pressedYet && t >= press.tick && (!p.quantize || onGrid)) { doFlip = true; pressedYet = true; u = press.front ? -1 : 1; if (p.quantize) uHeld = u; }
      step(s, { u, flip: doFlip }, ev);
      snap();
      if (s.ground) { after++; if (after > 40) break; }
      else if (ev.bodyHitTick !== undefined && s.tick - ev.bodyHitTick > 150) break;
      if (!s.ground && s.z < -200) break;
    }
    return { p, s, ev, frames, press };
  }

  // First tick any part of the car touches the ground with no flip (the ghost). Returns {run, tick}
  function ghost(p) { const r = run(p, null); return { run: r, tick: r.ev.firstContactTick }; }

  // ---- classification against the formal definition (docs/WAVEDASH_FORMAL.md)
  function classify(r) {
    const ev = r.ev, p = r.p, out = { evidence: [], cls: 'NO FLIP PRESSED', vx0: r.frames[0].vx };
    const vxEnd = r.frames[r.frames.length - 1].vx;
    out.vxEnd = vxEnd; out.dv = vxEnd - out.vx0;
    if (ev.rejected && ev.pressTick === undefined) { out.cls = 'FLIP UNAVAILABLE'; out.note = ev.rejected; return out; }
    if (ev.pressTick === undefined) { out.note = 'no flip was pressed'; return out; }
    const dir = ev.dir, tP = ev.pressTick, tC = ev.firstContactTick, tG = ev.groundTick;
    const trailing = ev.front ? 'rear' : 'front';
    const W1 = !(ev.contactAtPress), W2 = tC !== undefined && tC >= tP && (tC - tP) <= C.TAU;
    const W3 = !!ev.firstContactSet && ev.firstContactSet.length > 0 && ev.firstContactSet.every(n => n === trailing);
    const W4 = ev.cancelTick !== undefined && tC !== undefined && (ev.cancelTick - tC) <= C.DELTA && ev.bodyHitTick === undefined;
    const gain = (vxEnd - ev.vx0AtPress) * dir, Jabs = Math.abs(ev.J);
    const W5 = W4 && gain >= C.RHO * Jabs;
    out.W = [W1, W2, W3, W4, W5];
    out.evidence = [
      ['W1 flip started airborne (no contact at the press)', W1],
      ['W2 first contact within ' + C.TAU + ' ticks of the press' + (tC !== undefined ? ' (' + (tC - tP) + ')' : ' (no contact)'), W2],
      ['W3 only trailing wheels (' + trailing + ') touch first' + (ev.firstContactSet ? ' [' + ev.firstContactSet.join(',') + ']' : ''), W3],
      ['W4 leading wheels follow and the flip is cancelled within ' + C.DELTA + ' ticks' + (ev.cancelTick !== undefined && tC !== undefined ? ' (' + (ev.cancelTick - tC) + ')' : ''), W4],
      ['W5 speed retained: gain along travel ' + gain.toFixed(0) + ' uu/s vs J ' + Jabs.toFixed(0), W5],
    ];
    out.gain = gain; out.J = ev.J;
    if (W1 && W2 && W3 && W4 && W5) out.cls = 'WAVEDASH';
    else if (ev.bodyHitTick !== undefined) { out.cls = 'NOSE/BODY CRASH'; out.note = 'a hitbox corner hit the ground (' + (ev.firstContactSet || []).join(',') + ' first); speed along travel ' + (ev.speedAtBodyHit | 0) + ' -> ' + (vxEnd | 0); }
    else if (ev.flipEndedByTimer !== undefined && (tG === undefined || ev.flipEndedByTimer <= tG)) { out.cls = 'FLIP HOP'; out.note = 'the flip finished in the air (' + C.FLIP_TICKS + ' ticks) before landing; speed ' + (out.vx0 | 0) + ' -> ' + (vxEnd | 0); }
    else if (tG === undefined) { out.cls = 'TUMBLING'; out.note = 'flip not cancelled and the car never settled on its wheels in ' + r.frames.length + ' ticks'; }
    else { out.cls = 'OTHER'; out.note = 'cancelled but outside the formal window (see the checklist)'; }
    return out;
  }

  return { C, clamp, DEG, newState, step, run, ghost, classify, pdInput, worldPt };
})();
if (typeof module !== 'undefined') module.exports = WD;
