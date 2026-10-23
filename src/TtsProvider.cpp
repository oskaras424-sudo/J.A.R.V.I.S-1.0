#include "TtsProvider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPair>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <Windows.h>

namespace {

const QString kPowerShell = QStringLiteral("powershell.exe");
constexpr int kMaxAutomaticDownloadRetries = 2;

QStringList powerShellArguments(const QString &script)
{
    return {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
            QStringLiteral("-NonInteractive"), QStringLiteral("-STA"),
            QStringLiteral("-Command"), script};
}

void hideProcessWindow(QProcess *process)
{
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
}

QString cleanForSpeech(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("```[\\s\\S]*?```")));
    text.remove(QRegularExpression(QStringLiteral("`[^`]*`")));
    text.replace(QRegularExpression(QStringLiteral("!?\\[([^\\]]+)\\]\\([^)]*\\)")), QStringLiteral("\\1"));
    text.remove(QRegularExpression(QStringLiteral("https?://\\S+")));
    text.remove(QRegularExpression(QStringLiteral("(^|\\n)\\s{0,3}#{1,6}\\s*")));
    text.remove(QRegularExpression(QStringLiteral("^[ \\t]*[-*+]\\s+"), QRegularExpression::MultilineOption));
    text.remove(QRegularExpression(QStringLiteral("^[ \\t]*\\d+[.)]\\s+"), QRegularExpression::MultilineOption));
    text.remove(QRegularExpression(QStringLiteral("[*_~>`]")));
    text.replace(QRegularExpression(QStringLiteral("([!?.,;:])\\1{1,}")), QStringLiteral("\\1"));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

const QStringList kokoroVoiceIds = {
    QStringLiteral("bm_george"), QStringLiteral("bm_fable"),
    QStringLiteral("bm_lewis"), QStringLiteral("bm_daniel")
};

QString fileDigest(QFile &file, qint64 *bytesRead, QString *error)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            if (error)
                *error = file.errorString();
            return {};
        }
        total += chunk.size();
        hash.addData(chunk);
    }
    if (bytesRead)
        *bytesRead = total;
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

TtsProvider::TtsProvider(QObject *parent)
    : QObject(parent)
{
}

void TtsProvider::setStatusText(const QString &status)
{
    if (statusText_ == status)
        return;
    statusText_ = status;
    emit statusChanged(statusText_);
}

QString TtsProvider::currentStatusText() const
{
    return statusText_;
}

WindowsSapiTtsProvider::WindowsSapiTtsProvider(QObject *parent)
    : TtsProvider(parent), process_(new QProcess(this))
{
    hideProcessWindow(process_);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::started, this, [this] {
        process_->write(pendingRequest_.toUtf8() + '\n');
        process_->closeWriteChannel();
        speaking_ = true;
        setStatusText(QStringLiteral("Speaking with Windows Speech."));
        emit speakingChanged(true);
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString error = QString::fromLocal8Bit(process_->readAllStandardError()).trimmed();
        const bool wasStopped = stoppedByUser_;
        stoppedByUser_ = false;
        speaking_ = false;
        emit speakingChanged(false);
        if (!wasStopped && (exitCode != 0 || exitStatus != QProcess::NormalExit)) {
            const QString message = error.isEmpty()
                                        ? QStringLiteral("Windows speech output failed.")
                                        : error;
            setStatusText(message);
            emit errorOccurred(message);
        } else if (!wasStopped) {
            setStatusText(QStringLiteral("Windows speech is ready."));
        }
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            speaking_ = false;
            emit speakingChanged(false);
            const QString message = QStringLiteral("Windows PowerShell could not be started for speech output.");
            setStatusText(message);
            emit errorOccurred(message);
        }
    });
    setStatusText(QStringLiteral("Checking installed Windows voices."));
}

QString WindowsSapiTtsProvider::id() const { return QStringLiteral("windows-sapi"); }
QString WindowsSapiTtsProvider::displayName() const { return QStringLiteral("Windows Speech"); }
QStringList WindowsSapiTtsProvider::voices() const { return voices_; }
bool WindowsSapiTtsProvider::isReady() const { return !voices_.isEmpty(); }
QString WindowsSapiTtsProvider::statusText() const
{
    if (!isReady())
        return QStringLiteral("No Windows Speech voice is installed.");
    return QStringLiteral("Windows Speech is available with %1 installed voice(s).").arg(voices_.size());
}

void WindowsSapiTtsProvider::setInstalledVoices(const QStringList &voices)
{
    voices_ = voices;
    setStatusText(statusText());
}

void WindowsSapiTtsProvider::speak(const QString &text, const QString &voice, int rate, int volume)
{
    const QString cleanedText = cleanForSpeech(text);
    if (cleanedText.isEmpty())
        return;
    if (!isReady()) {
        emit errorOccurred(statusText());
        return;
    }
    if (process_->state() != QProcess::NotRunning)
        stop();

    const QJsonObject request{{QStringLiteral("text"), cleanedText},
                              {QStringLiteral("voice"), voice},
                              {QStringLiteral("rate"), qBound(-10, rate, 10)},
                              {QStringLiteral("volume"), qBound(0, volume, 100)}};
    pendingRequest_ = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
    stoppedByUser_ = false;
    static const QString script = QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
    Add-Type -AssemblyName System.Speech
    $request = [Console]::In.ReadLine() | ConvertFrom-Json
    $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
    if (-not [string]::IsNullOrWhiteSpace($request.voice)) { $synth.SelectVoice($request.voice) }
    $synth.Rate = [Math]::Max(-10, [Math]::Min(10, [int]$request.rate))
    $synth.Volume = [Math]::Max(0, [Math]::Min(100, [int]$request.volume))
    $synth.Speak([string]$request.text)
    $synth.Dispose()
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
}
)PS");
    process_->start(kPowerShell, powerShellArguments(script));
    setStatusText(QStringLiteral("Starting Windows speech."));
}

