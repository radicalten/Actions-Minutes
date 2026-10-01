---
name: coding-agent-hardened
description: Drive Codex CLI, Claude Code, OpenCode, or Pi Coding Agent in this no-PTY/no-stdin sandbox with one-shot commands and background processes; project guidance is aligned to NooDS-Wii's ARM-to-PPC JIT runbook.
metadata:
  {
    "openclaw": { "emoji": "🧩", "requires": { "anyBins": ["claude", "codex", "opencode", "pi"] } },
  }
---

# Coding Agent (sandbox-adapted, bash-first)

Use **bash** for one-shot agent calls and **start_process** for work that may outlive a bash call. For this project, `/home/user/AGENTS.md` is the repository-specific source of truth: read it before project work, and follow it over any conflicting project note in this skill.

## ⚠️ This sandbox: NO PTY, NO stdin — one-shot modes only

This environment has no `pty:true` parameter and no `process action:write/submit/send-keys/paste`. `bash` runs with stdin closed and no controlling terminal; background processes cannot be typed into either. Therefore:

1. **Never launch an interactive REPL.** Bare `claude`, `codex`, `opencode`, or `pi` can wait at an unanswered prompt forever.
2. **Always use one-shot flags:** `codex exec`, `claude -p`, `opencode run`, or `pi -p`.
3. **Make prompts self-contained.** The agent cannot ask a mid-run question. Include scope, edit/approval boundaries, required context, output location, and a precise definition of done. Resolve important ambiguities before spawning it.
4. **Expect plain-text output.** There is no TTY, so do not rely on colors, boxes, or ANSI formatting.
5. **Check availability first.** CLI installation is not guaranteed. Do not install a CLI or global tool without the user's approval.

### Credentials and optional tools

- Assume no API key is available unless the user has configured a supported local login. Never invent a key or put passwords, API keys, or tokens in a prompt, command line, or process I/O. If authentication is missing, ask the user to configure it locally.
- `gh` may be absent and is needed only for GitHub CLI workflows. Do not install it without approval; plain `git` can handle fetch/checkout operations when suitable.
- If a coding-agent CLI is missing, check with the user before installing it. If approved, use the CLI's documented installation method; do not fetch random binaries, forks, or tarballs.

### Bash tool parameters

| Parameter | Type | Description |
| --- | --- | --- |
| `command` | string | Shell command; stdin is closed and there is no PTY |
| `cwd` | string | Working directory (default `/home/user`) |
| `timeout` | number | Seconds; default 30, maximum 1800. **Set 1800 for foreground coding-agent runs.** |

`bash` kills the command at timeout. Use `start_process` for work expected to run longer.

### Background process tools

| Tool | Purpose |
| --- | --- |
| `start_process(name, command, cwd, startup_wait)` | Start a background process that survives across turns; use a clear user-facing name |
| `get_process_output(wait_for: log\|exit\|port)` | Block for a log pattern, process exit, or a newly listening port; each wait is at most 180 seconds |
| `get_process_output(tail_lines)` | Read the current process log without waiting |
| `stop_process(process_id)` | Send SIGTERM, then SIGKILL if needed |

- Prefer one `wait_for: exit` or `wait_for: log` call over polling loops. For a longer run, wait again or report that it is still running.
- Do not use `ps`/`sleep` loops to monitor a task. Use the process tools.
- `wait_for: port` is for servers/previews, not coding-agent runs.

---

## Quick Start: One-Shot Tasks

```bash
# Scratch work. Keep scratch files under /home/user so they persist.
mkdir -p /home/user/tmp
SCRATCH=$(mktemp -d /home/user/tmp/scratch.XXXXXX)
git -C "$SCRATCH" init
(cd "$SCRATCH" && codex exec --full-auto "Your self-contained prompt here")
# Foreground agent runs: use timeout 1800.

# In an existing project, set the cwd to that project and keep the prompt scoped.
```

Codex may require a trusted Git directory; initialize a scratch Git repository as above for disposable scratch work. Do not initialize or rewrite Git state in a user's project unless the task requires it.

### Real project example

```text
bash cwd:/home/user/NooDS-Wii command:"claude -p 'Read /home/user/AGENTS.md first. Fix the reported bug, run the required verification, and report the exact commands and fresh artifacts.'" timeout:1800
```

For longer tasks, put the one-shot CLI command in `start_process`, with the same scope and verification instructions in the prompt.

---

## The Pattern: cwd + one-shot + background when needed

