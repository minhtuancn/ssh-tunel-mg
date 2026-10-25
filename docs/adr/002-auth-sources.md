# ADR 002 — Authentication: config/key/agent only in v1.0

**Date:** 2026-10-25  
**Status:** Proposed  
**Supersedes:** spec v1 DPAPI raw-key vault

## Context

Original design proposed DPAPI-encrypted private-key vault in SQLite for app-managed keys. Security review determined that building a raw private-key store with temp-file lifecycle, crash hygiene, zeroization and migration is a distinct product capability and cannot be safely delivered in v1.0 P0 scope without dedicated threat model, audit and test investment.

## Decision

v1.0 supports only two credential sources:

1. **`ssh_config` aliases + identity file / Windows ssh-agent** — OpenSSH resolves HostName/User/Port/IdentityFile/ProxyJump from `%USERPROFILE%\.ssh\config`. Application does not copy key content; it only references the path for diagnostics or passes `-i <path>` if user explicitly selects an identity file.
2. **Identity file path stored in profile** — path string only; user manages key file and permissions.

No passphrase collection via stdin redirect; no password forms; no automatic sudo or key vault.

## Consequences

- Simplifies v1.0 attack surface and DB schema.
- Users without key/agent must set one up before background auth works; GUI error guides them to test interactively or load agent.
- If product later requires key vault, it is a v1.2+ backlog item with independent threat model and security testing.

## Verification

- DB scan shows no raw key bytes, passphrases or private key strings in any table.
- Auth failure on passphrase-locked key without agent yields actionable message and does not hang.

## Rollback

If usability is unacceptable after v1.0 release, key-vault backlog item (C04) is fast-tracked with dedicated security review and CI gates.