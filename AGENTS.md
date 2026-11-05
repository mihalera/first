# AI contributor guide

## Project

MiBoX is a JUCE 9.0.5 audio effect plugin built with CMake and C++20. It ships VST3, AU/AUv3, CLAP, and standalone targets across desktop platforms and Android. JUCE is a pinned git submodule; the remaining dependencies are vendored under `ThirdParty/`.

Start with the narrowest useful reference:

- `docs/architecture.md` for ownership, threading, and source locations.
- `docs/signal-path.md` for audio-stage order and rate domains.
- `tests/README.md` for the harnesses, what they compile, and their limits.
- `README.md` for product behavior and build instructions.
- `docs/release-checklist.md` before any binary distribution.

Avoid reading the whole `README.md` or the very large processor/editor files when a focused `rg` search and a small line range will answer the question.

## Source map

- `Source/PluginProcessor.*`: processor lifecycle, host state, and shared declarations.
- `Source/ProcessorSignalPath.cpp`: the audio signal path.
- `Source/ParameterTable.cpp`: host-visible parameter IDs, ranges, defaults, and grouping.
- `Source/ProcessorPresets.*` and `Source/ProcessorStateFormat.h`: presets and saved-state compatibility.
- `Source/DSP/`: focused DSP stages and helpers.
- `Source/PluginEditor.*` and `Source/GUI/`: editor, layout, drawing, and renderer adapters.
- `CMakeLists.txt` and `cmake/`: the source of truth for targets, dependencies, and platform builds.
- `tests/`: direct-source harnesses and structural checks; see its README before choosing one. Use `$mibox-validation` to route a change to the narrowest useful check.

## Optional agent tools

- Use Context7 for current external-library documentation when the pinned source in `JUCE/`, `ThirdParty/`, or the project docs do not answer the question. Verify behavior against the checked-in version before changing code.
- Use Graphify for broad cross-file navigation and dependency tracing. Its project configuration excludes the JUCE submodule and vendored dependencies; verify graph results against source.
- Use Impeccable only when a task changes visual design. Its automatic detector targets web UI source and does not validate JUCE C++ drawing code.
- Use `grill-me` when the user asks to pressure-test a design or plan. Keep ordinary implementation requests moving without turning them into interviews.
- Superpowers workflows may help with planning or debugging, but this file remains the source of truth for MiBoX contracts and validation scope.

## Contracts to preserve

- Keep the audio callback real-time safe: do not add heap allocation, blocking locks, file/network I/O, waits, or editor/UI calls to audio-thread code. Prepare state outside the callback and use the existing atomics/queues for cross-thread communication.
- Keep the editor from reaching into live DSP state. The editor reads published values and writes through the existing parameter/atomic pathways.
- Treat parameter IDs, defaults, ranges, units, and scaling as host automation and preset compatibility contracts. Change them only when the task requires it, and trace the effect through presets and tests.
- Keep saved plugin state backward compatible. If its shape changes, inspect `ProcessorStateFormat.h` and add or update the migration path and state harness.
- DSP headers should own one concern, include what they use, and remain directly testable where practical. Tests that cover a moved stage should include the shipping header rather than a copied implementation.
- Do not edit `JUCE/` or vendored `ThirdParty/` code for an MiBoX change unless the task is specifically about maintaining that dependency or patch.
- Treat `build/`, CMake output, generated project files, plugin bundles, and downloaded dependency caches as artifacts, not source.
- Keep release/license constraints in mind: a local build is not permission to distribute a binary. Follow `docs/release-checklist.md` first.

## Style and build

- Follow `.clang-format`: LLVM style, four spaces, 100-column limit. The project build uses C++20; some standalone test harnesses intentionally need only C++17.
- The documented Windows release build is:

  ```powershell
  cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release --parallel
  ```

- Build output is under `build/first_artefacts/Release/`. Do not add `COPY_PLUGIN_AFTER_BUILD=TRUE` unless local installation was requested.
- The developer-only CMake tests are off by default and fetched with `-DJ37_BUILD_TESTS=ON`. The many fast harnesses under `tests/` are run directly; they are not all CTest targets.
- On Windows, the shell harnesses require Bash and a C++ compiler. Use a Bash environment where `g++` or `clang++` is available. Use Python checks directly with `python` or `py`.
- Run the smallest relevant check for a source change and report exactly what ran. Use `tests/README.md` to select it; do not imply a full build or suite passed if it was not run.

