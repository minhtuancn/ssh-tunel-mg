# Implementation Status — SSH Tunnel Manager

**Ngày khảo sát:** 2026-10-25  
**Baseline thiết kế:** `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design-v2.md`  
**Baseline kế hoạch:** `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32-v2.md`

## Repository evidence

| Area | State | Evidence / blocker | Next action |
|---|---|---|---|
| Application source | PARTIAL | Có `CMakeLists.txt`, `src/main.cpp`, `src/domain/validation.*`, test skeleton và `build.ps1`; MinGW x64 Release build + GUI launch smoke pass; CTest 1/1 pass. Đây chưa phải yêu cầu MSVC build. | Hoàn tất MSVC Release build; tiếp tục modules T2+ theo plan. |
| Existing utility | PARTIAL | Có `tunnel.bat`, `tunnel-v1.bat`, `tunnel-103.bat`, `tunnel-126.bat`; chỉ triển khai batch/PowerShell SSH launcher. | Giữ nguyên; dùng làm behavior reference. |
| Product spec v2 | DONE | Đã đọc toàn bộ file `*-design-v2.md` (285 dòng); đây là trạng thái tài liệu, không phải feature implementation. | Dùng làm product baseline. |
| Implementation plan v2 | DONE | Đã đọc toàn bộ file `*-win32-v2.md` (410 dòng), Task 0–12 và A01–A18. | Dùng làm task baseline, áp dụng bổ sung trong master plan. |
| Canonical path alignment | PARTIAL | Yêu cầu chỉ định path không hậu tố `-v2`; workspace giữ cả bản cũ và bản v2. Chưa ghi đè bản cũ. | Duy trì v2 là nguồn chuẩn; chuẩn hóa tham chiếu, chỉ promote khi được duyệt. |
| Tests | PARTIAL | Có CTest `self_check`: port/PID/host/systemd unit và Windows argv quoting. 1/1 pass với MinGW; MSVC chưa xác minh. | Mở rộng tests theo T2–T12; chạy authoritative suite bằng MSVC. |
| Build | PARTIAL | CMake/Ninja + MinGW 15.2.0 x64 Release build và CTest 1/1 pass; GUI launch/close smoke pass. VS Build Tools 17.14.42 có nhưng component `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` vắng; installer log `SelectionState: NotSelected`, no `cl.exe`/SDK; không elevated. | Admin cần thêm workload `Microsoft.VisualStudio.Workload.VCTools`, MSVC x64 tools + Windows SDK. MinGW không thay acceptance MSVC. Không dùng OpenSSH hệ thống để chứng minh gói độc lập. |
| Linux integration | PARTIAL | SSH `dev@10.20.10.103` trả `SSH_OK` bằng `BatchMode=yes`, `StrictHostKeyChecking=yes`; remote `uname -s` là Linux, `ss -H -ltnup` trả TCP/UDP listener thật, systemd 255. Chưa xác minh distro/version hoặc chạy operation phá hủy. | Dùng host cho read-only integration tests; xác nhận môi trường disposable trước systemd/process mutations. |
| Git | BLOCKED | Thư mục hiện tại không phải Git repository (`git status` trả fatal); không có branch, commit, PR hoặc CI workflow trong workspace. | Xác định repo/remote và khởi tạo workflow có chủ sở hữu trước code/PR. |
| GitHub workflow | PARTIAL | Remote `https://github.com/minhtuancn/ssh-tunel-mg.git`; PR #1, #2 merged to `main` (`8481e6c`, `97020df`); no open issues/workflows observed during initial inventory. | Tạo branch tiếp theo từ updated `origin/main`; thêm CI workflow khi MSVC runner/toolchain được xác minh. |
| Security follow-up | BLOCKED | `openclaw-126 info.txt` không còn ở workspace, chưa từng tracked/commit; token đã lộ theo lịch sử handoff nhưng không có quyền/provider access để xác nhận revoke. | Chủ sở hữu revoke/rotate token qua dịch vụ phát hành; giữ file trong `.gitignore`; secret-scan trước commit/push. |

## Task state

