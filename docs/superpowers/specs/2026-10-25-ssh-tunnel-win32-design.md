# SSH Tunnel Manager — Product & Technical Design v2.0 (C++ Win32)

> **Trạng thái:** Revised specification — 2026-10-25  
> **Nguồn:** Cập nhật từ `2026-10-25-ssh-tunnel-win32-design.md`. Những yêu cầu gốc về Win32, OpenSSH, Linux, SQLite, quản lý ports/systemd/PID, tác vụ nền và bảo mật được giữ; các đề xuất UX, tunnel target linh hoạt, authentication thực tế và kiểm thử được bổ sung.  
> **Plan triển khai:** `docs/superpowers/plans/2026-10-25-ssh-tunnel-win32.md` (thay nội dung bằng file plan v2 đi kèm).  
> **Quy ước:** P0 = bắt buộc trước khi release v1.0; P1 = v1.1; P2 = v1.2+. Các tính năng P1/P2 không được âm thầm kéo vào scope v1.0.

## 1. Tầm nhìn và mục tiêu sản phẩm

Thay `tunnel.bat` bằng ứng dụng **Windows native, nhẹ, dễ dùng** để kết nối các máy Linux qua SSH, xem cổng/service/process và tạo TCP tunnel về Windows, kể cả khi đích cuối nằm trên **máy khác trong mạng phía SSH server**.

**Trải nghiệm đích:** chọn server → xem port đang listen → chọn port → tạo tunnel (cho phép sửa remote host/local port) → mở ứng dụng qua `127.0.0.1:local_port`. Người dùng luôn biết SSH có hoạt động không, cổng local đã lắng nghe chưa và đích cuối có được xác minh hay không.

### Nguyên tắc bất biến

1. Native **C++17 + Win32 Common Controls + Windows SDK + MSVC x64**; không cần .NET/PowerShell/OpenSSH cài sẵn **khi chạy**. `build.ps1` chỉ là công cụ lúc build.
2. Đóng gói OpenSSH client có kiểm soát version/provenance/license; không tạo SSH protocol implementation riêng.
3. Không chạy mạng/I/O/SQLite nặng trên UI thread; lỗi/timeout/cancel không được treo UI.
4. Default an toàn: local bind `127.0.0.1`, giữ host-key checking, không lưu password, không tự nâng quyền, không thực thi remote command tùy ý.
5. Chỉ quản lý/terminate SSH process được ứng dụng tạo và còn sở hữu handle; không đụng `ssh.exe` bên ngoài.
6. Ưu tiên SSH config + key/agent sẵn có; **v1.0 không import raw private key vào DB**. Cách này thay thế mục lưu key bằng DPAPI trong spec cũ; xem mục 9.
7. Không tự start lại tunnel khi mở app; không tự start/stop service Linux. Auto-reconnect chỉ có từ v1.1, opt-in.

## 2. Phạm vi theo phiên bản

| Khả năng | v1.0 P0 | v1.1 P1 | v1.2+ P2 |
|---|---|---|---|
| Nhiều SSH profiles, SSH config/agent, kết nối kiểm tra | Có | Nâng cấp import/quản lý | |
| Phân nhóm server, tìm kiếm đơn giản | Tìm kiếm profile | Nhóm + favorite | |
| Xem Linux TCP/UDP ports, PID/process nếu đủ quyền | Có | Filter nâng cao | |
| Local TCP forward `-L` tới localhost hoặc host khác phía Linux | Có | | |
| Nhiều tunnel đồng thời, start/stop riêng, preset | Có | | |
| Trạng thái SSH/listener/target rõ ràng, lỗi phân loại | Có | Probe chuyên biệt | |
| Xem/quản lý systemd system/user services | Có, không sudo tự động | Favorite + journal tail | |
| Process TERM/KILL theo PID, xác nhận | Có | | |
| SQLite, migration, audit tối thiểu, recovery | Có | Import/export không kèm secret | |
| UI dashboard/sidebar/tabs/DPI/keyboard | Có | Dark theme nếu khả thi | |
| Tray, minimize to tray, tự reconnect có giới hạn, sleep/wake | | Có, opt-in | |
| SOCKS5 `-D`, reverse `-R`, start cùng Windows | | | Có, opt-in |
| SSH terminal, SFTP, Docker/Proxmox manager, firewall, shell tự do | **Ngoài phạm vi** | **Ngoài phạm vi** | Xem xét dự án khác |

