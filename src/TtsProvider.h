#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QSaveFile;
class QCryptographicHash;

class TtsProvider : public QObject
{
    Q_OBJECT

public:
    explicit TtsProvider(QObject *parent = nullptr);
    ~TtsProvider() override = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList voices() const = 0;
    virtual bool isReady() const = 0;
    virtual QString statusText() const = 0;
    virtual void speak(const QString &text, const QString &voice, int rate, int volume) = 0;
    virtual void stop() = 0;
    virtual bool isSpeaking() const = 0;

signals:
    void speakingChanged(bool speaking);
    void statusChanged(const QString &status);
    void errorOccurred(const QString &message);

protected:
    void setStatusText(const QString &status);
    QString currentStatusText() const;

private:
    QString statusText_;
};

class WindowsSapiTtsProvider final : public TtsProvider
{
    Q_OBJECT

public:
    explicit WindowsSapiTtsProvider(QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList voices() const override;
    bool isReady() const override;
    QString statusText() const override;
    void setInstalledVoices(const QStringList &voices);
    void speak(const QString &text, const QString &voice, int rate, int volume) override;
    void stop() override;
    bool isSpeaking() const override;

private:
    QProcess *process_ = nullptr;
    QStringList voices_;
    QString pendingRequest_;
    bool stoppedByUser_ = false;
    bool speaking_ = false;
};

class KokoroTtsProvider final : public TtsProvider
{
    Q_OBJECT

public:
    explicit KokoroTtsProvider(QObject *parent = nullptr);
    ~KokoroTtsProvider() override;

    QString id() const override;
    QString displayName() const override;
    QStringList voices() const override;
    bool isReady() const override;
    QString statusText() const override;
    QString modelDirectory() const;
    QString pythonExecutable() const;
    QString pythonInstallCommand() const;
    QString runtimeStatusText() const;
    QString modelStatusText() const;
    bool runtimeReady() const;
    void setPythonExecutable(const QString &path);
    bool modelsInstalled() const;
    bool modelFilesVerified() const;
    bool hasLocalModelFiles() const;
    bool isDownloading() const;
    void refreshStatus();
    void downloadModels();
    void cancelDownload();
    bool removeModels(QString *error = nullptr);
    void speak(const QString &text, const QString &voice, int rate, int volume) override;
    void stop() override;
    bool isSpeaking() const override;

signals:
    void modelStatusChanged(bool installed, const QString &details);
    void runtimeStatusChanged(bool ready, const QString &details);
    void downloadProgress(const QString &fileName, qint64 received, qint64 total);
    void downloadFinished();
    void downloadFailed(const QString &message);

private:
    struct DownloadAsset {
        QString fileName;
        QUrl url;
        qint64 expectedBytes = 0;
        QByteArray sha256;
    };

    void checkRuntime();
    void checkModels();
    void updateModelStatus();
    void validateModelIfReady();
    void finishModelValidation(int exitCode, int exitStatus);
    void startNextDownload();
    void finishDownload(QNetworkReply *reply);
    void finishSynthesis(int exitCode, int exitStatus);
    void startPlayback(const QString &audioPath);
    void setSpeakingState(bool speaking);
    QString workerPath() const;
    QString modelPath() const;
    QString voicesPath() const;

    QProcess *runtimeProcess_ = nullptr;
    QProcess *modelValidationProcess_ = nullptr;
    QProcess *synthesisProcess_ = nullptr;
    QProcess *playbackProcess_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *downloadReply_ = nullptr;
    QSaveFile *downloadFile_ = nullptr;
    QString pythonExecutable_;
    QString modelDirectory_;
    QString temporaryAudioPath_;
    QString temporaryValidationAudioPath_;
    QByteArray pendingSynthesis_;
    QByteArray pendingValidation_;
    QByteArray downloadPrefix_;
    QString runtimeStatus_;
    QString modelStatus_;
    QString modelVoiceWarning_;
    QString lastDownloadIssue_;
    QString modelFilesError_;
    QString modelValidationError_;
    QString downloadWriteError_;
    QStringList verifiedKokoroVoices_;
    QList<DownloadAsset> downloadAssets_;
    int downloadIndex_ = 0;
    int downloadRetries_ = 0;
    qint64 downloadBytesWritten_ = 0;
    QCryptographicHash *downloadHash_ = nullptr;
    bool runtimeReady_ = false;
    bool modelsInstalled_ = false;
    bool modelFilesVerified_ = false;
    bool modelSynthesisVerified_ = false;
    bool modelValidationAttempted_ = false;
    bool speaking_ = false;
    bool stopping_ = false;
    bool downloading_ = false;
};
