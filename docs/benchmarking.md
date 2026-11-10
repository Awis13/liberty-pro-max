# Benchmarking and diagnostic captures

Keep measurements, provenance, charts and logs under ignored `out/`. Commit the
capture tools and their tests; keep reports from individual runs outside `docs/`.

These tools require a logged-in macOS session, a Metal GPU and a local game
installation. Use isolated game data and cache folders so a capture does not
change saves or consume a prepared seed. Qualify the scene, loaded libraries and
cache state before comparing timing. A cache hit alone does not establish a
performance improvement.

## Tools

- `performance_build.py` exports `main` and `HEAD`, uses each revision's dependency
  pin, and builds instrumented copies. Override `--main` and `--feature` to compare
  other revisions. `--renderer-source` selects a local dependency repository that
  contains both pins. The original checkouts remain untouched.
- `performance_probe.cpp` samples frame callbacks and process memory in those
  diagnostic builds. `performance_compare.py` captures one run;
  `performance_matrix.py` coordinates cache states and restarts.
- `shader_capture.py` captures cache lookup/persistence behavior and a gameplay
  frame. `shader_prepare_benchmark.py` measures preparation phases;
  `shader_status_benchmark.py` measures checks against an unchanged cache.
- `performance_analyze.py` validates completed captures and writes JSON/CSV.
  `performance_chart.py` renders the analysis as HTML. These tools report CPU
  callback cadence; use presentation evidence to establish displayed frame rate.

Build comparison inputs with enough disk space for two builds and cache copies:

```sh
python3 tools/performance_build.py out/metal-comparison --renderer-source /path/to/x86recomp
```

Preparation must finish before capturing a prepared first-play case. Keep its seed
free of gameplay observations, and use a copy for each independent launch. A cold
case needs an empty owned renderer cache and a new diagnostic bundle identity;
a warm case reuses the preceding run's cache and identity. Shared OS/compiler
caches can still affect results.

For the matrix directory layout, analysis defaults to `RAW/comparison.json` and
writes a CSV alongside it. Supply exclusion reasons for rejected capture folders:

```sh
python3 tools/performance_analyze.py out/metal-comparison-runs \
  --exclude 'interrupted-pilot=Interrupted before the gameplay window'
python3 tools/performance_chart.py out/metal-comparison-runs/comparison.json \
  out/metal-comparison-runs/comparison.html
```

The analysis expects source refs in `initial-inputs.json` and accepted timed runs
under `capped/block-*`. Use `--cohort NAME` to keep named implementations separate.
Run each tool with `--help` for its required executable, probe, game and cache paths.
After inspection, remove only the diagnostic bundles and cache namespaces owned
by that run; preserve logs, provenance and the acceptance/rejection record.

## Stock benchmark reports

The game's report caller at `0048EF44` queries WMI for operating-system and CPU
information. x86recomp returns `WBEM_E_NOT_SUPPORTED` with a null enumerator for
unsupported queries, allowing the guest's failure path to handle missing hardware
fields. `Win32_PNPEntity` enumeration remains available for controller discovery.
VARIANT initialization and clearing run through the runtime's COM handlers.

The dependency's `wbem_report` tests exercise report-query failure, COM ownership
and controller enumeration without game files or a GPU:

```sh
cmake --preset release -DGTAIV_RUNTIME_TESTS=ON
cmake --build --preset release --target wbem_report_test gtaiv
ctest --test-dir out/build/release -R '^wbem_report_' --output-on-failure
```

A passing WMI test does not qualify the full stock benchmark. Require a native
report and guest `ExitProcess(0)` before accepting that path; `open -W` returning
success is insufficient. A known stock-benchmark startup fault reaches guest
address `00000004` in lifted `f_007E8100`. The control renderer and startup code
reproduce it. Normal gameplay and the synthetic renderer suites exercise separate
paths and do not establish stock-benchmark completion.
