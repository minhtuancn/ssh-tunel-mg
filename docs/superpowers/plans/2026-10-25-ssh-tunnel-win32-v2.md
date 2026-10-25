# SSH Tunnel Manager Win32 — Implementation Plan v2.0

> **For agentic workers:** Thực hiện từng task theo TDD/verification, ưu tiên `superpowers:subagent-driven-development` hoặc `superpowers:executing-plans` **nếu các skill này hiện diện**; nếu không thì làm theo checklist trong plan. **Không tự đánh dấu done nếu chưa chạy test thật.**  
> **Date:** 2026-10-25
> **Repository survey:** 2026-10-25 — no Git repository or application source exists in the current workspace; MSVC and Linux integration host are BLOCKED. See `docs/implementation-status.md`.  
> **Specification:** `docs/superpowers/specs/2026-10-25-ssh-tunnel-win32-design.md` (canonical v2 baseline; source snapshot retained as `2026-10-25-ssh-tunnel-win32-design-v2.md`).  
> **Goal:** Ship v1.0 Release x64: native GUI, SSH profile, TCP tunnels có remote target tùy chọn, Linux port discovery, systemd/process management, SQLite persistence, security + tests. v1.1/v1.2 đi theo backlog riêng.

## 0. Rules of engagement / quy trình agent

### 0.1 Luôn kiểm tra trước khi sửa

- [ ] Đọc `README`, `AGENTS.md`, `CMakeLists.txt`, code, tests, docs, git status, branches, issues/PR, CI/workflows hiện có.
- [ ] Không ghi đè thay đổi chưa commit của người khác. Nếu repo chưa có các tệp này, tạo file theo plan; nếu đã có, refactor có kiểm tra tương thích.
- [ ] Xác nhận máy chạy agent có toolchain Windows/MSVC; nếu không, chỉ chạy được bước static/source checks, **không** tuyên bố Windows build/GUI pass.
- [ ] Lập bảng `Task → files → tests → status → evidence` trong `docs/implementation-status.md`; cập nhật sau mỗi task.
- [ ] Lưu quyết định kiến trúc quan trọng trong `docs/adr/`; thay đổi spec cần được phản ánh vào acceptance tests.

### 0.2 Coding loop bắt buộc

1. Chọn task P0 kế tiếp còn pending và đọc acceptance criteria liên quan trong spec.
2. Viết test/fixture tái hiện case (RED) nếu có thể, rồi implementation tối thiểu (GREEN), refactor.
3. Build + run tests thích hợp **trên Windows/MSVC**; sửa tới khi pass, không bỏ qua test đang fail.
4. Kiểm thử ứng dụng có thể nhìn thấy/điều khiển được: khởi động exe, verify controls, thực hiện ít nhất một smoke path thật khi môi trường cho phép.
5. Review security/resource/UX, cập nhật docs, commit thay đổi nhỏ, gắn issue/PR khi có GitHub workflow.
6. Đóng issue chỉ khi toàn bộ acceptance tương ứng có evidence; PR chỉ merge sau CI, review và kiểm thử cần thiết; không xóa branch còn công việc hoặc thay đổi chưa merge.

**Không được:** tự tăng scope P1/P2 vào v1.0; làm giả test pass, screenshot, binary, benchmark; xóa/truncate DB user; tắt host-key checking; chạy destructive remote command trên production để test; tự bấm sudo hoặc bỏ qua xác nhận.

### 0.3 Gate phát hành

| Gate | Bắt buộc |
|---|---|
| G0 | Spec v2, assumptions, UX wireframe, API/state contract được chốt |
| G1 | Build x64, self-check, UI window skeleton chạy thật |
| G2 | SSH auth/host-key handling + tunnels end-to-end hoạt động |
| G3 | Ports/systemd/process + SQLite + GUI hoàn chỉnh |
| G4 | Security + Windows/Linux integration + clean machine smoke + package |

Nếu môi trường không có Windows hoặc SSH Linux test host, ghi rõ `BLOCKED — missing environment` và chỉ report những gì đã thực sự chứng minh. Không hạ tiêu chí release.

---

## 1. File/module map sau khi hoàn tất

