# Trainer speed: what buta-ppo suggested, what was measured, what was kept (2026-11-23)

Status: measurements on the live `ts2` run (800k-row batch, 1 epoch, ledger H-019). User asked (2026-11-23) to run it with the speed improvements from `ButaMasta/buta-ppo`.
Source read: README, changelog and the rollout / trainer source via a sub-agent's fetch (not built or run here). Summary in section 4.

## 1. Where the time goes (this machine: RTX 3090, 20-core CPU)

Per iteration, 800k rows, mean over the live log:

| Phase | Time | Note |
|---|---|---|
| Collection wall | 6.5-8.3 s | runs on a worker thread, concurrent with the learn pass of the previous batch |
| of which inference | 4.4-4.9 s (about 55-72%) | 390 env steps per iteration, so 11-13 ms per step for a forward that is about 1 ms alone |
| of which env step (physics) | 1.0-1.4 s | not the bottleneck |
| Learn pass | 5.0 s | forward+loss 1.6 s, backward 2.8 s, clip 5 ms, optimizer 73 ms (new `Learn Phase/*` timers) |

The learn pass is almost all matmul compute, about 16-19 TFLOP/s effective (6 FLOPs per parameter per row estimate), so buta's graph-safe optimizer / clipping would save about 1-2% for us.

## 2. Why inference is slow: the split, then the test

Host-side split of the inference call (new `Inference H2D / Launch / Wait Time` panels), per iteration: copy-in from pageable memory 1.7 s, CPU launch of the forward 2.9 s, wait for the result 0.3 s.
That pointed at CPU-side launch overhead, and buta-style CUDA graphs plus pinned buffers attack exactly that. Both were built and measured:

| Variant | Inference time per iteration | Overall |
|---|---|---|
| eager (before) | 4.9 s | about 102k steps/s |
| graph: forward + sampling captured, copies outside | 5.9 s | no better |
| graph: pinned copies and device-to-host copies inside the graph (two CUDA calls per step) | 5.3 s | no better |

Both graphs captured, matched the eager probabilities exactly (max difference 0) and sampled legal actions; the entropy, KL and update size were unchanged, so they are correct but give no speedup.
**Conclusion: the cost is not the number of launches or API calls.** The inference kernels wait for the GPU because the learn pass, running on another thread at the same time, keeps it busy with large kernels (the earlier per-iteration data agree: iterations whose collection did NOT overlap a learn pass had 0.5 s of inference against 4.7 s for the mean). The pipelining that hides the CPU work behind the learn pass is the thing that slows each inference step.
The graph code stays in the tree as an opt-in (`GGL_GRAPH_INFER=1`, `GraphedPolicy.h`); default off.

## 3. Smaller learn minibatches: tried and reverted (user-directed 2026-11-23)

`miniBatchSize` 25k -> 6.25k for the ts2 arms, mathematically identical (gradient accumulation; each learn kernel 4x shorter so the concurrent inference kernels get on the GPU sooner). Seven iterations at 6.25k against six at 25k, same run:

| | minibatch 25k | minibatch 6.25k |
|---|---|---|
| Collection wall | 8.7 s | 5.9 s |
| Inference per iteration | 5.4 s | 3.6 s |
| Learn pass | 5.0 s | 5.5 s |
| Overall steps/s (mean / median) | 98k / 100k | 80k / 53k |

Collection got faster and the learn pass slower; overall throughput did not show a gain in the few iterations measured (the overall figure is noisy because the pipeline alternates). Reverted to 25k at the user's request; `ConfigureGCOFork` carries a comment. The graph code (`GraphedPolicy.h`) remains in the tree as an opt-in, default off.

## 4. What buta-ppo does and what carries over

Their pipeline (as read, unverified by running): two env groups with one in flight, so CPU physics for one group overlaps GPU inference for the other; CUDA-graphed inference and a graphed training minibatch step with bf16 and fused Adam; a one-at-a-time worker pool; pinned persistent buffers. Their 580k steps/s (own hardware, 6M-parameter net, one epoch) is mostly a lighter workload: at equal parameters and epochs their per-FLOP efficiency is similar to ours (about 21 vs 15-19 TFLOP/s).
Not carried over: the optimizer/clip graphs (1-2% of our learn pass), the two-group pipeline (physics is 12-17% of collection and the inference wait is GPU contention, not physics), the Rust physics bridge and their obs layout.

## 5. Not done, and the lever that remains

- The 1.6M-to-800k and 1-epoch changes (H-019) already halve the learn cost; with the concurrency contention above the better knob is the learn / collection schedule, not the code path.
- Untested ideas, each cheap to try as its own lever, with a clean A/B of at least 30 iterations (the first tests here were interrupted by restarts): the learn pass on a lower-priority stream (it is on the default stream now), and a short pause of learn kernels during inference (a mutex, which costs learn time).
