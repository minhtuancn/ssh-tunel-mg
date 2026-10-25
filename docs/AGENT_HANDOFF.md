# Agent Handoff — SSH Tunnel Manager

## Goal

Deliver native C++17 Win32 x64 SSH tunnel manager for Windows 10/11. Product source of truth: `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design-v2.md`. Detailed tasks and A01–A18 baseline: `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32-v2.md`.

## Verified workspace state (2026-10-25)

- Workspace has C++17 Win32 skeleton (`CMakeLists.txt`, `src/main.cpp`), validators/Win32 argv quoting/Linux fixed-command builders, CTest, `build.ps1`, and `.gitignore`; four legacy batch files and planning docs retained.
- Git repo remote: `https://github.com/minhtuancn/ssh-tunel-mg.git`; public repo was empty, no issues/PRs/workflows. Current branch `feature/win32-foundation`; local baseline commit `74d8954`; `main` pushed to remote.
- Tools observed: CMake 4.2.1, Ninja 1.13.2, system OpenSSH 9.5p2, GitHub CLI authenticated. VS Build Tools 17.14.42 installed, but no `cl.exe` and `vswhere` does not find `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`; MSVC build is BLOCKED/NOT VERIFIED.
- MinGW Ninja Release build succeeds locally and GUI process smoke confirms window title `SSH Tunnel Manager`; this does not satisfy required MSVC build.
- SSH `dev@10.20.10.103` verified reachable with `BatchMode=yes` + `StrictHostKeyChecking=yes` (prints `SSH_OK`). Remote is Linux with `ss`, `systemd`; read-only probes pass. Not yet verified for destructive/systemd/process integration.
- MSVC blocker root cause: VCTools workload/component never installed (`SelectionState: NotSelected`, `RequestedState: Absent`); installer elevation attempt from non-admin session returned exit 1 and produced no new setup logs.
- Linux host reachable, but target control/disposable status not confirmed; destructive mutations remain BLOCKED.
- No implementation task is DONE. See `docs/implementation-status.md` for evidence/status.
- Credential: token-bearing `openclaw-126 info.txt` is absent from workspace, never tracked/staged/committed; Git history search found no matching file. Token validity/revocation cannot be checked without provider access. Owner must revoke/rotate token; do not assume it is safe.

## Architecture decisions

- v1.0 auth: SSH config/key file/agent only; no raw-key DB vault or passphrase form. See `docs/adr/002-auth-sources.md`.
- Bundle pinned OpenSSH with dependencies/notices; no runtime download. See `docs/adr/001-openssh-packaging.md`.
- Strict host-key verification; changed host key blocks. Console onboarding requires out-of-band fingerprint verification.
- Linux arbitrary shell, automatic sudo, UDP forwarding, non-loopback bind, autostart/reconnect are out of v1.0.
- Worker threads own SSH process/pipe; UI receives typed generation-tagged messages. Tunnel runtime tracks process/listener/target independently.

## Next steps

1. Continue T3: current synchronous process runner is a prototype; implement async pipe draining, cancellation and ownership cleanup, then verify on MSVC.
2. Maintain branch/PR flow: current branch `feature/ssh-process-runner`; open PR to `main`, merge only with green tests.
3. Establish MSVC x64 environment (admin Visual Studio Installer: add `Microsoft.VisualStudio.Workload.VCTools` + x64 compiler + Windows SDK) and a controlled/disposable Linux SSH integration host.
4. Run authoritative build/tests with MSVC once available; record compiler/SDK versions in docs.
5. Follow the task dependencies, no overlapping file ownership for parallel agents, update status and evidence after every task.

Current milestone: PR #1 (`5b0e830`, merge `8481e6c`) and PR #2 (`d5daf8a`, merge `97020df`) merged; PR #3 docs merge `d4f2326`. T2 regression test failed first for `*.service`, then passed after rejecting shell expansions. T3 synchronous process prototype committed `c0f98b2` on branch `feature/ssh-process-runner`; 2/2 CTest pass, NOT DONE. Async lifecycle remains.

## Commands

Once a Windows/MSVC project exists:

- `.[0mbuild.ps1 -Configure`
- `.[0mbuild.ps1 -Build`
- `.[0mbuild.ps1 -Test`
- `ctest --test-dir build -C Release --output-on-failure`

These commands are planned, not currently runnable; no `build.ps1` or build directory exists.

## Reporting rules

For each task report changed files, exact commands/environment, build/test status, GUI/integration evidence, security findings, blockers and next task. Mark unavailable checks BLOCKED/NOT VERIFIED. Do not claim pass from source inspection. Never copy secrets into notes, logs, issues or test artifacts.