```text
start_process name:"Codex agent" cwd:/home/user/NooDS-Wii command:"codex exec --full-auto 'Read /home/user/AGENTS.md first. <self-contained task and definition of done>'"

# Wait for completion (one wait at a time; maximum 180 seconds per wait)
get_process_output process_id:XXX wait_for:exit wait_timeout:180

# Or wait for a known milestone
get_process_output process_id:XXX wait_for:log wait_pattern:"BUILD_OK|BUILD_FAIL" wait_timeout:180

# Read the log or stop only when necessary
get_process_output process_id:XXX tail_lines:200
stop_process process_id:XXX
```

**Why `cwd` matters:** it focuses the agent on the intended repository. Still tell it to read `/home/user/AGENTS.md`; do not assume it will discover or obey project-specific guidelines automatically.

---

## Coding-agent CLI reference

### Codex CLI

Use one-shot `exec` for tasks. Prefer the sandboxed/auto-approval mode for scoped edits; reserve `--yolo` for disposable scratch directories with no secrets or production configuration.

```bash
# Foreground, with the bash timeout explicitly set to 1800
codex exec --full-auto "Read /home/user/AGENTS.md first. <task>"

# Review only; do not add edit-approval flags to a review
codex review --base origin/main
```

For a PR review, clone or check out the PR in a separate scratch directory under `/home/user`, not inside another live project. Use `gh` only if installed and authorized; otherwise use plain Git fetch/checkout commands. For parallel PR reviews, use isolated checkouts and distinct process names/cwds.

### Claude Code

```bash
# Print mode; never launch bare `claude` in this environment
claude -p "Read /home/user/AGENTS.md first. <task>"

# Background version with edits explicitly allowed
claude -p --permission-mode acceptEdits "Read /home/user/AGENTS.md first. <task>"
```

### OpenCode

```bash
opencode run "Read /home/user/AGENTS.md first. <task>"
```

### Pi Coding Agent

```bash
pi -p "Read /home/user/AGENTS.md first. <task>"

# Only if the user has selected/configured this provider and model
pi --provider openai --model gpt-4o-mini -p "<task>"
```

Do not choose or change a provider/model on the user's behalf when that choice affects cost, credentials, or access.

---

## Parallel Issue Fixing with Git Worktrees

Use isolated worktrees when independent tasks really can proceed in parallel. Worktree paths must be under `/home/user` to persist. Do not let multiple agents edit the same checkout or branch.

```bash
mkdir -p /home/user/tmp
git worktree add -b fix/issue-78 /home/user/tmp/issue-78 main
git worktree add -b fix/issue-99 /home/user/tmp/issue-99 main

# Start one agent per worktree; every prompt is complete before launch.
# For NooDS-Wii, include the environment and verification requirements below.

# Monitor each process with get_process_output wait_for:exit.
# Review changes and run verification before pushing or opening a PR.

git worktree remove /home/user/tmp/issue-78
git worktree remove /home/user/tmp/issue-99
```

NooDS-Wii is a constrained sandbox (2 vCPU, about 1.9 GiB RAM); avoid running several heavy Dolphin tests concurrently. Keep each long Dolphin tool call under about 25 minutes, and allow extra time for its poweroff/SD writeback.

---

## NooDS-Wii project workflow

**Project runbook:** `/home/user/AGENTS.md` (currently v04). Read it before project work. **Read §6 before touching JIT code.** This skill summarizes the operational rules; the runbook contains the detailed source map, invariants, exact commands, and expected behavior.

### Environment and build

- Repository: `/home/user/NooDS-Wii`; source tree is under `NooDS-Wii/` within the repository. Tools and test ROMs are under `/home/user/tools` and `/home/user/nds` / `/home/user/gba`.
- The environment is Debian 13, headless, with about 2 vCPU and 1.9 GiB RAM. `/opt` and apt-installed packages may disappear between sandbox sessions; `/home/user` persists.
- If required system packages are missing, follow AGENTS.md §0's package list (`zstd`, `dolphin-emu`, `xvfb`, `imagemagick`, `mtools`, `libgl1-mesa-dri`) and obtain any required installation approval. Do not add unrelated packages.
- If `/opt/devkitpro` is missing, use the project bootstrap `/home/user/setup-devkitppc.sh` as directed by AGENTS.md. The specified mirror is `https://wii.leseratte10.de/devkitPro/`; `pkg.devkitpro.org` returns 403. Do not improvise another toolchain source or use `dkp-pacman` here.
- **Re-export PATH in every fresh shell call** (shell environment does not persist):

  ```bash
  export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
  ```