void WindowsSapiTtsProvider::stop()
{
    if (process_->state() == QProcess::NotRunning)
        return;
    stoppedByUser_ = true;
    process_->kill();
    process_->waitForFinished(200);
    if (speaking_) {
        speaking_ = false;
        emit speakingChanged(false);
    }
    setStatusText(QStringLiteral("Windows speech stopped."));
}

bool WindowsSapiTtsProvider::isSpeaking() const { return speaking_; }

KokoroTtsProvider::KokoroTtsProvider(QObject *parent)
    : TtsProvider(parent),
      runtimeProcess_(new QProcess(this)),
      modelValidationProcess_(new QProcess(this)),
      synthesisProcess_(new QProcess(this)),
      playbackProcess_(new QProcess(this)),
      network_(new QNetworkAccessManager(this))
{
    const QString defaultDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                     + QStringLiteral("/voice-models/kokoro");
    modelDirectory_ = QDir::cleanPath(defaultDirectory);
    pythonExecutable_ = QSettings().value(QStringLiteral("voice/kokoro/python"),
                                          QStandardPaths::findExecutable(QStringLiteral("python")))
                            .toString();
    if (pythonExecutable_.isEmpty())
        pythonExecutable_ = QStringLiteral("python");

    // Pinned values independently match the GitHub Releases API digest and byte-size fields
    // for tag model-files-v1.1 (asset ids 520133701 and 520134664).
    const QString releaseBase = QStringLiteral("https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/");
    downloadAssets_ = {
        {QStringLiteral("kokoro-v1.0.int8.onnx"),
         QUrl(releaseBase + QStringLiteral("kokoro-v1.0.int8.onnx")),
         114119327,
         QByteArray::fromHex("ae315a79b623f244700e4afb9246c46a26066782e049ba174bf3ba433970ee9c")},
        {QStringLiteral("voices-v1.0.bin"),
         QUrl(releaseBase + QStringLiteral("voices-v1.0.bin")),
         28214398,
         QByteArray::fromHex("bca610b8308e8d99f32e6fe4197e7ec01679264efed0cac9140fe9c29f1fbf7d")}
    };

    hideProcessWindow(runtimeProcess_);
    runtimeProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(runtimeProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray output = runtimeProcess_->readAllStandardOutput().trimmed();
        const QString errorOutput = QString::fromLocal8Bit(runtimeProcess_->readAllStandardError()).trimmed();
        const QJsonDocument document = QJsonDocument::fromJson(output);
        const QJsonObject report = document.object();
        const QString interpreter = report.value(QStringLiteral("executable")).toString(pythonExecutable_);
        const QString version = report.value(QStringLiteral("version")).toString();
        QStringList issues;
        for (const QJsonValue &value : report.value(QStringLiteral("issues")).toArray()) {
            if (value.isString())
                issues.append(value.toString());
        }
        runtimeReady_ = document.isObject() && report.value(QStringLiteral("ready")).toBool()
                        && exitCode == 0 && exitStatus == QProcess::NormalExit;
        if (runtimeReady_) {
            runtimeStatus_ = QStringLiteral("Kokoro runtime is ready in Python %1 (%2).")
                                 .arg(version, interpreter);
            modelValidationAttempted_ = false;
            modelValidationError_.clear();
        } else {
            QString detail = issues.join(QStringLiteral("; "));
            if (detail.isEmpty())
                detail = errorOutput.isEmpty() ? QString::fromUtf8(output) : errorOutput;
            if (detail.isEmpty())
                detail = QStringLiteral("The interpreter exited with code %1.").arg(exitCode);
            const QString pipHint = report.value(QStringLiteral("pip")).toBool()
                                        ? QStringLiteral("Run: %1").arg(pythonInstallCommand())
                                        : QStringLiteral("pip is missing. Run %1 -m ensurepip --upgrade, then %2")
                                              .arg(interpreter, pythonInstallCommand());
            runtimeStatus_ = QStringLiteral("Kokoro runtime unavailable in Python %1 (%2): %3. %4")
                                 .arg(version.isEmpty() ? QStringLiteral("version unknown") : version,
                                      interpreter, detail.left(700), pipHint);
        }
        emit runtimeStatusChanged(runtimeReady_, runtimeStatus_);
        updateModelStatus();
        setStatusText(statusText());
    });
    connect(runtimeProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        runtimeReady_ = false;
        runtimeStatus_ = QStringLiteral("Python could not be started at %1. Install Python 3.10–3.13 or select its python.exe.")
                             .arg(pythonExecutable_);
        emit runtimeStatusChanged(false, runtimeStatus_);
        updateModelStatus();
        setStatusText(statusText());
    });

    hideProcessWindow(modelValidationProcess_);
    modelValidationProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(modelValidationProcess_, &QProcess::started, this, [this] {
        modelValidationProcess_->write(pendingValidation_);
        modelValidationProcess_->closeWriteChannel();
    });
    connect(modelValidationProcess_, &QProcess::started, this, [this] {
        modelValidationProcess_->write(pendingValidation_);
        modelValidationProcess_->closeWriteChannel();
    });
    connect(modelValidationProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        finishModelValidation(exitCode, exitStatus);
    });
    connect(modelValidationProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        modelValidationError_ = QStringLiteral("Python could not start the Kokoro model validation worker at %1.")
                                    .arg(pythonExecutable_);
        modelSynthesisVerified_ = false;
        updateModelStatus();
    });

    hideProcessWindow(synthesisProcess_);
    synthesisProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(synthesisProcess_, &QProcess::started, this, [this] {
        synthesisProcess_->write(pendingSynthesis_);
        synthesisProcess_->closeWriteChannel();
    });
    connect(synthesisProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) { finishSynthesis(exitCode, exitStatus); });
    connect(synthesisProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setSpeakingState(false);
            if (!temporaryAudioPath_.isEmpty()) {
                QFile::remove(temporaryAudioPath_);
                temporaryAudioPath_.clear();
            }
            const QString message = QStringLiteral("Python could not start the Kokoro speech worker.");
            setStatusText(message);
            emit errorOccurred(message);
        }
    });

    hideProcessWindow(playbackProcess_);
    playbackProcess_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(playbackProcess_, &QProcess::started, this, [this] {
        const QJsonObject request{{QStringLiteral("path"), temporaryAudioPath_}};
        playbackProcess_->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        playbackProcess_->closeWriteChannel();
        stopping_ = false;
        setSpeakingState(true);
        setStatusText(QStringLiteral("Speaking with the Kokoro British English voice."));
    });
    connect(playbackProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString error = QString::fromLocal8Bit(playbackProcess_->readAllStandardError()).trimmed();
        const bool wasStopped = stopping_;
        setSpeakingState(false);
        if (!temporaryAudioPath_.isEmpty()) {
            QFile::remove(temporaryAudioPath_);
            temporaryAudioPath_.clear();
        }
        if (!wasStopped && (exitCode != 0 || exitStatus != QProcess::NormalExit)) {
            const QString message = error.isEmpty()
                                        ? QStringLiteral("Kokoro audio playback failed.")
                                        : error.left(500);
            setStatusText(message);
            emit errorOccurred(message);
        } else if (!wasStopped) {
            setStatusText(QStringLiteral("Kokoro speech finished."));
        }
    });
    connect(playbackProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString message = QStringLiteral("Windows PowerShell could not start audio playback.");
        setSpeakingState(false);
        if (!temporaryAudioPath_.isEmpty()) {
            QFile::remove(temporaryAudioPath_);
            temporaryAudioPath_.clear();
        }
        setStatusText(message);
        emit errorOccurred(message);
    });

    connect(network_, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
        if (reply == downloadReply_)
            finishDownload(reply);
    });

    checkModels();
    runtimeStatus_ = QStringLiteral("Checking for Python and the Kokoro runtime.");
    setStatusText(statusText());
}