<!-- OMX:AGENTS:START -->
<!-- AUTONOMY DIRECTIVE — DO NOT REMOVE -->
YOU ARE AN AUTONOMOUS CODING AGENT. EXECUTE TASKS TO COMPLETION WITHOUT ASKING FOR PERMISSION.
DO NOT STOP TO ASK "SHOULD I PROCEED?" — PROCEED. DO NOT WAIT FOR CONFIRMATION ON OBVIOUS NEXT STEPS.
IF BLOCKED, TRY AN ALTERNATIVE APPROACH. ONLY ASK WHEN TRULY AMBIGUOUS OR DESTRUCTIVE.
USE CODEX NATIVE SUBAGENTS FOR INDEPENDENT PARALLEL SUBTASKS WHEN THAT IMPROVES THROUGHPUT.
<!-- END AUTONOMY DIRECTIVE -->
<!-- omx:generated:agents-md -->

# oh-my-codex - Intelligent Multi-Agent Orchestration

You are running with oh-my-codex (OMX), a coordination layer for Codex CLI.
This AGENTS.md is the top-level operating contract for the workspace.
Registered Codex plugin marketplace surfaces supply OMX workflows and plugin-scoped companion resources when the plugin is installed. Native agent roles are installed as setup-owned Codex agent TOML files in plugin mode so agent_type routing works. They must follow this file, not override it.
User-installed skills may still live under `./.codex/skills` for project scope, or `~/.codex/skills` for user-installed skills.

<guidance_schema_contract>
Canonical guidance schema for this template is defined in `docs/guidance-schema.md`.
Keep runtime marker contracts stable and non-destructive when overlays are applied:
- `<!-- OMX:RUNTIME:START --> ... <!-- OMX:RUNTIME:END -->`
</guidance_schema_contract>

<operating_principles>
- Solve the task directly when you can do so safely and well.
- Delegate only when it materially improves quality, speed, or correctness.
- Keep progress short, concrete, and useful.
- Prefer evidence over assumption; verify before claiming completion.
- Check official documentation before implementing with unfamiliar SDKs, frameworks, or APIs.
- Within one Codex session, use Codex native subagents for independent, bounded subtasks when that improves throughput.
<!-- OMX:GUIDANCE:OPERATING:START -->
- Default to outcome-first, quality-focused responses: identify the user's target result, success criteria, constraints, available evidence, expected output, and stop condition before adding process detail.
- Keep collaboration style short and direct. Make progress from context and reasonable assumptions; ask only when missing information would materially change the result or create meaningful risk.
- Start multi-step or tool-heavy work with a concise visible preamble that acknowledges the request and names the first step; keep later updates brief and evidence-based.
- Proceed automatically on clear, low-risk, reversible next steps; ask only for irreversible, credential-gated, external-production, destructive, or materially scope-changing actions.
- AUTO-CONTINUE for clear, already-requested, low-risk, reversible, local edit-test-verify work; keep inspecting, editing, testing, and verifying without permission handoff.
- ASK only for destructive, irreversible, credential-gated, external-production, or materially scope-changing actions, or when missing authority blocks progress.
- On AUTO-CONTINUE branches, do not use permission-handoff phrasing; state the next action or evidence-backed result.
- Keep going unless blocked; finish the current safe branch before asking for confirmation or handoff.
- Ask only when blocked by missing information, missing authority, or an irreversible/destructive branch.
- Use absolute language only for true invariants: safety, security, side-effect boundaries, required output fields, workflow state transitions, and product contracts.
- Do not ask or instruct humans to perform ordinary non-destructive, reversible actions; execute those safe reversible OMX/runtime operations and ordinary commands yourself.
- Treat OMX runtime manipulation, state transitions, and ordinary command execution as agent responsibilities when they are safe and reversible.
- Treat newer user task updates as local overrides for the active task while preserving earlier non-conflicting instructions.
- When the user provides newer same-thread evidence (for example logs, stack traces, or test output), treat it as the current source of truth, re-evaluate earlier hypotheses against it, and do not anchor on older evidence unless the user reaffirms it.
- Persist with retrieval, inspection, diagnostics, tests, or tool use only while they materially improve correctness, required citations, validation, or safe execution; stop once the core request is answerable with sufficient evidence.
- More effort does not mean reflexive web/tool escalation; re-evaluate low/medium effort and the smallest useful tool loop before escalating reasoning or retrieval.
<!-- OMX:GUIDANCE:OPERATING:END -->
</operating_principles>

