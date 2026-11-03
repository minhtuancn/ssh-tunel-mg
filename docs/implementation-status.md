# Implementation Status — SSH Tunnel Manager

**Ngày khảo sát:** 2026-11-03  
**Baseline thiết kế:** `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design-v2.md`  
**Baseline kế hoạch:** `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32-v2.md`

## Repository evidence

| Area | State | Evidence / blocker | Next action |
|---|---|---|---|
| Application source | PARTIAL | MinGW x64 Release build thành công sạch (`build_verify`, 70 target). GUI `SSHTunnelManager.exe` render thật: liệt kê cửa sổ thấy 7 tab, toolbar + listview từng trang, không có hộp thoại lỗi DB. Chuyển tab 0→6 bằng click; `ProfileDialog` mở/đóng, default đúng (22 / 127.0.0.1 / 0 / 80 / 30 / 15); tạo profile qua dialog làm list 0→1 và ghi DB. Đây chưa phải MSVC build. | Hoàn tất MSVC Release build; tiếp tục integration T5–T10. |
| Existing utility | PARTIAL | Có `tunnel.bat`, `tunnel-v1.bat`, `tunnel-103.bat`, `tunnel-126.bat`; chỉ triển khai batch/PowerShell SSH launcher. | Giữ nguyên; dùng làm behavior reference. |
| Product spec v2 | DONE | Đã đọc toàn bộ `*-design-v2.md`; đây là trạng thái tài liệu, không phải feature implementation. | Dùng làm product baseline. |
| Implementation plan v2 | DONE | Đã đọc toàn bộ `*-win32-v2.md`, Task 0–12 và A01–A18. | Dùng làm task baseline. |
| Canonical path alignment | PARTIAL | Yêu cầu chỉ định path không hậu tố `-v2`; workspace giữ cả bản cũ và bản v2. Chưa ghi đè bản cũ. | Duy trì v2 là nguồn chuẩn; chỉ promote khi được duyệt. |
| Tests | PARTIAL | 10/10 CTest pass (MinGW Release, 2026-11-03), assertions được bật bằng `-UNDEBUG`. Trước đó suite "8/8 pass" là vô nghĩa vì `-DNDEBUG` biến mọi `assert()` thành no-op. GUI smoke có kiểm chứng bằng enumeration + click + DB round-trip. Unit-only; chưa có integration/E2E qua SSH host thật. | Thêm test cho preflight/host-key và chạy suite bằng MSVC. |
| Build | PARTIAL | CMake 4.4.4, Ninja 1.13.2, WinLibs MinGW GCC 16.2.0 (UCRT, POSIX, SEH) x64 Release. RC resource dùng `COMPILE_LANGUAGE` để `windres` không nhận `-I` có dấu cách (lỗi `cc1.exe: No such file or directory`). GUI link static runtime (`-static -static-libgcc -static-libstdc++`) nên chỉ phụ thuộc DLL hệ thống. VS/VCTools không có trên máy (`vswhere` không tồn tại, không có `cl.exe`); `build_msvc/` chỉ là cache cũ, không build được. | Admin cài `Microsoft.VisualStudio.Workload.VCTools` + Windows SDK; chạy `build.ps1 -Action Test`. MinGW không thay acceptance MSVC. |
| Linux integration | NOT RE-VERIFIED | Lần xác minh gần nhất 2026-10-25: `dev@10.20.10.103` trả `SSH_OK` với `BatchMode=yes` + `StrictHostKeyChecking=yes`; remote Linux có `ss` và systemd. Không chạy lại trong phiên 2026-11-03. | Chạy read-only probe lại; xác nhận host disposable trước mutation systemd/process. |
| Git | DONE | Repo hợp lệ; branch `feature/ssh-process-runner`, HEAD `3082e63`, remote `https://github.com/minhtuancn/ssh-tunel-mg.git`. Toàn bộ công việc phiên này chưa commit. | Commit theo nhóm thay đổi; không commit `build_verify/`, `build_msvc/`, `sqlite3.zip`. |
| GitHub workflow | PARTIAL | PR #1–#3 đã merge vào `main`; chưa có CI workflow. | Thêm CI khi có MSVC runner/toolchain xác minh. |
| Security follow-up | BLOCKED | `openclaw-126 info.txt` không còn trong workspace và chưa từng tracked/commit; không có quyền/provider access để xác nhận revoke token. | Chủ sở hữu revoke/rotate token; secret-scan trước commit/push. |
| Host-key onboarding | BLOCKED | Xem mục "Known defects" bên dưới. Đường onboarding hiện là dead code, không thể xác minh/chấp nhận host key mới từ GUI. | Thiết kế lại theo spec §9.2 (phiên `ssh.exe` tương tác trong console riêng). |