KokoroTtsProvider::~KokoroTtsProvider()
{
    cancelDownload();
    stop();
    if (modelValidationProcess_->state() != QProcess::NotRunning) {
        modelValidationProcess_->kill();
        modelValidationProcess_->waitForFinished(500);
    }
    if (!temporaryValidationAudioPath_.isEmpty())
        QFile::remove(temporaryValidationAudioPath_);
}

QString KokoroTtsProvider::id() const { return QStringLiteral("kokoro-onnx"); }
QString KokoroTtsProvider::displayName() const { return QStringLiteral("Kokoro ONNX · offline"); }
QStringList KokoroTtsProvider::voices() const { return modelsInstalled_ ? verifiedKokoroVoices_ : QStringList{}; }
QString KokoroTtsProvider::modelDirectory() const { return modelDirectory_; }
QString KokoroTtsProvider::pythonExecutable() const { return pythonExecutable_; }
QString KokoroTtsProvider::pythonInstallCommand() const
{
    if (runtimeReady_)
        return {};
    QString quotedPython = pythonExecutable_;
    quotedPython.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\" -m pip install --user kokoro-onnx").arg(quotedPython);
}
QString KokoroTtsProvider::runtimeStatusText() const { return runtimeStatus_; }
QString KokoroTtsProvider::modelStatusText() const { return modelStatus_; }
bool KokoroTtsProvider::runtimeReady() const { return runtimeReady_; }
bool KokoroTtsProvider::modelsInstalled() const { return modelsInstalled_; }
bool KokoroTtsProvider::modelFilesVerified() const { return modelFilesVerified_; }
bool KokoroTtsProvider::hasLocalModelFiles() const
{
    return QFileInfo::exists(modelPath()) || QFileInfo::exists(voicesPath());
}
bool KokoroTtsProvider::isDownloading() const { return downloading_; }
bool KokoroTtsProvider::isSpeaking() const { return speaking_; }

bool KokoroTtsProvider::isReady() const
{
    return runtimeReady_ && modelsInstalled_;
}

