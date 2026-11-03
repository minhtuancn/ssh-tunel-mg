# Agent Handoff — SSH Tunnel Manager

## Goal

Deliver native C++17 Win32 x64 SSH tunnel manager for Windows 10/11. Product source of truth: `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design-v2.md`. Detailed tasks and A01–A18 baseline: `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32-v2.md`.

## Verified workspace state (2026-11-03)

- Git repo remote `https://github.com/minhtuancn/ssh-tunel-mg.git`; branch `feature/ssh-process-runner`; HEAD `3082e63`. All work from this session is **uncommitted**. `build_verify/`, `build_msvc/`, `sqlite3.zip` are untracked and must not be committed.
- Toolchain actually used: CMake 4.4.4, Ninja 1.13.2, WinLibs MinGW GCC 16.2.0 (x86_64-ucrt-posix-seh) Release. System OpenSSH is 9.5p2.
- `build_verify` configures and builds all 70 targets; `ctest --test-dir build_verify --output-on-failure` reports **10/10 passed**.
- GUI smoke (window enumeration + synthetic input, 2026-11-03): `SSHTunnelManager.exe` double-click launch works because the GUI is now linked with static MinGW runtime; it depends only on system DLLs. Verified: 7 tab pages, tab switching 0→6 via clicks, toolbar button counts matching source (3/5/1/1/2), no DB error dialog, `ProfileDialog` opens/closes with correct defaults (SSH 22, local 127.0.0.1:0, remote 127.0.0.1:80, keepalive 30, timeout 15), and creating a profile through the dialog updates the list 0→1 and persists to SQLite.
- GUI build fixes: `src/main.cpp` now actually constructs `MainWindow`; `src/ui/resources.rc` is compiled; project `-I` paths are excluded from the RC language so `windres` does not split paths containing spaces; static runtime linking prevents missing-DLL launch failures.
- MSVC is still **BLOCKED**: no Visual Studio/VCTools, no `vswhere.exe`, no `cl.exe`. `build_msvc/` is a stale cache, not a build. MinGW results do not satisfy the MSVC acceptance requirement.
- Linux host `dev@10.20.10.103` was last verified 2026-10-25 (BatchMode + strict host key, `SSH_OK`, `ss`/systemd present). It was **not re-verified in this session**; destructive mutations remain BLOCKED.
- Credential note unchanged: token-bearing `openclaw-126 info.txt` is absent, never tracked; token validity/revocation cannot be checked without provider access. Owner must revoke/rotate.

## What changed in this session

### Test integrity (most important)

The previous "8/8 pass" was vacuous: Release defines `-DNDEBUG`, so every `assert()` in the tests was compiled out. Two tests were also disabled and their binaries could not even compile once assertions were enabled.

- `tests/CMakeLists.txt`: add `-UNDEBUG` (`/UNDEBUG` on MSVC) so test assertions run; re-enable `test_ssh_tunnel` and `test_ssh_process_mgmt`; run `process_runner_test` from `$<TARGET_FILE_DIR:process_helper>`.
- The old "segfault" on those two tests was `STATUS_DLL_NOT_FOUND` (`0xC0000135`) because the MinGW runtime had been removed, not a static-init/Tunnel bug.

### Runtime and parsing defects fixed

- `src/ssh/ssh_process.cpp`: `timeoutMs == 0` now disables the deadline (required for long-lived tunnels); added an external cancel token; cancel/timeout now terminate, wait, and drain pipes; no more premature `break` before draining.
- `src/ssh/ssh_tunnel.cpp`: wired `stop_requested` and external cancel into the SSH child; replaced leaking `std::thread*` with a joined `std::thread`; guarded restart; port availability now uses `SO_EXCLUSIVEADDRUSE` (previously `SO_REUSEADDR` reported busy ports as free); `-L` and extra options are inserted without iterator invalidation.
- `src/ssh/ssh_arguments.cpp/.h`: builder returns raw tokens; `RunSshProcess` performs Windows argv quoting (removes double-quoting). `STRICT_KEYS` emits `-o StrictHostKeyChecking=yes`. Preflight builders force `BATCH_MODE|STRICT_KEYS`. `add_option()` now emits `-o` and survives `build()`.
- `src/ssh/ssh_auth.cpp`: `parse_ssh_config` and `extract_host_alias` were defined in the global namespace; fixed. `extract_host_alias` strips `user@`, `:port`, and handles bracketed IPv6.
- `src/ssh/ssh_process_mgmt.cpp`: `ps` parser used `size-2` for `stat` (wrong column); now uses `size-11` when `lstart` is present with a safe fallback.
- `src/ssh/ssh_port_discovery.cpp`: strip `[]` from IPv6 endpoints. Include order fixed in `ssh_tunnel.cpp`/`ssh_port_discovery.cpp` so `winsock2.h` precedes `windows.h` (MSVC requirement; removes the MinGW `-Wcpp` warning).
- `src/ssh/ssh_systemd.cpp/.h`: expose `parse_service_state/type` for tests.
- `src/storage/sqlite_db.cpp/.h`: fixed non-recursive mutex double-lock in `get_schema_version()`; rewrote `migrate()` to create bookkeeping tables first and use bound parameters; added `column_text()` so SQL NULL columns no longer crash `std::string`; default `Migration.version = 0`.
- `src/ssh/ssh_auth.h`: `SshConfig.host_pattern` defaults to `L"*"`.

### GUI launch/render fixes

