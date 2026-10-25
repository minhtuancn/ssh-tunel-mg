# ADR 001 — Bundled OpenSSH packaging

**Date:** 2026-10-25  
**Status:** Proposed  
**Supersedes:** earlier DPAPI raw-key vault design (removed from v1.0 scope)

## Context

Product requires native Windows application that connects to Linux SSH servers without requiring system OpenSSH, .NET, or PowerShell runtime on the target machine. The SSH binary must support key-based authentication, host-key verification, ProxyJump, TCP forwarding (`-L`) and strict host-key checking.

## Decision

Package a pinned upstream OpenSSH for Windows x64 build with its full dependency closure. No runtime download. Version and SHA256 recorded in package metadata. All license and NOTICE files included in distribution.

### Binary source criteria

- Official OpenSSH portable release or Microsoft Win32-OpenSSH build; identical architecture (x64).
- Dependency list: `ssh.exe`, `sshd.exe` not required, required DLLs (`libcrypto*.dll`, `libssl*.dll`, `libz*.dll`, `bcrypt.dll`, etc.).
- Build configuration: built with Windows APIs for agent integration, `SSH_AUTH_SOCK` support on Windows if available.

### Deployment

- Binaries placed under `resources/openssh/` at build time.
- Application resolves absolute path via module directory; `CreateProcessW` uses `lpApplicationName` to avoid PATH search.
- Missing/corrupt binary or dependency yields `DEPENDENCY_MISSING` / `INTEGRITY_FAILURE` with actionable guidance.

### Host-key onboarding

- Unknown host: launch interactive OpenSSH console (separate process) for user to verify fingerprint out-of-band; then retry background preflight. GUI never auto-trusts.
- Changed host-key: blocking error, no bypass. User must edit known_hosts manually.

## Consequences

- Package size increases by OpenSSH dependency closure (~5–10 MB compressed). Measured size replaces any earlier estimate.
- Windows agent integration must be tested on clean machine; agent may be absent/disabled on some targets. Failure is an actionable error, not a fallback.
- Security review required before bumping pinned version; reproducible build/artifact hash verified at release.
- No DPAPI raw-key vault in v1.0. Identity file path and ssh-agent remain the supported credential sources. Private-key vault deferred to post-v1.0 threat-model and hygiene design.

## Alternatives considered

- Build minimal SSH client using libssh2/Windows SSP: rejected; reimplementing protocol stack violates design principle 2.
- Rely on system OpenSSH: rejected; clean-machine and distribution requirement.

## Verification

- Build with exact source commit and toolchain; artifact SHA256 recorded.
- Clean Windows 10/11 VM smoke test: application launches and runs SSH preflight/tunnel without system OpenSSH installed.
- Auth scenarios: key+agent, identity file, unknown/changed host key, passphrase-locked key without agent.

## Rollback

If packaging fails to meet clean-machine requirement or introduces vulnerability, revert to using system OpenSSH with documented prerequisite and re-evaluate v1.0 scope.