| Task | Priority | Dependencies | State | Evidence | Blocker | Next action |
|---|---|---|---|---|---|---|
| T0 Contract/spec baseline | P0 | — | PARTIAL | Spec/plan v2 tồn tại, đã đọc; bổ sung matrix và alignment còn pending. | Canonical path ambiguity; repo không có Git. | Chuẩn hóa file baseline và hoàn thiện docs trước coding. |
| T1 Native build skeleton | P0 | T0 | PARTIAL | MinGW x64 CMake/Ninja Release build pass; `self_check` 1/1; process smoke title `SSH Tunnel Manager`, graceful close. | VS 17.14.42 registered, but VCTools workload/component absent (`vswhere -requires ...VC.Tools.x86.x64` empty); session not elevated, installer elevation attempt returned 1; no compiler. | User/Admin must complete Visual Studio Installer VCTools + x64 compiler + Windows SDK. Then run `build.ps1 -Action Test` with MSVC. |
| T2 Models/validation/commands | P0 | T1 | PARTIAL | RED→GREEN: port/PID/host/unit tests + Windows argv quoting + Linux fixed-command builders (ports/services/processes) pass 1/1 CTest (MinGW). Regression: `*.service` rejected after test first failed (shell glob expansion). | typed `SshTarget`/`UnitName`/`TargetHost`/`LocalBind` structs; SSH argv builder; broader malformed fixture sweep còn lại. | Bổ sung typed structs + SSH argv builder + fixtures; chạy lại suite. |
| T3 SSH process runner | P0 | T1,T2 | PARTIAL | Added Win32 `CreateProcessW` prototype, inherited stdout/stderr pipes, bounded capture/truncation, timeout/cancel flag; helper subprocess covers exit codes, separate/concurrent streams, >limit output, infinite-output timeout and cancel. MinGW build + 2/2 CTest pass; commit `c0f98b2`. | Synchronous API; cancellation flag supported but caller must manage worker thread; no async task lifecycle/job object/shutdown race tests; MSVC authoritative build blocked. | Refactor into owned asynchronous task with dedicated pipe readers, explicit cancellation/wait/cleanup; add job lifecycle tests; MSVC verification after admin installs tools. |
| T4 OpenSSH/auth | P0 | T3 | TODO | Chưa có bundled binaries/manifest. | Chốt upstream binary provenance/runtime dependencies. | Bundle audit + auth/host-key tests. |
| T5 TCP tunnel | P0 | T2,T3,T4 | TODO | Chưa có tunnel code/test host. | Linux gateway + reachable target fixture. | End-to-end forwarding and ownership. |
| T6 Port discovery | P0 | T2,T3,T4 | TODO | Chưa có Linux parser/query. | Linux SSH test host. | `ss` fixtures then integration. |
| T7 systemd | P0 | T2,T3,T4 | TODO | Chưa có code/test service. | Disposable system/user unit fixture. | Structured `systemctl show` and mutations. |
| T8 Process management | P0 | T2,T3,T4 | TODO | Chưa có code/process fixture. | Disposable Linux process. | Identity refresh, TERM/KILL tests. |
| T9 SQLite/migrations | P0 | T1,T2 | TODO | No DB/schema/library. | Pin SQLite source/license. | Migration and corruption tests. |
| T10 Full UI | P0 | T1,T2,T3,T5,T6,T7,T8,T9 | TODO | No UI code. | Feature modules first. | Integrate views, lifecycle and responsive UI. |
| T11 Security/integration | P0 | T2–T10 | TODO | No test suite/environment evidence. | Windows clean VM and Linux host. | Run matrix and preserve evidence. |
| T12 Package/release | P0 | T11 | TODO | No package/artifact. | MSVC + clean Windows VM. | Build, dependency audit, measured release. |

## Handoff

Next executable action: implement T3 SSH process runner (start with subprocess test helper + RAII handles/bounded pipes). MSVC compiler install requires admin; known SSH host reachable for read-only integration. T1 remains PARTIAL until MSVC Release build and UI interaction/DPI evidence pass; T2 remains PARTIAL pending typed models/SSH argv builder.
