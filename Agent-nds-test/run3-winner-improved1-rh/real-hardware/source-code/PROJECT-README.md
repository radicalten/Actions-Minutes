# `suite.nds` — ARM7 / ARM9 CPU and system test suite

This project builds a bare-metal Nintendo DS CPU/system-vector ROM with generators, emulator runners, trace capture, QEMU comparisons, and fault-injection checks.

> **Development suite only — not a release.** Combined-ROM redistribution clearance remains unresolved. See [`NOTICE.md`](NOTICE.md).

## Testing documentation

The one canonical testing handbook is [`suite-readme.md`](suite-readme.md). It consolidates coverage/results, build and test commands, branch and immediate matrices, trace/ID architecture, the agent emulator-comparison workflow, limitations, and release status. The older pages under [`docs/guide/`](docs/guide/README.md) are retained as short redirects for existing links.

> **Snapshot warning:** the workspace currently mixes an older ROM/generated manifest with newer trace-v5 source and test reports. The `suite-readme.md` records the exact hashes and current host-test failure. Rebuild and regenerate hash-matched evidence before using the suite as a current validated snapshot.

## Start here

- Read [`suite-readme.md`](suite-readme.md) before building, running, comparing, or interpreting tests.
- Read [`AGENTS.md`](AGENTS.md) for agent operating constraints and [`handoff.md`](handoff.md) for the current handoff state.
- Review [`NOTICE.md`](NOTICE.md) and the release gate before packaging or redistributing anything.

The requested `.patch` file is intentionally not part of the workspace and must not be recreated; updated source, suite documentation, and the upload staging folder are maintained directly for the user to review.
