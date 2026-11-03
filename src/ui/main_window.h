#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "ssh_tunnel.h"
#include "ssh_systemd.h"
#include "ssh_process_mgmt.h"
#include "ssh_port_discovery.h"
#include "sqlite_db.h"

namespace ssh_tunnel {

// ------------------------------------------------------------
// Main Window - Win32 GUI with tabs for each module
// ------------------------------------------------------------

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    // Initialize and show the main window
    bool create(int nCmdShow);

    // Message loop
    int run_message_loop();

    // Handle window messages
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE h_instance_;
    HWND hwnd_ = nullptr;
    HWND tab_control_ = nullptr;
    
    // Tab pages
    HWND tab_dashboard_ = nullptr;
    HWND tab_tunnels_ = nullptr;
    HWND tab_profiles_ = nullptr;
    HWND tab_ports_ = nullptr;
    HWND tab_services_ = nullptr;
    HWND tab_processes_ = nullptr;
    HWND tab_settings_ = nullptr;
    
    // Database
    std::unique_ptr<Database> db_;
    bool db_ready_ = false;
    std::wstring db_error_;
    
    // Tunnel manager
    std::unique_ptr<TunnelManager> tunnel_mgr_;
    
    // Current selection
    int active_tab_ = 0;
    
    // UI state
    std::vector<int64_t> profile_ids_;
    std::vector<int64_t> tunnel_profile_ids_;  // profile ID for each tunnel index
    
    // --------------------------------------------------------
    // Tab creation
    // --------------------------------------------------------
    void create_tabs();
    HWND create_tab_page(int index);
    void setup_dashboard_tab(HWND parent);
    void setup_tunnels_tab(HWND parent);
    void setup_profiles_tab(HWND parent);
    void setup_ports_tab(HWND parent);
    void setup_services_tab(HWND parent);
    void setup_processes_tab(HWND parent);
    void setup_settings_tab(HWND parent);
    
    // --------------------------------------------------------
    // Dashboard tab
    // --------------------------------------------------------
    void refresh_dashboard();
    void update_dashboard_stats();
    
    // --------------------------------------------------------
    // Tunnels tab
    // --------------------------------------------------------
    void refresh_tunnels_list();
    void on_tunnel_start(int index);
    void on_tunnel_stop(int index);
    void on_tunnel_remove(int index);
    void reload_tunnels();
    
    // --------------------------------------------------------
    // Profiles tab
    // --------------------------------------------------------
    void refresh_profiles_list();
    void on_profile_add();
    void on_profile_edit(int index);
    void on_profile_delete(int index);
    void on_profile_up(int index);
    void on_profile_down(int index);
    bool show_profile_dialog(TunnelProfile* profile = nullptr);
    
    // Resource IDs
    enum {
        IDD_PROFILE_DIALOG = 101,
        IDC_EDIT_NAME = 1001,
        IDC_EDIT_SSH_HOST = 1002,
        IDC_EDIT_SSH_PORT = 1003,
        IDC_EDIT_SSH_USER = 1004,
        IDC_EDIT_IDENTITY_FILE = 1005,
        IDC_BROWSE_KEY = 1006,
        IDC_CHK_USE_AGENT = 1007,
        IDC_EDIT_LOCAL_HOST = 1008,
        IDC_EDIT_LOCAL_PORT = 1009,
        IDC_EDIT_REMOTE_HOST = 1010,
        IDC_EDIT_REMOTE_PORT = 1011,
        IDC_CHK_COMPRESSION = 1012,
        IDC_EDIT_KEEPALIVE = 1013,
        IDC_EDIT_TIMEOUT = 1014,
        IDC_CHK_ENABLED = 1015,
    };
    
    // --------------------------------------------------------
    // Ports tab
    // --------------------------------------------------------
    void refresh_ports_list();
    void on_discover_ports();
    
    // --------------------------------------------------------
    // Services tab
    // --------------------------------------------------------
    void refresh_services_list();
    void on_service_action(const std::string& unit, const std::string& action);
    
    // --------------------------------------------------------
    // Processes tab
    // --------------------------------------------------------
    void refresh_processes_list();
    void on_kill_process(uint32_t pid);
    
    // --------------------------------------------------------
    // Settings tab
    // --------------------------------------------------------
    void load_settings();
    void save_settings();
    
    // --------------------------------------------------------
    // Helpers
    // --------------------------------------------------------
    void refresh_current_tab();
    void show_tab_page(int index);
    void layout_page(int index);
    void log_message(const std::wstring& msg);
    void show_error(const std::wstring& title, const std::wstring& msg);
    void on_kill_selected_process();
public:
    static std::wstring str_to_wstr(const std::string& s);
    static std::wstring str_to_wstr(int v);
    static std::wstring str_to_wstr(unsigned int v);
    static std::wstring str_to_wstr(uint16_t v);
    static std::wstring str_to_wstr(uint64_t v);
    static std::string wstr_to_str(const std::wstring& s);
private:
    
    // List view helpers
    void init_list_view(HWND lv, const std::vector<std::pair<int, std::wstring>>& columns);
    void add_list_item(HWND lv, int index, const std::vector<std::wstring>& items);
    void clear_list_view(HWND lv);
    int get_selected_list_item(HWND lv);
    
    // Timer for auto-refresh
    static void CALLBACK refresh_timer(HWND hwnd, UINT msg, UINT_PTR idTimer, DWORD dwTime);
    UINT_PTR refresh_timer_id_ = 0;
};

// ------------------------------------------------------------
// Profile Dialog
// ------------------------------------------------------------

class ProfileDialog {
public:
    ProfileDialog(HINSTANCE hInst, HWND parent, Database* db, TunnelProfile* profile = nullptr);
    ~ProfileDialog();
    
    bool show();  // Returns true if OK/saved
    
private:
    HINSTANCE h_inst_;
    HWND parent_;
    HWND hwnd_ = nullptr;
    Database* db_;
    TunnelProfile owned_profile_;
    TunnelProfile* profile_;
    bool is_new_;
    bool result_ = false;
    
    // Controls
    HWND edit_name_ = nullptr;
    HWND edit_ssh_host_ = nullptr;
    HWND edit_ssh_port_ = nullptr;
    HWND edit_ssh_user_ = nullptr;
    HWND edit_identity_file_ = nullptr;
    HWND chk_use_agent_ = nullptr;
    HWND edit_local_host_ = nullptr;
    HWND edit_local_port_ = nullptr;
    HWND edit_remote_host_ = nullptr;
    HWND edit_remote_port_ = nullptr;
    HWND chk_compression_ = nullptr;
    HWND edit_keepalive_ = nullptr;
    HWND edit_timeout_ = nullptr;
    HWND chk_enabled_ = nullptr;
    
    static INT_PTR CALLBACK dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void init_controls();
    bool get_data();
    void set_data();
    void on_browse_key();
    void on_ok();
    void on_cancel();
};

} // namespace ssh_tunnel