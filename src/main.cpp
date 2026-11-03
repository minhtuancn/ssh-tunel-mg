#include <windows.h>

#include "main_window.h"
#include "ssh_tunnel.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (!ssh_tunnel::init_winsock()) {
        MessageBoxW(nullptr, L"Không thể khởi tạo Winsock.", L"SSH Tunnel Manager",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    ssh_tunnel::MainWindow window(instance);
    if (!window.create(showCommand)) {
        MessageBoxW(nullptr, L"Không thể khởi tạo cửa sổ chính.", L"SSH Tunnel Manager",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    return window.run_message_loop();
}
