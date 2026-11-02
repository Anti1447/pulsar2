// Headless check of the sim core on the presets (node check_presets.js). Prints the outcome class and key ticks.
const WD = require('./wavedash_core.js');
const base = { v0: 1000, z0: 70, vz0: 0, th0: 25, ctrl: 'hold', targetDeg: 25, avail: 'nojump', airSinceJump0: 0, quantize: false, phase: 0, afterFlipTarget: 'same' };
const P = {
  wavedash: { ...base },
  early: { ...base },
  level_j1: { ...base, th0: 0, targetDeg: 0 },
  crash: { ...base, th0: -25, targetDeg: -25 },
  flip: { ...base, z0: 800, th0: 0, targetDeg: 0, afterFlipTarget: 'level' },
  ceiling: { ...base, z0: 2027, th0: 180, targetDeg: 25 },
  noflip_expired: { ...base, avail: 'jump', airSinceJump0: 1.3 },
};
function go(name, p, j) {
  const g = WD.ghost(p);
  const r = WD.run(p, { tick: g.tick - j, front: true });
  const c = WD.classify(r);
  const ev = r.ev;
  console.log(`${name.padEnd(15)} j=${String(j).padStart(3)} ghostContact@${g.tick} press@${ev.pressTick ?? '-'} firstContact@${ev.firstContactTick ?? '-'} [${(ev.firstContactSet || []).join(',')}] cancel@${ev.cancelTick ?? '-'} ground@${ev.groundTick ?? '-'} body@${ev.bodyHitTick ?? '-'}  => ${c.cls}  dv=${c.dv.toFixed(0)} ${c.note || ''}`);
  return c;
}
go('wavedash', P.wavedash, 1);
go('wavedash', P.wavedash, 2);
go('early', P.early, 3);
go('early', P.early, 6);
go('level_j1', P.level_j1, 1);
go('crash', P.crash, 1);
go('crash', P.crash, 5);
go('flip', P.flip, 200);
go('ceiling', P.ceiling, 1);
go('noflip_expired', P.noflip_expired, 1);
console.log('\nscan wavedash geometry (hold nose-up 25): press j ticks before first contact');
for (let j = 14; j >= -1; j--) { const g = WD.ghost(P.wavedash); const c = WD.classify(WD.run(P.wavedash, { tick: g.tick - j, front: true })); process.stdout.write(`j=${j}:${c.cls.split(' ')[0]} `); }
console.log('\nscan nose-down 25');
for (let j = 14; j >= -1; j--) { const g = WD.ghost(P.crash); const c = WD.classify(WD.run(P.crash, { tick: g.tick - j, front: true })); process.stdout.write(`j=${j}:${c.cls.split(' ')[0]} `); }
console.log();
