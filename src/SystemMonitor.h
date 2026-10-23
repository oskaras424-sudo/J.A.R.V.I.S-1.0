#pragma once

#include <QObject>
#include <QString>

class SystemMonitor final : public QObject
{
    Q_OBJECT

public:
    explicit SystemMonitor(QObject *parent = nullptr);

    void refresh();

    bool cpuReady() const { return cpuReady_; }
    double cpuPercent() const { return cpuPercent_; }
    double memoryPercent() const { return memoryPercent_; }
    double memoryUsedGb() const { return memoryUsedGb_; }
    double memoryTotalGb() const { return memoryTotalGb_; }
    QString gpuName() const { return gpuName_; }

signals:
    void updated();

private:
    bool cpuReady_ = false;
    double cpuPercent_ = 0.0;
    double memoryPercent_ = 0.0;
    double memoryUsedGb_ = 0.0;
    double memoryTotalGb_ = 0.0;
    QString gpuName_ = QStringLiteral("Adapter unavailable");
    quint64 previousIdle_ = 0;
    quint64 previousKernel_ = 0;
    quint64 previousUser_ = 0;
    bool hasCpuBaseline_ = false;
};
