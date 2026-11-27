"""Read-only Pulsar3 status for periodic updates. usage: python3 tools/pulse.py"""
import glob
import json
import os
import statistics
import time

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C = P + '/build/checkpoints_pulsar3'


def trainer_up():
    for p in glob.glob('/proc/[0-9]*/exe'):
        try:
            if os.readlink(p).startswith(P + '/build/GigaLearnBot'):
                return True
        except OSError:
            pass
    return False


path = os.path.realpath(P + '/run_logs/latest.log')
with open(path, errors='replace') as f:
    lines = f.read().splitlines()


def numbers(key):
    return [float(l.split(':')[-1].replace(',', '')) for l in lines if l.startswith(key)]


speed, steps = numbers('Overall Steps/Second'), numbers('Total Timesteps')
print(time.strftime('%H:%M'), 'up' if trainer_up() else 'DOWN', 'log', os.path.basename(path))
print(f'steps {steps[-1] / 1e9:.3f}B  speed median(last 150) {statistics.median(speed[-150:]):.0f}  '
      f'last 10 {statistics.median(speed[-10:]):.0f}  crashes {sum("CRASH: exit" in l for l in lines)}')
print('light mode share:', open(C + '/THROTTLE').read().strip() if os.path.exists(C + '/THROTTLE') else 'off (full speed)')
m = json.load(open(C + '/metrics_latest.json'))['m']
pick = lambda k: round(m[k], 4) if k in m else None
print('entropy', pick('Policy Entropy'), 'KL', pick('Mean KL Divergence'), 'car speed', pick('Player/Speed'),
      'CarEnergy share', pick('Reward Share %/N4RLGC24CarEnergyPotentialRewardE'),
      'WavedashGain share', pick('Reward Share %/N4RLGC18WavedashGainRewardE'))
history = []
for line in open(C + '/metrics.jsonl'):
    try:
        history.append(json.loads(line))
    except ValueError:
        pass
for key in ('Rating/1v1', 'Rating/2v2', 'Rating/3v3', 'Player/Speed', 'Ref/Share'):
    values = [r['m'][key] for r in history if key in r['m']]
    if values:
        print(f'{key}: latest row {values[-1]:.4g}  mean of last 6 {sum(values[-6:]) / len(values[-6:]):.4g}')
rows = [json.loads(l) for l in open(C + '/GAUNTLET_LOG.jsonl')]
four = ['Nexto', 'GalactusV2', 'ChronosV2', 'Dice']
everyone = four + ['Pulsar2']


def pct(subset, bots, mode=None):
    won = games = 0
    for r in subset:
        for b in bots:
            v = r.get(b, {})
            v = v.get(mode, {}) if mode else v
            won += v.get('wins', 0)
            games += v.get('games', 0)
    return round(100 * won / games, 1) if games else None


for label, subset in (('latest', rows[-1:]), ('last 4', rows[-4:]), ('4 before', rows[-8:-4])):
    print(f"gauntlet {label} @{subset[-1]['total_timesteps'] / 1e9:.3f}B: 1v1 four {pct(subset, four)} | Pulsar2 {pct(subset, ['Pulsar2'])}"
          f" | 2v2 {pct(subset, everyone, '2v2')} | 3v3 {pct(subset, everyone, '3v3')}")
st = os.statvfs('/home')
print('disk free GB', round(st.f_bavail * st.f_frsize / 1e9), '| GPU',
      os.popen('/usr/lib/wsl/lib/nvidia-smi --query-gpu=memory.used,utilization.gpu,temperature.gpu --format=csv,noheader').read().strip())
for l in lines[::-1]:
    if l.startswith('GPU/Peak Reserved GB'):
        print(l)
        break