```text
CMakeLists.txt
CMakePresets.json
build.ps1                         # chỉ dùng khi build, không phải runtime dependency
src/
  main.cpp                       # DPI + WinMain + message loop
  app.h / app.cpp                 # main window / controller
  ui/
    sidebar.h/.cpp                # server navigation
    dashboard.h/.cpp
    ports_view.h/.cpp
    tunnels_view.h/.cpp
    services_view.h/.cpp
    processes_view.h/.cpp
    activity_view.h/.cpp
    tunnel_dialog.h/.cpp
    connection_dialog.h/.cpp
    ui_messages.h                 # typed WM_APP + request generation
  domain/
    models.h                     # Profile, TunnelPreset, Service, Port, enums
    state_machine.h/.cpp
    validation.h/.cpp
  ssh/
    ssh_process.h/.cpp            # CreateProcessW, pipes, lifetime, bounded output
    ssh_auth.h/.cpp               # preflight, known-host/agent errors, console onboarding
    ssh_arguments.h/.cpp          # argv builder, Windows quoting
    tunnel_manager.h/.cpp        # owned process registry + local TCP bind
  linux/
    commands.h/.cpp               # fixed allowlisted remote operations
    ports_parser.h/.cpp
    systemd_parser.h/.cpp
    process_parser.h/.cpp
  storage/
    storage.h/.cpp                # SQLite CRUD + migration
    migrations.h/.cpp
  core/
    task_queue.h/.cpp            # bounded short tasks + cancellation
    diagnostics.h/.cpp           # redaction, error taxonomy
    win_handle.h                 # RAII HANDLE, Job Object wrappers
resources/
  app.manifest / app.rc
  openssh/                      # pinned binary + deps + NOTICES, packaged, not secrets
tests/
  unit/                         # validation, parser, state, SQL, argument builder
  integration/                  # SSH test server/fixtures on controlled host
  smoke/                        # GUI/manual checklist
third_party/
  sqlite3.c / sqlite3.h / LICENSE
packaging/
  package.ps1 / RELEASE_NOTES.md / THIRD_PARTY_NOTICES.md
docs/
  implementation-status.md
  adr/
  testing.md
  security.md
```

**Giữ cấu trúc repo đang có nếu hợp lý**; sơ đồ này là logical module boundaries, không phải bắt buộc di chuyển/mở tệp trống hàng loạt. Không tạo dependency mới nếu chưa có nhu cầu rõ.

### Interfaces/cấu trúc dữ liệu cốt lõi

```cpp
struct SshProfile {
  ProfileId id;
  std::wstring name, sshTarget;      // host alias / FQDN / IPv4 / IPv6
  std::optional<std::wstring> user;   // leave config defaults when absent
  std::optional<uint16_t> sshPort;
  IdentitySource identitySource;      // ssh_config / ssh_agent / identity_file
  std::optional<std::wstring> identityPath;
};

struct TunnelPreset {
  TunnelId id;
  ProfileId profileId;
  std::wstring name, localBind;       // v1.0 = 127.0.0.1
  uint16_t localPort;
  std::wstring targetHost;
  uint16_t targetPort;
  UrlScheme scheme;                   // none / http / https
};

enum class SshTaskKind { Preflight, ReadPorts, ReadServices, ReadProcesses,
                         ServiceMutation, SignalProcess, Tunnel };
enum class TunnelSummary { Stopped, Starting, Listening, Healthy,
                           Degraded, Error };
enum class TargetCheck { NotConfigured, Unknown, Passed, Failed };
```

Contracts chi tiết có thể đổi khi implement nhưng phải giữ semantic và viết migration/tests nếu thay đổi persisted schema.

---

## Dependency graph and milestones

```text
T0 → T1 → T2
          ├→ T3 → T4 → T5 (TCP tunnel vertical slice)
          │          ├→ T6 (ports)
          │          ├→ T7 (systemd)
          │          └→ T8 (processes)
          └→ T9 (SQLite; parallel with T3–T8 after T2)
T5 + T6 + T7 + T8 + T9 → T10 (full UI integration)
T2–T10 → T11 (security/integration) → T12 (package/release)
```

**Critical path:** T0 → T1 → T2 → T3 → T4 → T5 → T10 → T11 → T12. T6/T7/T8/T9 can proceed in parallel after their prerequisites only when each worker owns distinct files/worktree. T10 integrates all modules. Do not assign overlapping UI/command files to parallel agents; establish branch/worktree ownership first.

**Milestones:** M1=T1; M2=T2–T4; M3=T5–T6; M4=T7–T9; M5=T10–T11; M6=T12. Gates G1–G4 above are release gates; blocked Windows/Linux environments never count as passing.

## Task 0 — Chốt contract/spec và baseline [P0]

**Deliverables:** design v2 đưa vào đúng path, `docs/implementation-status.md`, flow diagrams/ADR nếu thiếu.  
**Depends:** none.