- Do **not** source the obsolete `/home/user/wii-env.sh`; current AGENTS.md uses the PATH export above.
- From the repository root, `make -j4` builds the default JIT DOL (`jit.dol`). `make JIT=0` builds `NooDS-Wii-interp.dol` for the interpreter. The build takes about 22 seconds on the documented sandbox. JIT and interpreter objects are separated in `obj-jit1/` and `obj-jit0/`; avoid deleting them unless the task calls for a clean rebuild.
- Make compiles C++ and assembly sources (`*.S`/`*.s`) with dependency generation. Keep assembler style and build rules consistent with AGENTS.md.

Typical build command:

```bash
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
cd /home/user/NooDS-Wii
make -j4
```

### Verification and definition of done

- A successful compile is not sufficient evidence for a JIT behavior change. **After every JIT change, run all three checks in AGENTS.md §4:**
  1. Differential jittest, expecting `TOTAL FAILURES: 0` in a fresh `jittest.txt` log.
  2. End-to-end autodump with JIT and interpreter; compare their generated dumps.
  3. Screenshot comparison between JIT and interpreter runs. For the documented rockwrestler reference, the expected absolute-error metric is `AE 0`.
- The differential test starts from the repo root with `make -j4 && tools/jittest/build.sh`; then follow AGENTS.md §4 to create the SD image, run `jittest.dol` in headless Dolphin, and inspect `sd.raw.out` using `mtype`. Do not report success from a stale log or old DOL.
- Autodump interface: `tools/autodump.sh ROM JIT(0|1) FRAMES OUTDIR [timeout]`. Run both `JIT=1` and `JIT=0` with the same ROM/frame count, then compare the generated `autodump.txt` and frame output as specified in AGENTS.md.
- The screenshot workflow uses autoboot without `dump=` and `tools/run-dolphin.sh`; compare the actual fresh PNG outputs with ImageMagick `compare -metric AE`. Follow AGENTS.md for SD setup and artifact names.
- `tools/mksd.sh` builds a 128 MiB FAT16 SD image; `tools/run-dolphin.sh` captures `cap-<t>.png`, logs to `dolphin.log`, and writes back `SD.raw.out`. Dolphin's ALSA errors can be harmless. Input via `xdotool` does not reach Dolphin; avoid interactive tests and use the autoboot hook.
- The sandbox is headless: Dolphin on llvmpipe is the test harness, not a substitute for real Wii hardware. Its timing numbers are relative and must not be presented as real Gekko/cache performance.
- For non-JIT changes, select tests that match the changed behavior and report the commands and fresh evidence. Never claim a result you did not verify in a fresh run (jittest log, dump, or screenshot as applicable).

### JIT safety checklist — AGENTS.md §6 is authoritative

Before modifying JIT code, read the complete invariants in AGENTS.md §6. At minimum, preserve these constraints:

- Guest registers map to PPC host registers as documented there. Do not use `r0` as a D-form/X-form load/store or `addi` base; never touch `r2` or `r13`; only `cr0`/`cr1` are available to the JIT.
- Keep `armJit` as the last `Interpreter` member so field offsets remain usable. Preserve the interpreter/JIT PC and pipeline conventions.
- Every exit must follow the correct writeback path (`exitWB` or the already-written-back `exitNoWB`). Preserve the fallback state/PC/cycle contract and its exit conditions.
- Analysis and emission must agree; add native instructions as a new kind rather than re-decoding them differently. Match interpreter cycle costs exactly, including conditional-instruction accounting.
- Preserve the PPC flag mapping and carry/overflow rules described in §6.
- Keep code buffers/block pools in static `.bss` in MEM1 with POD data, call `jit_sync_icache` after emission, bump `jitGen` for stores, and reset the JIT in `loadState`.

### Common project pitfalls and known references

- When differential tests find a mismatch, verify the interpreter against the ARM reference before assuming the JIT is wrong. The runbook records fixed interpreter bugs and the LDRT/STRT fallback behavior.
- Test harness addresses must remain within the restored data region; invalid random scaled offsets can create false failures.
- `libfat` commits file data at `fclose`; logs that must survive a kill should be reopened and closed per write. Dolphin poweroff may take minutes, so budget for it and inspect the already-written SD result before deciding a process is stuck.
- `arm.gba` failure #234 is known in both cores because the interpreter lacks the ARMv3 CMP-pc mode change; do not mislabel it as a JIT regression.
- Use AGENTS.md's pinned ROM paths/checksums and known-good outputs. Do not substitute unrelated ROMs or claim hardware behavior from emulator output.
- The current next-work roadmap is in `01_JIT_PROGRESS.md` §6. Do not begin unrequested roadmap work; rerun §4 after each JIT step.

### Deliverables

When asked to prepare project deliverables, follow AGENTS.md §9. It specifies `/home/user/deliverables/`, the expected DOL/write-up/runbook/patch/tools/evidence bundle, and naming conventions (`NN_TOPIC.md` and versioned `AGENTS_NN.md`). Do not replace existing deliverables or advance the runbook version without checking the user's requested scope.

