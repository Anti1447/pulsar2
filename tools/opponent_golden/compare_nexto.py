"""Checklist 1.2, Nexto: the ORIGINAL nexto_obs.py + nexto-model.pt against the NextoOpponent PORT, on the same
sim-recorded states (tools/opponent_golden/golden_dump.cpp writes them and the port's answers).

  ~/nexto_ref_venv/bin/python tools/opponent_golden/compare_nexto.py <nexto.json> [rlbot-run/nexto]

Thresholds (docs/Pre_Run_Checklist.md 1.0): obs difference <= 1e-5 per float; masked argmax identical except
near-ties (top-2 logit gap < 1e-4), which are listed. The mask is all zeros for Nexto, so plain argmax.
Reference states are duck-typed stand-ins for rlgym_compat's V1GameState (fields read by encode_gamestate); how
rlgym_compat derives has_flip/on_ground/boost from an RLBot packet was settled by reading it (EXTERNAL_OPPONENTS.md).
"""
import json
import sys

import numpy as np
import torch

path = sys.argv[1]
nexto_dir = sys.argv[2] if len(sys.argv) > 2 else "rlbot-run/nexto"
sys.path.insert(0, nexto_dir)
from nexto_obs import NextoObsBuilder  # noqa: E402

torch.set_num_threads(4)
OBS_TOL = 1e-5
TIE_GAP = 1e-4

FEATURES = (["IS_SELF", "IS_MATE", "IS_OPP", "IS_BALL", "IS_BOOST"]
            + [f"POS{a}" for a in "xyz"] + [f"VEL{a}" for a in "xyz"] + [f"FW{a}" for a in "xyz"]
            + [f"UP{a}" for a in "xyz"] + [f"ANGVEL{a}" for a in "xyz"]
            + ["BOOST", "DEMO", "ON_GROUND", "HAS_FLIP"]
            + [f"PREV{i}" for i in range(8)])


class PO:
    def __init__(self, pos, vel, angvel, mtx=None):
        self.position = np.array(pos, dtype=np.float64)
        self.linear_velocity = np.array(vel, dtype=np.float64)
        self.angular_velocity = np.array(angvel, dtype=np.float64)
        self._m = mtx

    def rotation_mtx(self):
        return self._m


class Pl:
    pass


class St:
    pass


def make_state(snap, order):
    st = St()
    st.blue_score = st.orange_score = 0
    st.boost_pads = np.array([1.0 if b else 0.0 for b in snap["pads"]])
    b = snap["ball"]
    st.ball = PO(b["pos"], b["vel"], b["angvel"])
    st.inverted_ball = st.ball
    players = []
    for idx in order:
        p = snap["players"][idx]
        f, u = np.array(p["fwd"], dtype=np.float64), np.array(p["up"], dtype=np.float64)
        left = np.cross(u, f)  # proper rotation: columns forward, left, up
        mtx = np.stack([f, left, u], axis=1)
        q = Pl()
        q.car_id = idx + 1
        q.team_num = p["team"]
        q.car_data = PO(p["pos"], p["vel"], p["angvel"], mtx)
        q.inverted_car_data = q.car_data
        q.is_demoed = p["demoed"]
        q.on_ground = p["on_ground"]
        q.ball_touched = False
        q.has_flip = p["has_flip"]
        q.boost_amount = p["boost"] / 100.0
        players.append(q)
    st.players = players
    return st


def ref_obs(snap, order, self_idx):
    st = make_state(snap, order)
    builder = NextoObsBuilder()
    player = st.players[order.index(self_idx)]
    prev = np.array(snap["players"][self_idx]["prev_action"], dtype=np.float64)
    q, kv, m = builder.build_obs(player, st, prev)
    return q, kv, m


model = torch.jit.load(f"{nexto_dir}/nexto-model.pt", map_location="cpu")
model.eval()


def ref_logits(q, kv, m):
    with torch.no_grad():
        out, _ = model((torch.from_numpy(q).float(), torch.from_numpy(kv).float(), torch.from_numpy(m).float()))
    return out.numpy().reshape(-1)


def top2_gap(l):
    s = np.sort(l)
    return float(s[-1] - s[-2])


data = json.load(open(path))
has_cuda = data["has_cuda"]
worst_obs = (0.0, None)
obs_fail = []
argmax_fail, ties = [], []
logit_dev = 0.0
act_vs_argmax = 0
cuda_diff = []
perm_fail, perm_dev = [], 0.0
rows = 0