- [ ] Kiểm tra toàn bộ feature/spec v2, tạo bảng `P0/P1/P2` và link đến plan task; scope loại trừ phải rõ.
- [ ] Thống nhất: Connect = preflight SSH ngắn hạn; Tunnel = process dài hạn; Profile `Verified` có timestamp; không phải persistent connection.
- [ ] Thống nhất **key/agent/ssh config only ở v1.0**, không app-managed private-key vault/DPAPI raw key; không GUI passphrase giả.
- [ ] Thống nhất `127.0.0.1` default bind, target cuối có thể là IP khác SSH server, UDP không hỗ trợ tunnel -L.
- [ ] Tạo bảng UI states, empty/errors, confirmation text và route của các nút theo spec.
- [ ] Tạo test matrix với environment thực/không thực, đánh dấu test requirements cần remote host; dùng chi tiết `docs/TEST_PLAN.md`.
- [ ] Duy trì `docs/implementation-status.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/AGENT_HANDOFF.md`, ADR; nếu path không có Git thì ghi blocker, không tuyên bố đã commit.
- [ ] Không đưa app-managed key vault vào P0; sử dụng ADR 002.
- [ ] Bảo vệ `openclaw-126 info.txt`: nghi token URL đã lộ; không copy giá trị vào tài liệu/log; chủ sở hữu cần rotate/revoke token và redact/xóa artifact nhạy cảm.

**Done:** contract nhất quán giữa spec + plan; mọi feature P0 có task/test; trạng thái, blocker và evidence được ghi. Commit docs trước feature code khi repository Git được xác định.

## Task 1 — CMake/MSVC Release x64 + runnable GUI skeleton [P0]

**Files:** `CMakeLists.txt`, `CMakePresets.json`, `src/main.cpp`, `resources/app.manifest`, `resources/app.rc`, `build.ps1`, `tests/unit/self_check.cpp`.

- [ ] Thiết lập C++17, Win32 GUI target, DPI awareness per-monitor v2, Common Controls v6 manifest, resource version/icon placeholder có license hợp lệ.
- [ ] Bắt đầu từ tối thiểu một main window + menu + status label; phân tách UI thread/message loop.
- [ ] Build script có `-Configure`, `-Build`, `-Test`, `-Package`, `-Clean` (Clean chỉ xóa build artifacts đã xác định, không đụng user data).
- [ ] Tạo test runner CTest, code warnings/Release x64, không cần runtime PowerShell/.NET.
- [ ] Chạy `./build.ps1 -Build` và `./build.ps1 -Test` bằng PowerShell trên máy dev Windows; test launch `.exe` thực tế.
- [ ] Ghi exact compiler/SDK/CMake versions và build command trong docs.

**Done:** `SSHTunnelManager.exe` mở/đóng được trên Windows thật, không crash, CTest xanh; nếu không có Windows → BLOCKED.

## Task 2 — Models, input validation, command & argv builder [P0]

**Files:** `src/domain/*`, `src/ssh/ssh_arguments.*`, `src/linux/commands.*`, `tests/unit/*`.

- [ ] RED tests: port 1/65535 valid, 0/65536/non-number invalid; PID zero/overflow/negative; IPv4/IPv6, DNS, ssh alias; host bắt đầu `-` hoặc chứa control chars bị reject; unit bắt đầu `-`, shell metacharacters bị reject.
- [ ] Dùng kiểu riêng cho `SshTarget`, `TargetHost`, `LocalBind`, `UnitName`, `ProcessIdentity`; không trộn SSH server với target service.
- [ ] Tạo argv builder và Windows `CreateProcessW` quoting độc lập, không nối command thành chuỗi rồi đưa qua `cmd.exe`.
- [ ] Tạo Linux fixed-command builder: chỉ cho phép operation enum + validated parameters; remote shell escaping/locale/option termination rõ ràng; reject arbitrary text.
- [ ] Unit names v1.0 chỉ `.service` theo allowlist, không chứa slash/shell control, không chấp nhận leading `-`; ghi rõ giới hạn tên unit phức tạp nếu có.
- [ ] Tạo fixtures valid + malicious examples, verify output expected cho mọi builder, kể cả UTF-16 Windows paths có spaces.

**Done:** parser/validator test pass; không còn string-concatenation shell injection từ UI-provided host/unit/PID/port.

## Task 3 — Bounded asynchronous SSH process runner & lifecycle [P0]

**Files:** `src/ssh/ssh_process.*`, `src/core/win_handle.h`, `src/core/task_queue.*`, `src/core/diagnostics.*`, tests.

- [ ] `StartSshTask` dùng absolute packaged `ssh.exe` với explicit `lpApplicationName`, không shell; credential không qua commandline.
- [ ] Implement reader đồng thời stdout/stderr; bounded buffers (gợi ý stdout 1–4MiB tùy operation, stderr ≤256KiB); đánh dấu truncated, phân biệt stdout data và stderr diagnostics.
- [ ] Implement task state/IDs, timeout, cancellation, wait/cleanup; message tới UI bằng typed `WM_APP` + generation/request id; không chạm UI controls ngoài UI thread.
- [ ] Quản lý per-task handles RAII; xử lý lỗi `CreateProcessW`, broken pipes, early exit, hung process, double cancel, duplicate completion.
- [ ] Thử Job Objects quản lý child/job; test nested jobs / incompatible security contexts; fallback an toàn bằng owned handle nếu assignment fail, không kill SSH bên ngoài.
- [ ] Thực hiện test helper subprocess giả lập: thoát nhanh/timeout/output vô hạn/stderr dài/stdout+stderr cùng lúc/cancel đồng thời/shutdown race.
- [ ] Test không có child/process leak; app thoát khi một worker đang báo kết quả không gây use-after-free.