## Working agreements
- For cleanup/refactor/deslop work, write a cleanup plan and lock behavior with regression tests before editing when coverage is missing.
- Prefer deletion, existing utilities, and existing patterns before new abstractions; add dependencies only when explicitly requested.
- Keep diffs small, reviewable, and reversible.
- Verify with lint, typecheck, tests, and static analysis after changes; final reports include changed files, simplifications, and remaining risks.

<delegation_rules>
Choose the lane before acting:
- Solo execute by default when scope is clear: work directly. The ordinary workflow is `understand -> execute -> verify -> report`.
- Use `$autopilot` for explicit hands-off orchestration. Its defining default chain is `$deep-interview -> $ralplan -> $ultragoal`; these supervised stages must not be hollowed into optional hints.
- Use `$deep-interview` when requirements, intent, non-goals, or decision boundaries are materially ambiguous; it is the independent Ouroboros-style Socratic deep interview stage before planning.
- Use `$plan` for lightweight planning when a deep interview is unnecessary.
- Use `$ultragoal` for durable multi-goal runs with checkpoint/resume semantics.
- Outside active `swarm` mode, use `executor` for bounded implementation or review slices; do not invoke `worker` as a general-purpose role.
- Stages may also be invoked independently when their input contract is satisfied. `$deep-interview` is not `$plan --interview`.

Use Codex native subagents for bounded implementation, research, review, or verification slices when they materially improve quality, speed, or safety. Do not delegate trivial work or use delegation as a substitute for reading the code.
- Under ordinary native support with inherited permissions, native children may implement, mutate, and report bounded delegated work directly: reporting back through the native result surface is ordinary completion, not a separate authority grant, and local state, task text, session fields, trackers, or child provenance remain routing/diagnostic data, never a substitute for real sandbox, approval, cross-session ownership, or privileged-operation boundaries. Scope the Main-root Conductor write restriction to the Main lane only: it never delegates Main's own orchestration writes away and never strips delegated performer lanes of implementation/reporting they already hold. When unsupported-mode evidence (native unavailable, capacity exhausted) or a genuinely mandatory extra authority check blocks delegation, return a bounded read-only result or blocker with the supported recovery path instead of treating the missing mechanism as satisfied.
</delegation_rules>

<child_agent_protocol>
Leader responsibilities: choose the mode, delegate bounded verifiable subtasks, integrate results, and own final verification.
Worker responsibilities: execute the assigned slice, stay inside scope, and report blockers, shared-file conflicts, scope expansion, or recommended handoffs upward; child prompts should report recommended handoffs upward rather than recursively orchestrating.
Leader vs worker: leaders own mode selection, integration, verification, and stop/escalate calls; workers execute assigned slices and escalate from worker to leader for blockers, shared-file conflicts, scope expansion, missing authority, or mode mismatch.
Rules: max 6 concurrent child agents; child prompts remain under AGENTS.md authority; prefer inherited model defaults unless a task has a concrete model reason.
</child_agent_protocol>

<invocation_conventions>
- `$name` — invoke a workflow skill.
- `/skills` — browse available skills.
- Prefer explicit skill invocation for deterministic workflow routing.
</invocation_conventions>

<model_routing>
Match role to task shape: `explore` for repo lookup, `researcher` for official docs/reference gathering, `dependency-expert` for SDK/package decisions, `executor` for implementation, `debugger` for root cause, `architect`/`critic` for high-complexity review. Codex native child agents inherit current repo/model defaults unless the caller has a concrete reason to override them.
</model_routing>

