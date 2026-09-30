---
name: coding-agent-hardened
description: Drive Codex CLI, Claude Code, OpenCode, or Pi Coding Agent in this sandbox (no PTY, no stdin control) via bash one-shot flags and background processes — adapted for Wii homebrew projects with devkitPPC.
metadata:
  {
    "openclaw": { "emoji": "🧩", "requires": { "anyBins": ["claude", "codex", "opencode", "pi"] } },
  }
---

# Coding Agent (sandbox-adapted, bash-first)

Use **bash** for one-shot agent calls and **start_process** for anything that may outlive a bash call. Simple and effective.

## ⚠️ This sandbox: NO PTY, NO stdin — one-shot modes only

This environment has no `pty:true` parameter and no `process action:write/submit/send-keys/paste`. `bash` runs with stdin closed and no controlling terminal; background processes can't be typed into either. Consequences:

1. **Never launch an interactive REPL.** `claude`, `codex`, `opencode`, or `pi` without a one-shot flag will sit at a prompt forever with no way to answer it.
2. **Always use one-shot flags:** `codex exec`, `claude -p`, `opencode run`, `pi -p`.
3. **Prompts must be fully self-contained** — the agent can never ask a mid-run question. Pre-empt everything: grant edit approval in the prompt, state where output goes, and define "done".
4. **Output is plain text** (no TTY = no colors/boxes). Don't parse ANSI.
5. **Agents are "not installed" by default** here — see Prerequisites.

### Prerequisites (per sandbox instance)

- Agent CLIs are NOT pre-installed. Install from the npm registry only (node/npm exist at `/usr/bin`):
  `npm install -g @anthropic-ai/claude-code` · `npm install -g @openai/codex` · `npm install -g @mariozechner/pi-coding-agent` · `opencode` per its docs.
- **No API keys are present** in env or config. Ask the user where the key lives; never invent one. Never transmit keys through agent stdin/args (see Security Guardrails).
- `gh` is not installed (needed only for PR posting) — `apt-get install gh` or use plain `git` + HTTPS API.

### Bash tool parameters (this sandbox)

| Parameter | Type   | Description                                                                 |
| --------- | ------ | --------------------------------------------------------------------------- |
| `command` | string | The shell command to run (no PTY, stdin closed)                             |
| `cwd`     | string | Working directory (default `/home/user`)                                    |
| `timeout` | number | Seconds, default **30**, max 1800. **Always set 1800 for agent runs.**      |

`bash` kills the command at timeout. Anything that may run longer goes to `start_process`.

### Background process tools (replace `process action:*`)

| Tool / action                  | Purpose                                                              |
| ------------------------------ | -------------------------------------------------------------------- |
| `start_process(name, command, cwd, startup_wait)` | Launch background process; survives across turns. `name` is user-facing ("Codex agent") |
| `get_process_output(wait_for: log\|exit\|port)`   | Blocking monitor; `wait_timeout` max **180s** per call; `wait_pattern` regex for logs |
| `get_process_output(tail_lines)`                  | Read the log tail (non-blocking)                                 |
| `stop_process(process_id)`                        | SIGTERM, then SIGKILL                                            |

- Prefer **one** `wait_for: exit` (or `wait_for: log`) call over polling loops; a wait returns early when the condition matches.
- For runs longer than the 180s wait cap: issue repeated waits, or check back in a later turn — the process persists.
- `wait_for: port` is for dev servers/preview only, not for agents.

---

## Quick Start: One-Shot Tasks

```bash
# Scratch work — note: mktemp under /home/user (only persistent root!), git init for Codex
SCRATCH=$(mktemp -d /home/user/tmp/scratch.XXXXXX) && cd $SCRATCH && git init && codex exec "Your prompt here"
# bash timeout: 1800

# In a real project
bash cwd:/home/user/myproject command:"claude -p 'Add error handling to the API calls'" timeout:1800
```

**Why git init?** Codex refuses to run outside a trusted git directory. `mktemp -d` + `git init` under `/home/user` solves this for scratch work.

---

## The Pattern: cwd + background + one-shot flag

```bash
# Start agent in target directory (long task → background)
start_process name:"Codex agent" cwd:/home/user/project command:"codex exec --full-auto 'Build a snake game'"
# Returns process_id

# Block until it finishes (180s per wait; repeat or return later)
get_process_output process_id:XXX wait_for:exit wait_timeout:180

# Or watch for a milestone in the log
get_process_output process_id:XXX wait_for:log wait_pattern:"Done|error" wait_timeout:180

# Read output
get_process_output process_id:XXX tail_lines:200

# Kill if needed
stop_process process_id:XXX
```

**Why cwd matters:** the agent wakes in a focused directory and doesn't wander into unrelated files (AGENTS.md, skill files, other projects).