**Done:** mock subprocess tests xanh; UI không treo khi task fail/cancel/output lớn; owned process cleanup deterministic khi bình thường.

## Task 4 — Bundled OpenSSH, SSH config + authentication preflight [P0]

**Files:** `resources/openssh/*`, `src/ssh/ssh_auth.*`, `src/ssh/ssh_arguments.*`, packaging manifests, tests.

- [ ] Chọn nguồn OpenSSH Win64 đáng tin, pin version và SHA256 cho từng binary/DLL, kiểm license/notices và dependency closure. Không tải binary thiếu xác minh tại runtime.
- [ ] Resolve absolute path packaged binary; thiếu/bị corrupt → `DEPENDENCY_MISSING` hoặc `INTEGRITY_FAILURE` rõ ràng.
- [ ] Support host alias từ `%USERPROFILE%\.ssh\config` bằng cách để OpenSSH đọc cấu hình, tránh phá `HostName/User/Port/IdentityFile/ProxyJump`; identity file/agent fallback được định nghĩa rõ.
- [ ] Background `BatchMode=yes` cho preflight/query/tunnel; test SSH key không passphrase, key loaded agent, alias, non-22 ports, ProxyJump nếu test env có.
- [ ] Host key unknown: **không** suppress/auto-trust. Tạo UI hướng dẫn + launch OpenSSH **console onboarding** tách biệt khi user yêu cầu; người dùng phải xác nhận SHA256 fingerprint qua kênh tin cậy; sau onboarding preflight lại.
- [ ] Host key changed = blocking error; passphrase locked without agent = actionable error; không giả định redirected pipe tự giải quyết tương tác.
- [ ] Test auth failure, timeout, missing key, missing agent, changed known_hosts, first-use onboarding, command failure, canceled preflight.
- [ ] Không bắt người dùng phải có system OpenSSH; Windows ssh-agent có thể không tồn tại/không chạy trên máy sạch và phải được báo đúng.

**Done:** các trường hợp xác thực pass/fail thể hiện đúng; không có `StrictHostKeyChecking=no`, không lưu private key/password.

## Task 5 — Tunnel manager `-L`, state machine, health semantics [P0]

**Files:** `src/ssh/tunnel_manager.*`, `src/domain/state_machine.*`, `src/ui/tunnel_dialog.*`, tests.

- [ ] Tunnel config `sshProfile + localBind + localPort + targetHost + targetPort + URL scheme`; multi-tunnel cho cùng profile và các profile khác nhau.
- [ ] Form Create Tunnel validate fields, prefill từ selected TCP port; UDP disabled. Wildcard `0.0.0.0`/`::` không được dùng nguyên làm target.
- [ ] OpenSSH options: `-N -T`, `BatchMode=yes`, `ExitOnForwardFailure=yes`, `ConnectTimeout`, `ServerAliveInterval`, `ServerAliveCountMax`; không `-f`, không `-g`, không `-A` mặc định.
- [ ] Check local bind conflict trước start; sau start vẫn xử lý race `bind failed` qua process exit/stderr; localhost-only; test IPv6 target (bracket syntax).
- [ ] Process + listener state tách riêng; exit status và process handle là nguồn lifecycle; `Listening` **không** đồng nghĩa target reachable.
- [ ] Target check tùy protocol, mặc định Unknown; chỉ chuyển Healthy khi có end-to-end protocol-specific probe đáng tin, không coi local TCP connect là đủ.
- [ ] Stop one / Stop All chỉ owned process; nếu graceful stop không thành công, cho force local process với confirmation; không kill PID từ DB/scan tasklist.
- [ ] Test 2 tunnels cùng profile; local port collision; SSH server up, target down; SSH server killed; start then stop quickly; cancel during start; unknown target check; Open Browser chỉ với http/https scheme và chỉ khi người dùng bấm/opt-in.

**Done:** SSH gateway example Windows `127.0.0.1:15432` → SSH `10.20.10.100` → target `10.20.10.103:5432` hoạt động qua mạng test; đúng state và không ảnh hưởng `ssh.exe` ngoài app.

## Task 6 — Linux ports discovery & parsers [P0]

