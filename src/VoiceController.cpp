#include "VoiceController.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QDataStream>
#include <QDir>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QProcess>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextStream>
#include <QtMath>

namespace {

const QString kPowerShell = QStringLiteral("powershell.exe");

QStringList powerShellArguments(const QString &script)
{
    return {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
            QStringLiteral("-NonInteractive"), QStringLiteral("-STA"),
            QStringLiteral("-Command"), script};
}

QAudioFormat speechCaptureFormat()
{
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

void writeWaveHeader(QIODevice *device, quint32 pcmBytes)
{
    QDataStream stream(device);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcmBytes);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000)
           << quint32(32000) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << pcmBytes;
}

} // namespace

VoiceController::VoiceController(QObject *parent)
    : QObject(parent),
      probeProcess_(new QProcess(this)),
      recognitionProcess_(new QProcess(this)),
      mediaDevices_(new QMediaDevices(this)),
      windowsTtsProvider_(new WindowsSapiTtsProvider(this)),
      kokoroTtsProvider_(new KokoroTtsProvider(this))
{
    selectedAudioInputId_ = QSettings().value(QStringLiteral("voice/inputDeviceId")).toString();
    if (selectedAudioInputId_.isEmpty())
        selectedAudioInputId_ = QString::fromUtf8(QMediaDevices::defaultAudioInput().id());
    connect(mediaDevices_, &QMediaDevices::audioInputsChanged, this, [this] {
        const QStringList ids = audioInputIds();
        if (!ids.contains(selectedAudioInputId_)) {
            selectedAudioInputId_ = QString::fromUtf8(QMediaDevices::defaultAudioInput().id());
            QSettings().setValue(QStringLiteral("voice/inputDeviceId"), selectedAudioInputId_);
        }
        emit audioInputsChanged();
    });

    ttsEngine_ = QSettings().value(QStringLiteral("voice/engine"), QStringLiteral("windows-sapi")).toString();
    if (ttsEngine_ != QStringLiteral("kokoro-onnx"))
        ttsEngine_ = QStringLiteral("windows-sapi");

    const auto connectTtsProvider = [this](TtsProvider *provider) {
        connect(provider, &TtsProvider::speakingChanged, this, &VoiceController::speakingChanged);
        connect(provider, &TtsProvider::statusChanged, this, [this, provider](const QString &status) {
            if (provider == activeTtsProvider())
                emit ttsStatusChanged(status);
        });
        connect(provider, &TtsProvider::errorOccurred, this, &VoiceController::errorOccurred);
    };
    connectTtsProvider(windowsTtsProvider_);
    connectTtsProvider(kokoroTtsProvider_);

    probeProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(probeProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray output = probeProcess_->readAllStandardOutput();
        const QByteArray error = probeProcess_->readAllStandardError();
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            emit errorOccurred(QStringLiteral("Windows Speech could not be inspected: %1")
                                   .arg(QString::fromLocal8Bit(error).trimmed()));
            emit enginesDiscovered({}, {});
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(output);
        if (!document.isObject()) {
            emit errorOccurred(QStringLiteral("Windows Speech returned an invalid device list."));
            emit enginesDiscovered({}, {});
            return;
        }
        const QJsonObject object = document.object();
        voices_.clear();
        recognizers_.clear();
        for (const QJsonValue &value : object.value(QStringLiteral("voices")).toArray()) {
            if (value.isString())
                voices_.append(value.toString());
        }
        for (const QJsonValue &value : object.value(QStringLiteral("recognizers")).toArray()) {
            if (value.isString())
                recognizers_.append(value.toString());
        }
        windowsTtsProvider_->setInstalledVoices(voices_);
        emit enginesDiscovered(voices_, recognizers_);
    });
    connect(probeProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit errorOccurred(QStringLiteral("Windows PowerShell could not be started."));
            emit enginesDiscovered({}, {});
        }
    });

    recognitionProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(recognitionProcess_, &QProcess::readyReadStandardOutput,
            this, &VoiceController::processRecognitionOutput);
    connect(recognitionProcess_, &QProcess::readyReadStandardError, this, [this] {
        recognitionError_ += QString::fromLocal8Bit(recognitionProcess_->readAllStandardError());
        if (recognitionError_.size() > 8000)
            recognitionError_.remove(0, recognitionError_.size() - 8000);
    });
    connect(recognitionProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        recognitionError_ += QString::fromLocal8Bit(recognitionProcess_->readAllStandardError());
        setListening(false);
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            const QString details = recognitionError_.trimmed();
            emit errorOccurred(details.isEmpty()
                                   ? QStringLiteral("Windows speech recognition stopped unexpectedly.")
                                   : details);
        }
        if (recognitionWav_) {
            recognitionWav_->close();
            recognitionWav_->deleteLater();
            recognitionWav_ = nullptr;
        }
        recognitionError_.clear();
        recognitionBuffer_.clear();
    });
    connect(recognitionProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setListening(false);
            if (recognitionWav_) {
                recognitionWav_->close();
                delete recognitionWav_;
                recognitionWav_ = nullptr;
            }
            emit errorOccurred(QStringLiteral("Windows PowerShell could not be started for microphone input."));
        }
    });
    connect(recognitionProcess_, &QProcess::started, this, [this] {
        if (!recognitionWav_ || !recognitionWav_->isOpen())
            return;
        const QByteArray path = recognitionWav_->fileName().toUtf8().toBase64();
        recognitionProcess_->write(path + '\n');
        recognitionProcess_->closeWriteChannel();
    });
}

