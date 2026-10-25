# Implementation Status — SSH Tunnel Manager

**Ngày khảo sát:** 2026-10-25  
**Baseline thiết kế:** `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design-v2.md`  
**Baseline kế hoạch:** `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32-v2.md`

## Repository evidence

| Area | State | Evidence / blocker | Next action |
|---|---|---|---|
| Application source | TODO | Không có `src/`, CMake project hoặc executable. | Task 1 — native skeleton. |
| Existing utility | PARTIAL | Có `tunnel.bat`, `tunnel-v1.bat`, `tunnel-103.bat`, `tunnel-126.bat`; chỉ triển khai batch/PowerShell SSH launcher. | Giữ nguyên; dùng làm behavior reference. |
| Product spec v2 | DONE | Đã đọc toàn bộ file `*-design-v2.md` (285 dòng); đây là trạng thái tài liệu, không phải feature implementation. | Dùng làm product baseline. |
| Implementation plan v2 | DONE | Đã đọc toàn bộ file `*-win32-v2.md` (410 dòng), Task 0–12 và A01–A18. | Dùng làm task baseline, áp dụng bổ sung trong master plan. |
| Canonical path alignment | PARTIAL | Yêu cầu chỉ định path không hậu tố `-v2`; workspace giữ cả bản cũ và bản v2. Chưa ghi đè bản cũ. | Duy trì v2 là nguồn chuẩn; chuẩn hóa tham chiếu, chỉ promote khi được duyệt. |
| Tests | TODO | Không có `tests/`, CTest hoặc test runner. | Tạo theo Task 1 trở đi. |
| Build | BLOCKED | Có CMake 4.2.1, Ninja 1.13.2, OpenSSH hệ thống 9.5p2; không tìm thấy `cl.exe`, `msbuild.exe` hoặc Visual Studio C++ tools. | Cài/khởi chạy môi trường MSVC x64 trước Windows build. Không dùng OpenSSH hệ thống để chứng minh gói độc lập. |
| Linux integration | BLOCKED | Không có cấu hình/evidence host Linux test. | Cấp disposable Linux SSH host và service/process fixtures trước integration gate. |
| Git | BLOCKED | Thư mục hiện tại không phải Git repository (`git status` trả fatal); không có branch, commit, PR hoặc CI workflow trong workspace. | Xác định repo/remote và khởi tạo workflow có chủ sở hữu trước code/PR. |
| GitHub issues | NOT VERIFIED | GitHub CLI đăng nhập, nhưng không có repository local/remote để xác định repo issues liên quan. | Sau khi xác định repo, rà soát issue trước khi tạo; chưa tạo issue. |
| Security follow-up | TODO | `openclaw-126 info.txt` chứa token trong URL; không đưa giá trị vào tài liệu hoặc logs. | Thu hồi/rotate token đã lộ; xóa file hoặc redact token theo chủ sở hữu. |

## Task state

| Task | Priority | Dependencies | State | Evidence | Blocker | Next action |
|---|---|---|---|---|---|---|
| T0 Contract/spec baseline | P0 | — | PARTIAL | Spec/plan v2 tồn tại, đã đọc; bổ sung matrix và alignment còn pending. | Canonical path ambiguity; repo không có Git. | Chuẩn hóa file baseline và hoàn thiện docs trước coding. |
| T1 Native build skeleton | P0 | T0 | TODO | Chưa có source/build. | MSVC chưa xác minh/cài đặt. | Tạo CMake + Win32 skeleton; chạy trên Windows/MSVC thật. |
| T2 Models/validation/commands | P0 | T1 | TODO | Chưa có code/tests. | — | Test-first validation + command safety. |
| T3 SSH process runner | P0 | T1,T2 | TODO | Chưa có code/tests. | Windows process tests cần MSVC/Windows. | Bounded pipe draining, RAII, cancellation. |
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

Next executable action: complete T0 document alignment and issue mapping once repository identity is supplied. First coding task after planning approval: **T1 Native build skeleton**, blocked until MSVC x64 availability is established. No implementation task is marked DONE.