## Task state

| Task | Priority | Dependencies | State | Evidence | Blocker | Next action |
|---|---|---|---|---|---|---|
| T0 Contract/spec baseline | P0 | — | PARTIAL | Spec/plan v2 tồn tại, đã đọc; canonical path alignment pending. | Canonical path ambiguity. | Chuẩn hóa file baseline trước release. |
| T1 Native build skeleton | P0 | T0 | PARTIAL | MinGW CMake/Ninja Release build sạch 70/70 target; GUI launch/render smoke pass; `build.ps1` tồn tại. | VCTools/Windows SDK vắng; không có `cl.exe`. | Admin cài MSVC x64 tools + SDK; chạy build/test bằng MSVC. |
| T4 OpenSSH/auth | P0 | T3 | IN_PROGRESS | `ssh_auth.*`, `ssh_arguments.*`, `ssh_known_hosts.*`; preflight builders ép `BatchMode=yes` + `StrictHostKeyChecking=yes`; `extract_host_alias` xử lý `user@`, `:port`, IPv6 bracket; `add_option` phát `-o` và sống qua `build()`. | Onboarding host key chưa hoạt động end-to-end; provenance binary OpenSSH chưa chốt. | Sửa onboarding + audit gói OpenSSH. |
| T10 Full UI | P0 | T1,T2,T3,T5–T9 | IN_PROGRESS | `main_window.*` 7-tab GUI + `ProfileDialog`; GUI render thật và không có dialog lỗi DB. Smoke xác minh: tab 0→6 chuyển được, toolbar button count khớp source (3/5/1/1/2), dialog default đúng, tạo profile qua dialog cập nhật list 0→1 và ghi DB. Settings load/save qua registry hoạt động. `test_ssh_tunnel` và `test_ssh_process_mgmt` đã bật lại. | Chưa có DPI/lifecycle evidence đầy đủ; chưa thao tác start tunnel thật từ UI. | Manual smoke + DPI + lifecycle. |
| T2 Models/validation/commands | P0 | T1 | PARTIAL | `self_check` pass (assertions bật); validators + Windows argv quoting + Linux fixed-command builders. | Chưa có typed `SshTarget`/`UnitName`/`TargetHost`/`LocalBind`. | Bổ sung typed structs + fixture xấu. |
| T3 SSH process runner | P0 | T1,T2 | PARTIAL | `RunSshProcess` đồng bộ: timeout (0 = không timeout), cancel nội bộ + external token, terminate/wait/drain, bounded capture. `process_runner_test` pass gồm zero-timeout và external-cancel. | Chưa có Job Object; chưa có AsyncRunner. | Thêm Job Object + AsyncRunner; test shutdown race. |
| T4 OpenSSH/auth | P0 | T3 | IN_PROGRESS | `ssh_auth.*`, `ssh_arguments.*`, `ssh_known_hosts.*`; preflight builders ép `BatchMode=yes` + `StrictHostKeyChecking=yes`; `extract_host_alias` xử lý `user@`, `:port`, IPv6 bracket; `add_option` phát `-o` và sống qua `build()`. | Onboarding host key chưa hoạt động end-to-end; provenance binary OpenSSH chưa chốt. | Sửa onboarding + audit gói OpenSSH. |
| T5 TCP tunnel | P0 | T2,T3,T4 | IN_PROGRESS | `ssh_tunnel.*`: PIMPL, `TunnelManager`, `std::thread` join trong destructor, restart guard, `stop_requested` + external cancel nối vào SSH child, port check dùng `SO_EXCLUSIVEADDRUSE`, chèn `-L`/option không invalidate iterator; `test_ssh_tunnel` pass với listener thật. | Chưa có Linux gateway + target fixture. | E2E forwarding và ownership. |
| T6 Port discovery | P0 | T2,T3,T4 | IN_PROGRESS | `ssh_port_discovery.*`; parser `ss` bỏ bracket IPv6; test fixtures pass. | Chưa có Linux integration. | Chạy `ss` thật read-only. |
| T7 systemd | P0 | T2,T3,T4 | IN_PROGRESS | `ssh_systemd.*`; `parse_service_state/type` được export và test pass. | Chưa có unit fixture disposable. | Integration read-only trước, mutation sau. |
| T8 Process management | P0 | T2,T3,T4 | IN_PROGRESS | `ssh_process_mgmt.*`; sửa index parser `ps` (trước đó `size-2` gây đọc sai/segfault); test pass. | Chưa có process fixture. | Identity refresh + TERM/KILL tests. |
| T9 SQLite/migrations | P0 | T1,T2 | IN_PROGRESS | `sqlite_db.*`; sửa deadlock double-lock trong `get_schema_version`, bootstrap bảng metadata, `migrate()` dùng bound parameters, `column_text()` xử lý NULL thay vì crash; `test_sqlite_db` pass. | Chưa test corruption/concurrency. | Corruption + concurrent access tests. |
| T10 Full UI | P0 | T1,T2,T3,T5–T9 | IN_PROGRESS | `main_window.*` 7-tab GUI + `ProfileDialog`; GUI render thật và không có dialog lỗi DB. Smoke xác minh: tab 0→6 chuyển được, toolbar button count khớp source (3/5/1/1/2), dialog default đúng, tạo profile qua dialog cập nhật list 0→1 và ghi DB. Settings load/save qua registry hoạt động. `test_ssh_tunnel` và `test_ssh_process_mgmt` đã bật lại. | Chưa có DPI/lifecycle evidence đầy đủ; chưa thao tác start tunnel thật từ UI. | Manual smoke + DPI + lifecycle. |
| T11 Security/integration | P0 | T2–T10 | TODO | Chưa có test matrix/E2E. | Windows clean VM + Linux disposable host. | Chạy matrix và lưu evidence. |
| T12 Package/release | P0 | T11 | TODO | Chưa có artifact. | MSVC + clean Windows VM. | Build, dependency audit, measured release. |