QString KokoroTtsProvider::statusText() const
{
    if (speaking_ || synthesisProcess_->state() != QProcess::NotRunning
        || playbackProcess_->state() != QProcess::NotRunning) {
        return currentStatusText();
    }
    if (!runtimeReady_)
        return runtimeStatus_;
    if (!modelsInstalled_)
        return modelStatus_;
    return QStringLiteral("Kokoro is ready for offline British English speech.");
}

void KokoroTtsProvider::setPythonExecutable(const QString &path)
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty())
        return;
    pythonExecutable_ = cleaned;
    QSettings().setValue(QStringLiteral("voice/kokoro/python"), pythonExecutable_);
    runtimeReady_ = false;
    modelSynthesisVerified_ = false;
    verifiedKokoroVoices_.clear();
    modelVoiceWarning_.clear();
    modelValidationAttempted_ = false;
    modelValidationError_.clear();
    runtimeStatus_ = QStringLiteral("Checking the selected Python runtime.");
    setStatusText(statusText());
    checkRuntime();
}

void KokoroTtsProvider::refreshStatus()
{
    modelValidationAttempted_ = false;
    modelValidationError_.clear();
    checkModels();
    checkRuntime();
}

void KokoroTtsProvider::checkRuntime()
{
    if (runtimeProcess_->state() != QProcess::NotRunning)
        return;
    runtimeReady_ = false;
    runtimeStatus_ = QStringLiteral("Checking for Python and the Kokoro runtime.");
    setStatusText(statusText());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    runtimeProcess_->setProcessEnvironment(environment);
    static const QString probe = QStringLiteral(R"PY(
import importlib, json, sys

issues = []
version = sys.version_info
if not ((3, 10) <= version[:2] <= (3, 13)):
    issues.append("Python 3.10 through 3.13 is required")
modules = [
    ("kokoro_onnx", "kokoro-onnx"),
    ("numpy", "numpy"),
    ("onnxruntime", "onnxruntime"),
    ("phonemizer", "phonemizer"),
    ("espeakng_loader", "espeakng-loader"),
]
for module, package in modules:
    try:
        importlib.import_module(module)
    except Exception as error:
        issues.append(f"{package}: {type(error).__name__}: {error}")
try:
    import pip
    pip_available = True
except Exception:
    pip_available = False
report = {
    "executable": sys.executable,
    "version": ".".join(map(str, version[:3])),
    "pip": pip_available,
    "issues": issues,
    "ready": not issues,
}
print(json.dumps(report, ensure_ascii=False))
sys.exit(0 if not issues else 2)
)PY");
    runtimeProcess_->start(pythonExecutable_, {QStringLiteral("-c"), probe});
}

void KokoroTtsProvider::checkModels()
{
    const QList<QPair<QString, QString>> paths = {
        {modelPath(), QStringLiteral("kokoro-v1.0.int8.onnx")},
        {voicesPath(), QStringLiteral("voices-v1.0.bin")}
    };
    QStringList missing;
    modelFilesError_.clear();
    modelFilesVerified_ = true;
    for (qsizetype index = 0; index < paths.size(); ++index) {
        const DownloadAsset &asset = downloadAssets_.at(index);
        const QFileInfo info(paths.at(index).first);
        if (!info.exists()) {
            missing.append(paths.at(index).second);
            modelFilesVerified_ = false;
            continue;
        }
        if (!info.isFile() || info.size() != asset.expectedBytes) {
            modelFilesError_ = QStringLiteral("%1 is present but invalid: size %2 bytes, expected %3.")
                                        .arg(paths.at(index).second).arg(info.size()).arg(asset.expectedBytes);
            modelFilesVerified_ = false;
            continue;
        }
        QFile file(paths.at(index).first);
        if (!file.open(QIODevice::ReadOnly)) {
            modelFilesError_ = QStringLiteral("Could not read %1 for integrity verification: %2")
                                        .arg(paths.at(index).second, file.errorString());
            modelFilesVerified_ = false;
            continue;
        }
        qint64 bytesRead = 0;
        QString hashError;
        const QString actualHash = fileDigest(file, &bytesRead, &hashError);
        if (!hashError.isEmpty() || bytesRead != asset.expectedBytes) {
            modelFilesError_ = QStringLiteral("Could not fully verify %1: %2")
                                        .arg(paths.at(index).second,
                                             hashError.isEmpty() ? QStringLiteral("read size changed during verification") : hashError);
            modelFilesVerified_ = false;
        } else if (actualHash.compare(QString::fromLatin1(asset.sha256.toHex()), Qt::CaseInsensitive) != 0) {
            modelFilesError_ = QStringLiteral("Checksum verification failed for installed %1. Expected SHA-256 %2; found %3.")
                                        .arg(paths.at(index).second,
                                             QString::fromLatin1(asset.sha256.toHex()), actualHash);
            modelFilesVerified_ = false;
        }
    }
    if (!modelFilesVerified_ && !missing.isEmpty() && modelFilesError_.isEmpty())
        modelFilesError_ = QStringLiteral("Not downloaded: %1. Total download: 142,333,725 bytes.")
                                    .arg(missing.join(QStringLiteral(" and ")));
    if (!modelFilesVerified_) {
        modelSynthesisVerified_ = false;
        verifiedKokoroVoices_.clear();
    }
    updateModelStatus();
}

