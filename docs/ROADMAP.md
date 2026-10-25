# Roadmap

**Status:** planning baseline from Spec v2.0. All feature milestones remain TODO until verified evidence is recorded in `docs/implementation-status.md`.

## v1.0 — Production-ready (P0)

### M0 — Contract and project baseline
- Confirm v2 spec/plan source paths and repository identity.
- Establish versioned implementation status, ADRs, acceptance/test traceability and issue mapping.
- **Exit gate:** every P0 feature maps to task and acceptance evidence; canonical docs agreed.

### M1 — Native Windows foundation
- C++17 Win32 Common Controls x64 app, DPI v2, CMake/CTest, deterministic build script, RAII and typed UI messages.
- **Exit gate:** actual Windows/MSVC Release x64 build, executable starts/closes, CTest passes.

### M2 — SSH profile and verified connection
- Profiles, SSH config aliases, key files/agent, bundled pinned OpenSSH, trust onboarding, bounded async runner and diagnostics.
- **Exit gate:** clean host preflight success and auth/unknown/changed-host-key error evidence; no host-key bypass.

### M3 — Port discovery and TCP tunnel vertical slice
- Linux TCP/UDP discovery; create loopback-bound TCP tunnel to gateway or other remote-LAN target; multiple owned tunnels and truthful independent state.
- **Exit gate:** real useful TCP traffic end-to-end, UDP tunnel blocked, collision/race handling and no impact on external SSH.

### M4 — Persistence and Linux management
- SQLite migrations/profiles/presets/favorites; systemd system/user read/actions; process metadata/TERM/KILL with fresh identity.
- **Exit gate:** DB restart/migration checks; disposable Linux service/process operation evidence; no implicit sudo or stale PID action.

### M5 — Full UX, security, recovery
- Dashboard/sidebar/tabs, diagnostics, progress/cancel, confirmations, keyboard/DPI, shutdown and crash cleanup; A01–A18 suite.
- **Exit gate:** Windows UI smoke and security matrix pass; no P0 issue unresolved.

### M6 — Package and release candidate
- Bundle OpenSSH dependency closure/notices, clean VM testing, provenance/SHA256, docs and measured footprint.
- **Exit gate:** actual package passes clean Windows 10/11 smoke; all A01–A18 PASS; no blocker. Any unavailable environment means NOT READY.

**Critical path:** M0 → M1 → M2 → M3 → M5 → M6. M4 starts after M2 and runs in parallel with M3 where ownership is separate; M5 integrates both. T2 models/validation and T9 DB can run after M1 in parallel by isolated file ownership. T3 runner precedes T4 auth and T5–T8 remote features.

## v1.1 — Daily usability (P1)

- Optional system tray and close-to-tray setting with explicit Exit semantics.
- Opt-in reconnect with capped exponential backoff, sleep/wake/network change handling; stop retry on auth/host-key failures.
- Server groups, favorites, advanced search/filters and keyboard shortcuts.
- Bounded journal tail; sudo -n only after separate threat model and UX approval.
- Profile import/export without secrets, improved SSH-config diagnostics.
- Protocol-specific endpoint probes before any Healthy indication.

**Release gates:** independent issues, opt-in defaults, no regression in v1.0 auth/tunnel safety, Windows/SSH integration tests.

## v1.2+ — Advanced capabilities (P2, separate security review)

- SOCKS5 `-D`, reverse `-R`, non-loopback binds/LAN sharing, startup tunnels.
- App-managed key vault only with dedicated threat model, DPAPI lifecycle, temp file/crash cleanup and security suite.
- Dark theme only if supported Win32 controls remain accessible/stable.
- SSH terminal, SFTP, Docker/Proxmox, firewall and arbitrary shell remain out of product scope unless a new product spec approves them.

**Release gates:** separate spec/ADR, misuse/threat analysis, explicit opt-in and dedicated acceptance tests.
