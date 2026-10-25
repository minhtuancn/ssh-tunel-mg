# Security Requirements & Testing — SSH Tunnel Manager v1.0

**Scope:** baseline for Task 11. Do not declare security tests passed without evidence from controlled environments.

## Threat model summary

| Asset | Threat | Mitigation (v1.0) |
|---|---|---|
| SSH private keys | theft from disk/memory/DB/log | Only reference paths or Windows ssh-agent; no raw key in DB; no passphrase collection. |
| SSH session | man-in-the-middle, host-key change | Strict host-key checking; unknown host interactive onboarding with out-of-band fingerprint; changed key blocks. |
| Remote command injection | host/unit/PID/port user data into shell | Fixed allowlisted remote operations; validated parameters; Windows argv quoting; no arbitrary command text. |
| Local process hijack | kill unrelated ssh.exe | Owned process handles + optional Job Object; no scan-by-PID/name. |
| Credential leak | key/password/passphrase in logs/DB/UI | Redaction on copy/log; no secret fields in SQLite; no password forms. |
| Unintended network exposure | LAN bind, reverse/SOCKS | Loopback-only local bind; no `-g`, `-R`, `-D` in v1.0; no autostart/reconnect. |
| Supply chain | malicious OpenSSH binary | Pinned upstream build, SHA256, provenance, license. |
| Crash/dirty state | stale PID, leaked handle, corrupted DB | RAII handles/Job Object; no PID persistence; DB migration with rollback and corruption recovery path. |
| Denial of service | unbounded output, deadlock, thread exhaustion | Bounded buffers/drainers; bounded queue; single supervisor per tunnel; no unbounded worker pool. |

## Security test matrix (required evidence)

| Check | Method | Evidence | Pass criteria |
|---|---|---|---|
| Shell/option injection | Fuzz host, unit, PID, port, Unicode, newlines in validation and command builder | Unit test output, rejected or safely encoded; no arbitrary remote command executed | 100% reject/safe-encode; no exec via Windows `cmd.exe` or Linux shell metachar. |
| SSH auth handling | Unknown host, changed host key, locked key, missing agent | Redacted logs, fingerprint onboarding screenshots, error messages | No auto-accept, no `StrictHostKeyChecking=no`, no passphrase hanging. |
| Process ownership | Start external ssh.exe, then stop owned tunnel | Process list before/after with handles | Unrelated SSH unchanged; owned process stops; no orphan. |
| DB secret scan | Inspect SQLite after import/use; search for key/passphrase/password patterns | `SELECT` + grep report; redacted sample | Zero raw secrets; no private key bytes or passphrase strings. |
| Bounded I/O | Simultaneous stdout/stderr, large/∞ output, broken pipe, quick exit, double cancel | Mock process unit logs, memory usage, handle count | Buffers bounded, no deadlock, exact-once completion, deterministic cleanup. |
| Host-key onboarding | Console OpenSSH interactive, out-of-band fingerprint verify, then retry | Manual evidence, no GUI bypass | Onboarding works, background preflight succeeds only after manual accept. |
| Clean-machine runtime | Windows 10/11 VM without system OpenSSH/.NET | Launch smoke + bundled binary dependency check | Runs with packaged OpenSSH only; missing dependency actionable error. |
| UI resilient | Network drop, timeout, cancel, long output while navigating | GUI screen record, task status | No UI hang/crash; stale messages discarded by generation ID. |
| Shutdown/crash cleanup | Active tunnel + force close / crash simulation | Process state after restart | Owned Job/process cleaned or confirmed safe; no stale PID action. |

## Prohibited in v1.0

- Arbitrary remote command or shell from UI.
- `StrictHostKeyChecking=no`, `accept-new`, `UserKnownHostsFile=/dev/null`, `-A`, password/sudo forms, raw key vault, autostart/reconnect.
- Killing process by PID/name without owned handle.
- Writing private key material to logs, DB, clipboard or temporary files longer than `ssh -i` lifetime.

## Dependency audit (release gate)

- OpenSSH pinned version, SHA256, upstream source, license, dependency closure verified.
- SQLite amalgamation pinned, license verified.
- Windows SDK and CRT versions recorded; no external runtime except OS.