void KokoroTtsProvider::updateModelStatus()
{
    if (downloading_) {
        const int assetIndex = qMin(downloadIndex_, static_cast<int>(downloadAssets_.size()) - 1);
        const DownloadAsset &asset = downloadAssets_.at(assetIndex);
        modelStatus_ = QStringLiteral("Downloading %1: %2 of %3 bytes received.")
                           .arg(asset.fileName).arg(downloadBytesWritten_).arg(asset.expectedBytes);
    } else if (!modelFilesVerified_) {
        modelSynthesisVerified_ = false;
        verifiedKokoroVoices_.clear();
        modelStatus_ = modelFilesError_.isEmpty()
                           ? QStringLiteral("Kokoro model files are missing or invalid.")
                           : modelFilesError_;
        if (!lastDownloadIssue_.isEmpty())
            modelStatus_ += QStringLiteral(" Last download: ") + lastDownloadIssue_;
    } else if (!runtimeReady_) {
        modelStatus_ = QStringLiteral("Model files are present and SHA-256 verified; the Python runtime is unavailable.");
    } else if (modelSynthesisVerified_) {
        modelStatus_ = QStringLiteral("Installed and SHA-256 verified. Kokoro synthesized a British English sample with: %1. Folder: %2.")
                           .arg(verifiedKokoroVoices_.join(QStringLiteral(", ")), modelDirectory_);
        if (!modelVoiceWarning_.isEmpty())
            modelStatus_ += QStringLiteral(" Some voices failed their synthesis check: %1").arg(modelVoiceWarning_);
    } else if (!modelValidationError_.isEmpty() && modelValidationAttempted_) {
        modelStatus_ = QStringLiteral("Model files passed SHA-256 checks but the selected TTS engine could not use them: %1")
                           .arg(modelValidationError_);
    } else {
        modelStatus_ = QStringLiteral("Model files passed SHA-256 checks. Testing Kokoro model loading and speech synthesis…");
        validateModelIfReady();
    }
    modelsInstalled_ = runtimeReady_ && modelFilesVerified_ && modelSynthesisVerified_;
    emit modelStatusChanged(modelsInstalled_, modelStatus_);
    setStatusText(statusText());
}