## Fixes landed 2026-11-03 (chưa commit)

- **Test integrity:** `tests/CMakeLists.txt` thêm `-UNDEBUG` (MSVC: `/UNDEBUG`) để `assert()` không bị `-DNDEBUG` vô hiệu hóa; bật lại `test_ssh_tunnel` và `test_ssh_process_mgmt`; `process_runner_test` chạy trong thư mục chứa `process_helper.exe`.
- **Process runner:** `timeoutMs == 0` = không timeout; thêm external cancel token; terminate + `WaitForSingleObject` + drain sau cancel/timeout.
- **Tunnel:** nối `stop_requested`/external cancel vào child; thay `std::thread*` rò rỉ bằng `std::thread` join trong destructor; chặn `start()` chồng; port check dùng `SO_EXCLUSIVEADDRUSE`; sửa chèn `-L`/option gây invalidate iterator.
- **Arguments:** token trả về raw (việc quote do `RunSshProcess` đảm nhận) để tránh double-quote; `STRICT_KEYS` sinh `-o StrictHostKeyChecking=yes`; preflight builder luôn ép `BATCH_MODE|STRICT_KEYS`; `add_option()` phát `-o` và không bị `build()` xóa.
- **Auth/parse:** `parse_ssh_config`/`extract_host_alias` đúng namespace; `extract_host_alias` bỏ `user@`, `:port`, giữ IPv6 bracket.
- **SQLite:** sửa deadlock trong `get_schema_version()`; `migrate()` tạo bảng metadata trước và dùng bound parameters; đọc cột NULL an toàn; default `Migration.version = 0`.
- **Parsers:** `ps` lấy đúng cột `stat` (fallback khi thiếu `lstart`); `ss` bỏ `[]` khỏi IPv6; `SshConfig.host_pattern` mặc định `L"*"`; sửa thứ tự include `winsock2.h` trước `windows.h` để tránh cảnh báo và lỗi Winsock trên MSVC.
- **GUI bootstrap / launch:** `src/main.cpp` trước đây chỉ là skeleton cửa sổ trống và không hề tạo `MainWindow`; nay khởi tạo Winsock, DPI awareness, `MainWindow`, kèm MessageBox lỗi. Tạo thư mục `%LOCALAPPDATA%\SSHTunnelManager` trước khi mở SQLite và hiển thị lỗi DB thay vì im lặng.
- **GUI wiring:** thêm `TabPageProc` forward `WM_COMMAND`/`WM_NOTIFY` từ tab page lên main window; route command cho tunnels/profiles/ports/services/processes/settings; `show_tab_page()`/`layout_page()`; sửa `WM_SIZE` (tab control full client, page theo `TabCtrl_AdjustRect`); `TB_AUTOSIZE`; sửa column index list profiles.
- **ProfileDialog:** sửa crash do dereference profile null khi Add; map đúng control ID (`1001`–`1015`, Browse `1006`); default values; kiểm tra kết quả `create_profile`/`update_profile` và báo lỗi thay vì đóng dialog.
- **Resources/build:** thêm `src/ui/resources.rc` + `IDD_PROFILE_DIALOG`/`IDC_*`, bật language `RC`; loại include dir của project khỏi RC để `windres` không tách path có dấu cách; link static runtime cho GUI để double-click không thiếu `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll`.
- **Tunnels tab wiring:** `TunnelManager` populated from enabled DB profiles on startup and after profile changes (`reload_tunnels()`); Start/Stop/Remove buttons wired; list shows profile name, status, endpoints.
- **Settings persistence:** `load_settings()`/`save_settings()` read/write `OpenSSHPath` and `DefaultTimeoutMs` to `HKCU\Software\SSHTunnelManager`; verified working across restarts.