**Files:** `src/linux/ports_parser.*`, `src/linux/commands.*`, `src/ui/ports_view.*`, tests.

- [ ] Fetch `ss` output TCP/UDP IPv4/IPv6 với `LC_ALL=C` / `ss -H -ltnup` hoặc equivalent đã kiểm tra; phân biệt query error với empty result.
- [ ] Parser typed struct: protocol, local addr, port, PID/process optional, raw line reference hạn chế; network interfaces wildcard và scope IPv6 hiển thị đúng.
- [ ] Fixtures: `ss` thiếu PID/process do permission, `*:80`, `127.0.0.1:5432`, `[::1]:8080`, `0.0.0.0:...`, UDP, process name có spaces, malformed/truncated lines.
- [ ] Tạo list columns, sorting/search/filter TCP/UDP, Refresh với loading/error/empty states; Create Tunnel chỉ enabled khi selection TCP valid.
- [ ] Test `ss missing`, parser failure, permission restricted, disconnected, query canceled, output quá lớn.

**Done:** port list thực từ host Linux, trường unavailable thể hiện rõ và luôn có đường tạo tunnel 2 bước cho TCP.

## Task 7 — systemd service inspector/actions [P0]

**Files:** `src/linux/systemd_parser.*`, `src/linux/commands.*`, `src/ui/services_view.*`, tests.

- [ ] Query list service bằng `systemctl list-units --type=service --all --no-legend --plain --no-pager`, tách system/user (`systemctl --user`).
- [ ] Query unit details bằng `systemctl show` properties/parse `key=value` và phân biệt `load/active/sub/file` (không parse tiếng Anh trong `status` như source of truth).
- [ ] `start/stop/restart` enum fixed, validated unit `.service`, không sudo mặc định; prompt confirmation hiện host/unit/action, log audit redacted.
- [ ] Disable button double submit; pre-query + mutation + post-query; cancel sau dispatch báo `REMOTE_OUTCOME_UNCERTAIN` và post refresh.
- [ ] Test active/inactive/failed/missing service, system vs user, access denied, no systemd, transient service và slow operation timeout; fake parser fixtures + Linux sandbox test.

**Done:** systemd thao tác chính xác trên test service riêng; không tự sửa cấu hình/init và không nâng quyền ngầm.

## Task 8 — Linux processes / TERM / KILL [P0]

**Files:** `src/linux/process_parser.*`, `src/linux/commands.*`, `src/ui/processes_view.*`, tests.

- [ ] Read-only process list/detail bằng fixed commands (`ps`/`/proc`), owner + start-time identity nếu có; giới hạn số row, output và refresh.
- [ ] Cho phép chọn PID dương hợp lệ, không phải PID 1; validate input + option injection + identity freshness.
- [ ] TERM tách KILL; KILL có confirmation nổi bật, hiển thị host/user/PID/command; nếu target đổi giữa refresh và action thì reject.
- [ ] Thực thi tín hiệu chỉ bằng allowlisted remote command, không sudo; stop thao tác khi không đủ quyền.
- [ ] Test stale PID/reuse, invalid PID, no permission, exited process, TERM, forced KILL trên **disposable test process**; tuyệt đối không test destructive trên host production.

**Done:** thao tác chỉ xảy ra với process đã được xác nhận đủ thông tin; mỗi mutation có before/after evidence.

## Task 9 — SQLite persistence + migrations [P0]

**Files:** `src/storage/storage.*`, `src/storage/migrations.*`, `third_party/sqlite3.*`, tests.

- [ ] Pin SQLite amalgamation, license, CMake linkage; location `%LOCALAPPDATA%\SSHTunnelManager\data.db`, kiểm ACL user; handle lỗi không ghi được DB.
- [ ] Schema: profiles, tunnel_presets, service_favorites, recent_activities, app_settings/schema_version. Dùng FK, unique constraints, prepared statements, indices tối thiểu.
- [ ] CRUD save/load profiles/presets/favorites; validation cả UI và DB; transactions + migrations với rollback; backup có kiểm soát trước migration phá schema.
- [ ] Kiểm migration từ empty v0→v1, corrupt DB, permission denial, crash giữa transaction, profile delete cascade behavior, foreign key on.
- [ ] **Không triển khai DPAPI private-key vault trong v1.0**. Lưu identity file path, không lưu key bytes. Test scan DB và logs không có raw keys/password/passphrase.
- [ ] Không persist live PID/handles/runtime state; reopen app chỉ hiện presets stopped.

**Done:** roundtrip/migration tests xanh; DB không chứa secrets và không kill process sau restart bằng stale PID.

## Task 10 — Full Win32 UX, task progress, close/recovery [P0]

**Files:** `src/app.*`, `src/ui/*`, `src/core/task_queue.*`, resources, tests/smoke.