void KokoroTtsProvider::validateModelIfReady()
{
    if (!runtimeReady_ || !modelFilesVerified_ || modelSynthesisVerified_ || modelValidationAttempted_
        || modelValidationProcess_->state() != QProcess::NotRunning || downloading_)
        return;
    modelValidationAttempted_ = true;
    temporaryValidationAudioPath_ = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                        .filePath(QStringLiteral("jarvis-kokoro-check-%1.wav")
                                                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QJsonObject request{{QStringLiteral("text"), QStringLiteral("The quick brown fox jumps over the lazy dog.")},
                              {QStringLiteral("voice"), QStringLiteral("bm_george")},
                              {QStringLiteral("test_voices"), QJsonArray::fromStringList(kokoroVoiceIds)},
                              {QStringLiteral("speed"), 1.0},
                              {QStringLiteral("volume"), 70},
                              {QStringLiteral("model"), modelPath()},
                              {QStringLiteral("voices"), voicesPath()},
                              {QStringLiteral("output"), temporaryValidationAudioPath_}};
    pendingValidation_ = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    modelValidationProcess_->setProcessEnvironment(environment);
    modelValidationProcess_->start(pythonExecutable_, {workerPath()});
}

void KokoroTtsProvider::finishModelValidation(int exitCode, int exitStatus)
{
    const QByteArray output = modelValidationProcess_->readAllStandardOutput().trimmed();
    const QString errorOutput = QString::fromLocal8Bit(modelValidationProcess_->readAllStandardError()).trimmed();
    const QJsonDocument result = QJsonDocument::fromJson(output);
    const QFileInfo audio(temporaryValidationAudioPath_);
    const bool generated = exitCode == 0 && exitStatus == QProcess::NormalExit && result.isObject()
                           && result.object().value(QStringLiteral("ok")).toBool()
                           && QDir::cleanPath(result.object().value(QStringLiteral("output")).toString())
                                  == QDir::cleanPath(temporaryValidationAudioPath_)
                           && audio.exists() && audio.isFile() && audio.size() > 44;
    verifiedKokoroVoices_.clear();
    for (const QJsonValue &value : result.object().value(QStringLiteral("available_voices")).toArray()) {
        const QString voice = value.toString();
        if (generated && kokoroVoiceIds.contains(voice))
            verifiedKokoroVoices_.append(voice);
    }
    QStringList failedVoices;
    for (const QJsonValue &value : result.object().value(QStringLiteral("voice_errors")).toArray()) {
        if (value.isString())
            failedVoices.append(value.toString());
    }
    QFile::remove(temporaryValidationAudioPath_);
    temporaryValidationAudioPath_.clear();
    modelSynthesisVerified_ = generated && !verifiedKokoroVoices_.isEmpty();
    modelVoiceWarning_ = failedVoices.join(QStringLiteral("; "));
    modelValidationError_ = modelSynthesisVerified_ ? QString()
                                      : (errorOutput.isEmpty()
                                             ? QStringLiteral("Kokoro failed to load the model or generate a valid WAV sample (exit code %1).")
                                                   .arg(exitCode)
                                             : errorOutput.left(700));
    updateModelStatus();
}

void KokoroTtsProvider::downloadModels()
{
    if (downloading_) {
        emit downloadFailed(QStringLiteral("A Kokoro model download is already running."));
        return;
    }
    if (isSpeaking())
        stop();
    if (!QDir().mkpath(modelDirectory_)) {
        emit downloadFailed(QStringLiteral("Could not create the voice model folder: %1").arg(modelDirectory_));
        return;
    }
    downloading_ = true;
    lastDownloadIssue_.clear();
    modelValidationError_.clear();
    modelSynthesisVerified_ = false;
    verifiedKokoroVoices_.clear();
    modelVoiceWarning_.clear();
    modelValidationAttempted_ = false;
    downloadIndex_ = 0;
    downloadRetries_ = 0;
    updateModelStatus();
    startNextDownload();
}

void KokoroTtsProvider::startNextDownload()
{
    if (!downloading_)
        return;
    if (downloadIndex_ >= downloadAssets_.size()) {
        downloading_ = false;
        checkModels();
        emit downloadFinished();
        return;
    }

    const DownloadAsset &asset = downloadAssets_.at(downloadIndex_);
    downloadFile_ = new QSaveFile(QDir(modelDirectory_).filePath(asset.fileName));
    if (!downloadFile_->open(QIODevice::WriteOnly)) {
        const QString message = QStringLiteral("Could not write %1: %2").arg(asset.fileName, downloadFile_->errorString());
        downloadFile_->cancelWriting();
        delete downloadFile_;
        downloadFile_ = nullptr;
        downloading_ = false;
        emit downloadFailed(message);
        return;
    }

    downloadBytesWritten_ = 0;
    downloadPrefix_.clear();
    downloadWriteError_.clear();
    delete downloadHash_;
    downloadHash_ = new QCryptographicHash(QCryptographicHash::Sha256);
    QNetworkRequest request(asset.url);
    request.setRawHeader("User-Agent", "JARVIS-Assistant/0.2");
    request.setRawHeader("Accept", "application/octet-stream");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setTransferTimeout(180000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    downloadReply_ = network_->get(request);
    connect(downloadReply_, &QIODevice::readyRead, this, [this, reply = downloadReply_] {
        if (reply != downloadReply_ || !downloadFile_)
            return;
        const QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;
        if (downloadPrefix_.size() < 512)
            downloadPrefix_.append(data.constData(), qMin<qsizetype>(512 - downloadPrefix_.size(), data.size()));
        const DownloadAsset &asset = downloadAssets_.at(downloadIndex_);
        if (downloadBytesWritten_ + data.size() > asset.expectedBytes) {
            downloadWriteError_ = QStringLiteral("The server response exceeded the expected file size.");
            reply->abort();
            return;
        }
        if (downloadFile_->write(data) != data.size()) {
            downloadWriteError_ = downloadFile_->errorString();
            reply->abort();
            return;
        }
        downloadHash_->addData(data);
        downloadBytesWritten_ += data.size();
    });
    connect(downloadReply_, &QNetworkReply::downloadProgress, this,
            [this, fileName = asset.fileName, expected = asset.expectedBytes](qint64 received, qint64 total) {
        const qint64 reportedTotal = total > 0 ? total : expected;
        emit downloadProgress(fileName, qMin(received, reportedTotal), reportedTotal);
    });
    emit downloadProgress(asset.fileName, 0, asset.expectedBytes);
    if (downloadRetries_ > 0) {
        setStatusText(QStringLiteral("Retry %1 of %2 for %3 after an incomplete or invalid response.")
                          .arg(downloadRetries_).arg(kMaxAutomaticDownloadRetries).arg(asset.fileName));
    } else {
        setStatusText(QStringLiteral("Downloading the official Kokoro model: %1.").arg(asset.fileName));
    }
}

void KokoroTtsProvider::finishDownload(QNetworkReply *reply)
{
    if (reply != downloadReply_) {
        reply->deleteLater();
        return;
    }
    const QByteArray remainder = reply->readAll();
    if (!remainder.isEmpty() && downloadFile_) {
        if (downloadPrefix_.size() < 512)
            downloadPrefix_.append(remainder.constData(), qMin<qsizetype>(512 - downloadPrefix_.size(), remainder.size()));
        if (downloadFile_->write(remainder) == remainder.size()) {
            downloadHash_->addData(remainder);
            downloadBytesWritten_ += remainder.size();
        } else {
            downloadWriteError_ = downloadFile_->errorString();
        }
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    const bool cancelled = !downloading_;
    QString failure;
    const DownloadAsset &asset = downloadAssets_.at(downloadIndex_);
    const QString actualHash = downloadHash_ ? QString::fromLatin1(downloadHash_->result().toHex())
                                             : QStringLiteral("unavailable");
    const QString expectedHash = QString::fromLatin1(asset.sha256.toHex());
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const qint64 contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    const bool contentLengthMismatch = contentLength > 0 && contentLength != asset.expectedBytes;
    const bool receivedSizeMismatch = downloadBytesWritten_ != asset.expectedBytes;
    const QString responseUrl = reply->url().toString();
    const QString prefix = QString::fromLatin1(downloadPrefix_).trimmed().toLower();
    const bool htmlBody = contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)
                          || prefix.startsWith(QStringLiteral("<!doctype html"))
                          || prefix.startsWith(QStringLiteral("<html"));
    if (cancelled) {
        failure = QStringLiteral("Download interrupted for %1 after %2 of %3 bytes. The temporary file was discarded; press Download / Retry to try again.")
                      .arg(asset.fileName).arg(downloadBytesWritten_).arg(asset.expectedBytes);
    } else if (!networkError.isEmpty() || httpStatus < 200 || httpStatus >= 300) {
        failure = QStringLiteral("Download failed for %1: HTTP %2; received %3 of %4 bytes (Content-Length %5) from %6. Network error: %7")
                      .arg(asset.fileName).arg(httpStatus).arg(downloadBytesWritten_).arg(asset.expectedBytes)
                      .arg(contentLength).arg(responseUrl, networkError.isEmpty() ? QStringLiteral("none") : networkError);
    } else if (!downloadWriteError_.isEmpty()) {
        failure = QStringLiteral("Could not save the response for %1: %2. HTTP %3; received %4 of %5 bytes (Content-Length %6) from %7.")
                      .arg(asset.fileName, downloadWriteError_).arg(httpStatus).arg(downloadBytesWritten_)
                      .arg(asset.expectedBytes).arg(contentLength).arg(responseUrl);
    } else if (htmlBody) {
        failure = QStringLiteral("The server returned an HTML error page for %1 instead of the model file. HTTP %2; Content-Type %3; received %4 bytes (Content-Length %5) from %6.")
                      .arg(asset.fileName).arg(httpStatus).arg(contentType).arg(downloadBytesWritten_)
                      .arg(contentLength).arg(responseUrl);
    } else {
        if (contentLengthMismatch || receivedSizeMismatch) {
            failure = QStringLiteral("Download size validation failed for %1: HTTP %2; received %3 bytes and Content-Length %4; expected exactly %5 bytes. Content-Type %6; final URL %7. The file was not installed.")
                          .arg(asset.fileName).arg(httpStatus).arg(downloadBytesWritten_).arg(contentLength)
                          .arg(asset.expectedBytes)
                          .arg(contentType.isEmpty() ? QStringLiteral("unknown") : contentType, responseUrl);
        } else if (!downloadHash_ || actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0) {
            failure = QStringLiteral("SHA-256 verification failed for %1: HTTP %2; received %3 of %4 expected bytes; actual hash %5. The file was not installed.")
                          .arg(asset.fileName).arg(httpStatus).arg(downloadBytesWritten_).arg(asset.expectedBytes)
                          .arg(actualHash);
        } else if (!downloadFile_ || !downloadFile_->commit()) {
            failure = QStringLiteral("Could not save %1: %2")
                          .arg(asset.fileName, downloadFile_ ? downloadFile_->errorString() : QString());
        }
    }

    if (!failure.isEmpty()) {
        failure += QStringLiteral(" Expected SHA-256 %1 from the GitHub Releases API for tag model-files-v1.1; actual SHA-256 %2; Content-Type %3; final URL %4.")
                       .arg(expectedHash, actualHash, contentType.isEmpty() ? QStringLiteral("unknown") : contentType,
                            responseUrl);
    }

    if (!failure.isEmpty() && downloadFile_)
        downloadFile_->cancelWriting();
    const bool successfulHttpStatus = httpStatus >= 200 && httpStatus < 300;
    const bool retryableStatus = httpStatus == 0 || httpStatus == 408 || httpStatus == 429 || httpStatus >= 500;
    const bool retryableSuccessfulResponse = successfulHttpStatus && !htmlBody
        && downloadWriteError_.isEmpty()
        && (!networkError.isEmpty() || contentLengthMismatch || receivedSizeMismatch
            || (downloadHash_ && actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0));
    const bool shouldRetry = !cancelled && (retryableStatus || retryableSuccessfulResponse);
    delete downloadFile_;
    downloadFile_ = nullptr;
    delete downloadHash_;
    downloadHash_ = nullptr;
    downloadReply_ = nullptr;
    reply->deleteLater();

    if (!failure.isEmpty()) {
        lastDownloadIssue_ = failure;
        if (shouldRetry && downloadRetries_ < kMaxAutomaticDownloadRetries) {
            ++downloadRetries_;
            QTimer::singleShot(1000, this, [this] {
                if (downloading_)
                    startNextDownload();
            });
            return;
        }
        downloading_ = false;
        modelValidationError_.clear();
        checkModels();
        setStatusText(failure);
        emit downloadFailed(failure);
        return;
    }

    ++downloadIndex_;
    downloadRetries_ = 0;
    startNextDownload();
}

void KokoroTtsProvider::cancelDownload()
{
    if (!downloading_)
        return;
    downloading_ = false;
    lastDownloadIssue_ = QStringLiteral("Download interrupted. The incomplete temporary file is being discarded.");
    if (downloadReply_)
        downloadReply_->abort();
    else {
        if (downloadFile_)
            downloadFile_->cancelWriting();
        delete downloadFile_;
        downloadFile_ = nullptr;
    }
}

bool KokoroTtsProvider::removeModels(QString *error)
{
    if (downloading_) {
        if (error)
            *error = QStringLiteral("Cancel the current download before removing voice models.");
        return false;
    }
    if (isSpeaking())
        stop();
    if (modelValidationProcess_->state() != QProcess::NotRunning) {
        modelValidationProcess_->kill();
        modelValidationProcess_->waitForFinished(500);
    }
    if (!temporaryValidationAudioPath_.isEmpty()) {
        QFile::remove(temporaryValidationAudioPath_);
        temporaryValidationAudioPath_.clear();
    }
    const bool modelRemoved = !QFileInfo::exists(modelPath()) || QFile::remove(modelPath());
    const bool voicesRemoved = !QFileInfo::exists(voicesPath()) || QFile::remove(voicesPath());
    if (!modelRemoved || !voicesRemoved) {
        if (error)
            *error = QStringLiteral("Could not remove one or more files from %1.").arg(modelDirectory_);
        checkModels();
        return false;
    }
    checkModels();
    return true;
}

void KokoroTtsProvider::speak(const QString &text, const QString &voice, int rate, int volume)
{
    const QString cleanedText = cleanForSpeech(text);
    if (cleanedText.isEmpty())
        return;
    if (!isReady()) {
        const QString message = statusText();
        emit errorOccurred(message);
        return;
    }
    stop();
    stopping_ = false;
    const QString selectedVoice = voices().contains(voice) ? voice : QStringLiteral("bm_george");
    temporaryAudioPath_ = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                              .filePath(QStringLiteral("jarvis-kokoro-%1.wav")
                                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QJsonObject request{{QStringLiteral("text"), cleanedText},
                              {QStringLiteral("voice"), selectedVoice},
                              {QStringLiteral("speed"), qBound(0.7, 1.0 + rate * 0.025, 1.3)},
                              {QStringLiteral("volume"), qBound(0, volume, 100)},
                              {QStringLiteral("model"), modelPath()},
                              {QStringLiteral("voices"), voicesPath()},
                              {QStringLiteral("output"), temporaryAudioPath_}};
    pendingSynthesis_ = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    synthesisProcess_->setProcessEnvironment(environment);
    synthesisProcess_->start(pythonExecutable_, {workerPath()});
    setSpeakingState(true);
    setStatusText(QStringLiteral("Generating offline speech with Kokoro.") );
}

void KokoroTtsProvider::stop()
{
    stopping_ = true;
    if (synthesisProcess_->state() != QProcess::NotRunning) {
        synthesisProcess_->kill();
        synthesisProcess_->waitForFinished(200);
    }
    if (playbackProcess_->state() != QProcess::NotRunning) {
        playbackProcess_->kill();
        playbackProcess_->waitForFinished(200);
    }
    if (!temporaryAudioPath_.isEmpty()) {
        QFile::remove(temporaryAudioPath_);
        temporaryAudioPath_.clear();
    }
    setSpeakingState(false);
    setStatusText(QStringLiteral("Kokoro speech stopped."));
}

void KokoroTtsProvider::finishSynthesis(int exitCode, int exitStatus)
{
    if (stopping_) {
        if (!temporaryAudioPath_.isEmpty()) {
            QFile::remove(temporaryAudioPath_);
            temporaryAudioPath_.clear();
        }
        return;
    }
    const QByteArray output = synthesisProcess_->readAllStandardOutput().trimmed();
    const QString errorOutput = QString::fromLocal8Bit(synthesisProcess_->readAllStandardError()).trimmed();
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        const QString message = errorOutput.isEmpty()
                                    ? QStringLiteral("Kokoro speech generation failed. Confirm kokoro-onnx and numpy are installed.")
                                    : errorOutput.left(500);
        QFile::remove(temporaryAudioPath_);
        temporaryAudioPath_.clear();
        setSpeakingState(false);
        setStatusText(message);
        emit errorOccurred(message);
        return;
    }
    const QJsonDocument result = QJsonDocument::fromJson(output);
    const QString reportedPath = result.object().value(QStringLiteral("output")).toString();
    const QFileInfo audio(reportedPath);
    if (!result.isObject() || !result.object().value(QStringLiteral("ok")).toBool()
        || QDir::cleanPath(audio.absoluteFilePath()) != QDir::cleanPath(temporaryAudioPath_)
        || !audio.exists() || !audio.isFile() || audio.size() <= 44) {
        const QString message = errorOutput.isEmpty()
                                    ? QStringLiteral("Kokoro returned an invalid or empty audio file.")
                                    : errorOutput.left(500);
        QFile::remove(temporaryAudioPath_);
        temporaryAudioPath_.clear();
        setSpeakingState(false);
        setStatusText(message);
        emit errorOccurred(message);
        return;
    }
    startPlayback(temporaryAudioPath_);
}

void KokoroTtsProvider::startPlayback(const QString &audioPath)
{
    static const QString script = QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
    $request = [Console]::In.ReadLine() | ConvertFrom-Json
    $player = New-Object System.Media.SoundPlayer([string]$request.path)
    $player.Load()
    $player.PlaySync()
    $player.Dispose()
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
}
)PS");
    Q_UNUSED(audioPath)
    playbackProcess_->start(kPowerShell, powerShellArguments(script));
}

void KokoroTtsProvider::setSpeakingState(bool speaking)
{
    if (speaking_ == speaking)
        return;
    speaking_ = speaking;
    emit speakingChanged(speaking_);
}

QString KokoroTtsProvider::workerPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kokoro_worker.py"));
}

QString KokoroTtsProvider::modelPath() const
{
    return QDir(modelDirectory_).filePath(QStringLiteral("kokoro-v1.0.int8.onnx"));
}

QString KokoroTtsProvider::voicesPath() const
{
    return QDir(modelDirectory_).filePath(QStringLiteral("voices-v1.0.bin"));
}
