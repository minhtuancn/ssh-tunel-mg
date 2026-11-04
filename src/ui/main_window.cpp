#include "main_window.h"
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

using namespace ssh_tunnel;

namespace {

// Tab pages are plain child windows. Commands from toolbars/buttons are sent
// to the page window, so forward them to the main window where they are
// handled instead of being silently dropped.
LRESULT CALLBACK TabPageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        HWND tab = GetParent(hwnd);
        HWND main = tab ? GetParent(tab) : nullptr;
        if (main) {
            return SendMessageW(main, msg, wParam, lParam);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

constexpr wchar_t kTabPageClass[] = L"SSHTunnelManagerTabPage";

} // namespace

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static ssh_tunnel::TunnelConfig profile_to_tunnel_config(const ssh_tunnel::TunnelProfile& p) {
    ssh_tunnel::TunnelConfig cfg;
    cfg.local_host = MainWindow::str_to_wstr(p.local_host);
    cfg.local_port = p.local_port;
    cfg.remote_host = MainWindow::str_to_wstr(p.remote_host);
    cfg.remote_port = p.remote_port;
    cfg.ssh_host = MainWindow::str_to_wstr(p.ssh_host);
    cfg.ssh_port = p.ssh_port;
    cfg.ssh_user = MainWindow::str_to_wstr(p.ssh_user);
    cfg.identity_file = MainWindow::str_to_wstr(p.identity_file);
    cfg.use_agent = p.use_agent;
    cfg.enable_compression = p.enable_compression;
    cfg.keepalive_interval_sec = p.keepalive_interval_sec;
    cfg.connect_timeout_sec = p.connect_timeout_sec;
    return cfg;
}

static void load_tunnels_from_profiles(TunnelManager* mgr, Database* db, std::vector<int64_t>* out_profile_ids) {
    if (!mgr || !db) return;
    auto profiles = db->get_all_profiles(true); // only enabled
    for (const auto& p : profiles) {
        mgr->add_tunnel(profile_to_tunnel_config(p));
        if (out_profile_ids) out_profile_ids->push_back(p.id);
    }
}

// ------------------------------------------------------------
// MainWindow implementation
// ------------------------------------------------------------

MainWindow::MainWindow(HINSTANCE hInstance) : h_instance_(hInstance) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | 
                 ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);
    
    DbConfig db_cfg;
    wchar_t appdata[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) == S_OK) {
        std::wstring dir = appdata;
        dir += L"\\SSHTunnelManager";
        // sqlite3_open_v2 creates the file but not missing parent directories.
        CreateDirectoryW(dir.c_str(), nullptr);
        db_cfg.path = dir + L"\\tunnels.db";
    } else {
        db_cfg.path = L"tunnels.db";
    }
    db_ = std::make_unique<Database>(db_cfg);
    db_ready_ = db_->initialize();
    if (!db_ready_) {
        db_error_ = MainWindow::str_to_wstr(db_->get_last_error());
        if (db_error_.empty()) {
            db_error_ = L"Unknown database error.";
        }
    }
    
    tunnel_mgr_ = std::make_unique<TunnelManager>();
    if (db_ready_) {
        load_tunnels_from_profiles(tunnel_mgr_.get(), db_.get(), &tunnel_profile_ids_);
    }
}

MainWindow::~MainWindow() {
    if (refresh_timer_id_) {
        KillTimer(hwnd_, refresh_timer_id_);
    }
}

bool MainWindow::create(int nCmdShow) {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wnd_proc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = h_instance_;
    wcex.hIcon = LoadIcon(h_instance_, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"SSHTunnelManagerClass";
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wcex)) {
        return false;
    }
    
    hwnd_ = CreateWindowExW(
        0,
        L"SSHTunnelManagerClass",
        L"SSH Tunnel Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, h_instance_, this
    );
    
    if (!hwnd_) {
        return false;
    }
    
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    create_tabs();
    load_settings();
    
    if (!db_ready_) {
        std::wstring msg = L"Không thể mở cơ sở dữ liệu:\n" + db_error_;
        MessageBoxW(hwnd_, msg.c_str(), L"SSH Tunnel Manager", MB_OK | MB_ICONERROR);
    }

    refresh_timer_id_ = SetTimer(hwnd_, 1, 5000, refresh_timer);
    
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    
    return true;
}

