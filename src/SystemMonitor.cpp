#include "SystemMonitor.h"

#include <Windows.h>
#include <dxgi.h>

#include <algorithm>

namespace {

quint64 fileTimeValue(const FILETIME &value)
{
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

QString findGpuAdapter()
{
    IDXGIFactory1 *factory = nullptr;
    const IID factoryId = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    if (FAILED(CreateDXGIFactory1(factoryId, reinterpret_cast<void **>(&factory))))
        return QStringLiteral("Adapter unavailable");

    QString preferred;
    QString fallback;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1 *adapter = nullptr;
        const HRESULT result = factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(result) || adapter == nullptr)
            continue;

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            const QString name = QString::fromWCharArray(desc.Description).trimmed();
            if (!name.isEmpty()) {
                fallback = name;
                if (name.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)) {
                    preferred = name;
                    adapter->Release();
                    break;
                }
                if (preferred.isEmpty() && name.contains(QStringLiteral("AMD"), Qt::CaseInsensitive))
                    preferred = name;
            }
        }
        adapter->Release();
    }
    factory->Release();

    if (!preferred.isEmpty())
        return preferred;
    if (!fallback.isEmpty())
        return fallback;
    return QStringLiteral("Adapter unavailable");
}

} // namespace

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent), gpuName_(findGpuAdapter())
{
}

void SystemMonitor::refresh()
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const quint64 idle = fileTimeValue(idleTime);
        const quint64 kernel = fileTimeValue(kernelTime);
        const quint64 user = fileTimeValue(userTime);
        if (hasCpuBaseline_) {
            const quint64 idleDelta = idle - previousIdle_;
            const quint64 kernelDelta = kernel - previousKernel_;
            const quint64 userDelta = user - previousUser_;
            const quint64 totalDelta = kernelDelta + userDelta;
            if (totalDelta > 0) {
                const double busy = 1.0 - static_cast<double>(idleDelta) / static_cast<double>(totalDelta);
                cpuPercent_ = std::clamp(busy * 100.0, 0.0, 100.0);
                cpuReady_ = true;
            }
        }
        previousIdle_ = idle;
        previousKernel_ = kernel;
        previousUser_ = user;
        hasCpuBaseline_ = true;
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        constexpr double bytesPerGb = 1024.0 * 1024.0 * 1024.0;
        memoryTotalGb_ = static_cast<double>(memory.ullTotalPhys) / bytesPerGb;
        memoryUsedGb_ = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys) / bytesPerGb;
        memoryPercent_ = static_cast<double>(memory.dwMemoryLoad);
    }

    emit updated();
}