for snap in data["snapshots"]:
    n = snap["n"]
    arena_order = list(range(n))
    for i in range(n):
        rows += 1
        tag = f'{snap["tag"]} p{i} (team {snap["players"][i]["team"]})'
        q, kv, m = ref_obs(snap, arena_order, i)
        pq = np.array(snap["port"][i]["q"], dtype=np.float64)
        pkv = np.array(snap["port"][i]["kv"], dtype=np.float64).reshape(-1, 24)
        rq, rkv = q.reshape(-1), kv.reshape(-1, 24)
        if rkv.shape != pkv.shape:
            obs_fail.append((tag, f"shape {rkv.shape} vs {pkv.shape}"))
            continue
        dq, dkv = np.abs(rq - pq), np.abs(rkv - pkv)
        dmax = max(float(dq.max()), float(dkv.max()))
        if dmax > worst_obs[0]:
            worst_obs = (dmax, tag)
        if dmax > OBS_TOL:
            if dq.max() > OBS_TOL:
                j = int(np.argmax(dq))
                where = f"q[{j}] {FEATURES[j]}"
            else:
                r, c = np.unravel_index(int(np.argmax(dkv)), dkv.shape)
                where = f"kv row {r} col {c} {FEATURES[c]}"
            obs_fail.append((tag, f"max diff {dmax:.3g} first at {where}"))

        # logits of the ORIGINAL on the ORIGINAL obs vs the port's logits on the port's obs
        rl = ref_logits(q, kv, m)
        pl = np.array(snap["port"][i]["logits_cpu"])
        logit_dev = max(logit_dev, float(np.abs(rl - pl).max()))
        ra = int(np.argmax(rl))
        if int(snap["port"][i]["argmax_cpu"]) != ra:
            (ties if top2_gap(rl) < TIE_GAP else argmax_fail).append(
                (tag, f"orig {ra} port {snap['port'][i]['argmax_cpu']} gap {top2_gap(rl):.3g}"))
        if has_cuda and int(snap["port"][i]["argmax_cuda"]) != ra:
            cuda_diff.append((tag, f"orig {ra} cuda {snap['port'][i]['argmax_cuda']} gap {top2_gap(rl):.3g}"))
        if snap["act_cpu"][i] != snap["port"][i]["argmax_cpu"]:
            act_vs_argmax += 1

        # live ordering [self, mates, opponents] must not change the answer
        if n > 2:
            me = snap["players"][i]["team"]
            mates = [j for j in arena_order if j != i and snap["players"][j]["team"] == me]
            opps = [j for j in arena_order if snap["players"][j]["team"] != me]
            q2, kv2, m2 = ref_obs(snap, [i] + mates + opps, i)
            rl2 = ref_logits(q2, kv2, m2)
            perm_dev = max(perm_dev, float(np.abs(rl - rl2).max()))
            if int(np.argmax(rl2)) != ra:
                perm_fail.append((tag, f"arena {ra} live-order {int(np.argmax(rl2))} gap {top2_gap(rl):.3g}"))

print(f"{len(data['snapshots'])} states, {rows} player observations")
print(f"obs:      worst difference {worst_obs[0]:.3g} ({worst_obs[1]}); over {OBS_TOL}: {len(obs_fail)}")
for t, d in obs_fail[:12]:
    print("   ", t, d)
print(f"logits:   max |original - port| {logit_dev:.3g}")
print(f"argmax:   CPU mismatches {len(argmax_fail)} (+{len(ties)} near-ties under {TIE_GAP})")
for t, d in (argmax_fail + ties)[:12]:
    print("   ", t, d)
if has_cuda:
    print(f"CUDA:     argmax mismatches vs original {len(cuda_diff)}")
    for t, d in cuda_diff[:12]:
        print("   ", t, d)
print(f"Act():    {act_vs_argmax} players where the returned action index != the net's argmax index (0 expected if the action map is the identity)")
print(f"order:    live-order [self, mates, opps] vs arena order: max logit change {perm_dev:.3g}, argmax changes {len(perm_fail)}")
for t, d in perm_fail[:12]:
    print("   ", t, d)

ok = not obs_fail and not argmax_fail and not cuda_diff and not perm_fail and act_vs_argmax == 0
print("RESULT:", "PASS" if ok else "FAIL (read the lines above)")
sys.exit(0 if ok else 1)
