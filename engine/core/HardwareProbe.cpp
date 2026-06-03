#include "engine/core/HardwareProbe.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winioctl.h>
#endif

#include <algorithm>
#include <thread>
#include <vector>

namespace engine {
namespace {

#ifdef _WIN32
int count_physical_cores() {
    DWORD buffer_size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &buffer_size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer_size == 0) {
        return 1;
    }

    std::vector<std::uint8_t> buffer(buffer_size);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
            &buffer_size)) {
        return 1;
    }

    int cores = 0;
    DWORD offset = 0;
    while (offset < buffer_size) {
        const auto* info =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (info->Relationship == RelationProcessorCore) {
            ++cores;
        }
        offset += info->Size;
    }
    return std::max(1, cores);
}

int count_logical_cores() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return std::max(1, static_cast<int>(info.dwNumberOfProcessors));
}

size_t total_ram_bytes() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return 0;
    }
    return static_cast<size_t>(status.ullTotalPhys);
}

bool probe_has_ssd() {
    HANDLE drive =
        CreateFileW(L"\\\\.\\PhysicalDrive0", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, 0, nullptr);
    if (drive == INVALID_HANDLE_VALUE) {
        return true;
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR penalty{};
    DWORD bytes_returned = 0;
    const BOOL ok = DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                                    &penalty, sizeof(penalty), &bytes_returned, nullptr);
    CloseHandle(drive);

    if (!ok) {
        return true;
    }
    return !penalty.IncursSeekPenalty;
}
#endif

} // namespace

CpuHardware HardwareProbe::run_cpu() {
    CpuHardware hw{};

#ifdef _WIN32
    hw.logical_cores = count_logical_cores();
    hw.physical_cores = count_physical_cores();
    hw.ram_bytes = total_ram_bytes();
    hw.has_ssd = probe_has_ssd();
#else
    const int hw_threads = static_cast<int>(std::thread::hardware_concurrency());
    hw.logical_cores = std::max(1, hw_threads);
    hw.physical_cores = std::max(1, hw_threads / 2);
    hw.ram_bytes = 8ULL * 1024 * 1024 * 1024;
    hw.has_ssd = true;
#endif

    return hw;
}

} // namespace engine
