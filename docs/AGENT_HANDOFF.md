# Agent Handoff — SSH Tunnel Manager

## Goal

Deliver native C++17 Win32 x64 SSH tunnel manager for Windows 10/11. Product source of truth: `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design-v2.md`. Detailed tasks and A01–A18 baseline: `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32-v2.md`.

## Verified workspace state (2026-10-25)

- Workspace contains four batch files, an `openclaw-126 info.txt`, v1 spec/plan, and v2 spec/plan only. No C++ source, CMake project, README, tests, CI workflows or package.
- Workspace is **not a Git repository**. No branch, commit, PR, issue mapping, CI state or safe commit available here.
- Tools observed: CMake 4.2.1, Ninja 1.13.2, system OpenSSH 9.5p2, GitHub CLI authenticated. No `cl.exe` or `msbuild.exe` found; Windows/MSVC build is BLOCKED/NOT VERIFIED.
- No Linux integration host configured; real SSH/service/process tests are BLOCKED.
- No implementation task is DONE. See `docs/implementation-status.md` for evidence/status.
- `openclaw-126 info.txt` includes a token URL. Do not reproduce it. Owner must revoke/rotate token and redact/remove sensitive artifact.

## Architecture decisions

- v1.0 auth: SSH config/key file/agent only; no raw-key DB vault or passphrase form. See `docs/adr/002-auth-sources.md`.
- Bundle pinned OpenSSH with dependencies/notices; no runtime download. See `docs/adr/001-openssh-packaging.md`.
- Strict host-key verification; changed host key blocks. Console onboarding requires out-of-band fingerprint verification.
- Linux arbitrary shell, automatic sudo, UDP forwarding, non-loopback bind, autostart/reconnect are out of v1.0.
- Worker threads own SSH process/pipe; UI receives typed generation-tagged messages. Tunnel runtime tracks process/listener/target independently.

## Next steps

1. Confirm/canonicalize repository root and Git remote. Current directory is not a repository, so do not create branches/issues or claim CI status.
2. Reconcile v2 snapshot names with canonical paths requested by product; preserve old v1 docs until promotion is approved.
3. Establish MSVC x64 environment and controlled Linux SSH integration host.
4. Begin Task 1 only after T0 contract/docs acceptance; report exact build/test output. T1 remains blocked by toolchain in this session.
5. Follow the task dependencies, no overlapping file ownership for parallel agents, update status and evidence after every task.

## Commands

Once a Windows/MSVC project exists:

- `.[0mbuild.ps1 -Configure`
- `.[0mbuild.ps1 -Build`
- `.[0mbuild.ps1 -Test`
- `ctest --test-dir build -C Release --output-on-failure`

These commands are planned, not currently runnable; no `build.ps1` or build directory exists.

## Reporting rules

For each task report changed files, exact commands/environment, build/test status, GUI/integration evidence, security findings, blockers and next task. Mark unavailable checks BLOCKED/NOT VERIFIED. Do not claim pass from source inspection. Never copy secrets into notes, logs, issues or test artifacts.
