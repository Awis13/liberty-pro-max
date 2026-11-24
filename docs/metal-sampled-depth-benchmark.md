# Metal sampled-depth specialization benchmark

[Runtime PR #16](https://github.com/Awis13/x86recomp/pull/16) contains the implementation and GPU regression tests.

The runtime optimization specializes expensive pixel shaders for the actual sampled-depth texture mask. Metal can remove per-sample uniform checks while preserving the required depth reversal. Background compilation and generic-shader fallback avoid making the draw wait for the extra shader or pipeline. Texture bindings are checked every eligible draw; at most four masks are created per shader instance. This applies to the shared Apple Metal backend, not every graphics backend or every workload.

## Measured result: Apple M1 Pro

A fixed outdoor traffic scene was rendered at **5120×2880**, SMAA 1x, the existing graphics settings and mods, with the 60 FPS limit retained. The save position was `-421.9,1494,18.9`, heading `90`, hour `13`, weather `1`. This is a GPU-bound test; the cap does not conceal its result.

| Warm measurement | Original | Specialized |
| --- | ---: | ---: |
| Median reported FPS | 44.625 | 45.930 |
| Median frame interval | 22.287 ms | 21.613 ms |
| Median of reported p99 intervals | 24.280 ms | 23.340 ms |
| Worst interval in retained reports | 25.065 ms | 24.374 ms |

That is **2.9% more FPS**, or **0.674 ms less typical frame time**, in this scene. It is a small single-device sample, not a general speedup guarantee. The recorded rows and calculation inputs are in [the benchmark JSON](../benchmarks/metal-sampled-depth-m1-pro.json).

Separate process runs varied substantially. The reported result therefore alternates the original and optimized paths in one process every 10 seconds using diagnostic-only instrumentation. The first FPS report after each switch was excluded, as were loading/teleport and later traffic occlusion. There are two original and three optimized five-second reports in the retained 30–75-second FPS-clock window. Diagnostic logging confirmed actual optimized pipeline selection; an earlier experiment that never activated it was discarded.

The final production build at **2560×1440** held **60 FPS**: median reported p99 16.673 ms and worst interval 17.199 ms in its 35–65-second warm window. It recorded 50 specialized draws per frame and no shader or pipeline compilation while drawing in that window. Its shader cache was kept and warmed, not cleared.

## Reproduce the same-process diagnostic

A small [diagnostic-only runtime patch](../benchmarks/metal-sampled-depth-interleaving.patch) is included to reproduce the alternating experiment. Apply it to a disposable checkout of the runtime PR, rebuild, and set `RECOMP_TEST_DEPTH_AB=1` for that test process. The patch was checked with `git apply --check`; it is not part of the production optimization. It alternates every 10 process-clock seconds and prints a phase marker. Existing PERF counters show whether specialized draws actually occurred.

Retain only reports whose entire preceding FPS-report interval belongs to one phase: discard the first report after every phase marker. Use log order rather than comparing the two clocks directly. Exclude scene loading/teleport, allow the variants to finish background compilation, and use the same preselected stable scene window for both paths. Recalculate the medians from the retained rows as shown in the JSON. Do not publish a gain if only the generic path ran, both paths hit the FPS cap, or phase-to-phase variation exceeds the apparent improvement.

## Comparable tests on another device

1. Build the runtime PR and keep the game data, save, graphics, anti-aliasing, resolution, FPS limit, and route identical. Use a copied save/data directory and disable save synchronization for automated runs. Record the runtime commit and device/OS with the results.
2. Warm the shader/pipeline caches before measurement. Use `RECOMP_SAMPLED_DEPTH=0` for the original path and `RECOMP_SAMPLED_DEPTH=1` for specialization. Keep the shader cache; cold compilation is a separate experiment. Repeat in alternating order (for example off/on/on/off) to expose drift.
3. Capture the existing `GTAIV_PERF=1`, `GTAIV_FPS_REPORT=5`, `GTAIV_HITCH=50`, `GTAIV_LOAD_LOG=1` reports. The fixed scene can be selected with `GTAIV_BENCH=-421.9,1494,18.9,90,13,1`. Choose a warm window after the explicit scene load/teleport has finished. The FPS clock and process/loading clocks differ.
4. Compare the original and candidate log files using the same window:

```sh
python3 tools/shader_bench_report.py original.log candidate.log \
  --compare-warm --start 35 --end 65 --json comparison.json
```

The command rejects missing/changed render resolutions, different resolutions between runs, empty windows, and interrupted/failed measurements. It reports median FPS, typical frame interval, median reported p99, worst interval, and the FPS/frame-time deltas. It does not infer that all other graphics settings match: record and check them yourself. Medians of aggregate FPS reports are not pooled per-frame percentiles, and tail differences from a small sample are descriptive, not statistical significance.

The existing `tools/shader_bench.sh warm` can collect isolated Mac runs when its farm runner is configured; see its header for required paths and save options. The new report mode can also consume logs collected manually or on another device. It does not launch the game, close a running game, alter settings, or delete caches.

## Correctness and applicability

The runtime PR includes 20 exact GPU pixel comparisons across depth masks and sampler slots (including alpha), plus an end-to-end Metal-backend test for generic fallback, asynchronous pipeline activation, continued activation after handoff, texture rebinding, the variant bound, destruction with pending work and the diagnostic opt-out. Reintroducing the dangling-reference pipeline-key bug makes that integration test fail; the corrected code passes.

Only the M1 Pro has been performance-tested here. iOS 17 and tvOS 27.1 SDK syntax checks passed, but base M1, M2 and A18 gains remain unmeasured. GPU-bound users may benefit; CPU-bound, FPS-capped or ineligible shader workloads may show little or no FPS difference. These hidden/windowed host intervals are not physical display presentation timestamps or a promise of zero stutter everywhere.

This companion PR does not change `X86RECOMP`: the normal installer fetches the upstream `liberty` branch, so its pin should move only after the runtime optimization merges upstream. Pinning an unmerged fork commit would break that install path.