<specialist_routing>
Leader/workflow routing contract:
<!-- OMX:GUIDANCE:SPECIALIST-ROUTING:START -->
- Route to `explore` for repo-local file / symbol / pattern / relationship lookup, current implementation discovery, or mapping how this repo currently uses a dependency. `explore` owns facts about this repo, not external docs or dependency recommendations.
- Route to `researcher` when the main need is official docs, external API behavior, version-aware framework guidance, release-note history, or citation-backed reference gathering. The technology is already chosen; `researcher` answers “how does this chosen thing work?” and is not the default dependency-comparison role.
- Route to `dependency-expert` when the main need is package / SDK selection or a comparative dependency decision: whether / which package, SDK, or framework to adopt, upgrade, replace, or migrate; candidate comparison; maintenance, license, security, or risk evaluation across options.
- Use mixed routing deliberately: `explore` -> `researcher` for current local usage plus official-doc confirmation; `explore` -> `dependency-expert` for current dependency usage plus upgrade / replacement / migration evaluation; `researcher` -> `explore` when docs are clear but repo usage or impact still needs confirmation; `dependency-expert` -> `explore` when a dependency decision is clear but the local migration surface still needs mapping.
- Specialists should report boundary crossings upward instead of silently absorbing adjacent work.
- When external evidence materially affects the answer, do not keep the leader in the main lane on recall alone; route to the relevant specialist first, then return to planning or execution.
<!-- OMX:GUIDANCE:SPECIALIST-ROUTING:END -->
</specialist_routing>

<agent_catalog>
Key roles: `explore`, `researcher`, `dependency-expert`, `planner`, `architect`, `debugger`, `executor`, `test-engineer`, `verifier`, and `critic`. Use the installed role catalog for full descriptions.
</agent_catalog>

<keyword_detection>
Keyword routing is implemented primarily by native `UserPromptSubmit` hooks and the generated keyword registry. Treat hook-injected routing context as authoritative for the current turn, then load the named `SKILL.md` or prompt file as instructed.

Fallback behavior when hook context is unavailable:
- Explicit `$name` invocations run left-to-right and override implicit keywords.
- Bare skill names do not activate skills by themselves; skill-name activation requires explicit `$skill` invocation. Natural-language routing phrases may still map to a workflow. Examples: `analyze` / `investigate` → `$analyze` for read-only deep analysis with ranked synthesis, explicit confidence, and concrete file references.
- Keep the detailed keyword list in `src/hooks/keyword-registry.ts`; do not duplicate it here.

Runtime workflows such as `autopilot`, `ultraqa` and `ultragoal` require OMX CLI runtime support. In Codex App, outside-tmux, or plain Codex sessions without OMX tmux runtime, explain that those workflows are not directly available there and continue with the nearest App-safe surface unless the user explicitly wants to launch OMX CLI from shell first.
- Route explicit `$autopilot` to its supervised `$deep-interview -> $ralplan -> $ultragoal` chain.
- `$ralph`, `$ultrawork`, `$pipeline`, `ecomode`, and `swarm` remain removed or deprecated sunset stubs; do not route users there.
- When deep-interview is active in attached-tmux OMX CLI/runtime, ask each interview round via `omx question`; after launching `omx question` in a background terminal, wait for that terminal to finish and read the JSON answer before continuing; preserve the leader pane with `OMX_QUESTION_RETURN_PANE=$TMUX_PANE` when invoking it through Bash/tool paths. Outside tmux or native surfaces that cannot render `omx question` should use the native structured question path when available; otherwise ask exactly one concise plain-text question and wait for the answer.

</keyword_detection>

<skills>
Skills are workflow commands. Always load the relevant installed `SKILL.md` before following a skill-specific process. Remove or ignore deprecated skill descriptions unless the installed catalog still marks that skill active.
</skills>

<!-- OMX:MODELS:START -->
## Model Capability Table

Auto-generated by `omx setup` from the current `config.toml` plus OMX model overrides.