**Quy tắc release:** ship v1.0 sau khi tất cả P0 vượt acceptance tests; P1/P2 tạo issue riêng, không chặn v1.0.

## 3. UX / Information Architecture

### 3.1 Main window

```text
┌───────────────────────────────────────────────────────────────────────────┐
│ SSH Tunnel Manager                                  [Settings] [Help]    │
├──────────────────┬────────────────────────────────────────────────────────┤
│ Dashboard        │ Server: Linux-01  [Verified]      [Test] [Disconnect] │
│ Search servers   │ user@10.20.10.100:22  • Last checked: hh:mm:ss         │
│                  ├────────────────────────────────────────────────────────┤
│ Production       │  TCP Ports   Active Tunnels   Services   Last Error   │
│  • Linux-01      ├────────────────────────────────────────────────────────┤
│  • Database      │ [Ports] [Tunnels] [Services] [Processes] [Activity]   │
│ Development      │ Search / Filter                    [Refresh] [+Tunnel] │
│  • Dev-LXC       │                                                        │
│                  │  Local address   Port   Proto   PID   Process Action  │
│ [+ Add server]   │  127.0.0.1       5432   TCP     441   postgres Tunnel │
│                  │  0.0.0.0         8080   TCP     —     unknown  Tunnel │
│                  │  0.0.0.0         5353   UDP     —     unknown  Info   │
│                  ├────────────────────────────────────────────────────────┤
│                  │ Activity: Loading ports...  [Cancel if safe]          │
└──────────────────┴────────────────────────────────────────────────────────┘
```

Giao diện ASCII là bố cục chức năng, không phải mockup pixel-perfect. Thiết kế theo tab để không nhồi 5 bảng trên một trang.

### 3.2 Screens / tabs v1.0

- **Dashboard:** card số profiles/tunnels/ports/service, lỗi gần nhất, thao tác nhanh; card chỉ hiển thị số liệu vừa truy vấn, không ngụ ý real-time khi chưa refresh.
- **Servers (sidebar):** tìm profile, chọn server, Add/Edit/Delete, Test connection, Disconnect. Không đánh đồng profile `Verified` với SSH session tồn tại liên tục.
- **Ports:** cột `Protocol | Listen address | Port | PID | Process | Service (nếu map được) | Actions`; filter TCP/UDP, sort/search, Refresh; `Create Tunnel` chỉ bật với TCP; PID/process có thể là `Unavailable`.
- **Tunnels:** `Name | SSH server | Local bind:port | Target host:port | SSH state | Listener state | Target check | Uptime | Actions`; Start, Stop, Edit preset, Open Browser (chỉ HTTP/HTTPS preset), Copy address, Stop All Owned.
- **Services:** selector `system` / `user`, unit name, description, `LoadState/ActiveState/SubState/UnitFileState`, start/stop/restart với confirmation và refresh sau lệnh.
- **Processes:** PID, owner (nếu truy vấn được), command/process name, state, start identity (nếu có), TERM và KILL tách riêng; luôn cần xác nhận rõ host/PID/process.
- **Activity:** trạng thái tác vụ hiện tại, lịch sử gần đây (ẩn thông tin nhạy cảm), stderr đã redacted và copy diagnostic.
- **Settings:** SSH binary info/version, key source, timeout, log retention, data path, startup/UI prefs; thay đổi thiết lập an toàn.

### 3.3 Quy tắc UI

- Win32 `ListView` report/virtual mode nếu dữ liệu lớn, `TreeView` hoặc list sidebar; Tab/arrow/Enter/Escape, accelerator keys, focus visible.
- Tối thiểu hỗ trợ 1366×768; DPI per-monitor v2 100–200%; layout co giãn, min-size hợp lý, cuộn đúng, nhớ độ rộng cột và window placement.
- Search/filter không block UI. Những cột không khả dụng ghi `— / Không có quyền`, không đổi thành số 0.
- Empty states khác nhau: `Không có dữ liệu`, `Đang tải`, `Không có quyền`, `Thiếu lệnh ss`, `Chưa kết nối`, `Timeout`.
- Các nút destructive có xác nhận, ít nhất nêu **host + unit/PID + tác động**, và không được gửi trùng lệnh khi đang thực thi.
- Progress là indeterminate nếu không đo được phần trăm; hiển thị pha `Resolving → Authenticating → Querying → Parsing → Done`; cancel chỉ bật khi có ý nghĩa.
- Trong v1.0, đóng app có tunnel phải chọn **Stop tunnels & Exit** hoặc **Cancel**. Tray/Minimize to tray chỉ thêm từ v1.1.
- UI hiển thị ngôn ngữ tiếng Việt nhất quán; chuỗi hiển thị tách resource để sau này hỗ trợ tiếng Anh. Không render tùy tiện output server vào control có script/HTML.

