// Test helper child process for process-runner tests.
// Usage: helper.exe <mode> [arg]
// Modes:
//   exit            - exit immediately with code 0
//   exit-code <n>   - exit with the given code
//   stdout <n>      - write n 'A' bytes to stdout, exit 0
//   stderr <n>      - write n 'B' bytes to stderr, exit 0
//   both <n>        - write n bytes to stdout and stderr simultaneously, exit 0
//   infinite        - write to stdout forever (tests bounded buffers + timeout)
//   sleep <ms>      - sleep then exit 0 (tests timeout/cancel)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string mode = argv[1];

    if (mode == "exit") return 0;
    if (mode == "exit-code" && argc >= 3) return std::atoi(argv[2]);
    if (mode == "stdout" && argc >= 3) {
        const long bytes = std::atol(argv[2]);
        std::string chunk(4096, 'A');
        long remaining = bytes;
        while (remaining > 0) {
            const long n = remaining < 4096 ? remaining : 4096;
            fwrite(chunk.data(), 1, static_cast<size_t>(n), stdout);
            remaining -= n;
        }
        fflush(stdout);
        return 0;
    }
    if (mode == "stderr" && argc >= 3) {
        const long bytes = std::atol(argv[2]);
        std::string chunk(4096, 'B');
        long remaining = bytes;
        while (remaining > 0) {
            const long n = remaining < 4096 ? remaining : 4096;
            fwrite(chunk.data(), 1, static_cast<size_t>(n), stderr);
            remaining -= n;
        }
        fflush(stderr);
        return 0;
    }
    if (mode == "both" && argc >= 3) {
        const long bytes = std::atol(argv[2]);
        std::string chunkOut(4096, 'A');
        std::string chunkErr(4096, 'B');
        long remaining = bytes;
        while (remaining > 0) {
            const long n = remaining < 4096 ? remaining : 4096;
            fwrite(chunkOut.data(), 1, static_cast<size_t>(n), stdout);
            fwrite(chunkErr.data(), 1, static_cast<size_t>(n), stderr);
            remaining -= n;
        }
        fflush(stdout);
        fflush(stderr);
        return 0;
    }
    if (mode == "infinite") {
        std::string chunk(4096, 'C');
        for (;;) {
            fwrite(chunk.data(), 1, chunk.size(), stdout);
            fflush(stdout);
            Sleep(5);
        }
    }
    if (mode == "sleep" && argc >= 3) {
        Sleep(static_cast<DWORD>(std::atol(argv[2])));
        return 0;
    }
    return 2;
}