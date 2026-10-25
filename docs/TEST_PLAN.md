# Test Plan — SSH Tunnel Manager v1.0

**Status:** assertion-based CTest baseline is active. `self_check` exercises numeric validation, host/unit validation, Win32 argv quoting, and Linux fixed-command builders (ports/services/processes; validates untrusted unit/PID/SSH-target params). MinGW run passes; release still requires evidence from MSVC x64 Windows and controlled Linux environments.

## Traceability

| Spec | Plan task | Test IDs | Evidence | Release gate |
|---|---|---|---|---|
| Authentication/SSH trust | T3,T4,T11 | A01–A03, A13, plus expanded cases below | Redacted logs + screenshots/test report | G2/G4 |
| Ports and protocols | T2,T6,T10,T11 | A04,A05 | Fixture results + Linux host capture | G3/G4 |
| Tunnel forwarding/state | T2,T3,T5,T10,T11 | A06–A10,A13,A15 | End-to-end client traffic, state screenshots, owned-process evidence | G2/G4 |
| systemd/process management | T2,T7,T8,T10,T11 | A11,A12,A17 | Disposable Linux test-unit/process output, confirmation screenshot | G3/G4 |
| Persistence/security | T9,T11 | A14,A17 | Migration output, sanitized DB scan | G3/G4 |
| Windows UI/package | T1,T10,T11,T12 | A15,A16,A18 | Windows build logs, clean VM report, screenshots | G1/G4 |

## A01–A18 acceptance matrix

For all tests, record exact build SHA, OS/version/architecture, app version, test date, sanitized logs/screenshots, actual result and PASS/FAIL/BLOCKED. Test host must be disposable and under operator control. Destructive systemd/process tests must never target production.

| ID | Requirement / preconditions | Input and execution | Expected / negative case | Automation / evidence / gate |
|---|---|---|---|---|
| A01 | Trusted host, supported key/agent, known_hosts entry | Test Connection with profile and run read-only preflight | Success timestamp, not persistent-session claim; wrong auth gives actionable error, UI responsive | Integration+GUI; redacted report; G2 |
| A02 | Controlled host-key fixtures | Unknown key then changed key | Unknown requires explicit verified onboarding; changed blocks; no bypass | Manual/integration; fingerprint evidence; G2 |
| A03 | Locked key, agent absent/unavailable | Background preflight | Actionable passphrase/agent error; no prompt hang or credential persistence | Integration; logs + DB scan; G2 |
| A04 | Linux `ss`; fixtures for IPv4/IPv6/TCP/UDP and missing owner | Refresh ports | Correct typed rows; unknown PID remains unavailable; malformed lines warn | Parser unit + integration; fixture/report; G3 |
| A05 | UDP row selected | Attempt create tunnel | Create disabled with TCP-only explanation; no `-L` process launched | UI smoke; screenshot/process evidence; G3 |
| A06 | TCP service on SSH gateway loopback | Tunnel target `127.0.0.1:<port>`; send controlled bytes | End-to-end bytes pass; stop closes owned listener | Integration; client result; G2 |
| A07 | Reachable TCP service on second remote-LAN host | Target other than SSH host; exchange useful protocol/test bytes | Forward reaches remote target through gateway | Integration; byte/transaction evidence; G2 |
| A08 | Local port already occupied; race fixture | Start with busy port, then bind between precheck and child bind | `PORT_IN_USE` or `BIND_FAILED`; never false success | Unit+integration; error/state capture; G2 |
| A09 | SSH gateway reachable, target service unavailable | Start forwarding, probe if configured | Listener can be Listening while target Unknown/Failed; never false Healthy | Integration/UI; state capture; G2 |
| A10 | Unrelated system ssh.exe active | Start/stop app-owned tunnel | Only owned child exits; unrelated SSH stays alive | Windows integration; before/after process handles; G2 |
| A11 | Disposable system/user service; restricted service case | List/show/start/stop/restart, confirm each mutation | Correct pre/post structured state; no implicit sudo; permission/missing systemd errors distinct | Linux integration; service state/log; G3 |
| A12 | Disposable process with stable identity | TERM then separately confirmed KILL; stale/reused PID fixture | Unsafe/stale identity rejected; TERM/KILL distinct; PID 1 rejected | Unit+Linux integration; process identity evidence; G3 |
| A13 | Network blackhole/drop, timeout, cancel, large output | Run query/tunnel and cancel at each phase | UI responsive, timeout/cancel classified, pipes drained, no owned-process leak; mutation says outcome uncertain | Fault injection+GUI; timing/logs/process snapshot; G4 |
| A14 | Empty, current, corrupt and interrupted-migration DB copies | Start, migrate, save/reopen profiles/presets | Transactional migration or actionable recovery; runtime PID absent; no raw key | SQLite unit/fault injection; DB inspection; G3 |
| A15 | Active tunnel and pending read/mutation | Close, choose Cancel then Stop Owned & Exit | Cancel keeps app/tunnel; exit stops owned tunnels and waits boundedly; mutation uncertainty shown | GUI integration; screen + process evidence; G4 |
| A16 | Clean Windows 10 and 11 x64 VMs, no system OpenSSH/.NET | Launch actual package and smoke SSH | App and bundled OpenSSH run; no undeclared runtime; missing/corrupt dependency actionable | Manual clean VM; VM image/build/package hashes; G4 |
| A17 | Malicious host/unit/PID/port/Unicode/newline inputs | Validation and command-builder suite; attempt crafted operation | Reject or safely encode; no shell/option injection, arbitrary remote command or data leakage | Unit/security tests; test report; G4 |
| A18 | Windows 10/11 DPI 100/125/150/200%, 1366x768, keyboard | Navigate screens and dialogs via keyboard; resize/switch DPI | No clipped controls/hidden focus; usable min layout; screen-reader basics checked where available | GUI manual/automation; screenshots/checklist; G4 |