### 3.4 Create Tunnel dialog (P0)

Fields: `Preset name`, `SSH profile`, `Bind address` (mặc định cố định `127.0.0.1`), `Local TCP port`, `Target hostname/IP` (mặc định `127.0.0.1` khi port listen trên loopback/all interfaces), `Target TCP port`, `URL scheme` (`none/http/https`), `Auto-open browser` (mặc định OFF), `Test mode` (local listener / optional protocol probe).

- Port chọn từ bảng Linux tự điền protocol, target, remote port; nếu service listen trên một IP cụ thể thì prefill IP tương ứng sau khi kiểm tra khả năng reachability từ SSH host. Người dùng được sửa.
- Port UDP: nút tunnel disabled + tooltip `OpenSSH -L chỉ chuyển tiếp TCP`; không tạo cảm giác UDP sẽ chạy được.
- `0.0.0.0`, `::` trong ss là **wildcard listen address**, không dùng nguyên chúng làm target cuối. Hiển thị gợi ý `127.0.0.1`/`::1` hoặc IP interface có thể truy cập.
- Nếu target là IPv6, biểu diễn host và cú pháp `-L` theo chuẩn OpenSSH đã kiểm thử; không nối bằng colon một cách ngây thơ.
- Không cho local bind `0.0.0.0` trong v1.0. Nếu muốn LAN sharing, viết RFC và cảnh báo security riêng.

## 4. Domain model và dữ liệu lưu

### 4.1 SSH profile

`id (UUID/local ID)`, `display_name`, `group_name (v1.1)`, `ssh_target` (host alias/FQDN/IP), `ssh_user` (optional if config resolves), `ssh_port` (optional override), `identity_source` (`ssh_config|ssh_agent|identity_file`), `identity_file_path` (optional), `notes`, `created_at`, `updated_at`.

- Nếu target là SSH config alias, giữ nguyên alias cho OpenSSH resolve `HostName`, `User`, `Port`, `ProxyJump`, `IdentityFile`. UI có thể hiển thị effective config bằng `ssh -G` nhưng chỉ là diagnostics, không áp đặt overrides gây mất ProxyJump.
- Đường dẫn `identity_file_path` chỉ lưu path; không copy nội dung private key vào DB.
- Không ghi password, passphrase, private key hay access token vào cấu hình/log.

### 4.2 Tunnel preset

`id`, `profile_id`, `name`, `local_bind` (`127.0.0.1`), `local_port`, `target_host`, `target_port`, `protocol` (`tcp`), `url_scheme`, `auto_open_browser` (false), `created_at`, `updated_at`.

`TunnelRuntime` **không persist**: `runtime_id`, `process_handle`, `job_handle`, `started_at`, `state`, `last_error`, `listener_check`, `target_check`. Process handles và PID runtime không được phục hồi từ SQLite để kill.

### 4.3 Service favorites và lịch sử

- Favorites: `(profile_id, scope, unit_name)`; scope = `system|user`.
- Activity: timestamp, profile id, action, result enum, duration, summary đã redact, không lưu nguyên stdout/stderr vô hạn, retention cấu hình.
- Settings: schema version, app version, timeout values, preferred UI locale, window state.
- SQLite `PRAGMA foreign_keys=ON`; schema migrations có version/transaction, backup DB trước migration rủi ro, rollback/repair error path rõ.

## 5. Luồng SSH / authentication (P0)

### 5.1 Phân biệt Test Connection và Tunnel