VoiceController::~VoiceController()
{
    stopSpeaking();
    stopListening();
    cleanupAudioCapture();
}

void VoiceController::initialise()
{
    static const QString script = QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$voiceNames = @($synth.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name })
$recognizerNames = @([System.Speech.Recognition.SpeechRecognitionEngine]::InstalledRecognizers() | ForEach-Object { "$($_.Name) ($($_.Culture.Name))" })
[PSCustomObject]@{ voices = $voiceNames; recognizers = $recognizerNames } | ConvertTo-Json -Depth 4 -Compress
)PS");
    probeProcess_->start(kPowerShell, powerShellArguments(script));
    kokoroTtsProvider_->refreshStatus();
}

QStringList VoiceController::installedVoices() const { return voices_; }
QStringList VoiceController::installedRecognizers() const { return recognizers_; }
bool VoiceController::isListening() const { return listening_; }
bool VoiceController::isSpeaking() const { return activeTtsProvider()->isSpeaking(); }

QString VoiceController::ttsEngine() const { return ttsEngine_; }

QStringList VoiceController::activeVoices() const
{
    return activeTtsProvider()->voices();
}

bool VoiceController::currentTtsEngineReady() const
{
    return activeTtsProvider()->isReady();
}

QString VoiceController::currentTtsEngineStatus() const
{
    return activeTtsProvider()->statusText();
}

KokoroTtsProvider *VoiceController::kokoroProvider() const
{
    return kokoroTtsProvider_;
}

QStringList VoiceController::audioInputNames() const
{
    QStringList names;
    for (const QAudioDevice &device : QMediaDevices::audioInputs())
        names.append(device.description());
    return names;
}

QStringList VoiceController::audioInputIds() const
{
    QStringList ids;
    for (const QAudioDevice &device : QMediaDevices::audioInputs())
        ids.append(QString::fromUtf8(device.id()));
    return ids;
}

QString VoiceController::selectedAudioInputId() const
{
    return selectedAudioInputId_;
}

void VoiceController::setAudioInputId(const QString &id)
{
    if (recognitionProcess_->state() != QProcess::NotRunning) {
        emit errorOccurred(QStringLiteral("Wait for the current speech transcript before changing microphones."));
        emit audioInputsChanged();
        return;
    }
    if (captureActive_)
        stopListening();
    selectedAudioInputId_ = id;
    QSettings().setValue(QStringLiteral("voice/inputDeviceId"), id);
}