## Known defects / gaps (không được coi là đã sửa)

1. **Host-key onboarding không hoạt động end-to-end.** `add_host_key()` caller truyền `public_key_blob = ""` nên ghi dòng known_hosts không hợp lệ; `update_host_key()` là no-op trả `OK`; `parse_known_hosts_line()` không tính fingerprint và luôn `verified = false`; `run_onboarding_console()` dùng `ReadConsoleW` nên không dùng được từ GUI không có console; `ssh_exit_to_auth_status()` map mọi exit ≠ 0 thành `FAIL`, nên nhánh `HOST_KEY_UNKNOWN/CHANGED` trong `run_full_preflight()` gần như không bao giờ chạy. Spec §9.2 yêu cầu onboarding qua phiên `ssh.exe` tương tác trong console riêng, không tự sửa `known_hosts` từ fingerprint chưa kiểm chứng.
2. **MSVC build chưa xác minh.** Không có `cl.exe`, Windows SDK hoặc `vswhere.exe`.
3. **Bundled OpenSSH chưa audit.** `resources/` tồn tại nhưng chưa xác minh version/SHA256/DLL/license; `run_full_preflight()` hard-code `resources\openssh\ssh.exe`.
4. **Chưa có AsyncRunner/Job Object.** UI vẫn phải gọi API đồng bộ hoặc tự quản lý thread.
5. **Stub còn lại:** `Database::get_sessions_for_profile()` trả rỗng; `create_session()` không kiểm tra kết quả `execute()` trước khi trả `last_insert_rowid`.
6. **Linux destructive integration chưa chạy**; mọi mutation systemd/process là NOT VERIFIED.
7. **Chưa có test cho preflight/host-key**; coverage hiện là unit-level.

## Handoff

Bước tiếp theo có thể thực thi ngay: (a) commit theo nhóm các fix đã pass 10/10, (b) thiết kế lại host-key onboarding theo spec §9.2, (c) bổ sung AsyncRunner/Job Object, (d) chờ Admin cài MSVC để chạy authoritative build/test. GUI launch hiện đã chạy được từ double-click (static runtime) và đã được smoke-test bằng enumeration/click/DB round-trip; chưa xác minh start tunnel thật từ UI.
