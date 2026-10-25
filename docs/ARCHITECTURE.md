# Architecture — SSH Tunnel Manager v1.0

**Status:** proposed architecture from Product Design v2.0; implementation not present or verified.

## Module boundaries

| Module | Responsibility | Must not |
|---|---|---|
| `src/app.*`, `src/ui/*` | Win32 controls, navigation, layout, confirmation, UI state projection. | Run SSH/SQLite/network work on UI thread; parse server output in handlers. |
| `src/domain/*` | Typed profiles, port/service/process models, validation, tunnel state. | Own Win32 handles, persist live state. |
| `src/ssh/*` | Build argv and own OpenSSH processes/pipes/jobs; auth preflight. | Use `cmd.exe`, PowerShell, arbitrary command text, or kill unowned process. |
| `src/linux/*` | Fixed allowlisted commands and bounded parsers for `ss`, systemd, process metadata. | Provide arbitrary shell feature or implicit sudo. |
| `src/storage/*` | SQLite schema/migrations and profiles/presets/settings/history. | Store raw private keys/passwords/passphrases or live PID/handles. |
| `src/core/*` | Bounded task scheduling, cancellation, diagnostics/redaction, RAII handles. | Unbounded worker creation or retain unbounded output. |
| `resources/openssh/*` | Pinned x64 OpenSSH binary, runtime dependency closure, provenance, notices. | Download binary at runtime. |

## Data flow

1. UI event validates typed user input and creates immutable request with request ID/generation.
2. Bounded worker queue selects a short SSH task or a long-lived tunnel supervisor; per-host concurrency limits prevent query floods without blocking tunnel ownership.
3. SSH runner invokes absolute packaged `ssh.exe` with explicit application path and Windows argv quoting; SSH config/agent resolve supported identity settings. SSH remote command is fixed-operation only and uses validated/quoted arguments.
4. Pipe readers drain stdout/stderr concurrently into bounded buffers. Completion reports exit code, timeout, cancellation, output truncation and classified error.
5. Typed result is posted to UI with `WM_APP`; UI rejects stale generation and is sole owner of control mutation.
6. Read/write profile metadata uses SQLite on worker thread; transaction/versioned migration; DPAPI raw-key vault is explicitly deferred.

## Authentication boundary

- `ssh_config` aliases stay intact so OpenSSH can resolve HostName, User, Port, IdentityFile and ProxyJump.
- Key file or Windows agent is supported. Background operation uses BatchMode; passphrase prompt is not collected by redirecting standard input.
- Unknown host uses explicit OpenSSH console onboarding plus out-of-band fingerprint verification. Changed key blocks. No auto-accept, `StrictHostKeyChecking=no`, `-A`, or password persistence.
- SSH binary and its dependencies are pinned, verified and packaged with license notices.

## Tunnel lifecycle

`Stopped -> Starting -> Listening | Error`; while running, `ssh_state`, `listener_state`, and `target_check` remain independent. `Healthy` is only allowed after an explicitly configured end-to-end protocol probe passes; a live ssh process, `ExitOnForwardFailure=yes`, or local listener is not proof of remote target reachability.

Each runtime tunnel owns process and optional Job Object handles; runtime PID/handles are never restored from SQLite. Bind remains `127.0.0.1`; TCP forwarding only. Start uses `-N -T`, `ExitOnForwardFailure=yes`, bounded keepalives/timeouts and no `-f`. Pre-bind check improves feedback, but actual bind failure remains authoritative to handle races. Stop affects only owned process. Graceful local close has bounded wait; force stop requires explicit confirmation. Job Object assignment is best effort and must be tested under nested-job policies; owned-handle cleanup remains fallback.

## Linux operation boundary

- Port discovery uses fixed `ss` queries with controlled locale and bounded output; unavailable owner/PID is represented as unavailable, not zero/empty.
- systemd list/detail uses `systemctl` machine-readable properties; explicit system/user scope. Only start/stop/restart of validated `.service` units; no implicit sudo.
- Process actions require fresh PID + owner + start identity where available; reject PID 1 and stale/insufficient identity. TERM and KILL are separate; KILL requires strong confirmation. PID reuse race cannot be made atomic and must be disclosed.
- Cancellation after remote mutation dispatch reports uncertain outcome and triggers refresh; it cannot promise server-side rollback.

## Persistence and single-instance policy

SQLite at `%LOCALAPPDATA%\SSHTunnelManager\data.db`; foreign keys and versioned transactional migrations. Persist profiles, tunnel presets, favorites, preferences, bounded/redacted activity. Never persist live process identity as authority. Multiple app instances are prevented or coordinated by a named mutex so DB writes and UI ownership are unambiguous; never terminate another instance's tunnel.

## UI/error handling and shutdown

Win32 Common Controls, per-monitor DPI v2, resizable layout, keyboard navigation, Vietnamese resources. Separate empty/loading/error/no-permission/not-connected states. Indeterminate progress uses named phase. Every worker event carries request/generation ID. WM_CLOSE with active tunnel offers Stop Owned Tunnels & Exit or Cancel; pending remote mutation reports uncertain result. Stale completions are discarded, and all handles use RAII.

## Key risks / decisions

- OpenSSH Windows packaging and agent behavior require clean-machine evidence.
- Console host-key onboarding experience must be implemented and manually verified; no unsafe GUI shortcut.
- Win32 pipe readers must drain both streams concurrently; otherwise subprocess can deadlock.
- Linux `ss` and `/proc` visibility depends on permissions and tool version.
- The endpoint may be down while SSH forwarding listener exists; UI must preserve Unknown/Failed distinction.
- SQLite migration/corruption behavior, UTF-16 paths, high DPI and Job Object constraints require Windows tests.
