#include <Windows.h>

#include <filesystem>
#include <fstream>

int main() {
    wchar_t path[32768]{};
    const auto length = GetEnvironmentVariableW(L"CATRO_TEST_PID_FILE", path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) {
        return 2;
    }
    std::ofstream output(std::filesystem::path(path), std::ios::trunc);
    output << GetCurrentProcessId() << '\n';
    output.close();
    Sleep(INFINITE);
    return 0;
}