---

## Operating Rules

1. **One-shot flags only** (`exec`, `-p`, or `run`); there is no way to answer a REPL prompt here.
2. **Respect tool choice.** If the user asks for Codex, use Codex. In orchestrator mode, do not hand-code the patch yourself. If an agent fails or hangs, report that state and respawn it or ask the user; do not silently take over.
3. **Self-contained prompts.** Include approval, context, files, constraints, and definition of done. For NooDS-Wii, every spawned agent prompt starts with: “Read `/home/user/AGENTS.md` first.” For JIT work, also state the §4 verification requirement.
4. **Be patient.** Background coding and Dolphin runs can take many minutes. Do not kill a process merely because it is slow; if it is genuinely stuck, tell the user before stopping it.
5. **Monitor with process tools**, not `ps`/`sleep` loops.
6. **Set timeouts deliberately.** Use `timeout: 1800` for foreground agent calls. Keep long Dolphin calls under about 25 minutes per the project runbook; use a background process when needed.
7. **Use safe approval modes.** Prefer `--full-auto` for scoped work in the workspace. `--yolo` is only for disposable scratch directories under `/home/user/tmp` with no credentials or production configuration.
8. **Parallelize carefully.** One process per isolated worktree, with distinct names and `cwd`s. Avoid concurrent resource-heavy Dolphin tests in this low-memory sandbox.
9. **Keep persistent work under `/home/user`.** Use `/home/user/tmp` for scratch data and worktrees. Respect `/home/user/deliverables` for requested release artifacts.
10. **Do not improvise the toolchain.** `/home/user/AGENTS.md` and `/home/user/setup-devkitppc.sh` define the approved setup path.
11. **Protect existing user work.** Inspect `git status` before edits; never overwrite unrelated uncommitted changes or generated evidence without approval.

---

## Progress Updates (Critical)

When starting a background coding agent, send one short update describing what is running and where. After that, update only when something changes:

- a milestone completes (build, jittest, dump comparison, screenshot comparison);
- the agent fails or needs user action (for example, missing local authentication);
- the process finishes, with a concise summary of changes and verification evidence;
- a process is stopped, immediately explain that it was stopped and why.

This prevents the user from seeing only “Agent failed before reply” with no context.

---

## Completion Handling

There is no `openclaw gateway wake` here. Completion detection is native:

- **Preferred:** `get_process_output wait_for:exit`; each wait is capped at 180 seconds. Repeat if the run is still active, or report its state and continue later.
- **Milestones:** have the process print a clear final marker such as `BUILD_OK`, `JITTEST_OK`, or `BUILD_FAIL`; use `wait_for:log` when helpful. A marker is not proof by itself—inspect the actual build/test artifact and exit status.
- Background processes survive across turns. Do not imply they completed if a wait timed out.
- Report the exact verification performed and the fresh artifacts inspected. For JIT changes, state the outcome of all three AGENTS.md §4 checks; distinguish failures, skipped checks, and unverified work.

---

## Learnings

- **No PTY/no stdin:** interactive modes are unrecoverable hangs; use one-shot flags only.
- **Bash defaults to 30 seconds:** set the timeout explicitly for foreground agent runs.
- **Environment does not persist between shell calls:** re-export the devkitPPC `PATH` each time for project builds.
- **Current build target is `jit.dol`, not `boot.dol`.** The interpreter target is `NooDS-Wii-interp.dol`; do not use old Wii-template build instructions.
- **For JIT changes, a successful build alone is not done:** run all three checks in AGENTS.md §4 and inspect the new logs/dumps/images.
- **Headless Dolphin is not hardware:** use it for the documented functional checks, not real-device timing claims.
- **`/home/user` is the persistent root.** Toolchain binaries under `/opt` and system packages may need the documented re-setup in a later sandbox session.

## Security Guardrails

- Use `--full-auto` (sandboxed) by default for scoped workspace edits. Reserve `--yolo` for throwaway scratch directories under `/home/user/tmp/` with no credentials, secrets, or production configs.
- Never let a coding agent install packages from non-registry sources, GitHub forks, URLs, or tarballs, or add global tools without the user's explicit confirmation. The only project-specific toolchain path is the one documented in `/home/user/AGENTS.md`; do not substitute other sources.
- Route autonomous changes through feature branches and pull requests. Push to `main`, `production`, or `release/*` only after a human has reviewed the actual changes.
- Never send passwords, API keys, or tokens through agent commands, prompts, or process I/O. If a key is required, ask the user to configure it through a supported local method.