## Additional mandatory cases

| Case | Test layer | Expected evidence |
|---|---|---|
| IPv6 SSH host, IPv6 target and bracketed forwarding syntax | Unit + Linux integration | Exact argv and successful controlled byte exchange. |
| Host aliases, HostName/User/Port/IdentityFile/ProxyJump | SSH integration | `ssh -G` diagnostics and successful test where environment permits; config remains authoritative. |
| DNS fail, custom SSH port, network disconnect/reconnect | Integration | Correct classified error/exit, responsive UI and no stale Healthy state. |
| Concurrent stdout+stderr, infinite output, broken pipe, quick exit, double cancel | Mock process unit/integration | Bounded memory/output, no deadlock, exact-once completion and handle cleanup. |
| SQLite constraints, FK cascade, busy DB, unwritable path, Unicode profile/identity paths | Unit/Windows integration | No corruption/data loss; clear actionable errors. |
| Job Object unavailable/nested parent job | Windows integration | Documented owned-handle fallback; unrelated process unaffected. |
| Multiple instances | Windows integration | Explicit activation/lock policy; no duplicate DB writer or cross-instance termination. |
| Host-key changed and known_hosts permissions | Manual/integration | Blocking warning, no silent known_hosts rewrite or bypass. |
| Crash/forced termination | Windows VM | Owned Job cleanup if supported; no stale PID action after restart. |

## Test layers and tools

- **Unit:** CTest + lightweight assertion-based C++17 test executable; pure validators, argv/remote command builders, parsers, state machine, migration helpers. No new framework unless existing project selects one.
- **Process integration:** deterministic test helper child for simultaneous streams, bounded output, timeout, cancellation, early exit, Job Object and handle lifecycle. Windows/MSVC required for authoritative results.
- **Linux integration:** controlled Linux SSH host with fixture TCP services, TCP/UDP listeners, system/user test service, disposable process and host-key states. No production host mutations.
- **GUI smoke:** launch actual Release executable; validate keyboard, DPI, progress, modal confirmation, stale worker completion and shutdown. Browser/screenshot evidence must come from actual running binary.
- **Clean-machine:** Windows 10 and 11 x64 VMs without system OpenSSH, .NET or PowerShell runtime; verify packaged dependencies only.
- **Static/security:** inspect all process launch paths, fixed command allowlist, validation, redaction and DB contents; dependency/license/hash audit.

## Release rules

- Required toolchain/env unavailable means `BLOCKED` or `NOT VERIFIED`, never pass inferred from source review.
- G1 requires actual MSVC Release x64 build and launch.
- G2 requires actual SSH host-key/auth/tunnel end-to-end evidence.
- G3 requires Linux ports/systemd/process plus migration tests.
- G4 requires security tests, clean-machine package, Windows DPI/keyboard smoke and resolved P0 issues.
- v1.0 cannot ship with any A01–A18 FAIL/BLOCKED; any blocked required environment means `NOT READY — BLOCKED`.