int MainWindow::run_message_loop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self && msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    
    if (self) {
        switch (msg) {
            case WM_NOTIFY: {
                LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);
                if (nmhdr->hwndFrom == self->tab_control_ && nmhdr->code == TCN_SELCHANGE) {
                    self->show_tab_page(TabCtrl_GetCurSel(self->tab_control_));
                    self->refresh_current_tab();
                    return 0;
                }
                break;
            }
            case WM_COMMAND: {
                const int cmd_id = LOWORD(wParam);
                auto list_selection = [&](HWND page, int list_id) {
                    HWND list = GetDlgItem(page, list_id);
                    return list ? self->get_selected_list_item(list) : -1;
                };

                switch (cmd_id) {
                    // Tunnels
                    case 2001: self->on_tunnel_start(list_selection(self->tab_tunnels_, 2001)); break;
                    case 2002: self->on_tunnel_stop(list_selection(self->tab_tunnels_, 2001)); break;
                    case 2003: self->on_tunnel_remove(list_selection(self->tab_tunnels_, 2001)); break;

                    // Profiles
                    case 3001: self->on_profile_add(); break;
                    case 3002: self->on_profile_edit(list_selection(self->tab_profiles_, 3001)); break;
                    case 3003: self->on_profile_delete(list_selection(self->tab_profiles_, 3001)); break;
                    case 3004: self->on_profile_up(list_selection(self->tab_profiles_, 3001)); break;
                    case 3005: self->on_profile_down(list_selection(self->tab_profiles_, 3001)); break;

                    // Ports, services, processes, settings
                    case 4001: self->on_discover_ports(); break;
                    case 5001: self->refresh_services_list(); break;
                    case 6001: self->refresh_processes_list(); break;
                    case 6002: self->on_kill_selected_process(); break;
                    case 7003: self->save_settings(); break;
                    default: break;
                }
                return 0;
            }
            case WM_TIMER: {
                if (wParam == 1) {
                    self->refresh_current_tab();
                }
                break;
            }
            case WM_SIZE: {
                if (self->tab_control_) {
                    RECT client;
                    GetClientRect(hwnd, &client);
                    MoveWindow(self->tab_control_, 0, 0,
                               client.right, client.bottom, TRUE);

                    // Get the tab control's display area (client area below tab headers)
                    // in tab control coordinates
                    RECT rc = {0};
                    GetClientRect(self->tab_control_, &rc);
                    TabCtrl_AdjustRect(self->tab_control_, FALSE, &rc);

                    for (int i = 0; i < 7; ++i) {
                        HWND page = nullptr;
                        switch (i) {
                            case 0: page = self->tab_dashboard_; break;
                            case 1: page = self->tab_tunnels_; break;
                            case 2: page = self->tab_profiles_; break;
                            case 3: page = self->tab_ports_; break;
                            case 4: page = self->tab_services_; break;
                            case 5: page = self->tab_processes_; break;
                            case 6: page = self->tab_settings_; break;
                        }
                        if (page) {
                            MoveWindow(page, rc.left, rc.top,
                                      rc.right - rc.left, rc.bottom - rc.top, TRUE);
                            self->layout_page(i);
                        }
                    }
                }
                return 0;
            }
            case WM_DESTROY: {
                PostQuitMessage(0);
                break;
            }
        }
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void MainWindow::create_tabs() {
    tab_control_ = CreateWindowExW(
        0, WC_TABCONTROLW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_FOCUSNEVER,
        0, 0, 0, 0, hwnd_, nullptr, h_instance_, nullptr
    );
    
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    
    const wchar_t* tab_names[] = {
        L"Dashboard", L"Tunnels", L"Profiles", 
        L"Ports", L"Services", L"Processes", L"Settings"
    };
    
    for (int i = 0; i < 7; ++i) {
        tie.pszText = const_cast<wchar_t*>(tab_names[i]);
        TabCtrl_InsertItem(tab_control_, i, &tie);
    }
    
    tab_dashboard_ = create_tab_page(0);
    tab_tunnels_ = create_tab_page(1);
    tab_profiles_ = create_tab_page(2);
    tab_ports_ = create_tab_page(3);
    tab_services_ = create_tab_page(4);
    tab_processes_ = create_tab_page(5);
    tab_settings_ = create_tab_page(6);
    
    setup_dashboard_tab(tab_dashboard_);
    setup_tunnels_tab(tab_tunnels_);
    setup_profiles_tab(tab_profiles_);
    setup_ports_tab(tab_ports_);
    setup_services_tab(tab_services_);
    setup_processes_tab(tab_processes_);
    setup_settings_tab(tab_settings_);
    
    show_tab_page(0);
}

HWND MainWindow::create_tab_page(int index) {
    (void)index;
    static bool page_class_registered = false;
    if (!page_class_registered) {
        WNDCLASSEXW page_class = {};
        page_class.cbSize = sizeof(page_class);
        page_class.style = CS_HREDRAW | CS_VREDRAW;
        page_class.lpfnWndProc = TabPageProc;
        page_class.hInstance = h_instance_;
        page_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        page_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        page_class.lpszClassName = kTabPageClass;
        RegisterClassExW(&page_class);
        page_class_registered = true;
    }

    return CreateWindowExW(
        0, kTabPageClass, L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, tab_control_, nullptr, h_instance_, nullptr
    );
}

void MainWindow::show_tab_page(int index) {
    if (index < 0 || index > 6) return;
    HWND pages[7] = {tab_dashboard_, tab_tunnels_, tab_profiles_, tab_ports_,
                     tab_services_, tab_processes_, tab_settings_};
    for (int i = 0; i < 7; ++i) {
        if (pages[i]) {
            ShowWindow(pages[i], i == index ? SW_SHOW : SW_HIDE);
        }
    }
    active_tab_ = index;
    layout_page(index);
}