- [ ] Sidebar server navigator + tabs Dashboard, Ports, Tunnels, Services, Processes, Activity. UI state đến từ typed models, không parse strings trong Win32 event handler.
- [ ] Add/Edit/Delete profile, Connection Test; Add/Edit/Start/Stop tunnel, Copy address, Open Browser conditional, Save preset.
- [ ] Integrate ports/service/PID actions với confirmations; buttons disabled trong khi action; per-row status/loading, indeterminate progress, cancel đúng semantics.
- [ ] Search/sort/filter tables; persist column widths/window position; keyboard & tab order; context menu đơn giản; tiếng Việt thống nhất bằng string resources.
- [ ] Test responsive UI khi truy vấn blocked/timeout/long output; worker complete after window destroyed không gọi HWND invalid; generation/request id bỏ stale messages.
- [ ] `WM_CLOSE` khi tunnel alive: Stop all & Exit / Cancel; local process teardown + bounded wait; khi remote mutation pending báo uncertain, refresh nếu còn thời gian/scope.
- [ ] Per-monitor DPI v2, min window size, 1366×768, 100/125/150/200% DPI; test keyboard/mouse focus/screen-reader basics khi có công cụ.
- [ ] Confirm không có tray/minimize-to-tray giả trong v1.0; tính năng chỉ issue P1.

**Done:** UI chạy với Linux thật và luồng `profile → ports → tunnel → localhost → stop` hoàn chỉnh, không crash/hang.

## Task 11 — Security, fault-injection, integration suite [P0]

**Files:** `tests/unit/*`, `tests/integration/*`, `tests/smoke/*`, `docs/testing.md`, `docs/security.md`.

### Automated unit/security

- [ ] Host/unit/PID/IP/port validation fuzz-ish matrix: quotes, `;`, `&`, `$()`, backticks, newline, Unicode, IPv6 colon/brackets, leading options.
- [ ] Bounded stdout/stderr + broken pipe + cancellation + timeout + thread/handle leak tests.
- [ ] State transition tests: start → listen → unknown target, bind fail, remote drop, stop, error. Healthy chỉ khi probe xác thực.
- [ ] SQLite migration/FK/error/secret scan; resource leak checks; redaction snapshot tests.
- [ ] Confirm no exec via shell on Windows and no arbitrary remote command from untrusted UI inputs.

### Integration on controlled Linux test host

- [ ] Test SSH port 22 và custom port, passwordless key/agent, known_hosts known/unknown/changed, wrong user/host.
- [ ] Test local TCP proxy target trên SSH server (`127.0.0.1`) và target **khác máy** (`10.x.x.x`), liveness/useful data đi qua TCP thật.
- [ ] SSH server alive + target down, SSH server disconnected, bind local already used, two active tunnels, unrelated external SSH process unaffected.
- [ ] `ss` with/without process visibility and IPv4/IPv6/UDP, service sandbox for start/stop/restart system/user, test process TERM/KILL.
- [ ] Cancel mutation sau dispatch phải nhận trạng thái uncertain và refresh state.

### Manual Windows GUI/clean machine

- [ ] Release x64 trên Windows 10 và Windows 11 (VM sạch) **không có system OpenSSH/.NET runtime riêng**: launch, SSH test, port scan, start tunnel, service view, close.
- [ ] 1366×768, DPI 100–200%, keyboard accessibility, long Vietnamese text, error modal and remote latency.
- [ ] Smoke unexpected app exit và handle cleanup; không nhận bừa stale PID từ DB.
- [ ] Báo kết quả theo bảng: test name, environment, steps, expected, actual, screenshot/log redacted, pass/fail/blocked.

**Done:** không còn P0 critical bug/security blocker; mọi testcase bắt buộc có evidence hoặc ghi BLOCKED và release phải hoãn.

## Task 12 — Packaging, measured footprint & release [P0]

**Files:** packaging scripts/metadata, `README.md`, `CHANGELOG.md`, `docs/*`, binary artifact.

- [ ] Deterministic Release x64 build từ clean checkout; package Win32 `.exe`, bundled `ssh.exe` & dependencies, SQLite, resources, notices (portable zip ưu tiên; installer tùy thời gian và yêu cầu).
- [ ] Tạo manifest `binary provenance/version/SHA256`, app version, license notices, `THIRD_PARTY_NOTICES.md`.
- [ ] Tài liệu install/first-use: SSH key, ssh-agent, known_hosts onboarding, `ssh_config` alias, Create Tunnel, target-other-machine, troubleshooting.
- [ ] Smoke clean Windows machine lần cuối với artifact thực; không dựa vào DLL/build tools vô tình có trong dev machine.
- [ ] Đo binary/package size, working set khi idle/1 tunnel/5 tunnels, startup time; ghi **số thực**, môi trường và phương pháp đo, không đưa số giả vào release notes.
- [ ] `README` có screenshot chạy thật (nếu chụp được), version, known limitations và security notes; bảo đảm docs đúng v1.0, không liệt kê P1 như đã xong.
- [ ] Tag/release sau khi tests, reviews, audit pass; issue/PR đóng đúng evidence; không xóa nhánh còn PR pending.