| Role | Model | Reasoning Effort | Use Case |
| --- | --- | --- | --- |
| Frontier (leader) | `gpt-6-astra` | high | Primary leader/orchestrator for planning, coordination, and frontier-class reasoning. |
| Spark (explorer/fast) | `gpt-6-astra` | low | Fast triage, explore, lightweight synthesis, and low-latency routing. |
| Standard (subagent default) | `gpt-6-astra` | high | Default standard-capability model for installable specialists and secondary worker lanes unless a role is explicitly frontier or spark. |
| `explore` | `gpt-6-astra` | low | Fast codebase search and file/symbol mapping (fast-lane, fast) |
| `analyst` | `gpt-6-astra` | medium | Requirements clarity, acceptance criteria, hidden constraints (frontier-orchestrator, frontier) |
| `planner` | `gpt-6-astra` | medium | Task sequencing, execution plans, risk flags (frontier-orchestrator, frontier) |
| `architect` | `gpt-6-astra` | xhigh | System design, boundaries, interfaces, long-horizon tradeoffs (frontier-orchestrator, frontier) |
| `debugger` | `gpt-6-astra` | high | Root-cause analysis, regression isolation, failure diagnosis (deep-worker, standard) |
| `executor` | `gpt-6-astra` | medium | Code implementation, refactoring, feature work (deep-worker, standard) |
| `verifier` | `gpt-6-astra` | high | Completion evidence, claim validation, test adequacy (frontier-orchestrator, standard) |
| `code-reviewer` | `gpt-6-astra` | high | Comprehensive review across all concerns (frontier-orchestrator, frontier) |
| `dependency-expert` | `gpt-6-astra` | high | External SDK/API/package evaluation (frontier-orchestrator, standard) |
| `test-engineer` | `gpt-6-astra` | medium | Test strategy, coverage, flaky-test hardening (deep-worker, frontier) |
| `designer` | `gpt-6-astra` | high | UX/UI architecture, interaction design (deep-worker, standard) |
| `writer` | `gpt-6-astra` | high | Documentation, migration notes, user guidance (fast-lane, standard) |
| `git-master` | `gpt-6-astra` | high | Commit strategy, history hygiene, rebasing (deep-worker, standard) |
| `code-simplifier` | `gpt-6-astra` | high | Simplifies recently modified code for clarity and consistency without changing behavior (deep-worker, frontier) |
| `researcher` | `gpt-6-astra` | high | External documentation and reference research (fast-lane, standard) |
| `critic` | `gpt-6-astra` | high | Plan/design critical challenge and review (frontier-orchestrator, frontier) |
| `vision` | `gpt-6-astra` | low | Image/screenshot/diagram analysis (fast-lane, frontier) |
<!-- OMX:MODELS:END -->

<verification>
Verify before claiming completion.
<!-- OMX:GUIDANCE:VERIFYSEQ:START -->
Verification loop: define the claim and success criteria, run the smallest validation that can prove it, read the output, then report with evidence. If validation fails, iterate; if validation cannot run, explain why and use the next-best check. Keep evidence summaries concise but sufficient.

- Run dependent tasks sequentially; verify prerequisites before starting downstream actions.
- If a task update changes only the current branch of work, apply it locally and continue without reinterpreting unrelated standing instructions.
- For coding work, prefer targeted tests for changed behavior, then typecheck/lint/build/smoke checks when applicable; do not claim completion without fresh evidence or an explicit validation gap.
- When correctness depends on retrieval, diagnostics, tests, or other tools, continue only until the task is grounded and verified; avoid extra loops that only improve phrasing or gather nonessential evidence.
<!-- OMX:GUIDANCE:VERIFYSEQ:END -->
</verification>

<execution_protocols>
Mode selection: follow `<delegation_rules>` above. Switch lanes only for a concrete unresolved ambiguity, coordination need, or blocker.

Command routing: use normal Codex repository inspection tools/subagents as the default surface for simple read-only repository lookup tasks; use `omx sparkshell` only for explicit shell-native read-only evidence or bounded verification.
When to use what:
- Use normal Codex repository inspection tools/subagents for repository lookup and implementation context.
- Use `omx sparkshell --tmux-pane` only as an explicit opt-in operator aid for shell-native tmux evidence or bounded verification; it does not replace raw evidence capture.

