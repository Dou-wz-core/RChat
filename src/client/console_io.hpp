#pragma once
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace console {
#ifdef _WIN32
inline std::string utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const auto length = static_cast<int>(wide.size());
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("invalid UTF-16 input");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, result.data(), size, nullptr, nullptr))
        throw std::runtime_error("UTF-8 conversion failed");
    return result;
}
#endif
inline bool readLine(std::string& line) {
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE); DWORD mode = 0;
    if (GetConsoleMode(input, &mode)) {
        std::wstring wide;
        while (true) {
            wchar_t buffer[1024]; DWORD count = 0;
            if (!ReadConsoleW(input, buffer, 1024, &count, nullptr) || !count) return false;
            wide.append(buffer, count);
            if (wide.find(L'\n') != std::wstring::npos) break;
        }
        while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n')) wide.pop_back();
        if (wide == L"\x1a") return false;
        line = utf8(wide); return true;
    }
#endif
    return static_cast<bool>(std::getline(std::cin, line)); // redirected input must be UTF-8
}
inline void writeLine(const std::string& line) {
#ifdef _WIN32
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode = 0;
    if (GetConsoleMode(output, &mode)) {
        std::string text = line + "\n";
        int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (!size) throw std::runtime_error("invalid UTF-8 output");
        std::wstring wide(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
        DWORD written = 0;
        if (!WriteConsoleW(output, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr)) throw std::runtime_error("console output failed");
        return;
    }
#endif
    std::cout << line << std::endl;
}
} // namespace console