- `src/main.cpp`: replaced the old blank-window skeleton with a real bootstrap that initializes Winsock, sets per-monitor DPI awareness, constructs `MainWindow`, and reports failures with a MessageBox.
- `src/ui/main_window.cpp`: create `%LOCALAPPDATA%\SSHTunnelManager` before opening SQLite; surface DB init errors instead of ignoring them; forward `WM_COMMAND`/`WM_NOTIFY` from tab pages to the main window; route toolbar commands; add `show_tab_page()`/`layout_page()`; size the tab control and pages on `WM_SIZE`; `TB_AUTOSIZE`; fix profile list column indices; add `on_kill_selected_process()`.
- `src/ui/main_window.cpp/.h`: `ProfileDialog` no longer dereferences a null profile on Add; control IDs corrected (`1001`–`1015`, Browse `1006`); defaults seeded; save failures reported instead of closing.
- `src/ui/resources.rc`: added dialog template and control IDs. `CMakeLists.txt`: enable `RC` language, add the resource, exclude project include dirs from RC compilation (`windres` space-in-path bug), and link the GUI with `-static -static-libgcc -static-libstdc++`.
- **Tunnels tab wiring:** `TunnelManager` is now populated from enabled profiles in the database on startup and after any profile change (`reload_tunnels()`). Start/Stop/Remove buttons operate on the correct tunnel index; list shows profile name, status, local/remote endpoints, SSH host:port.
- **Settings persistence:** `load_settings()`/`save_settings()` read/write `OpenSSHPath` and `DefaultTimeoutMs` under `HKCU\Software\SSHTunnelManager`; verified working across restarts.

### Tests added/updated

- `tests/unit/process_runner_test.cpp`: zero-timeout and external-cancel cases; helper path now relative to the test working directory.
- `tests/unit/test_ssh_tunnel.cpp`: real listener-based port availability assertions; `find_available_port` assertions.
- `tests/unit/test_ssh_arguments.cpp`: strict host-key option test; `add_option` persistence/rejection test; preflight builders always include strict + batch.

## Remaining gaps (NOT fixed — do not claim done)

1. **Host-key onboarding is non-functional end-to-end.** `add_host_key()` receives an empty `public_key_blob`; `update_host_key()` is a no-op returning `OK`; `parse_known_hosts_line()` never computes fingerprints and always sets `verified = false`; `run_onboarding_console()` uses `ReadConsoleW` so it cannot work from the GUI; `ssh_exit_to_auth_status()` maps every non-zero exit to `FAIL`, so the `HOST_KEY_UNKNOWN/CHANGED` branches in `run_full_preflight()` are effectively unreachable. Spec §9.2 requires onboarding through a real interactive `ssh.exe` console, and forbids silently writing `known_hosts` from an unverified fingerprint.
2. **MSVC authoritative build** blocked by missing tools/SDK.
3. **Bundled OpenSSH** provenance (version/SHA256/DLL/license) not audited; preflight hard-codes `resources\openssh\ssh.exe`.
4. **No AsyncRunner or Job Object**; UI-safe async lifecycle and process-tree cleanup are pending.
5. **Stubs:** `Database::get_sessions_for_profile()` returns empty; `create_session()` ignores `execute()` failure.
6. **No preflight/host-key tests; no Linux destructive integration.**
7. **UI tunnel lifecycle not verified end-to-end:** the GUI renders, switches tabs, and saves profiles, but starting/stopping a real tunnel from the UI, DPI scaling variants, and clean shutdown while tunnels run are not yet exercised.
8. **UI actions still stubbed:** `Ports` "Discover" only shows an Info MessageBox; `Processes` "Kill" only works on local processes (no SSH-side kill); Dashboard activity log is empty.

## Next steps

1. Commit the verified fixes in logical groups (tests/integrity, process+tunnel, parsers, SQLite). Keep build outputs untracked.
2. Redesign host-key onboarding per spec §9.2 (interactive `ssh.exe` console; real fingerprint verification; correct `known_hosts` blob handling).
3. Add AsyncRunner + Job Object and shutdown-race tests.
4. Re-verify read-only Linux probes; only then plan disposable-host mutations.
5. Admin installs VCTools + Windows SDK; run `build.ps1 -Action Test` and record MSVC compiler/SDK versions.

## Commands

Current verified (MinGW) flow:

```powershell
$mingw = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
$env:PATH = "$mingw;$env:PATH"
& "C:\Program Files\CMake\bin\cmake.exe" -S . -B build_verify -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_C_COMPILER=$mingw\gcc.exe" "-DCMAKE_CXX_COMPILER=$mingw\c++.exe" "-DCMAKE_MAKE_PROGRAM=$mingw\ninja.exe"
& "C:\Program Files\CMake\bin\cmake.exe" --build build_verify
& "C:\Program Files\CMake\bin\ctest.exe" --test-dir build_verify --output-on-failure --timeout 60
```

Planned MSVC flow (not runnable until VCTools/SDK are installed):

- `.\build.ps1 -Action Configure`
- `.\build.ps1 -Action Build`
- `.\build.ps1 -Action Test`
- `ctest --test-dir build -C Release --output-on-failure`

## Reporting rules

For each task report changed files, exact commands/environment, build/test status, GUI/integration evidence, security findings, blockers and next task. Mark unavailable checks BLOCKED/NOT VERIFIED. Do not claim pass from source inspection. Never copy secrets into notes, logs, issues or test artifacts.
