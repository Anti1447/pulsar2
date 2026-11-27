# Light mode (Pulsar3)

2026-11-26, user-directed: train slowly while the GPU and CPU are used for other things.

    tools/light.sh on [share]    # default 0.25; applies within seconds, survives restarts
    tools/light.sh off
    tools/light.sh status

The trainer rests in proportion to the time it worked, after each rollout step and learn minibatch (`Util/LightMode.h`, file
`build/checkpoints_pulsar3/THROTTLE`), and uses half the minibatch (same update through gradient accumulation, lower GPU memory).
Learning settings are otherwise unchanged. Test: `tests/test_light_mode.cpp`. Panel: `Light Mode/Share`.

First measurement (18:44, with other GPU load present: 82% busy at the 250 W limit, three RLBot clients running): 5-9k steps/s
against about 60k at full speed, peak reserved GPU memory 8.8 GB (14.4 GB at full speed). The trainer's own share of the GPU at
0.25 has not been measured on an otherwise idle machine.
