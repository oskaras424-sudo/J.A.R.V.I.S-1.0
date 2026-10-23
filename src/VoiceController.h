#pragma once

#include "TtsProvider.h"

#include <QObject>
#include <QElapsedTimer>
#include <QStringList>

class QProcess;
class QAudioSource;
class QIODevice;
class QMediaDevices;
class QTemporaryFile;

class VoiceController final : public QObject
{
    Q_OBJECT

public:
    explicit VoiceController(QObject *parent = nullptr);
    ~VoiceController() override;

    void initialise();
    QStringList installedVoices() const;
    QStringList installedRecognizers() const;
    bool isListening() const;
    bool isSpeaking() const;
    QString ttsEngine() const;
    QStringList activeVoices() const;
    bool currentTtsEngineReady() const;
    QString currentTtsEngineStatus() const;
    KokoroTtsProvider *kokoroProvider() const;
    QStringList audioInputNames() const;
    QStringList audioInputIds() const;
    QString selectedAudioInputId() const;
    void setAudioInputId(const QString &id);

    void setTtsEngine(const QString &engineId);
    void speak(const QString &text, const QString &voice, int rate, int volume);
    void stopSpeaking();
    void startListening();
    void stopListening();

signals:
    void enginesDiscovered(const QStringList &voices, const QStringList &recognizers);
    void ttsEngineChanged(const QString &engineId);
    void ttsStatusChanged(const QString &status);
    void listeningChanged(bool listening);
    void speakingChanged(bool speaking);
    void transcriptionReceived(const QString &text, double confidence);
    void audioLevelChanged(int level);
    void audioInputsChanged();
    void recognitionStatusChanged(const QString &status);
    void errorOccurred(const QString &message);

private:
    void processRecognitionOutput();
    void setListening(bool listening);
    void handleAudioReadyRead();
    void finishAudioCapture();
    void cleanupAudioCapture();
    void beginRecognition();
    TtsProvider *activeTtsProvider() const;

    QProcess *probeProcess_ = nullptr;
    QProcess *recognitionProcess_ = nullptr;
    QMediaDevices *mediaDevices_ = nullptr;
    QAudioSource *audioSource_ = nullptr;
    QIODevice *audioDevice_ = nullptr;
    QTemporaryFile *recognitionWav_ = nullptr;
    WindowsSapiTtsProvider *windowsTtsProvider_ = nullptr;
    KokoroTtsProvider *kokoroTtsProvider_ = nullptr;
    QString ttsEngine_ = QStringLiteral("windows-sapi");
    QStringList voices_;
    QStringList recognizers_;
    QString recognitionCulture_ = QStringLiteral("en-US");
    QByteArray recognitionBuffer_;
    QByteArray pcmBuffer_;
    QByteArray prerollBuffer_;
    QByteArray pendingAudioBytes_;
    QString selectedAudioInputId_;
    QString recognitionError_;
    qint64 lastSpeechMs_ = -1;
    qint64 captureStartedMs_ = 0;
    QElapsedTimer captureTimer_;
    double noiseRms_ = 100.0;
    bool speechDetected_ = false;
    bool captureActive_ = false;
    bool listening_ = false;
};