- **Test Connection** là SSH command *ngắn hạn* kiểm tra identity + host key + khả năng thực thi lệnh đọc tối thiểu; thành công → `Verified at timestamp`, không có nghĩa app đang giữ TCP connection.
- **Remote queries** (`ss`, `systemctl`, process) mở SSH task ngắn hạn, có timeout, không chiếm UI.
- **Start Tunnel** tạo process `ssh.exe` riêng, không yêu cầu terminal, được theo dõi liên tục; một profile có thể có nhiều tunnel.
- Hạn chế số task đồng thời theo host để tránh gọi quá nhiều ssh; tunnel lâu dài không chặn hàng đợi thao tác ngắn hạn.

### 5.2 Credential modes chính thức v1.0

1. **SSH config + ssh-agent** (ưu tiên): dùng Windows OpenSSH agent khi đang có và hoạt động; nếu không có thì hiển thị hướng dẫn rõ, không coi đây là dependency chắc chắn có trên máy sạch.
2. **Identity file**: `-i <path>`; hoạt động background chỉ nếu key không cần prompt hoặc đã unlock qua agent. Thông báo `Passphrase required` nếu `BatchMode=yes` bị từ chối.
3. **Host key chưa tin cậy**: quy trình onboarding tách riêng. Có thể mở phiên `ssh.exe` tương tác trong **console riêng** để OpenSSH hỏi xác nhận host key theo mặc định; hướng dẫn kiểm tra SHA256 fingerprint qua kênh tin cậy trước khi chấp nhận. Sau đó chạy lại background preflight. Không được đánh dấu `Verified` chỉ vì hiển thị fingerprint qua kết nối chưa tin cậy.

**Giới hạn đã quyết định:** v1.0 không cố dựng popup GUI nhận passphrase bằng cách chỉ redirect stdin/stdout. Không thu thập password/passphrase trong app; không `StrictHostKeyChecking=no`, `accept-new` ngầm, `UserKnownHostsFile=/dev/null` hay vô hiệu hóa kiểm tra known_hosts. Nếu không đáp ứng background key/agent, báo lỗi hữu ích, không fallback âm thầm.

### 5.3 Tham số SSH được quản lý

Ví dụ tương đương logic (không dùng shell để chạy):

```text
ssh.exe -N -T -o BatchMode=yes -o ExitOnForwardFailure=yes
        -o ConnectTimeout=10 -o ServerAliveInterval=15
        -o ServerAliveCountMax=3
        -L 127.0.0.1:15432:10.20.10.103:5432
        user@10.20.10.100
```

- Host-key checking của OpenSSH giữ mặc định/strict; `-N -T` chỉ dành cho tunnel. Query không dùng `-N`.
- OpenSSH port nếu specified phải truyền `-p`, user dùng `-l` khi phù hợp; target/alias truyền là argument riêng; không tự nối chuỗi chứa metacharacter.
- Không sử dụng `-f`; ứng dụng giữ child process handle để quản lý.
- `ExitOnForwardFailure=yes` xác nhận lập listener/forward, **không chứng minh target service phía cuối reachable**.
- Mỗi tunnel có đường lifecycle riêng và có thể start đồng thời; tránh race nếu local port clash.
- `ProxyJump`, ciphers và tùy chọn hợp lệ của `~/.ssh/config` được để OpenSSH xử lý, trừ các cấu hình vi phạm policy an toàn hoặc gây bất tương thích phải báo rõ.

## 6. Port discovery và trạng thái tunnel

### 6.1 Port discovery

- Linux-only; query `ss -H -ltnup` hoặc hai query TCP/UDP tương đương (đều đọc dữ liệu, không root mặc định); `LC_ALL=C` nếu control locale được, bounded stdout, timeout.
- Parser xử lý IPv4/IPv6, `127.0.0.1`, `0.0.0.0`, `::1`, `[::]`, wildcard, tên process chứa dấu cách, thiếu PID/process và output `ss` khác phiên bản. Những dòng không parse được phải log count và cảnh báo; không âm thầm bỏ qua toàn bộ.
- Linux host có thể hạn chế đọc PID/process của owner khác; hiển thị `Unavailable` và tooltip giải thích.
- Có lỗi riêng cho `ss missing`, `connection fail`, `permission restricted`, `parse error`, `empty (verified)`.

### 6.2 Tunnel state machine

