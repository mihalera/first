# Repository Conventions

## Branch naming
- `main` is the only long-lived integration branch.
- Feature work happens on short-lived branches and is merged back via PRs.

## Commit style
- Keep commit subjects short and imperative.
- Put the why in the body, not in the subject line.

## PRs
- One concern per PR.
- Rebase on `main` before pushing; do not add merge commits.
- PRs must not include unrelated changes.

## DSP micro-benchmarks in CI
- The repository contains a DSP micro-benchmark job (`dsp-benchmark`) that runs
  as part of the `Build JUCE Plugin (CMake)` workflow.
- `dsp-benchmark` is a measurement job, not a regression gate. It must not block
  merges, and a PR should not be refused because a benchmark number changed.
- The benchmark builds and runs the hot-path nanobench binary
  (`nonlin-hotpath-bench`) with explicit sample rate, channel count, and block
  size combinations so results are comparable across machines.
- Benchmark results are published as a workflow artifact (`dsp-benchmark-results`)
  for review during PR review. Do not read them as pass/fail.
- When reviewing benchmark results, treat material regressions as meaning the
  underlying implementation should be profiled and possibly revisited;
  treat small noise-level changes as expected variation.
- The benchmark job depends on `build-linux-debug` because the hot-path bench
  target is compiled from the same project tree with `J37_BUILD_TESTS=ON` and
  `J37_BUILD_HOTPATH_BENCH=ON`.