void MainWindow::layout_page(int index) {
    HWND page = nullptr;
    switch (index) {
        case 0: page = tab_dashboard_; break;
        case 1: page = tab_tunnels_; break;
        case 2: page = tab_profiles_; break;
        case 3: page = tab_ports_; break;
        case 4: page = tab_services_; break;
        case 5: page = tab_processes_; break;
        case 6: page = tab_settings_; break;
        default: return;
    }
    if (!page) return;

    RECT rc;
    GetClientRect(page, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    if (index == 0) {
        HWND activity = GetDlgItem(page, 1003);
        if (activity) {
            MoveWindow(activity, 10, 150, width - 20, height - 160, TRUE);
        }
        return;
    }

    int toolbar_id = 0;
    int list_id = 0;
    switch (index) {
        case 1: toolbar_id = 2000; list_id = 2001; break;
        case 2: toolbar_id = 3000; list_id = 3001; break;
        case 3: toolbar_id = 4000; list_id = 4001; break;
        case 4: toolbar_id = 5000; list_id = 5001; break;
        case 5: toolbar_id = 6000; list_id = 6001; break;
        case 6: break; // Settings tab has no toolbar/list
        default: return;
    }

    if (index != 6) {
        HWND toolbar = GetDlgItem(page, toolbar_id);
        if (toolbar) {
            MoveWindow(toolbar, 0, 0, width, 32, TRUE);
            SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
        }
        HWND list = GetDlgItem(page, list_id);
        if (list) {
            MoveWindow(list, 0, 35, width, height - 35, TRUE);
        }
    }
}

void MainWindow::refresh_current_tab() {
    switch (active_tab_) {
        case 0: refresh_dashboard(); break;
        case 1: refresh_tunnels_list(); break;
        case 2: refresh_profiles_list(); break;
        case 3: refresh_ports_list(); break;
        case 4: refresh_services_list(); break;
        case 5: refresh_processes_list(); break;
        case 6: break;
    }
}

// ------------------------------------------------------------
// Dashboard tab
// ------------------------------------------------------------

void MainWindow::setup_dashboard_tab(HWND parent) {
    CreateWindowExW(0, WC_STATIC, L"SSH Tunnel Manager Dashboard",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 10, 400, 30, parent, nullptr, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_STATIC, L"Running Tunnels: 0",
        WS_CHILD | WS_VISIBLE,
        10, 50, 200, 25, parent, (HMENU)1001, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_STATIC, L"Total Profiles: 0",
        WS_CHILD | WS_VISIBLE,
        10, 80, 200, 25, parent, (HMENU)1002, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_STATIC, L"Recent Activity",
        WS_CHILD | WS_VISIBLE,
        10, 120, 200, 25, parent, nullptr, h_instance_, nullptr);
    
    HWND activity_list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        10, 150, 500, 300, parent, (HMENU)1003, h_instance_, nullptr
    );
    
    init_list_view(activity_list, {
        {0, L"Time"}, {1, L"Profile"}, {2, L"Action"}, {3, L"Status"}
    });
}

void MainWindow::refresh_dashboard() {
    if (!db_) return;
    
    auto profiles = db_->get_all_profiles();
    auto tunnels = tunnel_mgr_->get_all_tunnel_infos();
    
    int running = 0;
    for (const auto& t : tunnels) {
        if (t.status == TunnelStatus::RUNNING) running++;
    }
    
    HWND lbl1 = GetDlgItem(tab_dashboard_, 1001);
    HWND lbl2 = GetDlgItem(tab_dashboard_, 1002);
    
    SetWindowTextW(lbl1, (L"Running Tunnels: " + MainWindow::str_to_wstr(running)).c_str());
    SetWindowTextW(lbl2, (L"Total Profiles: " + MainWindow::str_to_wstr(profiles.size())).c_str());
}

// ------------------------------------------------------------
// Tunnels tab
// ------------------------------------------------------------