**Done:** release candidate trên Windows sạch hoạt động; người dùng có thể tải và dùng mà không cần cài .NET/PowerShell/system OpenSSH.

---

## 2. Acceptance test matrix (bắt buộc liên kết implementation)

| ID | Feature | Preconditions/input | Execution | Expected result / negative case | Automation/evidence/release gate | Task |
|---|---|---|---|---|---|---|
| A01 | Trusted SSH preflight | Controlled host; known_hosts; valid key/agent | Test Connection | Verified timestamp; not persistent-session claim; auth failure actionable | Integration + UI report; G2 | 3,4,10 |
| A02 | Unknown/changed host key | Controlled host-key fixtures | First connect, then rotate host key | Unknown requires explicit fingerprint verification; changed blocks; never bypass | Integration/manual, redacted fingerprint evidence; G2 | 4,11 |
| A03 | Locked key/no agent | Passphrase-locked key; agent absent | Background preflight | Actionable error; no hanging prompt/password persistence | Integration logs + DB scan; G2 | 4,9 |
| A04 | TCP/UDP IPv4/IPv6 ports | Linux `ss`; missing-owner fixtures | Refresh ports | Typed rows, unavailable owner explicit, malformed lines warned | Parser unit + Linux integration; G3 | 6,11 |
| A05 | UDP selection | Select UDP listener | Open tunnel dialog/action | TCP `-L` disabled with explanation; no process started | GUI smoke screenshot; G3 | 5,6,10 |
| A06 | Tunnel to SSH-host loopback | Controlled TCP service on gateway | Start and exchange bytes | Useful TCP flow works; owned listener closes on stop | Linux integration evidence; G2 | 5,11 |
| A07 | Tunnel to another remote-LAN host | Gateway reaches disposable TCP target | Start with distinct target host; exchange bytes | Forwarded traffic reaches second host | Linux integration evidence; G2 | 5,11 |
| A08 | Port conflict/race | Busy port; race fixture | Start before/after port claim | `PORT_IN_USE`/`BIND_FAILED`; no silent success | Unit + Windows integration logs; G2 | 5,11 |
| A09 | Target unreachable | Gateway alive; target unavailable | Start tunnel and optional probe | Listening distinct from target Unknown/Failed; never false Healthy | Integration state report; G2 | 5,11 |
| A10 | Unrelated SSH process | External ssh.exe plus owned tunnel | Stop tunnel | Only owned process stops | Windows process evidence; G2 | 3,5,11 |
| A11 | systemd system/user | Disposable units and restricted case | List/show/start/stop/restart with confirmation | Pre/post state; permission errors; no sudo | Linux integration logs; G3 | 7,11 |
| A12 | PID identity/reuse/permission | Disposable process; stale identity fixture | TERM, then separately confirm KILL | Reject stale/unsafe PID; KILL explicitly confirmed | Unit + disposable Linux evidence; G3 | 8,11 |
| A13 | Network loss/timeout/cancel/output | Controlled drop, delayed response, large streams | Run/cancel tasks at phases | Responsive UI; correct status; no owned process leak; mutation uncertain | Fault injection logs + process snapshot; G4 | 3,5,10,11 |
| A14 | DB migration/restart | Empty, valid, corrupt/interrupted DB copies | Migrate/save/reopen | Presets restored stopped; no live PID/raw key; corruption actionable | SQLite tests + sanitized DB scan; G3 | 9,11 |
| A15 | Close with tunnel | Active tunnel and pending task | Choose Cancel and Stop & Exit | Cancel preserves app/tunnel; exit stops owned tunnel safely | GUI evidence + process state; G4 | 3,10,11 |
| A16 | Clean Windows VM | Windows 10/11 x64, no OpenSSH/.NET | Launch packaged app and smoke SSH | Runs with bundled dependencies only | Clean VM report and artifact hashes; G4 | 1,4,12 |
| A17 | Injection | Host/unit/PID/port malicious inputs | Validation/command tests | Reject/safely encode; no shell execution or arbitrary remote command | Security suite report; G4 | 2,7,8,11 |
| A18 | DPI/keyboard UX | 1366×768; DPI 100/125/150/200 | Resize, keyboard navigation, dialogs | Controls usable, focus visible, no clipping | Windows manual screenshots/checklist; G4 | 1,10,11 |

## 3. Example user journey — end-to-end test script

