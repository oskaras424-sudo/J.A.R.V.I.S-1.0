#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <atomic>

class QThread;

class ToolRegistry final : public QObject
{
    Q_OBJECT

public:
    struct Application {
        QString id;
        QString label;
        QString executable;
        QString processName;
        bool appFolderId = false;
    };

    struct SpokenAction {
        enum class Type { LaunchApplication, CloseApplication, OpenWebsite, OpenPath, StopSpeaking };
        Type type = Type::LaunchApplication;
        QString target;
        QString label;
    };

    explicit ToolRegistry(QObject *parent = nullptr);
    ~ToolRegistry() override;

    QVector<Application> availableApplications() const;
    QJsonArray aiToolDefinitions() const;
    bool aiToolRequiresConfirmation(const QString &toolName) const;
    bool executeAiTool(const QString &toolName, const QJsonObject &arguments,
                       QString *details = nullptr) const;
    bool launchApplication(const QString &applicationId, QString *details = nullptr) const;
    bool closeApplication(const QString &applicationId, QString *details = nullptr) const;
    bool openWebsite(const QString &url, QString *details = nullptr) const;
    bool openPath(const QString &path, QString *details = nullptr) const;
    bool parseSpokenCommand(const QString &text, QVector<SpokenAction> *actions,
                            QString *response = nullptr) const;
    bool spokenActionRequiresConfirmation(const SpokenAction &action) const;
    bool executeSpokenAction(const SpokenAction &action, QString *details = nullptr) const;
    bool createNote(const QString &title, const QString &content, QString *savedPath,
                    QString *error = nullptr) const;
    bool startFileSearch(const QString &rootDirectory, const QString &query, QString *error = nullptr);
    bool fileSearchRunning() const;
    void cancelFileSearch();

signals:
    void fileSearchProgress(qint64 filesScanned, int matchesFound);
    void fileSearchFinished(const QStringList &matches, qint64 filesScanned,
                            bool cancelled, const QString &error);

private:
    QThread *searchThread_ = nullptr;
    std::atomic_bool cancelSearch_{false};
};