TtsProvider *VoiceController::activeTtsProvider() const
{
    return ttsEngine_ == QStringLiteral("kokoro-onnx")
               ? static_cast<TtsProvider *>(kokoroTtsProvider_)
               : static_cast<TtsProvider *>(windowsTtsProvider_);
}

void VoiceController::setTtsEngine(const QString &engineId)
{
    const QString nextEngine = engineId == QStringLiteral("kokoro-onnx")
                                   ? QStringLiteral("kokoro-onnx")
                                   : QStringLiteral("windows-sapi");
    if (nextEngine == ttsEngine_)
        return;
    stopSpeaking();
    ttsEngine_ = nextEngine;
    emit ttsEngineChanged(ttsEngine_);
    emit ttsStatusChanged(activeTtsProvider()->statusText());
}

void VoiceController::speak(const QString &text, const QString &voice, int rate, int volume)
{
    TtsProvider *provider = activeTtsProvider();
    if (!provider->isReady()) {
        emit errorOccurred(provider->statusText());
        return;
    }
    if (provider->isSpeaking())
        provider->stop();
    provider->speak(text, voice, rate, volume);
}

void VoiceController::stopSpeaking()
{
    windowsTtsProvider_->stop();
    kokoroTtsProvider_->stop();
}

void VoiceController::startListening()
{
    if (listening_)
        return;
    if (recognitionProcess_->state() != QProcess::NotRunning) {
        emit errorOccurred(QStringLiteral("The previous recording is still being recognized. Wait for its transcript, then retry."));
        return;
    }
    recognitionCulture_.clear();
    for (const QString &name : recognizers_) {
        if (name.contains(QStringLiteral("(en-GB)"), Qt::CaseInsensitive)) {
            recognitionCulture_ = QStringLiteral("en-GB");
            break;
        }
        if (name.contains(QStringLiteral("(en-US)"), Qt::CaseInsensitive))
            recognitionCulture_ = QStringLiteral("en-US");
    }
    if (recognitionCulture_.isEmpty()) {
        emit errorOccurred(recognizers_.isEmpty()
                               ? QStringLiteral("No Windows speech recognizer is installed. Add an English Speech language under Windows Settings > Time & language > Language & region.")
                               : QStringLiteral("The installed Windows recognizer is not English. Add an English Speech language under Windows Settings > Time & language > Language & region."));
        return;
    }

    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    if (inputs.isEmpty()) {
        emit errorOccurred(QStringLiteral("Windows reports no microphone input devices. Connect a microphone and check Windows microphone privacy settings."));
        return;
    }
    QAudioDevice selectedDevice;
    for (const QAudioDevice &device : inputs) {
        if (QString::fromUtf8(device.id()) == selectedAudioInputId_) {
            selectedDevice = device;
            break;
        }
    }
    if (selectedDevice.isNull()) {
        selectedDevice = QMediaDevices::defaultAudioInput();
        selectedAudioInputId_ = QString::fromUtf8(selectedDevice.id());
        QSettings().setValue(QStringLiteral("voice/inputDeviceId"), selectedAudioInputId_);
    }
    const QAudioFormat format = speechCaptureFormat();
    if (!selectedDevice.isFormatSupported(format)) {
        emit errorOccurred(QStringLiteral("The selected microphone does not support 16 kHz mono 16-bit PCM capture required by the installed Windows English recognizer."));
        return;
    }

    pcmBuffer_.clear();
    prerollBuffer_.clear();
    pendingAudioBytes_.clear();
    noiseRms_ = 100.0;
    lastSpeechMs_ = -1;
    speechDetected_ = false;
    captureStartedMs_ = 0;
    captureTimer_.start();
    audioSource_ = new QAudioSource(selectedDevice, format, this);
    audioSource_->setBufferSize(6400);
    audioDevice_ = audioSource_->start();
    if (!audioDevice_ || audioSource_->error() != QAudio::NoError) {
        cleanupAudioCapture();
        emit errorOccurred(QStringLiteral("Could not open %1. Check that it is connected, not in use by an exclusive application, and allowed under Windows Settings > Privacy & security > Microphone.")
                               .arg(selectedDevice.description()));
        return;
    }
    captureActive_ = true;
    connect(audioDevice_, &QIODevice::readyRead, this, &VoiceController::handleAudioReadyRead);
    setListening(true);
    emit recognitionStatusChanged(QStringLiteral("Listening · speak a complete sentence. Recording ends after a pause."));
}