> Dùng Linux test server/gateway do người dùng kiểm soát. IP ví dụ là placeholders, không chạy lệnh destructive trên host thật.

1. Add profile `Linux-Gateway`, SSH target `10.20.10.100`, user `testuser`, key/agent.
2. Test Connection: host key trusted, preflight thành công; timestamp Verified được cập nhật.
3. Ports tab: fetch `ss -H -ltnup`, thấy postgres TCP 5432 và một UDP port; UDP không cho Create Tunnel.
4. Create Tunnel, chọn `Target host = 10.20.10.103`, `Target port = 5432`, `Local bind = 127.0.0.1`, `Local port = 15432`.
5. Start: UI `Starting` → `Listening`. Nếu chưa probe PostgreSQL thật thì `target_check = Unknown`.
6. Từ Windows test bằng client PostgreSQL (hoặc controlled TCP echo endpoint nếu không có PostgreSQL) tới `127.0.0.1:15432`; ghi evidence về byte traffic/transaction.
7. Tắt remote target test: SSH vẫn `Listening` có thể vẫn tồn tại; hoạt động protocol không thành công thì `target_check` có thể `Failed` nếu có probe. Không tô xanh `Healthy` giả.
8. Stop Tunnel: owned process dừng; kiểm chứng other unrelated SSH session còn sống.
9. Mở lại ứng dụng: preset được load, tunnel không autostart; không dùng PID cũ để kill.

## 4. Release workflow và cách report cho user

Sau mỗi task, report ngắn theo mẫu:

```text
Task: <ID/name>
Changed files: ...
Build: PASS/FAIL/BLOCKED — exact command, environment
Unit tests: X passed / Y failed
Integration/GUI: PASS/FAIL/BLOCKED — concrete evidence
Security review: findings + mitigations
Known blockers: ...
Next task: ...
Commit/PR/issue: <link or N/A>
```

**Final v1.0 readiness:** chỉ `READY` nếu tất cả P0 acceptance A01–A18 pass, clean-machine artifacts đã có, không có security blocker; nếu thiếu test environment ghi `NOT READY — BLOCKED` dù source code có vẻ đã xong.

## 5. v1.1 backlog (không chặn v1.0)

- [ ] **B01** System tray; close-to-tray theo user setting, actual Exit dừng owned tunnel; icon/tray menu trạng thái.
- [ ] **B02** Auto reconnect (OFF default), exponential backoff cap, network changes + Windows sleep/wake, stop retry cho auth/hostkey failure; test không duplicate process.
- [ ] **B03** Server groups, service favorites, search nâng cao, saved filters, keyboard shortcuts.
- [ ] **B04** `journalctl` recent logs (100–500 lines, bounded, sanitize), optional sudo -n **chỉ sau threat model & UX approval**, không tự nâng quyền.
- [ ] **B05** Profile import/export không kèm secrets và cải thiện SSH config parser/diagnostics.
- [ ] **B06** Optional protocol-aware TCP/HTTP/DB checks; định nghĩa handshake trước khi hiển thị Healthy.

## 6. v1.2+ backlog (cần security design bổ sung)

- [ ] **C01** SOCKS5 `-D` và reverse `-R`, giới hạn bind/exposure, UI warning rõ.
- [ ] **C02** Launch on Windows login / startup tunnels opt-in và quy tắc auth không tương tác.
- [ ] **C03** Binds ngoài loopback để chia sẻ LAN, chỉ khi có threat model và consent rõ.
- [ ] **C04** App-managed private-key vault DPAPI current-user **chỉ khi có lý do sản phẩm**, thiết kế temp file cleanup/crash hygiene riêng.
- [ ] **C05** Trải nghiệm dark theme nếu hỗ trợ đầy đủ Win32 controls mà không làm giảm độ ổn định.

## 7. Non-negotiable reminders

- `build.ps1` sử dụng PowerShell **chỉ ở build/dev**; ứng dụng phát hành tuyệt đối không gọi PowerShell hoặc .NET runtime.
- `ExitOnForwardFailure=yes` chỉ kiểm tra SSH forwarding setup, không xác minh đích cuối.
- Không dùng `0.0.0.0` làm local bind v1.0; không dùng UDP port với `-L`.
- `Connect` là test/preflight ngắn hạn; đừng thiết kế UI nói rằng profile giữ kết nối liên tục.
- Không tin stderr parsing là bằng chứng duy nhất: dùng exit status, process handle, socket bind, protocol probe khi có.
- Không tự thao tác sudo, không sử dụng shell tùy ý, không kill unrelated SSH, không lưu private key bytes ở v1.0.
- **Nếu có mâu thuẫn giữa spec/plan/code, sửa tài liệu và tests trước khi tiếp tục, không lặng lẽ làm theo suy đoán.**