void MainWindow::setup_tunnels_tab(HWND parent) {
    HWND toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER,
        0, 0, 0, 32, parent, (HMENU)2000, h_instance_, nullptr
    );
    
    SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    
    TBBUTTON buttons[] = {
        {0, 2001, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Start"},
        {1, 2002, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Stop"},
        {2, 2003, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Remove"},
    };
    SendMessage(toolbar, TB_ADDBUTTONS, 3, (LPARAM)buttons);
    SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
    
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 35, 0, 0, parent, (HMENU)2001, h_instance_, nullptr
    );
    
    init_list_view(list, {
        {0, L"#"}, {1, L"Name"}, {2, L"Status"}, {3, L"Local"}, {4, L"Remote"}, {5, L"SSH Host"}, {6, L"PID"}
    });
    
    ListView_SetColumnWidth(list, 0, 40);
    ListView_SetColumnWidth(list, 1, 150);
    ListView_SetColumnWidth(list, 2, 100);
    ListView_SetColumnWidth(list, 3, 120);
    ListView_SetColumnWidth(list, 4, 120);
    ListView_SetColumnWidth(list, 5, 150);
    ListView_SetColumnWidth(list, 6, 80);
}

void MainWindow::refresh_tunnels_list() {
    HWND list = GetDlgItem(tab_tunnels_, 2001);
    if (!list) return;
    
    clear_list_view(list);
    
    auto tunnels = tunnel_mgr_->get_all_tunnel_infos();
    
    for (size_t i = 0; i < tunnels.size(); ++i) {
        const auto& t = tunnels[i];
        std::vector<std::wstring> items;
        items.push_back(MainWindow::str_to_wstr(i + 1));
        
        // Get profile name from tunnel_profile_ids_
        std::wstring name = L"Unknown";
        if (i < tunnel_profile_ids_.size() && db_) {
            auto profile = db_->get_profile(tunnel_profile_ids_[i]);
            if (profile) name = MainWindow::str_to_wstr(profile->name);
        }
        items.push_back(name);
        
        const wchar_t* status_str = L"Stopped";
        switch (t.status) {
            case TunnelStatus::STARTING: status_str = L"Starting"; break;
            case TunnelStatus::RUNNING: status_str = L"Running"; break;
            case TunnelStatus::STOPPING: status_str = L"Stopping"; break;
            case TunnelStatus::ERROR_STATE: status_str = L"Error"; break;
        }
        items.push_back(status_str);
        
        // Local endpoint
        items.push_back(L"127.0.0.1:" + MainWindow::str_to_wstr(t.bound_local_port));
        
        // Remote endpoint (from tunnel config - would need to store it)
        // For now, we can get it from the profile
        std::wstring remote = L"remote:port";
        if (i < tunnel_profile_ids_.size() && db_) {
            auto profile = db_->get_profile(tunnel_profile_ids_[i]);
            if (profile) {
                remote = MainWindow::str_to_wstr(profile->remote_host) + L":" + MainWindow::str_to_wstr(profile->remote_port);
            }
        }
        items.push_back(remote);
        
        // SSH host
        std::wstring ssh_host = L"ssh_host:22";
        if (i < tunnel_profile_ids_.size() && db_) {
            auto profile = db_->get_profile(tunnel_profile_ids_[i]);
            if (profile) {
                ssh_host = MainWindow::str_to_wstr(profile->ssh_host) + L":" + MainWindow::str_to_wstr(profile->ssh_port);
            }
        }
        items.push_back(ssh_host);
        
        items.push_back(MainWindow::str_to_wstr(t.ssh_pid));
        
        add_list_item(list, static_cast<int>(i), items);
    }
}

void MainWindow::on_tunnel_start(int index) {
    if (index >= 0 && static_cast<size_t>(index) < tunnel_mgr_->count()) {
        tunnel_mgr_->start_tunnel(static_cast<size_t>(index));
        refresh_tunnels_list();
    }
}

void MainWindow::on_tunnel_stop(int index) {
    if (index >= 0 && static_cast<size_t>(index) < tunnel_mgr_->count()) {
        tunnel_mgr_->stop_tunnel(static_cast<size_t>(index));
        refresh_tunnels_list();
    }
}

void MainWindow::on_tunnel_remove(int index) {
    if (index >= 0 && static_cast<size_t>(index) < tunnel_mgr_->count()) {
        tunnel_mgr_->remove_tunnel(static_cast<size_t>(index));
        refresh_tunnels_list();
    }
}

// ------------------------------------------------------------
// Profiles tab
// ------------------------------------------------------------

void MainWindow::setup_profiles_tab(HWND parent) {
    HWND toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER,
        0, 0, 0, 32, parent, (HMENU)3000, h_instance_, nullptr
    );
    
    SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    
    TBBUTTON buttons[] = {
        {0, 3001, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Add"},
        {1, 3002, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Edit"},
        {2, 3003, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Delete"},
        {3, 3004, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Up"},
        {4, 3005, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Down"},
    };
    SendMessage(toolbar, TB_ADDBUTTONS, 5, (LPARAM)buttons);
    SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
    
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 35, 0, 0, parent, (HMENU)3001, h_instance_, nullptr
    );
    
    init_list_view(list, {
        {0, L"Name"}, {1, L"SSH Host"}, {2, L"Port"}, {3, L"User"},
        {4, L"Local"}, {5, L"Remote"}, {6, L"Enabled"}
    });
    
    ListView_SetColumnWidth(list, 0, 150);
    ListView_SetColumnWidth(list, 1, 150);
    ListView_SetColumnWidth(list, 2, 60);
    ListView_SetColumnWidth(list, 3, 100);
    ListView_SetColumnWidth(list, 4, 120);
    ListView_SetColumnWidth(list, 5, 120);
    ListView_SetColumnWidth(list, 6, 80);
}

void MainWindow::refresh_profiles_list() {
    HWND list = GetDlgItem(tab_profiles_, 3001);
    if (!list || !db_) return;
    
    clear_list_view(list);
    profile_ids_.clear();
    
    auto profiles = db_->get_all_profiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        profile_ids_.push_back(p.id);
        
        std::vector<std::wstring> items;
        items.push_back(MainWindow::str_to_wstr(p.name));
        items.push_back(MainWindow::str_to_wstr(p.ssh_host));
        items.push_back(MainWindow::str_to_wstr(p.ssh_port));
        items.push_back(MainWindow::str_to_wstr(p.ssh_user));
        items.push_back(L"127.0.0.1:" + MainWindow::str_to_wstr(p.local_port));
        items.push_back(MainWindow::str_to_wstr(p.remote_host) + L":" + MainWindow::str_to_wstr(p.remote_port));
        items.push_back(p.enabled ? L"Yes" : L"No");
        
        add_list_item(list, static_cast<int>(i), items);
    }
}

void MainWindow::on_profile_add() {
    ProfileDialog dlg(h_instance_, tab_profiles_, db_.get(), nullptr);
    if (dlg.show()) {
        refresh_profiles_list();
        reload_tunnels();
    }
}

void MainWindow::on_profile_edit(int index) {
    if (index >= 0 && static_cast<size_t>(index) < profile_ids_.size()) {
        int64_t id = profile_ids_[index];
        auto profile = db_->get_profile(id);
        if (profile) {
            ProfileDialog dlg(h_instance_, tab_profiles_, db_.get(), &*profile);
            if (dlg.show()) {
                refresh_profiles_list();
                reload_tunnels();
            }
        }
    }
}

void MainWindow::on_profile_delete(int index) {
    if (index >= 0 && static_cast<size_t>(index) < profile_ids_.size()) {
        int64_t id = profile_ids_[index];
        if (MessageBoxW(hwnd_, L"Delete this profile?", L"Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            db_->delete_profile(id);
            refresh_profiles_list();
            reload_tunnels();
        }
    }
}

void MainWindow::on_profile_up(int index) {
    if (index > 0 && static_cast<size_t>(index) < profile_ids_.size()) {
        std::swap(profile_ids_[index], profile_ids_[index - 1]);
        db_->reorder_profiles(profile_ids_);
        refresh_profiles_list();
        reload_tunnels();
    }
}

void MainWindow::on_profile_down(int index) {
    if (index >= 0 && static_cast<size_t>(index + 1) < profile_ids_.size()) {
        std::swap(profile_ids_[index], profile_ids_[index + 1]);
        db_->reorder_profiles(profile_ids_);
        refresh_profiles_list();
        reload_tunnels();
    }
}

void MainWindow::reload_tunnels() {
    if (!db_ready_) return;
    tunnel_profile_ids_.clear();
    tunnel_mgr_ = std::make_unique<TunnelManager>();
    load_tunnels_from_profiles(tunnel_mgr_.get(), db_.get(), &tunnel_profile_ids_);
    refresh_tunnels_list();
}

// ------------------------------------------------------------
// Ports tab
// ------------------------------------------------------------

void MainWindow::setup_ports_tab(HWND parent) {
    HWND toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER,
        0, 0, 0, 32, parent, (HMENU)4000, h_instance_, nullptr
    );
    
    TBBUTTON buttons[] = {
        {0, 4001, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Discover"},
    };
    SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(toolbar, TB_ADDBUTTONS, 1, (LPARAM)buttons);
    SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
    
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 35, 0, 0, parent, (HMENU)4001, h_instance_, nullptr
    );
    
    init_list_view(list, {
        {0, L"Protocol"}, {1, L"Address"}, {2, L"Port"}, {3, L"Process"}, {4, L"PID"}, {5, L"User"}
    });
}

void MainWindow::refresh_ports_list() {
    auto ports = get_local_listening_ports(TransportProtocol::UNKNOWN);
    HWND list = GetDlgItem(tab_ports_, 4001);
    if (!list) return;
    
    clear_list_view(list);
    
    for (size_t i = 0; i < ports.size(); ++i) {
        const auto& p = ports[i];
        std::vector<std::wstring> items;
        items.push_back(p.protocol == TransportProtocol::TCP ? L"TCP" : L"UDP");
        items.push_back(MainWindow::str_to_wstr(p.local_address));
        items.push_back(MainWindow::str_to_wstr(p.local_port));
        items.push_back(MainWindow::str_to_wstr(p.process_name));
        items.push_back(MainWindow::str_to_wstr(p.pid));
        items.push_back(L"");
        
        add_list_item(list, static_cast<int>(i), items);
    }
}

void MainWindow::on_discover_ports() {
    MessageBoxW(hwnd_, L"Select a profile first to discover remote ports", L"Info", MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------
// Services tab
// ------------------------------------------------------------

void MainWindow::setup_services_tab(HWND parent) {
    HWND toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER,
        0, 0, 0, 32, parent, (HMENU)5000, h_instance_, nullptr
    );
    
    TBBUTTON buttons[] = {
        {0, 5001, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Refresh"},
    };
    SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(toolbar, TB_ADDBUTTONS, 1, (LPARAM)buttons);
    SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
    
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 35, 0, 0, parent, (HMENU)5001, h_instance_, nullptr
    );
    
    init_list_view(list, {
        {0, L"Name"}, {1, L"State"}, {2, L"Type"}, {3, L"PID"}, {4, L"Description"}
    });
}

void MainWindow::refresh_services_list() {
    auto services = get_local_windows_services();
    HWND list = GetDlgItem(tab_services_, 5001);
    if (!list) return;
    
    clear_list_view(list);
    
    for (size_t i = 0; i < services.size(); ++i) {
        const auto& s = services[i];
        std::vector<std::wstring> items;
        items.push_back(MainWindow::str_to_wstr(s.name));
        items.push_back(MainWindow::str_to_wstr(s.state));
        items.push_back(L"Win32");
        items.push_back(MainWindow::str_to_wstr(s.pid));
        items.push_back(MainWindow::str_to_wstr(s.display_name));
        
        add_list_item(list, static_cast<int>(i), items);
    }
}

void MainWindow::on_service_action(const std::string& unit, const std::string& action) {
}

// ------------------------------------------------------------
// Processes tab
// ------------------------------------------------------------

void MainWindow::setup_processes_tab(HWND parent) {
    HWND toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NODIVIDER,
        0, 0, 0, 32, parent, (HMENU)6000, h_instance_, nullptr
    );
    
    TBBUTTON buttons[] = {
        {0, 6001, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Refresh"},
        {1, 6002, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Kill"},
    };
    SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(toolbar, TB_ADDBUTTONS, 2, (LPARAM)buttons);
    SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
    
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 35, 0, 0, parent, (HMENU)6001, h_instance_, nullptr
    );
    
    init_list_view(list, {
        {0, L"PID"}, {1, L"Name"}, {2, L"Memory"}, {3, L"Path"}
    });
}

void MainWindow::refresh_processes_list() {
    auto processes = get_local_processes();
    HWND list = GetDlgItem(tab_processes_, 6001);
    if (!list) return;
    
    clear_list_view(list);
    
    for (size_t i = 0; i < processes.size(); ++i) {
        const auto& p = processes[i];
        std::vector<std::wstring> items;
        items.push_back(MainWindow::str_to_wstr(p.pid));
        items.push_back(MainWindow::str_to_wstr(p.name));
        items.push_back(MainWindow::str_to_wstr(p.memory_usage / 1024) + L" KB");
        items.push_back(MainWindow::str_to_wstr(p.exe_path));
        
        add_list_item(list, static_cast<int>(i), items);
    }
}

void MainWindow::on_kill_process(uint32_t pid) {
    if (MessageBoxW(hwnd_, L"Kill this process?", L"Confirm", MB_YESNO | MB_ICONWARNING) == IDYES) {
        kill_local_process(pid, true);
        refresh_processes_list();
    }
}

void MainWindow::on_kill_selected_process() {
    HWND list = GetDlgItem(tab_processes_, 6001);
    if (!list) return;
    const int index = get_selected_list_item(list);
    if (index < 0) return;

    auto processes = get_local_processes();
    if (static_cast<size_t>(index) >= processes.size()) return;
    on_kill_process(processes[static_cast<size_t>(index)].pid);
}

// ------------------------------------------------------------
// Settings tab
// ------------------------------------------------------------

void MainWindow::setup_settings_tab(HWND parent) {
    CreateWindowExW(0, WC_STATIC, L"Settings",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 10, 200, 25, parent, nullptr, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_STATIC, L"OpenSSH Path:",
        WS_CHILD | WS_VISIBLE,
        10, 50, 120, 25, parent, nullptr, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_EDIT, L"resources\\openssh\\ssh.exe",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
        140, 50, 300, 25, parent, (HMENU)7001, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_STATIC, L"Default Timeout (ms):",
        WS_CHILD | WS_VISIBLE,
        10, 90, 120, 25, parent, nullptr, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_EDIT, L"30000",
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        140, 90, 100, 25, parent, (HMENU)7002, h_instance_, nullptr);
    
    CreateWindowExW(0, WC_BUTTON, L"Save Settings",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        10, 130, 120, 30, parent, (HMENU)7003, h_instance_, nullptr);
}

void MainWindow::load_settings() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SSHTunnelManager", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[512];
        DWORD size = sizeof(buf);
        DWORD type;
        // OpenSSH path
        if (RegQueryValueExW(key, L"OpenSSHPath", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
            HWND edit = GetDlgItem(tab_settings_, 7001);
            if (edit) SetWindowTextW(edit, buf);
        }
        // Timeout
        size = sizeof(buf);
        if (RegQueryValueExW(key, L"DefaultTimeoutMs", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
            HWND edit = GetDlgItem(tab_settings_, 7002);
            if (edit) SetWindowTextW(edit, buf);
        }
        RegCloseKey(key);
    }
}

void MainWindow::save_settings() {
    HKEY key;
    DWORD disp;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SSHTunnelManager", 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disp) == ERROR_SUCCESS) {
        HWND edit_ssh = GetDlgItem(tab_settings_, 7001);
        HWND edit_timeout = GetDlgItem(tab_settings_, 7002);
        wchar_t buf[512];
        if (edit_ssh) {
            GetWindowTextW(edit_ssh, buf, 512);
            RegSetValueExW(key, L"OpenSSHPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(buf), (wcslen(buf) + 1) * sizeof(wchar_t));
        }
        if (edit_timeout) {
            GetWindowTextW(edit_timeout, buf, 512);
            RegSetValueExW(key, L"DefaultTimeoutMs", 0, REG_SZ, reinterpret_cast<const BYTE*>(buf), (wcslen(buf) + 1) * sizeof(wchar_t));
        }
        RegCloseKey(key);
        MessageBoxW(hwnd_, L"Settings saved.", L"SSH Tunnel Manager", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd_, L"Failed to save settings.", L"Error", MB_OK | MB_ICONERROR);
    }
}

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

std::wstring MainWindow::str_to_wstr(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

std::wstring MainWindow::str_to_wstr(int v) {
    return std::to_wstring(v);
}

std::wstring MainWindow::str_to_wstr(unsigned int v) {
    return std::to_wstring(v);
}

std::wstring MainWindow::str_to_wstr(uint16_t v) {
    return std::to_wstring(v);
}

std::wstring MainWindow::str_to_wstr(uint64_t v) {
    return std::to_wstring(v);
}

std::string MainWindow::wstr_to_str(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string a(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &a[0], len, nullptr, nullptr);
    if (!a.empty() && a.back() == 0) a.pop_back();
    return a;
}

void MainWindow::init_list_view(HWND lv, const std::vector<std::pair<int, std::wstring>>& columns) {
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    
    for (const auto& col : columns) {
        lvc.iSubItem = col.first;
        lvc.cx = 100;
        lvc.pszText = const_cast<wchar_t*>(col.second.c_str());
        ListView_InsertColumn(lv, col.first, &lvc);
    }
}

void MainWindow::add_list_item(HWND lv, int index, const std::vector<std::wstring>& items) {
    LVITEMW lvi = {};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = index;
    lvi.iSubItem = 0;
    lvi.pszText = const_cast<wchar_t*>(items[0].c_str());
    ListView_InsertItem(lv, &lvi);
    
    for (size_t i = 1; i < items.size(); ++i) {
        ListView_SetItemText(lv, index, static_cast<int>(i), const_cast<wchar_t*>(items[i].c_str()));
    }
}

void MainWindow::clear_list_view(HWND lv) {
    ListView_DeleteAllItems(lv);
}

int MainWindow::get_selected_list_item(HWND lv) {
    return ListView_GetNextItem(lv, -1, LVNI_SELECTED);
}

void CALLBACK MainWindow::refresh_timer(HWND hwnd, UINT msg, UINT_PTR idTimer, DWORD dwTime) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) {
        self->refresh_current_tab();
    }
}

// ------------------------------------------------------------
// ProfileDialog implementation
// ------------------------------------------------------------

ProfileDialog::ProfileDialog(HINSTANCE hInst, HWND parent, Database* db, TunnelProfile* profile)
    : h_inst_(hInst), parent_(parent), db_(db), profile_(profile), is_new_(profile == nullptr) {
    if (!profile_) {
        // "Add" dialogs must edit an owned profile; dereferencing a null
        // pointer here used to crash as soon as OK was pressed.
        profile_ = &owned_profile_;
        profile_->local_host = "127.0.0.1";
        profile_->remote_host = "127.0.0.1";
        profile_->remote_port = 0;
    }
}

ProfileDialog::~ProfileDialog() {}

bool ProfileDialog::show() {
    return DialogBoxParamW(h_inst_, MAKEINTRESOURCEW(101), parent_, dlg_proc, reinterpret_cast<LPARAM>(this)) == IDOK;
}

INT_PTR CALLBACK ProfileDialog::dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ProfileDialog* self = reinterpret_cast<ProfileDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self && msg == WM_INITDIALOG) {
        self = reinterpret_cast<ProfileDialog*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        self->init_controls();
        self->set_data();
        return TRUE;
    }
    
    if (self) {
        switch (msg) {
            case WM_COMMAND: {
                int cmd_id = LOWORD(wParam);
                switch (cmd_id) {
                    case 1006:
                        self->on_browse_key();
                        return TRUE;
                    case IDOK:
                        self->on_ok();
                        return TRUE;
                    case IDCANCEL:
                        self->on_cancel();
                        return TRUE;
                }
                break;
            }
        }
    }
    return FALSE;
}

void ProfileDialog::init_controls() {
    edit_name_ = GetDlgItem(hwnd_, 1001);
    edit_ssh_host_ = GetDlgItem(hwnd_, 1002);
    edit_ssh_port_ = GetDlgItem(hwnd_, 1003);
    edit_ssh_user_ = GetDlgItem(hwnd_, 1004);
    edit_identity_file_ = GetDlgItem(hwnd_, 1005);
    chk_use_agent_ = GetDlgItem(hwnd_, 1007);
    edit_local_host_ = GetDlgItem(hwnd_, 1008);
    edit_local_port_ = GetDlgItem(hwnd_, 1009);
    edit_remote_host_ = GetDlgItem(hwnd_, 1010);
    edit_remote_port_ = GetDlgItem(hwnd_, 1011);
    chk_compression_ = GetDlgItem(hwnd_, 1012);
    edit_keepalive_ = GetDlgItem(hwnd_, 1013);
    edit_timeout_ = GetDlgItem(hwnd_, 1014);
    chk_enabled_ = GetDlgItem(hwnd_, 1015);
}

void ProfileDialog::set_data() {
    if (!profile_) return;

    if (is_new_) {
        SetWindowTextW(edit_ssh_port_, L"22");
        SetWindowTextW(edit_local_host_, L"127.0.0.1");
        SetWindowTextW(edit_local_port_, L"0");
        SetWindowTextW(edit_remote_host_, L"127.0.0.1");
        SetWindowTextW(edit_remote_port_, L"80");
        SetWindowTextW(edit_keepalive_, L"30");
        SetWindowTextW(edit_timeout_, L"15");
        SendMessage(chk_use_agent_, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(chk_enabled_, BM_SETCHECK, BST_CHECKED, 0);
        return;
    }
    
    SetWindowTextW(edit_name_, MainWindow::str_to_wstr(profile_->name).c_str());
    SetWindowTextW(edit_ssh_host_, MainWindow::str_to_wstr(profile_->ssh_host).c_str());
    SetWindowTextW(edit_ssh_port_, MainWindow::str_to_wstr(profile_->ssh_port).c_str());
    SetWindowTextW(edit_ssh_user_, MainWindow::str_to_wstr(profile_->ssh_user).c_str());
    SetWindowTextW(edit_identity_file_, MainWindow::str_to_wstr(profile_->identity_file).c_str());
    SendMessage(chk_use_agent_, BM_SETCHECK, profile_->use_agent ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(edit_local_host_, MainWindow::str_to_wstr(profile_->local_host).c_str());
    SetWindowTextW(edit_local_port_, MainWindow::str_to_wstr(profile_->local_port).c_str());
    SetWindowTextW(edit_remote_host_, MainWindow::str_to_wstr(profile_->remote_host).c_str());
    SetWindowTextW(edit_remote_port_, MainWindow::str_to_wstr(profile_->remote_port).c_str());
    SendMessage(chk_compression_, BM_SETCHECK, profile_->enable_compression ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(edit_keepalive_, MainWindow::str_to_wstr(profile_->keepalive_interval_sec).c_str());
    SetWindowTextW(edit_timeout_, MainWindow::str_to_wstr(profile_->connect_timeout_sec).c_str());
    SendMessage(chk_enabled_, BM_SETCHECK, profile_->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool ProfileDialog::get_data() {
    wchar_t buf[512];
    
    GetWindowTextW(edit_name_, buf, 512);
    profile_->name = MainWindow::wstr_to_str(buf);
    
    GetWindowTextW(edit_ssh_host_, buf, 512);
    profile_->ssh_host = MainWindow::wstr_to_str(buf);
    
    GetWindowTextW(edit_ssh_port_, buf, 512);
    profile_->ssh_port = static_cast<uint16_t>(_wtoi(buf));
    
    GetWindowTextW(edit_ssh_user_, buf, 512);
    profile_->ssh_user = MainWindow::wstr_to_str(buf);
    
    GetWindowTextW(edit_identity_file_, buf, 512);
    profile_->identity_file = MainWindow::wstr_to_str(buf);
    
    profile_->use_agent = (SendMessage(chk_use_agent_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    GetWindowTextW(edit_local_host_, buf, 512);
    profile_->local_host = MainWindow::wstr_to_str(buf);
    
    GetWindowTextW(edit_local_port_, buf, 512);
    profile_->local_port = static_cast<uint16_t>(_wtoi(buf));
    
    GetWindowTextW(edit_remote_host_, buf, 512);
    profile_->remote_host = MainWindow::wstr_to_str(buf);
    
    GetWindowTextW(edit_remote_port_, buf, 512);
    profile_->remote_port = static_cast<uint16_t>(_wtoi(buf));
    
    profile_->enable_compression = (SendMessage(chk_compression_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    GetWindowTextW(edit_keepalive_, buf, 512);
    profile_->keepalive_interval_sec = static_cast<uint32_t>(_wtoi(buf));
    
    GetWindowTextW(edit_timeout_, buf, 512);
    profile_->connect_timeout_sec = static_cast<uint32_t>(_wtoi(buf));
    
    profile_->enabled = (SendMessage(chk_enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    return !profile_->name.empty() && !profile_->ssh_host.empty() && !profile_->ssh_user.empty();
}

void ProfileDialog::on_browse_key() {
    MessageBoxW(hwnd_, L"File dialog not available in this build.\nPlease type the path manually.", L"Info", MB_OK | MB_ICONINFORMATION);
}

void ProfileDialog::on_ok() {
    if (!get_data()) {
        MessageBoxW(hwnd_, L"Please fill all required fields", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    if (!db_) {
        MessageBoxW(hwnd_, L"Database is not available.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (is_new_) {
        if (db_->create_profile(*profile_) <= 0) {
            MessageBoxW(hwnd_, L"Could not save profile. The name may already exist.",
                        L"Error", MB_OK | MB_ICONERROR);
            return;
        }
    } else if (!db_->update_profile(*profile_)) {
        MessageBoxW(hwnd_, L"Could not update profile.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    result_ = true;
    EndDialog(hwnd_, IDOK);
}

void ProfileDialog::on_cancel() {
    result_ = false;
    EndDialog(hwnd_, IDCANCEL);
}