```text
Stopped ──Start──> Starting ──SSH/listener ready──> Listening
    ▲                    │                                │
    │                    ├──fail──> Error                 ├──probe passed──> Healthy
    │                    └──cancel──> Stopped             ├──probe failed──> Degraded
    └──────────Stop/exit (owned process)──────────────────┤
                         │                                └──process exits──> Error
                         └──── v1.1 opt-in: Reconnecting (backoff/cap)
```

Mô hình trạng thái có **3 trường phân biệt**:

- `ssh_state`: `Starting|Running|Exited|Failed` theo owned process + SSH stderr/exit status.
- `listener_state`: `Unknown|Listening|BindFailed|Closed` theo kiểm tra bind local thực tế; việc TCP connect tới local port chỉ xác nhận local listener, không đảm bảo remote application.
- `target_check`: `NotConfigured|Unknown|Passed|Failed` theo **end-to-end protocol probe** được định nghĩa cụ thể. Nếu không có probe phù hợp, giữ `Unknown`, không gán Healthy giả.

UI summary `Listening` khi SSH/port local ready; chỉ hiển thị `Healthy` nếu probe đủ chứng cứ xác nhận dịch vụ; `Degraded` khi probe thất bại hoặc có dấu hiệu kênh đích lỗi. TCP connect riêng tới local listener **không đủ** làm chứng cứ endpoint healthy.

### 6.3 Timeouts / cancellation

- Giá trị mặc định đề xuất (configurable nội bộ): SSH connect 10 s; read-only query 20 s; mutation 30 s; graceful local stop 3 s; kill fallback có confirm. Không hardcode đây là guarantee mạng.
- Bounded stdout/stderr theo task, ví dụ tối đa 1–4 MiB cho output data tùy lệnh và 256 KiB stderr, có marker truncated; pipe phải drain đồng thời để không deadlock.
- Khi cancel read-only task: dừng SSH child/job được sở hữu, không update UI từ callback quá hạn.
- Khi cancel start/stop/restart/TERM/KILL đã dispatch: thông báo `Không chắc lệnh phía server đã hoàn tất; đang refresh trạng thái`; refresh trước khi kết luận.
- Sau mạng lỗi: v1.0 hiển thị Error, người dùng Start lại. v1.1 reconnect opt-in, exponential backoff với giới hạn, tạm dừng khi auth/host-key lỗi, ngừng khi user Stop/Exit.

## 7. Quản lý Linux systemd / processes

### 7.1 systemd

- Danh sách service lấy từ `systemctl list-units --type=service --all --no-legend --plain --no-pager` (hoặc command tương đương được test); có chọn **system / user** (`systemctl --user`).
- Chi tiết unit lấy từ `systemctl show --property=Id,LoadState,ActiveState,SubState,UnitFileState,Description,MainPID ...` và parse `key=value`, không coi `systemctl status` là machine-readable API.
- Mutations duy nhất: `start|stop|restart` với unit `.service` đã validate (tập ký tự allowlist nghiêm ngặt, cấm leading `-`, shell metacharacters và option injection). Có confirmation, disable duplicate action, đọc lại trạng thái sau thao tác.
- Default **không sudo**. Nếu thiếu quyền system service, hiện lỗi quyền với hướng dẫn cấu hình qua sysadmin; **không tự đổi sang sudo**. Tùy chọn `sudo -n` chỉ xét ở v1.1 sau security review & đặc tả riêng.
- Không chạy `enable/disable`, `daemon-reload`, cài service, sửa file service hoặc arbitrary shell.

### 7.2 Linux process

- Fetch process info qua các command chỉ đọc được định nghĩa cố định (`ps` / `/proc` ở mức cho phép); không hiển thị PID như định danh bền vững.
- TERM (`SIGTERM`) trước, KILL (`SIGKILL`) là thao tác riêng có xác nhận mạnh; không tự escalate TERM thành KILL remote.
- Trước thao tác, refresh và so khớp PID + owner + start-time/process identity nếu Linux cấp được; kiểm tra lại sát lúc gửi. Nếu PID đã đổi/tái sử dụng/không đủ evidence, dừng và yêu cầu refresh. Race còn tồn tại phải được phản ánh trong thông báo, không hứa atomicity.
- Không thao tác PID 1, PID không hợp lệ, tiến trình chưa có đủ dữ liệu, hoặc process không thuộc quyền; không tự chạy sudo.

## 8. Win32 process / thread / resource safety