void VoiceController::handleAudioReadyRead()
{
    if (!captureActive_ || !audioDevice_)
        return;
    pendingAudioBytes_.append(audioDevice_->readAll());
    const qsizetype byteCount = pendingAudioBytes_.size() - (pendingAudioBytes_.size() % 2);
    if (byteCount <= 0)
        return;
    const QByteArray chunk = pendingAudioBytes_.left(byteCount);
    pendingAudioBytes_.remove(0, byteCount);

    const auto *samples = reinterpret_cast<const qint16 *>(chunk.constData());
    const qsizetype sampleCount = chunk.size() / qsizetype(sizeof(qint16));
    long double sumSquares = 0.0;
    for (qsizetype i = 0; i < sampleCount; ++i) {
        const long double sample = samples[i];
        sumSquares += sample * sample;
    }
    const double rms = sampleCount > 0 ? qSqrt(double(sumSquares / sampleCount)) : 0.0;
    const int meter = qBound(0, qRound((20.0 * qLn(qMax(1.0, rms)) / qLn(10.0) - 35.0) * 2.4), 100);
    emit audioLevelChanged(meter);

    const qint64 elapsed = captureTimer_.elapsed();
    if (!speechDetected_) {
        prerollBuffer_.append(chunk);
        constexpr qsizetype maxPrerollBytes = 16000 * 2 * 4 / 10;
        if (prerollBuffer_.size() > maxPrerollBytes)
            prerollBuffer_.remove(0, prerollBuffer_.size() - maxPrerollBytes);

        const double threshold = qMax(420.0, noiseRms_ * 3.0);
        if (rms >= threshold) {
            speechDetected_ = true;
            captureStartedMs_ = elapsed;
            lastSpeechMs_ = elapsed;
            pcmBuffer_ = prerollBuffer_;
            pcmBuffer_.append(chunk);
            prerollBuffer_.clear();
            emit recognitionStatusChanged(QStringLiteral("Speech detected · capturing your sentence…"));
        } else {
            noiseRms_ = qBound(60.0, noiseRms_ * 0.94 + rms * 0.06, 5000.0);
        }
    } else {
        pcmBuffer_.append(chunk);
        if (rms >= qMax(350.0, noiseRms_ * 2.2))
            lastSpeechMs_ = elapsed;
        if (elapsed - lastSpeechMs_ >= 1800 || elapsed - captureStartedMs_ >= 30000) {
            finishAudioCapture();
            return;
        }
    }

    if (!speechDetected_ && elapsed >= 12000) {
        cleanupAudioCapture();
        setListening(false);
        emit recognitionStatusChanged(QStringLiteral("No speech detected · check the microphone level and try again."));
        emit errorOccurred(QStringLiteral("No speech was detected by %1. Check the live microphone meter, input selection and Windows microphone permission, then retry.")
                               .arg(selectedAudioInputId_.isEmpty() ? QStringLiteral("the microphone") : QStringLiteral("the selected microphone")));
    }
}

void VoiceController::finishAudioCapture()
{
    if (!captureActive_)
        return;
    cleanupAudioCapture();
    setListening(false);
    if (!speechDetected_ || pcmBuffer_.size() < 6400) {
        pcmBuffer_.clear();
        emit recognitionStatusChanged(QStringLiteral("No complete speech was captured. Try again."));
        emit errorOccurred(QStringLiteral("No complete speech was captured. Speak after the microphone level meter moves, then pause briefly to finish."));
        return;
    }
    emit recognitionStatusChanged(QStringLiteral("Recognizing captured English speech…"));
    beginRecognition();
}

