# v3 arena mesh on the GPU: accuracy and speed (2026-11-24, branch gpu-env-mesh)

## Accuracy: share of samples within 5 UU of RocketSim v3 (tools/parity/score5.py; 197 scenarios, every 0.25 s, moving objects)
| group | before the port (92bea19) | with the mesh | v2 vs v3 |
|---|---|---|---|
| scripted ball | 69.4% | 100% | 91.9% |
| random ball | 85.0% | 97.1% | 92.1% |
| random driving | 71.2% | 91.2% | 43.1% |
| random walls | 55.6% | 62.5% | 56.2% |
| scripted walls | 42.1% | 55.0% | 48.6% |
| hits | 82.2% | 84.3% | 67.1% |
| random hits | 100% | 100% | 93.4% |
| driving | 100% | 100% | 55.0% |
| flips | 40.0% | 41.2% | 38.8% |
| bumps / random bumps | 47.0% / 33.5% | 48.5% / 34.0% | 55.6% / 38.0% |
| ALL | 69.2% | 75.7% | 64.4% |
Pre-registered Part A bands vs v3: 38/43 (before: 37/43). GPU tests 14/14. Not ported: chassis-arena collision (flip landings, ramp bumper hits, ceiling) and car-car collision (bumps).

## Speed: tests/gpu/gpu_bench.cu (six-mode mix, random starts and actions, physics only), arena-ticks/s, best of 5 per run
Measured with the Pulsar3 trainer running on the same GPU, so runs vary widely; ranges are over 2-3 alternating runs.
| arenas | before the parity patches (66d8e41) | at 69% (92bea19) | mesh port |
|---|---|---|---|
| 16384 | 6.6-7.8 M | 3.2-4.3 M | about 1.0 M |
| 49152 | 11.8-15.6 M | 6.6-12.1 M | 2.7-3.4 M |
The mesh binary with the mesh disabled (analytic arena) ran 6.5 M at 49152, so most of the cost is the mesh queries themselves. The occupancy grid raised the mesh build from 2.4 M to about 3.4 M. Not tried: per-cell triangle lists, splitting the step kernel into stages.
