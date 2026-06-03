#include "engine/core/CrashHandlerWin32.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <dbghelp.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace engine {
namespace {

std::filesystem::path local_app_data_path() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        return {};
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

std::filesystem::path make_crash_dump_path() {
    const auto base = local_app_data_path() / "VoxelEngine" / "crashes";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);

    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);

    std::ostringstream name;
    name << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".dmp";
    return base / name.str();
}

void log_crash_dump_path(const std::filesystem::path& dump_path) {
    const auto message = std::string("Crash dump written to: ") + dump_path.string();
    if (auto logger = spdlog::default_logger()) {
        logger->critical(message);
        logger->flush();
    }
    OutputDebugStringA((message + "\n").c_str());
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* info) {
    if (!info) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const auto dump_path = make_crash_dump_path();
    const HANDLE file =
        CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(file);
        log_crash_dump_path(dump_path);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void crash_handler_install() {
    SetUnhandledExceptionFilter(unhandled_exception_filter);
}

} // namespace engine