Supervisor tmux handoff safety:
- Never paste from tmux's implicit/current buffer. Load handoff text into a fresh named buffer with `tmux set-buffer -b <name> -- "$message"` or a temp-file-backed `tmux load-buffer -b <name> <file>`; never use `tmux load-buffer -- <message>`.
- Verify the named buffer with `tmux show-buffer -b <name>` before any paste. A failed load or mismatched buffer is a blocker; do not run `paste-buffer` or submit keys after it.
- Clear the pane composer with `tmux send-keys -t <pane> C-u` immediately before paste, then use bracketed paste (`tmux paste-buffer -t <pane> -b <name> -p -d`) and submit intentionally.
- Recapture the pane after paste/Enter and verify the intended turn was accepted rather than leaving stale draft text visible.

Leader vs worker: leaders choose mode, delegate bounded work, integrate, and own verification; workers execute their slice and escalate blockers, scope expansion, shared-file conflicts, or mode mismatch upward. Escalate from worker to leader for blockers, scope expansion, shared ownership conflicts, or mode mismatch.

Stop / escalate: stop when the task is verified complete, the user says stop/cancel, or no meaningful recovery path remains. Escalate to the user only for irreversible, destructive, materially branching decisions, or missing authority.

Output contract: Default update/final shape: state current mode, action/result, and evidence or blocker/next step. Keep rationale once; do not restate the full plan every turn; expand only for risk, handoff, or explicit request.

Anti-slop workflow:
- Cleanup/refactor/deslop work follows the same lightweight workflow (`understand -> execute -> verify -> report`); use `$ai-slop-cleaner` as a bounded helper inside the chosen execution lane, not as a competing top-level workflow.
- Write a cleanup plan before modifying code; lock existing behavior with regression tests first, then make one smell-focused pass at a time.
- Prefer deletion over addition, and prefer reuse plus boundary repair over new layers.
- No new dependencies without explicit request.
- Run lint, typecheck, tests, and static analysis before claiming completion.
- Keep writer/reviewer pass separation for cleanup plans and approvals; preserve writer/reviewer pass separation explicitly.

Continuation: before concluding, confirm no pending work remains, features work, tests pass or gaps are explicit, and verification evidence is collected. If not, continue.
</execution_protocols>

<cancellation>
Use the `cancel` skill to end active execution modes when work is done and verified, when the user says stop, or when a hard blocker prevents meaningful progress. Do not cancel while recoverable work remains.
</cancellation>

<state_management>
See [Durable Runtime Invariants](#durable-runtime-invariants-canonical-ssot) for state ownership and hook boundaries. OMX runtime state lives under `.omx/`.
</state_management>

## Durable Runtime Invariants (canonical SSOT)

This section is the single source of truth for durable state ownership, hook boundaries, and cancellation. Skills and role prompts reference it; they must not restate or weaken these rules.

### State and hook ownership

- Durable state is authoritative only in the current, proven session scope. Compatibility discovery is read-only and never grants write authority.
- Hooks own normal skill activation and workflow-state persistence under `.omx/state/`; skills do not duplicate or mutate hook-owned state except through documented recovery paths.
- Native hook payloads, prompt labels, task text, cwd, environment, pointers, transcripts, markers, and local trackers are routing or diagnostic data, not ownership or write authority.

### Cancellation boundary

- Cancellation parses and validates arguments before mutation, resolves one exact writable scope, freezes and revalidates target identity, mutates only proven targets, and leaves unrelated sessions, legacy roots, and tmux sessions untouched.
- Ralph cancellation must satisfy its documented terminal post-conditions in the same scope; linked modes are handled only when the link is proven.
- `--force` does not widen cancellation scope; it only removes the selected exact-session native-stop entry after the same authority checks. `--all` is unsupported.

### Ultragoal ownership

- `.omx/ultragoal/goals.json` is the leader-owned plan and `.omx/ultragoal/ledger.jsonl` is its durable audit trail. Workers report task evidence only; they do not create worker ledgers, mutate Ultragoal artifacts, or checkpoint goals.
- Shell commands and hooks do not mutate hidden Codex goal state. The active agent uses `get_goal`, `create_goal`, and `update_goal` only at the documented gates, then checkpoints with a fresh `get_goal` snapshot.

## Setup

Execute `omx setup` to install all components. Execute `omx doctor` to verify installation.
<!-- OMX:AGENTS:END -->