- `CreateProcessW` dùng absolute path tới binary đã đóng gói, `lpApplicationName` tường minh, Windows argument quoting chuẩn, **không** qua `cmd.exe /c` hoặc PowerShell để thực thi remote commands.
- Remote command là **fixed allowlisted operation**; vì SSH thực thi command qua remote shell, argument phải allowlist/quote chặt và tuyệt đối không truyền dữ liệu người dùng vào đoạn shell tùy ý.
- Worker: one-shot async tasks + dedicated long-lived tunnel supervisors; UI nhận `WM_APP` typed messages với `request_id/generation` để bỏ stale messages; không chạm HWND từ worker. Không spawn thread không giới hạn theo click.
- RAII cho `HANDLE`, `PROCESS_INFORMATION`, pipes, SQLite statements, buffer; xử lý pipe reader/writer exit, shutdown races, app WM_CLOSE khi job chưa xong.
- Cân nhắc **Job Object cho từng SSH process** + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` để dọn child khi owner chết bất thường. Create suspended → assign → resume (nếu khả thi); thử trường hợp app đang ở parent job/nested jobs trên Windows 10/11. Nếu `AssignProcessToJobObject` fail thì log rõ và fallback theo owned process handle, không kill theo tên.
- Không dùng persisted PID để kill SSH trên lần chạy sau. Có thể dùng single-instance mutex/activation để tránh hai UI cùng ghi DB, nhưng không kill tunnel của instance khác.
- Không triển khai pool threads/logger async phức tạp nếu chưa cần. Dùng bounded queues và metrics thời gian chạy thực tế trước khi tối ưu.

## 9. Storage, security, diagnostics

### 9.1 Storage v1.0

- SQLite: `%LOCALAPPDATA%\SSHTunnelManager\data.db`; thư mục theo ACL current-user, không world-readable; explicit schema migrations và SQLite busy/error handling.
- Lưu profiles, presets, favorites, settings, history metadata; không lưu live PID/handle; không lưu passphrase/password/raw private key.
- **Thay đổi từ bản gốc:** DPAPI app-managed raw private-key vault bị **hoãn**; v1.0 chỉ tham chiếu key file hoặc agent. Nếu muốn tính năng lưu private key trong DB tương lai, phải có threat model, DPAPI current-user, lifecycle temp file, zeroization, crash cleanup, migration và security testing riêng, không tự triển khai trong P0.
- Nếu có secret settings khác trong tương lai, dùng DPAPI current-user với `CryptProtectData`, không khẳng định DPAPI bảo vệ khỏi malware đang chạy dưới cùng user.

### 9.2 Host key và ranh giới tin cậy

- Không tự sửa `known_hosts` từ fingerprint chưa kiểm chứng. SSH warning key changed là **blocking error**, không có nút bypass một click.
- Onboarding tương tác phải dựa OpenSSH prompt chính thống, đưa hướng dẫn xác minh fingerprint qua kênh khác. Không sử dụng `StrictHostKeyChecking=no`.
- `SSH_AUTH_SOCK`/agent integration phải được kiểm tra trên Windows sạch. Không bật SSH agent forwarding (`-A`) mặc định.
- Redact secrets, URL chứa credential, private paths nếu cần khi copy diagnostic; không in câu lệnh có secrets.
- Gói OpenSSH cần pinned version, SHA256, nguồn chính thức, kiến trúc x64, DLL/dependencies, NOTICE/license; tuyệt đối không tải binary ngẫu nhiên ở runtime.

### 9.3 Lỗi có thể hành động

Các error codes tối thiểu: `DEPENDENCY_MISSING`, `DNS_FAILED`, `CONNECT_TIMEOUT`, `HOSTKEY_UNKNOWN`, `HOSTKEY_CHANGED`, `AUTH_FAILED`, `KEY_UNAVAILABLE`, `REMOTE_COMMAND_MISSING`, `PERMISSION_DENIED`, `PORT_IN_USE`, `BIND_FAILED`, `REMOTE_UNREACHABLE_OR_UNKNOWN`, `PARSE_ERROR`, `DB_ERROR`, `CANCELLED`, `REMOTE_OUTCOME_UNCERTAIN`, `PROCESS_EXITED`.

Error details gồm: hành động, host/preset, thời gian, stderr snippet đã giới hạn/redact, hướng dẫn khắc phục; không suy đoán nguyên nhân nếu stderr không đủ dữ liệu.

## 10. Startup / exit / recovery

- Startup: khởi tạo logging/SQLite + migration, nạp profiles/presets, hiển thị `Not tested` cho tất cả profile; không tự SSH/tunnel/service nếu v1.0.
- WM_CLOSE nếu không có tunnel: hủy/dừng các task đọc an toàn rồi thoát sạch; nếu đang có remote mutation thì báo outcome uncertain khi cần.
- WM_CLOSE nếu có tunnel: `Stop tunnels and Exit` hoặc `Cancel`; terminate owned processes có giới hạn và xác nhận khi force local process cần thiết.
- Unexpected crash: owned process/job dọn theo lifecycle đã triển khai; nếu không bảo đảm dọn được thì không nhận bừa PID sau restart. Presets load lại nhưng không tự start.
- v1.1 tray: `X → minimize to tray` chỉ khi user bật; `Exit` thực sự dừng tunnel; không dùng dấu X như thao tác không rõ nghĩa.

## 11. Acceptance criteria / Definition of Done v1.0

- [ ] Release x64 build bằng MSVC, Windows 10/11; chạy trên máy sạch không cài sẵn OpenSSH/.NET/PowerShell runtime (PowerShell chỉ cần nếu chọn script build trên máy dev).
- [ ] Profile nhận SSH config alias/IdentityFile/ssh-agent; background preflight xác thực đúng. Host key chưa tin cậy/thay đổi, passphrase-locked key, timeout và permission trả lỗi rõ; không bypass bảo mật.
- [ ] Port discovery trả TCP/UDP, IPv4/IPv6; dòng thiếu PID vẫn hiện; UDP Create Tunnel disabled; server thiếu `ss` không bị coi là empty.
- [ ] Tạo nhiều tunnel cùng host hoặc khác host; SSH gateway target có thể là IP khác server; local bind chỉ loopback; local port conflict được phát hiện kể cả race sau pre-check.
- [ ] Tunnel trạng thái `Listening` không tự biến thành `Healthy` khi endpoint chưa test; Stop dừng đúng owned process, không dừng unrelated SSH.
- [ ] systemd `system` / `user` scope list/detail/start/stop/restart có confirm và kiểm tra trạng thái; không tự sudo. TERM/KILL chỉ trên PID đã kiểm tra, KILL phải confirm.
- [ ] UI responsive khi SSH timeout, query output dài, server offline, user bấm Cancel/Exit; không deadlock hoặc use-after-free.
- [ ] SQLite mở/lưu/migration/khôi phục lỗi hợp lý; không có secret/raw private key; không lưu live PID.
- [ ] Security suite: shell injection/option injection, hostname/IPv6, hostkey changed, truncated outputs, cancelled mutation, job cleanup, unexpected exit.
- [ ] Manual smoke test trên Windows sạch + Linux thật; bản release có file version, license notices, documented SHA256 và phép đo footprint thực tế.

## 12. Non-goals và quyết định để tránh scope creep

- Không yêu cầu app-managed private-key vault/DPAPI raw-key ở v1.0; không password/sudo-password form.
- Không có arbitrary SSH shell, SFTP, full process explorer, cron, Docker, firewall và thay đổi Linux configuration.
- Không automatic tunnel start on app launch; không service autostart.
- Không SOCKS/reverse/LAN-facing binds trước security review riêng.
- Không tuyên bố mục tiêu RAM/installer size cố định khi chưa đo được.

## 13. Tài liệu tham khảo kỹ thuật (đối chiếu khi triển khai)

- OpenSSH `ssh_config(5)`: https://man.openbsd.org/ssh_config — đặc biệt `ExitOnForwardFailure`, `BatchMode`, `ServerAlive*`.
- OpenSSH `ssh(1)`: https://man.openbsd.org/ssh — cú pháp `-L`, `-N`, `-T`, `-G`.
- Microsoft Job Objects: https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects
- systemd `systemctl`: https://www.freedesktop.org/software/systemd/man/latest/systemctl.html

**Ưu tiên thực thi:** Plan đi kèm là nguồn task/acceptance chi tiết; nếu plan mâu thuẫn với spec, dừng task và cập nhật tài liệu trước khi code.