void VoiceController::beginRecognition()
{
    if (recognitionProcess_->state() != QProcess::NotRunning) {
        pcmBuffer_.clear();
        emit errorOccurred(QStringLiteral("Speech recognition is still finishing the previous recording. Please retry in a moment."));
        return;
    }
    recognitionBuffer_.clear();
    recognitionError_.clear();
    recognitionWav_ = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/jarvis-speech-XXXXXX.wav"), this);
    if (!recognitionWav_->open()) {
        delete recognitionWav_;
        recognitionWav_ = nullptr;
        pcmBuffer_.clear();
        emit recognitionStatusChanged(QStringLiteral("Could not prepare the captured audio for recognition."));
        emit errorOccurred(QStringLiteral("Could not create a temporary WAV file for speech recognition."));
        return;
    }
    writeWaveHeader(recognitionWav_, quint32(pcmBuffer_.size()));
    recognitionWav_->write(pcmBuffer_);
    recognitionWav_->flush();
    recognitionWav_->close();
    pcmBuffer_.clear();

    const QString script = QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
    Add-Type -AssemblyName System.Speech
    $path64 = [Console]::In.ReadLine()
    if ([string]::IsNullOrWhiteSpace($path64)) { throw 'No captured audio file was provided.' }
    $wavPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($path64))
    $culture = [System.Globalization.CultureInfo]::GetCultureInfo('%1')
    $engine = New-Object -TypeName System.Speech.Recognition.SpeechRecognitionEngine -ArgumentList $culture
    $engine.LoadGrammar((New-Object System.Speech.Recognition.DictationGrammar))
    $engine.SetInputToWaveFile($wavPath)
    $result = $engine.Recognize()
    if ($null -eq $result -or [string]::IsNullOrWhiteSpace($result.Text)) {
        [Console]::Out.WriteLine('{"type":"no_speech"}')
    } else {
        @{ type = 'recognized'; text = $result.Text; confidence = $result.Confidence } | ConvertTo-Json -Compress
    }
    $engine.Dispose()
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
}
)PS").arg(recognitionCulture_);
    recognitionProcess_->start(kPowerShell, powerShellArguments(script));
}

void VoiceController::stopListening()
{
    if (captureActive_) {
        finishAudioCapture();
        return;
    }
    if (recognitionProcess_->state() != QProcess::NotRunning) {
        recognitionProcess_->kill();
        recognitionProcess_->waitForFinished(750);
    }
    setListening(false);
}

void VoiceController::processRecognitionOutput()
{
    recognitionBuffer_.append(recognitionProcess_->readAllStandardOutput());
    while (true) {
        const qsizetype newline = recognitionBuffer_.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = recognitionBuffer_.left(newline).trimmed();
        recognitionBuffer_.remove(0, newline + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject())
            continue;
        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready")) {
            setListening(true);
        } else if (type == QStringLiteral("recognized")) {
            const QString text = object.value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty()) {
                emit recognitionStatusChanged(QStringLiteral("Transcript ready · review it before sending or executing a command."));
                emit transcriptionReceived(text, object.value(QStringLiteral("confidence")).toDouble());
            }
        } else if (type == QStringLiteral("no_speech")) {
            emit recognitionStatusChanged(QStringLiteral("The English recognizer returned no final transcript. Try again or type your message."));
            emit errorOccurred(QStringLiteral("The installed English speech recognizer returned no final transcript for the captured audio. Check that you spoke English clearly and retry."));
        }
    }
}

void VoiceController::cleanupAudioCapture()
{
    captureActive_ = false;
    if (audioSource_) {
        if (audioDevice_)
            disconnect(audioDevice_, nullptr, this, nullptr);
        audioSource_->stop();
        audioDevice_ = nullptr;
        audioSource_->deleteLater();
        audioSource_ = nullptr;
    }
    emit audioLevelChanged(0);
}

void VoiceController::setListening(bool listening)
{
    if (listening_ == listening)
        return;
    listening_ = listening;
    emit listeningChanged(listening_);
}