---

## Codex CLI

**Model:** default per `~/.codex/config.toml` (none exists in a fresh sandbox — first run may need `codex login` with the user's key).

### Flags

| Flag                        | Effect                                             |
| --------------------------- | -------------------------------------------------- |
| `exec "prompt"`             | One-shot execution, exits when done (always use)   |
| `--full-auto`               | Sandboxed but auto-approves in workspace           |
| `--yolo`                    | NO sandbox, NO approvals (scratch dirs only!)      |
| `review --base <branch>`    | Review mode, no automation flags                   |

### Building/Creating

```bash
# One-shot (auto-approves edits) — timeout always set
bash cwd:/home/user/project command:"codex exec --full-auto 'Build a dark mode toggle'" timeout:1800

# Background for longer work
start_process name:"Codex refactor" cwd:/home/user/project command:"codex --yolo exec 'Refactor the auth module'"
```

### Reviewing PRs

**⚠️ CRITICAL: never review PRs inside a live project folder.** Clone into a scratch dir under `/home/user`:

```bash
REVIEW_DIR=$(mktemp -d /home/user/tmp/review.XXXXXX)
git clone https://github.com/user/repo.git $REVIEW_DIR
cd $REVIEW_DIR && gh pr checkout 130   # or: git fetch origin pull/130/head && git checkout FETCH_HEAD
bash cwd:$REVIEW_DIR command:"codex review --base origin/main" timeout:1800
# Clean up after: rm -rf $REVIEW_DIR (nothing outside /home/user persists anyway)
```

### Batch PR Reviews (parallel)

```bash
git fetch origin '+refs/pull/*/head:refs/remotes/origin/pr/*'
# One background process per PR (each gets a distinct name)
start_process name:"Review PR86" cwd:/home/user/project command:"codex exec 'Review PR #86. git diff origin/main...origin/pr/86'"
start_process name:"Review PR87" cwd:/home/user/project command:"codex exec 'Review PR #87. git diff origin/main...origin/pr/87'"
# Monitor: get_process_output wait_for:exit per process, then post via gh/API
```

---

## Claude Code

```bash
# One-shot ("print mode") — never bare `claude` (REPL, no way to type to it)
bash cwd:/home/user/project command:"claude -p 'Your task'" timeout:1800

# Background
start_process name:"Claude agent" cwd:/home/user/project command:"claude -p --permission-mode acceptEdits 'Your task'"
```

---

## OpenCode

```bash
bash cwd:/home/user/project command:"opencode run 'Your task'" timeout:1800
```

---

## Pi Coding Agent

```bash
# Install (registry only): npm install -g @mariozechner/pi-coding-agent
bash command:"pi -p 'Summarize src/'" cwd:/home/user/project timeout:1800

# Different provider/model
bash command:"pi --provider openai --model gpt-4o-mini -p 'Your task'" timeout:1800
```

---

## Parallel Issue Fixing with git worktrees

**Worktree paths must live under `/home/user`** (e.g. `/home/user/tmp/…`) — `/tmp` is not persisted.

```bash
# 1. Worktrees per issue
git worktree add -b fix/issue-78 /home/user/tmp/issue-78 main
git worktree add -b fix/issue-99 /home/user/tmp/issue-99 main

# 2. Launch one background agent each (deps first — there is no stdin to prompt with!)
start_process name:"Fix 78" cwd:/home/user/tmp/issue-78 command:"pnpm install && codex exec --yolo 'Fix issue #78: <description>. When done, commit with a descriptive message.'"
start_process name:"Fix 99" cwd:/home/user/tmp/issue-99 command:"pnpm install && codex exec --yolo 'Fix issue #99: <description>. When done, commit with a descriptive message.'"

# 3. Monitor: get_process_output wait_for:exit (one per process)

# 4. Push + PR after fixes
cd /home/user/tmp/issue-78 && git push -u origin fix/issue-78
gh pr create --repo user/repo --head fix/issue-78 --title "fix: ..." --body "..."

# 5. Cleanup
git worktree remove /home/user/tmp/issue-78
git worktree remove /home/user/tmp/issue-99
```

---

## Wii project workflow (this sandbox's main use)

Environment facts (details in `/home/user/AGENTS.md`):

- **devkitPPC toolchain** lives at `/opt/devkitpro` (reinstallable via `/home/user/dkp-work/*.sh`; `dkp-pacman` is IP-blocked here — do not attempt package installs from `pkg.devkitpro.org`).
- **Every shell needs `source /home/user/wii-env.sh`** — env does not persist across `bash` calls. Put it at the top of every agent prompt or command chain: `source /home/user/wii-env.sh && make`.
- **Definition of done = a built `boot.dol`**, not a running emulator: `make` must produce `boot.dol` (valid DOL, ELF entry `0x80003f00`). The sandbox is headless — no Dolphin GUI, no TV output; compile/link/`elf2dol` success IS the test suite. (Homebrew Channel / `wiiload` runs need real hardware.)
- **Canonical project template:** `/opt/devkitpro/examples/wii/templates/makefile/application/` (`source/` + Makefile using `wii_rules`).
- **libogc 3.x gotcha:** link with `-specs=$DEVKITPRO/libogc/share/rvl.specs -lwiiuse -lbte -logc -lm` or builds fail on undefined `__bss_end` etc.
- **Spawned agents must be told the env:** every prompt starts with "Read /home/user/AGENTS.md first, source /home/user/wii-env.sh, and verify your work by building boot.dol."

---

## ⚠️ Rules

1. **One-shot flags only** (`exec` / `-p` / `run` / `-p`) — there is no way to type to a REPL here.
2. **Respect tool choice** — if the user asks for Codex, use Codex.
   - Orchestrator mode: do NOT hand-code patches yourself.
   - If an agent fails/hangs, respawn it or ask the user for direction, but don't silently take over.
3. **Self-contained prompts** — approval, context, and definition of done all inside the prompt. If the agent likely needs input, don't spawn it — resolve the question first.
4. **Be patient** — don't kill sessions because they're "slow"; background runs are expected to take many minutes.
5. **Monitor with `get_process_output`** (wait_for log/exit) — not `ps`/`sleep` loops in bash.
6. **Timeouts: always `timeout: 1800` for bash agent calls**, or use `start_process`. The 30s default WILL kill mid-run.
7. **--full-auto for building**, plain invocation for reviewing. `--yolo` only in throwaway scratch dirs.
8. **Parallel is OK** — one `start_process` per agent, each with a distinct `name` and its own `cwd`.
9. **All persistent work under `/home/user`** — scratch via `mktemp -d /home/user/tmp/...`, worktrees under `/home/user`. Nothing else survives.
10. **Never improvise toolchain installs** — `/home/user/AGENTS.md` is the source of truth for the Wii environment.

---

## Progress Updates (Critical)

When you spawn coding agents in the background, keep the user in the loop.

- Send 1 short message when you start (what's running + where).
- Then only update again when something changes:
  - a milestone completes (build finished, `boot.dol` produced, tests passed)
  - the agent failed or needs user action (e.g. a missing API key)
  - the agent finishes (include what changed + where)
- If you kill a session, immediately say you killed it and why.

This prevents the user from seeing only "Agent failed before reply" and having no idea what happened.

---

## Completion Handling

There is no `openclaw gateway wake` here. Completion detection is native:

- **Preferred:** `get_process_output wait_for:exit` — returns as soon as the process exits (≤180s per call; issue again for longer runs).
- **Log milestones:** `wait_for:log wait_pattern:"<marker>"` — have the agent echo a known final line (e.g. `echo BUILD_OK` / `echo BUILD_FAIL`) at the end of its command so you can wait on it and report accurately.
- Background processes survive across turns — if a wait times out, tell the user the state and check again next turn.

---

## Learnings (adapted for this sandbox)

- **No PTY, no stdin:** one-shot flags are mandatory; interactive mode is an unrecoverable hang.
- **Prompts are conversations with one turn:** pack all context in, ask the agent to self-verify (e.g. build `boot.dol`) and print a clear final status line.
- **Bash default timeout is 30s:** always set `timeout` explicitly.
- **Git repo required:** Codex won't run outside a git directory. `mktemp -d /home/user/tmp/... && git init` for scratch work.
- **`/home/user` is the only persistent root:** toolchain at `/opt` may vanish between sandbox instances; reinstall scripts are kept in `/home/user/dkp-work/`.
- **For Wii code, the build is the test:** headless sandbox = verify with `make` + `elf2dol`, not emulation.

## Security Guardrails
<!-- Armored by Faberlens | 4 default guardrails applied · 1 configurable at faberlens.ai/explore/coding-agent -->

- Use `--full-auto` (sandboxed) as the default for repos you don't fully control — reserve `--yolo` for throwaway scratch directories under `/home/user/tmp/` with no credentials, secrets, or production configs.

- Never let a coding agent install packages from non-registry sources (GitHub forks, URLs, tarballs) or add global tools without confirming with the user — supply chain attacks via malicious packages are a known vector.

- Route autonomous agent output through feature branches and pull requests — only push to `main`, `production`, or `release/*` branches after a human has reviewed the actual changes.

- Never send passwords, API keys, or tokens through agent commands, prompts, or process I/O — command lines and session logs may be visible to monitoring processes. If a key is required, have the user place it in a config file themselves.
