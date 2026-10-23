#include "MainWindow.h"

#include "ReactorWidget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QComboBox>
#include <QFrame>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QUrl>
#include <QUuid>

namespace {

const QString kStyle = QStringLiteral(R"(
QMainWindow, QWidget#root, QScrollArea, QScrollArea > QWidget > QWidget {
    background: #07111b;
    color: #dceaf0;
}
QWidget#sidebar {
    background: #08131e;
    border-right: 1px solid #172a36;
}
QLabel { color: #dceaf0; }
QLabel#muted { color: #8199a7; }
QLabel#eyebrow { color: #67cde8; font-size: 9px; font-weight: 700; letter-spacing: 2px; }
QLabel#brand { color: #e5f9ff; font-size: 17px; font-weight: 700; letter-spacing: 5px; }
QLabel#pageTitle { color: #ecf8fc; font-size: 14px; font-weight: 600; }
QLabel#heroTitle { color: #effaff; font-size: 25px; font-weight: 500; }
QLabel#sectionTitle { color: #e3f4f8; font-size: 11px; font-weight: 700; letter-spacing: 1.5px; }
QLabel#metricValue { color: #e4faff; font-size: 24px; font-weight: 600; }
QLabel#metricDetail { color: #7e98a6; font-size: 10px; }
QFrame#panel {
    background: #0b1925;
    border: 1px solid #1a3342;
    border-radius: 3px;
}
QFrame#metric {
    background: #0d1d29;
    border: 1px solid #1a3342;
    border-radius: 2px;
}
QPushButton#navButton {
    text-align: left;
    color: #91a8b4;
    background: transparent;
    border: 1px solid transparent;
    padding: 10px 12px;
    font-size: 11px;
}
QPushButton#navButton:hover { color: #e8faff; background: #102331; }
QPushButton#navButton:checked {
    color: #b7f1ff;
    background: #102735;
    border: 1px solid #1e566a;
}
QPushButton#primaryButton {
    color: #06121a;
    background: #64cde7;
    border: 1px solid #8ce5f7;
    padding: 9px 16px;
    font-weight: 700;
}
QPushButton#primaryButton:hover { background: #8ce5f7; }
QPushButton#secondaryButton {
    color: #b8d4de;
    background: #102431;
    border: 1px solid #285063;
    padding: 8px 13px;
}
QPushButton#secondaryButton:hover { color: #e9fbff; border-color: #55bad6; }
QPushButton:disabled { color: #526a76; background: #0b1822; border-color: #172a35; }
QLineEdit, QPlainTextEdit {
    color: #d8eaf0;
    background: #07131d;
    border: 1px solid #1d3948;
    padding: 10px;
    selection-background-color: #12617b;
}
QLineEdit:focus, QPlainTextEdit:focus { border-color: #46aeca; }
QComboBox {
    color: #d8eaf0;
    background: #07131d;
    border: 1px solid #1d3948;
    padding: 8px;
}
QComboBox QAbstractItemView {
    color: #d8eaf0;
    background: #0b1925;
    selection-background-color: #12617b;
}
QCheckBox { color: #b8d4de; spacing: 8px; }
QSlider::groove:horizontal { height: 4px; background: #173441; }
QSlider::handle:horizontal { width: 12px; margin: -5px 0; background: #64cde7; border-radius: 6px; }
QTextBrowser#transcript {
    color: #c7dce4;
    background: #08141e;
    border: 1px solid #142a37;
    padding: 10px;
}
QProgressBar {
    background: #10232e;
    border: 0;
    max-height: 5px;
    text-align: center;
}
QProgressBar::chunk { background: #48b5d4; }
QStatusBar { color: #7e98a6; background: #08131e; border-top: 1px solid #172a36; }
QScrollBar:vertical { background: #08131e; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #254352; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");

QLabel *makeLabel(const QString &text, const QString &objectName = QString())
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty())
        label->setObjectName(objectName);
    return label;
}

QPushButton *makeButton(const QString &text, const QString &objectName)
{
    auto *button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("size unavailable");
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    if (bytes >= static_cast<qint64>(gib))
        return QStringLiteral("%1 GiB").arg(bytes / gib, 0, 'f', 1);
    if (bytes >= static_cast<qint64>(mib))
        return QStringLiteral("%1 MiB").arg(bytes / mib, 0, 'f', 0);
    return QStringLiteral("%1 KiB").arg(bytes / kib, 0, 'f', 0);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ollamaProvider_ = new OllamaProvider(this);
    openAIProvider_ = new OpenAICompatibleProvider(this);
    voiceController_ = new VoiceController(this);
    toolRegistry_ = new ToolRegistry(this);
    loadConversationHistory();

    setWindowTitle(QStringLiteral("J.A.R.V.I.S. | Personal Assistant"));
    setMinimumSize(960, 660);
    resize(1360, 860);
    setStyleSheet(kStyle);

    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(218);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(18, 24, 18, 18);
    sideLayout->setSpacing(8);

    sideLayout->addWidget(makeLabel(QStringLiteral("J.A.R.V.I.S."), QStringLiteral("brand")));
    auto *sideSubtitle = makeLabel(QStringLiteral("PERSONAL SYSTEM  /  01"), QStringLiteral("eyebrow"));
    sideSubtitle->setContentsMargins(1, 4, 0, 20);
    sideLayout->addWidget(sideSubtitle);

    navigation_ = new QButtonGroup(this);
    navigation_->setExclusive(true);
    const QStringList navNames = {
        QStringLiteral("Home"), QStringLiteral("Conversation"), QStringLiteral("Monitoring"),
        QStringLiteral("Tools & automation"), QStringLiteral("Voice"), QStringLiteral("Voice models"),
        QStringLiteral("AI models"), QStringLiteral("Personality & memory"), QStringLiteral("Diagnostics")
    };
    const QStringList navCodes = {
        QStringLiteral("01"), QStringLiteral("02"), QStringLiteral("03"), QStringLiteral("04"),
        QStringLiteral("05"), QStringLiteral("06"), QStringLiteral("07"), QStringLiteral("08"),
        QStringLiteral("09")
    };

    auto *navCaption = makeLabel(QStringLiteral("WORKSPACE"), QStringLiteral("eyebrow"));
    navCaption->setContentsMargins(2, 12, 0, 4);
    sideLayout->addWidget(navCaption);

    auto *rightColumn = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto *topbar = new QWidget;
    topbar->setFixedHeight(62);
    auto *topLayout = new QHBoxLayout(topbar);
    topLayout->setContentsMargins(26, 0, 26, 0);
    topLayout->setSpacing(16);
    pageTitle_ = makeLabel(QStringLiteral("Home"), QStringLiteral("pageTitle"));
    clockLabel_ = makeLabel(QString(), QStringLiteral("muted"));
    aiStatusLabel_ = makeLabel(QStringLiteral("AI  ·  CHECKING"), QStringLiteral("eyebrow"));
    topLayout->addWidget(pageTitle_);
    topLayout->addStretch(1);
    topLayout->addWidget(aiStatusLabel_);
    topLayout->addSpacing(10);
    topLayout->addWidget(clockLabel_);

    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("pageStack"));
    pages_->addWidget(wrapPage(buildHomePage()));
    pages_->addWidget(wrapPage(buildConversationPage()));
    pages_->addWidget(wrapPage(buildMonitorPage()));
    pages_->addWidget(wrapPage(buildToolsPage()));
    pages_->addWidget(wrapPage(buildVoicePage()));
    pages_->addWidget(wrapPage(buildVoiceModelsPage()));
    pages_->addWidget(wrapPage(buildAiPage()));
    pages_->addWidget(wrapPage(buildPersonalityPage()));
    pages_->addWidget(wrapPage(buildDiagnosticsPage()));

    for (int i = 0; i < navNames.size(); ++i) {
        auto *button = makeButton(navCodes.at(i) + QStringLiteral("     ") + navNames.at(i),
                                  QStringLiteral("navButton"));
        button->setCheckable(true);
        navigation_->addButton(button, i);
        sideLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i, navNames] {
            showPage(i, navNames.at(i));
        });
    }
    navigation_->button(0)->setChecked(true);
    sideLayout->addStretch(1);

    QVBoxLayout *connectionLayout = nullptr;
    auto *connectionPanel = makePanel(QStringLiteral("LOCAL CONNECTION"), &connectionLayout);
    sidebarAiStatus_ = makeLabel(QStringLiteral("Ollama  ·  checking"), QStringLiteral("metricDetail"));
    sidebarAiStatus_->setWordWrap(true);
    connectionLayout->addWidget(sidebarAiStatus_);
    sideLayout->addWidget(connectionPanel);
    sideLayout->addWidget(makeLabel(QStringLiteral("CORE SYSTEMS  /  AI · VOICE · TOOLS"), QStringLiteral("muted")));

    rightLayout->addWidget(topbar);
    rightLayout->addWidget(pages_, 1);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(rightColumn, 1);
    setCentralWidget(root);
    statusBar()->showMessage(QStringLiteral("Dashboard ready. Ollama and Windows speech will be checked shortly."));

    clockTimer_ = new QTimer(this);
    clockTimer_->setInterval(1000);
    connect(clockTimer_, &QTimer::timeout, this, &MainWindow::updateClock);
    clockTimer_->start();
    updateClock();

    monitorTimer_ = new QTimer(this);
    monitorTimer_->setInterval(1500);
    connect(monitorTimer_, &QTimer::timeout, &monitor_, &SystemMonitor::refresh);
    connect(&monitor_, &SystemMonitor::updated, this, &MainWindow::updateMonitorUi);
    monitor_.refresh();
    monitorTimer_->start();

    const auto connectProviderSignals = [this](AIProvider *provider) {
        connect(provider, &AIProvider::modelsLoaded, this, [this, provider](const QStringList &models) {
            if (provider != activeProvider())
                return;
            modelFetchRunning_ = false;
            providerConnected_ = true;
            providerError_.clear();
            providerModels_ = models;
            const QString currentModel = modelCombo_ ? modelCombo_->currentText() : QString();
            if (modelCombo_) {
                const QSignalBlocker modelSignals(modelCombo_);
                modelCombo_->clear();
                modelCombo_->addItems(models);
                if (models.contains(currentModel))
                    modelCombo_->setCurrentText(currentModel);
                else if (!models.isEmpty())
                    modelCombo_->setCurrentIndex(0);
                else
                    modelCombo_->clearEditText();
            }
            saveProviderSettings();
            updateAiStatus();
            updateOllamaModelManager();
            statusBar()->showMessage(models.isEmpty()
                                         ? QStringLiteral("%1 is responding; no models are installed yet.").arg(provider->displayName())
                                         : QStringLiteral("%1 is responding with %2 available model(s).")
                                               .arg(provider->displayName()).arg(models.size()), 7000);
            logEvent(models.isEmpty()
                         ? QStringLiteral("%1 responded with no available models.").arg(provider->displayName())
                         : QStringLiteral("%1 listed %2 model(s).").arg(provider->displayName()).arg(models.size()));
        });
        connect(provider, &AIProvider::tokenReceived, this, [this, provider](const QString &token) {
            if (provider != activeProvider() || !chatRequestActive_)
                return;
            streamingAssistantText_ += token;
            replaceStreamingMessage(streamingAssistantText_);
        });
        connect(provider, &AIProvider::chatFinished, this, [this, provider] {
            if (provider == activeProvider() && chatRequestActive_)
                finishChatRequest();
        });
        connect(provider, &AIProvider::requestFailed, this, [this, provider](const QString &message) {
            if (provider == activeProvider())
                handleProviderError(message);
        });
        connect(provider, &AIProvider::requestCancelled, this, [this, provider] {
            if (provider != activeProvider())
                return;
            if (chatRequestActive_) {
                chatRequestActive_ = false;
                if (streamingAssistantText_.isEmpty())
                    streamingAssistantText_ = QStringLiteral("Response cancelled.");
                else
                    streamingAssistantText_ += QStringLiteral("\n\n[Response cancelled]");
                appendMessage(QStringLiteral("JARVIS"), streamingAssistantText_, false);
                for (const QJsonValue &message : activeTurnMessages_)
                    conversationMessages_.append(message);
                conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                                         {QStringLiteral("content"), streamingAssistantText_}});
                saveConversationHistory();
                activeTurnMessages_ = QJsonArray();
                for (QLabel *label : streamingLabels_)
                    label->hide();
                if (cancelChatButton_)
                    cancelChatButton_->setEnabled(false);
                statusBar()->showMessage(QStringLiteral("AI response cancelled."), 5000);
                logEvent(QStringLiteral("AI response cancelled by the user."));
                resumeHandsFreeListening();
            } else if (modelFetchRunning_) {
                modelFetchRunning_ = false;
                providerError_ = QStringLiteral("Provider request cancelled.");
                updateAiStatus();
            }
        });
    };
    connectProviderSignals(ollamaProvider_);
    connectProviderSignals(openAIProvider_);

    connect(ollamaProvider_, &OllamaProvider::modelsMetadataLoaded, this, [this](const QJsonArray &models) {
        ollamaModelMetadata_ = models;
        updateOllamaModelManager();
    });
    connect(ollamaProvider_, &OllamaProvider::modelActionProgress, this,
            [this](const QString &status, qint64 completed, qint64 total) {
        if (ollamaActionStatusLabel_)
            ollamaActionStatusLabel_->setText(total > 0
                                                  ? QStringLiteral("%1 · %2 / %3")
                                                        .arg(status, humanSize(completed), humanSize(total))
                                                  : status);
        if (ollamaModelProgress_) {
            if (total > 0) {
                ollamaModelProgress_->setRange(0, 1000);
                ollamaModelProgress_->setValue(static_cast<int>(qMin<qint64>(1000, completed * 1000 / total)));
            } else {
                ollamaModelProgress_->setRange(0, 0);
            }
        }
        updateOllamaModelManager();
    });
    connect(ollamaProvider_, &OllamaProvider::modelActionFinished, this,
            [this](const QString &action, const QString &modelName) {
        if (ollamaActionStatusLabel_)
            ollamaActionStatusLabel_->setText(action == QStringLiteral("pull")
                                                  ? QStringLiteral("Pull completed for %1. Refreshing installed models…").arg(modelName)
                                                  : QStringLiteral("Removed %1 from the local Ollama library.").arg(modelName));
        if (ollamaModelProgress_) {
            ollamaModelProgress_->setRange(0, 1000);
            ollamaModelProgress_->setValue(1000);
        }
        updateOllamaModelManager();
        logEvent(QStringLiteral("Local Ollama model action completed: %1 %2.").arg(action, modelName));
        loadProviderModels();
    });
    connect(ollamaProvider_, &OllamaProvider::modelActionFailed, this, [this](const QString &message) {
        if (ollamaActionStatusLabel_)
            ollamaActionStatusLabel_->setText(message);
        if (ollamaModelProgress_ && !ollamaProvider_->modelActionBusy()) {
            ollamaModelProgress_->setRange(0, 1000);
            ollamaModelProgress_->setValue(0);
        }
        updateOllamaModelManager();
        statusBar()->showMessage(message, 9000);
        logEvent(QStringLiteral("Local Ollama model action failed: %1").arg(message));
    });

    connect(voiceController_, &VoiceController::enginesDiscovered, this,
            [this](const QStringList &voices, const QStringList &recognizers) {
        Q_UNUSED(voices)
        refreshVoiceChoices();
        refreshAudioInputs();
        if (stopSpeechButton_)
            stopSpeechButton_->setEnabled(false);
        if (recognizerStatusLabel_) {
            recognizerStatusLabel_->setText(recognizers.isEmpty()
                                                ? QStringLiteral("No Windows speech recognizer is installed. Add an English Speech language in Windows Settings > Time & language > Language & region.")
                                                : QStringLiteral("Installed recognizer(s): %1. JARVIS selects English, captures 16 kHz mono audio from the microphone below, then recognizes the complete recording.")
                                                      .arg(recognizers.join(QStringLiteral(", "))));
        }
        const bool canListen = !recognizers.isEmpty() && !voiceController_->audioInputNames().isEmpty();
        if (voiceInputButton_)
            voiceInputButton_->setEnabled(canListen);
        for (QPushButton *button : voiceInputButtons_)
            button->setEnabled(canListen);
        updateVoiceStatus();
    });
    connect(voiceController_, &VoiceController::ttsStatusChanged, this, [this](const QString &) {
        updateVoiceStatus();
        updateVoiceModelStatus();
    });
    connect(voiceController_, &VoiceController::ttsEngineChanged, this, [this](const QString &engineId) {
        if (voiceEngineCombo_)
            voiceEngineCombo_->setCurrentIndex(voiceEngineCombo_->findData(engineId));
        refreshVoiceChoices();
        updateVoiceStatus();
    });
    connect(voiceController_, &VoiceController::listeningChanged, this, [this](bool listening) {
        for (QPushButton *button : voiceInputButtons_)
            button->setText(listening ? QStringLiteral("STOP LISTENING")
                                      : (handsFreeVoiceCheck_ && handsFreeVoiceCheck_->isChecked()
                                             ? QStringLiteral("START VOICE SESSION")
                                             : QStringLiteral("VOICE COMMANDS")));
        updateVoiceStatus();
        refreshAudioInputs();
    });
    connect(voiceController_, &VoiceController::audioInputsChanged, this, [this] {
        refreshAudioInputs();
        const bool canListen = !voiceController_->installedRecognizers().isEmpty()
                               && !voiceController_->audioInputNames().isEmpty();
        if (voiceInputButton_)
            voiceInputButton_->setEnabled(canListen);
        for (QPushButton *button : voiceInputButtons_)
            button->setEnabled(canListen);
    });
    connect(voiceController_, &VoiceController::audioLevelChanged, this, [this](int level) {
        if (microphoneLevelBar_)
            microphoneLevelBar_->setValue(level);
    });
    connect(voiceController_, &VoiceController::recognitionStatusChanged, this, [this](const QString &status) {
        if (recognizerStatusLabel_)
            recognizerStatusLabel_->setText(status);
        statusBar()->showMessage(status, 5000);
        updateVoiceStatus();
    });
    connect(voiceController_, &VoiceController::speakingChanged, this, [this](bool speaking) {
        for (QPushButton *button : stopSpeechButtons_)
            button->setEnabled(speaking);
        updateVoiceStatus();
        for (QLabel *label : streamingLabels_) {
            if (speaking) {
                label->setText(QStringLiteral("JARVIS  ·  SPEAKING"));
                label->show();
            } else if (!chatRequestActive_) {
                label->hide();
            }
        }
        if (!speaking && interruptListeningDuringSpeech_) {
            interruptListeningDuringSpeech_ = false;
            if (voiceController_->isListening())
                voiceController_->stopListening();
            if (resumeVoiceAfterSpeech_ && !chatRequestActive_) {
                resumeVoiceAfterSpeech_ = false;
                resumeHandsFreeListening();
            }
        }
    });
    connect(voiceController_, &VoiceController::transcriptionReceived, this,
            [this](const QString &text, double confidence) {
        if (voiceController_->isSpeaking()) {
            QVector<ToolRegistry::SpokenAction> interruptActions;
            QString ignoredResponse;
            const bool isStopCommand = confidence >= 0.60 && toolRegistry_
                                       && toolRegistry_->parseSpokenCommand(text, &interruptActions, &ignoredResponse)
                                       && interruptActions.size() == 1
                                       && interruptActions.constFirst().type == ToolRegistry::SpokenAction::Type::StopSpeaking;
            if (isStopCommand) {
                voiceController_->stopListening();
                voiceController_->stopSpeaking();
                const QString response = QStringLiteral("Stopped current speech output.");
                appendMessage(QStringLiteral("YOU"), text, true);
                appendMessage(QStringLiteral("JARVIS"), response, false);
                conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                         {QStringLiteral("content"), text}});
                conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                                         {QStringLiteral("content"), response}});
                saveConversationHistory();
                statusBar()->showMessage(response, 5000);
                logEvent(QStringLiteral("Voice interrupt stopped current speech output."));
            }
            return;
        }
        const bool handsFree = handsFreeVoiceCheck_ && handsFreeVoiceCheck_->isChecked();

        QVector<ToolRegistry::SpokenAction> spokenActions;
        QString commandResponse;
        if (toolRegistry_ && toolRegistry_->parseSpokenCommand(text, &spokenActions, &commandResponse)) {
            bool transcriptAccepted = false;
            const QString confirmedText = QInputDialog::getText(
                this, QStringLiteral("Review voice command"),
                QStringLiteral("Edit the recognised command before JARVIS runs it:"),
                QLineEdit::Normal, text, &transcriptAccepted).trimmed();
            if (!transcriptAccepted || confirmedText.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("Voice command cancelled. You can retry or edit the transcript in the composer."), 6000);
                return;
            }
            spokenActions.clear();
            commandResponse.clear();
            if (!toolRegistry_->parseSpokenCommand(confirmedText, &spokenActions, &commandResponse)) {
                if (chatInput_)
                    chatInput_->setText(confirmedText);
                showPage(1, QStringLiteral("Conversation"));
                statusBar()->showMessage(QStringLiteral("The edited transcript is not a supported PC command; it is in the composer for review."), 7000);
                return;
            }
            if (voiceController_->isListening())
                voiceController_->stopListening();
            showPage(1, QStringLiteral("Conversation"));
            appendMessage(QStringLiteral("YOU"), confirmedText, true);
            conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                     {QStringLiteral("content"), confirmedText}});

            QStringList resultLines;
            if (!commandResponse.isEmpty()) {
                resultLines.append(commandResponse);
            } else {
                for (const ToolRegistry::SpokenAction &action : spokenActions) {
                    if (action.type == ToolRegistry::SpokenAction::Type::StopSpeaking) {
                        voiceController_->stopSpeaking();
                        resultLines.append(QStringLiteral("Stopped current speech output."));
                        continue;
                    }
                    if (toolRegistry_->spokenActionRequiresConfirmation(action)) {
                        QMessageBox confirmation(this);
                        confirmation.setIcon(QMessageBox::Question);
                        confirmation.setWindowTitle(QStringLiteral("Confirm close request"));
                        confirmation.setTextFormat(Qt::PlainText);
                        confirmation.setText(QStringLiteral("JARVIS heard a request to close %1. Windows may have unsaved work to save. Continue?")
                                                 .arg(action.label));
                        confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                        confirmation.setDefaultButton(QMessageBox::No);
                        if (confirmation.exec() != QMessageBox::Yes) {
                            resultLines.append(QStringLiteral("I left %1 open because you declined the close request.")
                                                   .arg(action.label));
                            continue;
                        }
                    }
                    QString details;
                    const bool succeeded = toolRegistry_->executeSpokenAction(action, &details);
                    resultLines.append(details.isEmpty()
                                           ? (succeeded ? QStringLiteral("Completed %1.").arg(action.label)
                                                        : QStringLiteral("Could not complete %1.").arg(action.label))
                                           : details);
                    logEvent(QStringLiteral("Spoken command %1: %2")
                                 .arg(succeeded ? QStringLiteral("completed") : QStringLiteral("failed"), details));
                }
            }

            const QString response = resultLines.join(QLatin1Char('\n'));
            appendMessage(QStringLiteral("JARVIS"), response, false);
            conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                                     {QStringLiteral("content"), response}});
            saveConversationHistory();
            statusBar()->showMessage(response, 9000);
            if (speakRepliesCheck_ && speakRepliesCheck_->isChecked()
                && voiceController_->currentTtsEngineReady() && voiceCombo_ && voiceCombo_->currentIndex() >= 0)
                speakText(response);
            else if (speakRepliesCheck_ && speakRepliesCheck_->isChecked())
                statusBar()->showMessage(response + QStringLiteral(" Speech output is unavailable, so the result is shown as text."), 9000);
            logEvent(QStringLiteral("A recognised spoken PC command was handled locally."));
            return;
        }

        if (chatInput_)
            chatInput_->setText(text);
        showPage(1, QStringLiteral("Conversation"));
        if (handsFree) {
            const bool sent = submitText(text);
            if (sent && chatInput_)
                chatInput_->clear();
            statusBar()->showMessage(sent ? QStringLiteral("Speech recognised (%1% confidence) and sent. Listening pauses until the reply is complete.").arg(qRound(confidence * 100))
                                          : QStringLiteral("Speech recognised and placed in the composer; configure a provider and model to continue."), 7000);
            logEvent(sent ? QStringLiteral("Hands-free speech was recognised and submitted to the selected provider.")
                          : QStringLiteral("Hands-free speech was recognised but could not be submitted."));
            return;
        }
        statusBar()->showMessage(QStringLiteral("Transcript recognised (%1% confidence). Review or edit it in the composer, then press Send; use Voice > Start again to retry.")
                                     .arg(qRound(confidence * 100)), 7000);
        logEvent(QStringLiteral("Speech was recognised and placed in the composer for review."));
    });
    connect(voiceController_, &VoiceController::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
        if (recognizerStatusLabel_ && (message.contains(QStringLiteral("microphone"), Qt::CaseInsensitive)
                                       || message.contains(QStringLiteral("recognizer"), Qt::CaseInsensitive)
                                       || message.contains(QStringLiteral("recognition"), Qt::CaseInsensitive)
                                       || message.contains(QStringLiteral("speech was"), Qt::CaseInsensitive)))
            recognizerStatusLabel_->setText(message);
        logEvent(QStringLiteral("Voice provider reported an error: %1").arg(message));
    });

    KokoroTtsProvider *kokoro = voiceController_->kokoroProvider();
    connect(kokoro, &KokoroTtsProvider::modelStatusChanged, this, [this](bool, const QString &) {
        updateVoiceModelStatus();
        refreshVoiceChoices();
        updateVoiceStatus();
    });
    connect(kokoro, &KokoroTtsProvider::runtimeStatusChanged, this, [this](bool, const QString &) {
        updateVoiceModelStatus();
        refreshVoiceChoices();
        updateVoiceStatus();
    });
    connect(kokoro, &KokoroTtsProvider::downloadProgress, this,
            [this](const QString &fileName, qint64 received, qint64 total) {
        if (voiceDownloadProgress_) {
            voiceDownloadProgress_->setRange(0, total > 0 ? 1000 : 0);
            if (total > 0)
                voiceDownloadProgress_->setValue(static_cast<int>(qMin<qint64>(1000, received * 1000 / total)));
        }
        if (voiceDownloadStatusLabel_)
            voiceDownloadStatusLabel_->setText(QStringLiteral("Downloading %1 · %2 / %3 MB")
                                              .arg(fileName)
                                              .arg(received / (1024.0 * 1024.0), 0, 'f', 1)
                                              .arg(total / (1024.0 * 1024.0), 0, 'f', 1));
        if (voiceCancelDownloadButton_)
            voiceCancelDownloadButton_->setEnabled(true);
        if (voiceDownloadButton_)
            voiceDownloadButton_->setEnabled(false);
    });
    connect(kokoro, &KokoroTtsProvider::downloadFinished, this, [this] {
        if (voiceDownloadProgress_)
            voiceDownloadProgress_->setValue(1000);
        if (voiceDownloadStatusLabel_)
            voiceDownloadStatusLabel_->setText(QStringLiteral("Downloads completed. SHA-256 and Kokoro speech synthesis status are shown above."));
        if (voiceDownloadButton_)
            voiceDownloadButton_->setEnabled(true);
        if (voiceCancelDownloadButton_)
            voiceCancelDownloadButton_->setEnabled(false);
        updateVoiceModelStatus();
        refreshVoiceChoices();
        statusBar()->showMessage(QStringLiteral("Kokoro downloads completed; checking model integrity and synthesis."), 7000);
        logEvent(QStringLiteral("Kokoro download completed; model integrity and synthesis are reported separately."));
    });
    connect(kokoro, &KokoroTtsProvider::downloadFailed, this, [this](const QString &message) {
        if (voiceDownloadStatusLabel_)
            voiceDownloadStatusLabel_->setText(message);
        if (voiceDownloadButton_)
            voiceDownloadButton_->setText(QStringLiteral("RETRY DOWNLOAD · 142 MB"));
        if (voiceDownloadButton_)
            voiceDownloadButton_->setEnabled(true);
        if (voiceCancelDownloadButton_)
            voiceCancelDownloadButton_->setEnabled(false);
        if (voiceDownloadProgress_) {
            voiceDownloadProgress_->setRange(0, 1000);
            voiceDownloadProgress_->setValue(0);
        }
        updateVoiceModelStatus();
        statusBar()->showMessage(message, 9000);
        logEvent(QStringLiteral("Kokoro model download did not complete: %1").arg(message));
    });

    QTimer::singleShot(0, this, &MainWindow::loadProviderModels);
    voiceController_->initialise();
    logEvent(QStringLiteral("Dashboard started."));
}

QWidget *MainWindow::makePanel(const QString &title, QVBoxLayout **bodyLayout) const
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("panel"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    layout->addWidget(makeLabel(title.toUpper(), QStringLiteral("sectionTitle")));
    if (bodyLayout != nullptr)
        *bodyLayout = layout;
    return frame;
}

QWidget *MainWindow::makePageHeader(const QString &eyebrow, const QString &title, const QString &subtitle) const
{
    auto *container = new QWidget;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(6);
    layout->addWidget(makeLabel(eyebrow.toUpper(), QStringLiteral("eyebrow")));
    layout->addWidget(makeLabel(title, QStringLiteral("heroTitle")));
    auto *description = makeLabel(subtitle, QStringLiteral("muted"));
    description->setWordWrap(true);
    layout->addWidget(description);
    return container;
}

QWidget *MainWindow::makeMetricCard(const QString &title, MetricDisplay *display)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("metric"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(makeLabel(title.toUpper(), QStringLiteral("eyebrow")));
    display->value = makeLabel(QStringLiteral("—"), QStringLiteral("metricValue"));
    display->detail = makeLabel(QStringLiteral("Reading system data"), QStringLiteral("metricDetail"));
    display->detail->setWordWrap(true);
    display->bar = new QProgressBar;
    display->bar->setRange(0, 100);
    display->bar->setValue(0);
    display->bar->setTextVisible(false);
    layout->addWidget(display->value);
    layout->addWidget(display->bar);
    layout->addWidget(display->detail);
    return frame;
}

QWidget *MainWindow::makeGpuCard(const QString &title)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("metric"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(makeLabel(title.toUpper(), QStringLiteral("eyebrow")));
    auto *name = makeLabel(monitor_.gpuName(), QStringLiteral("metricValue"));
    name->setWordWrap(true);
    name->setStyleSheet(QStringLiteral("font-size:16px"));
    layout->addWidget(name, 1);
    layout->addWidget(makeLabel(QStringLiteral("Adapter name detected through Windows DXGI"), QStringLiteral("metricDetail")));
    gpuDisplays_.append(name);
    return frame;
}

QWidget *MainWindow::makeReadinessCard(const QString &title, const QString &phase,
                                      const QString &description) const
{
    QVBoxLayout *body = nullptr;
    QWidget *frame = makePanel(title, &body);
    body->addWidget(makeLabel(phase.toUpper(), QStringLiteral("eyebrow")));
    auto *text = makeLabel(description, QStringLiteral("muted"));
    text->setWordWrap(true);
    body->addWidget(text);
    body->addStretch(1);
    return frame;
}

QWidget *MainWindow::wrapPage(QWidget *content) const
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QWidget *MainWindow::buildHomePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Personal assistant / local dashboard"),
                                     QStringLiteral("Your system, at a glance."),
                                     QStringLiteral("Live system readings, provider status, and local voice controls for your assistant.")));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    QVBoxLayout *reactorBody = nullptr;
    QWidget *reactorPanel = makePanel(QStringLiteral("CORE SYSTEM"), &reactorBody);
    auto *reactor = new ReactorWidget;
    reactor->setMinimumHeight(310);
    reactorBody->addWidget(reactor, 1);
    auto *reactorNote = makeLabel(QStringLiteral("Idle animation reflects assistant state; microphone activity is not being sampled."),
                                  QStringLiteral("metricDetail"));
    reactorNote->setWordWrap(true);
    reactorBody->addWidget(reactorNote);

    QVBoxLayout *sessionBody = nullptr;
    QWidget *sessionPanel = makePanel(QStringLiteral("LIVE SESSION"), &sessionBody);
    homeAiSummary_ = makeLabel(QStringLiteral("AI CONNECTION  ·  checking"), QStringLiteral("eyebrow"));
    homeAiSummary_->setWordWrap(true);
    sessionBody->addWidget(homeAiSummary_);
    homeTranscript_ = makeTranscript();
    homeTranscript_->setMinimumHeight(190);
    conversationWelcomeViews_.append(homeTranscript_);
    appendMessage(QStringLiteral("JARVIS"),
                  QStringLiteral("Checking the selected AI provider and model…"), false);
    sessionBody->addWidget(homeTranscript_, 1);
    auto *homeStreaming = makeLabel(QString(), QStringLiteral("muted"));
    homeStreaming->setWordWrap(true);
    homeStreaming->hide();
    streamingLabels_.append(homeStreaming);
    sessionBody->addWidget(homeStreaming);

    auto *composer = new QHBoxLayout;
    auto *input = new QLineEdit;
    input->setPlaceholderText(QStringLiteral("Enter a message"));
    auto *send = makeButton(QStringLiteral("SEND"), QStringLiteral("primaryButton"));
    composer->addWidget(input, 1);
    composer->addWidget(send);
    sessionBody->addLayout(composer);
    connect(send, &QPushButton::clicked, this, [this, input] {
        if (submitText(input->text()))
            input->clear();
    });
    connectComposer(input);

    auto *voiceRow = new QHBoxLayout;
    auto *homeVoiceButton = makeButton(QStringLiteral("START VOICE COMMANDS"), QStringLiteral("secondaryButton"));
    homeVoiceButton->setEnabled(false);
    homeVoiceButton->setToolTip(QStringLiteral("Start local speech recognition for conversation and supported Windows commands."));
    voiceInputButtons_.append(homeVoiceButton);
    connect(homeVoiceButton, &QPushButton::clicked, this, &MainWindow::showVoiceNotice);
    auto *stopSpeech = makeButton(QStringLiteral("STOP OUTPUT"), QStringLiteral("secondaryButton"));
    stopSpeech->setEnabled(false);
    stopSpeech->setToolTip(QStringLiteral("Stop current Windows speech output."));
    stopSpeechButtons_.append(stopSpeech);
    connect(stopSpeech, &QPushButton::clicked, this, &MainWindow::stopSpeech);
    voiceRow->addWidget(homeVoiceButton);
    voiceRow->addWidget(stopSpeech);
    voiceRow->addStretch(1);
    sessionBody->addLayout(voiceRow);

    grid->addWidget(reactorPanel, 0, 0);
    grid->addWidget(sessionPanel, 0, 1);

    QVBoxLayout *telemetryBody = nullptr;
    QWidget *telemetry = makePanel(QStringLiteral("SYSTEM TELEMETRY"), &telemetryBody);
    auto *metricGrid = new QGridLayout;
    metricGrid->setSpacing(10);
    MetricDisplay cpu;
    MetricDisplay memory;
    metricGrid->addWidget(makeMetricCard(QStringLiteral("CPU load"), &cpu), 0, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("Memory"), &memory), 0, 1);
    metricGrid->addWidget(makeGpuCard(QStringLiteral("Graphics adapter")), 0, 2);
    cpuDisplays_.append(cpu);
    memoryDisplays_.append(memory);
    telemetryBody->addLayout(metricGrid);
    grid->addWidget(telemetry, 1, 0, 1, 2);
    layout->addLayout(grid);
    return page;
}

QWidget *MainWindow::buildConversationPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Session / text interface"),
                                     QStringLiteral("Conversation"),
                                     QStringLiteral("Responses stream from the selected provider. Conversation history is stored locally and can be cleared at any time.")));
    QVBoxLayout *body = nullptr;
    QWidget *panel = makePanel(QStringLiteral("CONVERSATION"), &body);
    conversationTranscript_ = makeTranscript();
    conversationTranscript_->setMinimumHeight(330);
    body->addWidget(conversationTranscript_, 1);
    auto *conversationStreaming = makeLabel(QString(), QStringLiteral("muted"));
    conversationStreaming->setWordWrap(true);
    conversationStreaming->hide();
    streamingLabels_.append(conversationStreaming);
    body->addWidget(conversationStreaming);
    if (conversationMessages_.isEmpty()) {
        conversationWelcomeViews_.append(conversationTranscript_);
        appendMessageToView(conversationTranscript_, QStringLiteral("JARVIS"),
                            QStringLiteral("Checking the selected AI provider and model…"), false);
    } else {
        for (const QJsonValue &value : conversationMessages_) {
            const QJsonObject message = value.toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            if (role == QStringLiteral("assistant") && message.value(QStringLiteral("content")).toString().isEmpty()
                && message.value(QStringLiteral("tool_calls")).isArray()) {
                continue;
            }
            const QString speaker = role == QStringLiteral("user") ? QStringLiteral("YOU")
                                  : role == QStringLiteral("tool")
                                      ? QStringLiteral("TOOL · %1").arg(message.value(QStringLiteral("tool_name")).toString())
                                  : QStringLiteral("JARVIS");
            appendMessageToView(conversationTranscript_,
                                speaker,
                                message.value(QStringLiteral("content")).toString(), role == QStringLiteral("user"));
        }
    }
    auto *row = new QHBoxLayout;
    chatInput_ = new QLineEdit;
    chatInput_->setPlaceholderText(QStringLiteral("Type a message"));
    auto *send = makeButton(QStringLiteral("SEND"), QStringLiteral("primaryButton"));
    cancelChatButton_ = makeButton(QStringLiteral("CANCEL"), QStringLiteral("secondaryButton"));
    cancelChatButton_->setEnabled(false);
    auto *clear = makeButton(QStringLiteral("CLEAR HISTORY"), QStringLiteral("secondaryButton"));
    row->addWidget(chatInput_, 1);
    row->addWidget(send);
    auto *conversationStopSpeech = makeButton(QStringLiteral("STOP SPEAKING"), QStringLiteral("secondaryButton"));
    conversationStopSpeech->setEnabled(false);
    stopSpeechButtons_.append(conversationStopSpeech);
    connect(conversationStopSpeech, &QPushButton::clicked, this, &MainWindow::stopSpeech);
    row->addWidget(conversationStopSpeech);
    auto *conversationVoiceButton = makeButton(QStringLiteral("VOICE COMMANDS"), QStringLiteral("secondaryButton"));
    conversationVoiceButton->setEnabled(false);
    conversationVoiceButton->setToolTip(QStringLiteral("Use local Windows speech recognition for conversation and supported PC commands."));
    voiceInputButtons_.append(conversationVoiceButton);
    connect(conversationVoiceButton, &QPushButton::clicked, this, &MainWindow::showVoiceNotice);
    row->addWidget(conversationVoiceButton);
    row->addWidget(cancelChatButton_);
    body->addLayout(row);
    auto *historyRow = new QHBoxLayout;
    historyRow->addStretch(1);
    historyRow->addWidget(clear);
    body->addLayout(historyRow);
    connect(send, &QPushButton::clicked, this, [this] {
        if (submitText(chatInput_->text()))
            chatInput_->clear();
    });
    connectComposer(chatInput_);
    connect(cancelChatButton_, &QPushButton::clicked, this, &MainWindow::cancelChatRequest);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearConversationHistory);
    layout->addWidget(panel, 1);
    return page;
}

QWidget *MainWindow::buildMonitorPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Windows / live telemetry"),
                                     QStringLiteral("System monitoring"),
                                     QStringLiteral("CPU and memory are sampled from Windows. The GPU entry identifies a hardware adapter; GPU load is not read in this phase.")));

    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    MetricDisplay cpu;
    MetricDisplay memory;
    grid->addWidget(makeMetricCard(QStringLiteral("CPU load"), &cpu), 0, 0);
    grid->addWidget(makeMetricCard(QStringLiteral("Physical memory"), &memory), 0, 1);
    grid->addWidget(makeGpuCard(QStringLiteral("Graphics adapter")), 0, 2);
    cpuDisplays_.append(cpu);
    memoryDisplays_.append(memory);
    layout->addLayout(grid);

    QVBoxLayout *details = nullptr;
    QWidget *detailPanel = makePanel(QStringLiteral("DATA SOURCES"), &details);
    auto *cpuInfo = makeLabel(QStringLiteral("CPU load: Windows GetSystemTimes, sampled every 1.5 seconds."), QStringLiteral("muted"));
    auto *memoryInfo = makeLabel(QStringLiteral("Memory use: Windows GlobalMemoryStatusEx."), QStringLiteral("muted"));
    auto *gpuInfo = makeLabel(QStringLiteral("Adapter name: Windows DXGI enumeration. No GPU utilization counter is read."), QStringLiteral("muted"));
    for (QLabel *line : {cpuInfo, memoryInfo, gpuInfo}) {
        line->setWordWrap(true);
        details->addWidget(line);
    }
    layout->addWidget(detailPanel);
    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::buildToolsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(14);
    layout->addWidget(makePageHeader(QStringLiteral("Windows / controlled actions"),
                                     QStringLiteral("Tools & automation"),
                                     QStringLiteral("Run a short list of installed apps, open selected files and websites, save notes, or search file names in a folder.")));

    auto *appGrid = new QGridLayout;
    appGrid->setSpacing(12);
    QVBoxLayout *appsBody = nullptr;
    auto *appsPanel = makePanel(QStringLiteral("ALLOWLISTED APPLICATIONS"), &appsBody);
    auto *appDescription = makeLabel(QStringLiteral("Only supported applications discovered in the Windows Start menu or executable search path appear here. Voice commands are available on Home and Conversation."), QStringLiteral("muted"));
    appDescription->setWordWrap(true);
    appsBody->addWidget(appDescription);
    auto *applicationCombo = new QComboBox;
    applicationCombo->setObjectName(QStringLiteral("toolApplicationCombo"));
    const auto applications = toolRegistry_->availableApplications();
    for (const auto &application : applications)
        applicationCombo->addItem(application.label, application.id);
    if (applications.isEmpty())
        applicationCombo->addItem(QStringLiteral("No supported apps found"));
    auto *launchButton = makeButton(QStringLiteral("LAUNCH APPLICATION"), QStringLiteral("primaryButton"));
    launchButton->setObjectName(QStringLiteral("toolLaunchApplicationButton"));
    launchButton->setEnabled(!applications.isEmpty());
    auto *appRow = new QHBoxLayout;
    appRow->addWidget(applicationCombo, 1);
    appRow->addWidget(launchButton);
    appsBody->addLayout(appRow);
    auto *toolStatus = makeLabel(QStringLiteral("Ready."), QStringLiteral("metricDetail"));
    toolStatus->setObjectName(QStringLiteral("toolStatusLabel"));
    toolStatus->setWordWrap(true);
    appsBody->addWidget(toolStatus);
    appGrid->addWidget(appsPanel, 0, 0);

    QVBoxLayout *webBody = nullptr;
    auto *webPanel = makePanel(QStringLiteral("WEBSITE OR SELECTED PATH"), &webBody);
    auto *webDescription = makeLabel(QStringLiteral("Web addresses open in your default browser. Pick local paths yourself; executable, script, installer and shortcut files are blocked."), QStringLiteral("muted"));
    webDescription->setWordWrap(true);
    webBody->addWidget(webDescription);
    auto *websiteEdit = new QLineEdit;
    websiteEdit->setObjectName(QStringLiteral("toolWebsiteEdit"));
    websiteEdit->setPlaceholderText(QStringLiteral("https://example.com"));
    auto *openWebsiteButton = makeButton(QStringLiteral("OPEN WEBSITE"), QStringLiteral("secondaryButton"));
    auto *websiteRow = new QHBoxLayout;
    websiteRow->addWidget(websiteEdit, 1);
    websiteRow->addWidget(openWebsiteButton);
    webBody->addLayout(websiteRow);
    auto *pathEdit = new QLineEdit;
    pathEdit->setObjectName(QStringLiteral("toolPathEdit"));
    pathEdit->setPlaceholderText(QStringLiteral("Choose a document or folder"));
    auto *chooseFileButton = makeButton(QStringLiteral("CHOOSE FILE"), QStringLiteral("secondaryButton"));
    auto *chooseFolderButton = makeButton(QStringLiteral("CHOOSE FOLDER"), QStringLiteral("secondaryButton"));
    auto *openPathButton = makeButton(QStringLiteral("OPEN SELECTED PATH"), QStringLiteral("secondaryButton"));
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(chooseFileButton);
    webBody->addLayout(pathRow);
    auto *pathButtons = new QHBoxLayout;
    pathButtons->addWidget(chooseFolderButton);
    pathButtons->addWidget(openPathButton);
    pathButtons->addStretch(1);
    webBody->addLayout(pathButtons);
    appGrid->addWidget(webPanel, 0, 1);
    layout->addLayout(appGrid);

    auto *lowerGrid = new QGridLayout;
    lowerGrid->setSpacing(12);
    QVBoxLayout *notesBody = nullptr;
    auto *notesPanel = makePanel(QStringLiteral("LOCAL NOTES"), &notesBody);
    auto *notesDescription = makeLabel(QStringLiteral("Creates a UTF-8 text note in Documents\\JARVIS\\Notes. Existing notes are kept; duplicate titles receive a timestamp."), QStringLiteral("muted"));
    notesDescription->setWordWrap(true);
    notesBody->addWidget(notesDescription);
    auto *noteTitle = new QLineEdit;
    noteTitle->setObjectName(QStringLiteral("toolNoteTitleEdit"));
    noteTitle->setPlaceholderText(QStringLiteral("Note title"));
    notesBody->addWidget(noteTitle);
    auto *noteContent = new QPlainTextEdit;
    noteContent->setObjectName(QStringLiteral("toolNoteContentEdit"));
    noteContent->setPlaceholderText(QStringLiteral("Write a note…"));
    noteContent->setMinimumHeight(130);
    notesBody->addWidget(noteContent);
    auto *saveNoteButton = makeButton(QStringLiteral("CREATE NOTE"), QStringLiteral("primaryButton"));
    auto *noteRow = new QHBoxLayout;
    noteRow->addWidget(saveNoteButton);
    noteRow->addStretch(1);
    notesBody->addLayout(noteRow);
    lowerGrid->addWidget(notesPanel, 0, 0);

    QVBoxLayout *searchBody = nullptr;
    auto *searchPanel = makePanel(QStringLiteral("FILE NAME SEARCH"), &searchBody);
    auto *searchDescription = makeLabel(QStringLiteral("Searches file names recursively under a folder you choose. File contents are not read. Results are capped at 5,000."), QStringLiteral("muted"));
    searchDescription->setWordWrap(true);
    searchBody->addWidget(searchDescription);
    auto *searchFolder = new QLineEdit;
    searchFolder->setObjectName(QStringLiteral("toolSearchFolderEdit"));
    searchFolder->setPlaceholderText(QStringLiteral("Choose a folder to search"));
    auto *browseSearchButton = makeButton(QStringLiteral("BROWSE"), QStringLiteral("secondaryButton"));
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(searchFolder, 1);
    folderRow->addWidget(browseSearchButton);
    searchBody->addLayout(folderRow);
    auto *searchQuery = new QLineEdit;
    searchQuery->setObjectName(QStringLiteral("toolSearchQueryEdit"));
    searchQuery->setPlaceholderText(QStringLiteral("Part of a file name"));
    searchBody->addWidget(searchQuery);
    auto *searchStatus = makeLabel(QStringLiteral("Ready."), QStringLiteral("metricDetail"));
    searchStatus->setObjectName(QStringLiteral("fileSearchStatusLabel"));
    searchBody->addWidget(searchStatus);
    auto *results = new QPlainTextEdit;
    results->setObjectName(QStringLiteral("toolSearchResultsEdit"));
    results->setReadOnly(true);
    results->setPlaceholderText(QStringLiteral("Search results will appear here."));
    results->setMinimumHeight(130);
    searchBody->addWidget(results);
    auto *startSearchButton = makeButton(QStringLiteral("START SEARCH"), QStringLiteral("primaryButton"));
    auto *cancelSearchButton = makeButton(QStringLiteral("CANCEL SEARCH"), QStringLiteral("secondaryButton"));
    cancelSearchButton->setEnabled(false);
    auto *searchButtons = new QHBoxLayout;
    searchButtons->addWidget(startSearchButton);
    searchButtons->addWidget(cancelSearchButton);
    searchButtons->addStretch(1);
    searchBody->addLayout(searchButtons);
    lowerGrid->addWidget(searchPanel, 0, 1);
    layout->addLayout(lowerGrid);

    auto setToolStatus = [this, toolStatus](const QString &text, bool success) {
        toolStatus->setText(text);
        statusBar()->showMessage(text, 7000);
        logEvent(QStringLiteral("Tool %1: %2").arg(success ? QStringLiteral("completed") : QStringLiteral("failed"), text));
    };
    connect(launchButton, &QPushButton::clicked, this, [this, applicationCombo, setToolStatus] {
        QString details;
        const bool ok = toolRegistry_->launchApplication(applicationCombo->currentData().toString(), &details);
        setToolStatus(details, ok);
    });
    connect(openWebsiteButton, &QPushButton::clicked, this, [this, websiteEdit, setToolStatus] {
        QString details;
        const bool ok = toolRegistry_->openWebsite(websiteEdit->text(), &details);
        setToolStatus(details, ok);
    });
    connect(chooseFileButton, &QPushButton::clicked, this, [this, pathEdit] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a file"));
        if (!path.isEmpty())
            pathEdit->setText(path);
    });
    connect(chooseFolderButton, &QPushButton::clicked, this, [this, pathEdit] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder"));
        if (!path.isEmpty())
            pathEdit->setText(path);
    });
    connect(openPathButton, &QPushButton::clicked, this, [this, pathEdit, setToolStatus] {
        QString details;
        const bool ok = toolRegistry_->openPath(pathEdit->text(), &details);
        setToolStatus(details, ok);
    });
    connect(saveNoteButton, &QPushButton::clicked, this, [this, noteTitle, noteContent, setToolStatus] {
        QString savedPath;
        QString error;
        const bool ok = toolRegistry_->createNote(noteTitle->text(), noteContent->toPlainText(), &savedPath, &error);
        setToolStatus(ok ? QStringLiteral("Saved note: %1").arg(savedPath) : error, ok);
    });
    connect(browseSearchButton, &QPushButton::clicked, this, [this, searchFolder] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose folder to search"));
        if (!path.isEmpty())
            searchFolder->setText(path);
    });
    connect(startSearchButton, &QPushButton::clicked, this, [this, searchFolder, searchQuery, results, searchStatus,
                                                             startSearchButton, cancelSearchButton] {
        QString error;
        results->clear();
        if (!toolRegistry_->startFileSearch(searchFolder->text(), searchQuery->text(), &error)) {
            searchStatus->setText(error);
            logEvent(QStringLiteral("File search could not start: %1").arg(error));
            return;
        }
        searchStatus->setText(QStringLiteral("Searching file names…"));
        startSearchButton->setEnabled(false);
        cancelSearchButton->setEnabled(true);
    });
    connect(cancelSearchButton, &QPushButton::clicked, this, [this, searchStatus] {
        toolRegistry_->cancelFileSearch();
        searchStatus->setText(QStringLiteral("Stopping search…"));
    });
    connect(toolRegistry_, &ToolRegistry::fileSearchProgress, this, [searchStatus](qint64 scanned, int matches) {
        searchStatus->setText(QStringLiteral("Scanned %1 files · %2 matches…").arg(scanned).arg(matches));
    });
    connect(toolRegistry_, &ToolRegistry::fileSearchFinished, this,
            [this, results, searchStatus, startSearchButton, cancelSearchButton](const QStringList &matches,
                                                                                qint64 scanned, bool cancelled,
                                                                                const QString &message) {
        results->setPlainText(matches.join(QLatin1Char('\n')));
        QString summary = QStringLiteral("Scanned %1 files; found %2 match(es)%3.")
                              .arg(scanned).arg(matches.size())
                              .arg(cancelled ? QStringLiteral(" before cancellation") : QString());
        if (!message.isEmpty())
            summary += QLatin1Char(' ') + message;
        searchStatus->setText(summary);
        startSearchButton->setEnabled(true);
        cancelSearchButton->setEnabled(false);
        logEvent(QStringLiteral("File search finished: %1").arg(summary));
    });

    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::buildVoicePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Audio / local processing"),
                                     QStringLiteral("Voice settings"),
                                     QStringLiteral("Choose local Windows speech or the optional Kokoro British voice. The microphone starts only after you press the voice-session button.")));
    QVBoxLayout *body = nullptr;
    QWidget *panel = makePanel(QStringLiteral("SPEECH OUTPUT"), &body);
    auto *voiceForm = new QFormLayout;
    voiceEngineCombo_ = new QComboBox;
    voiceEngineCombo_->addItem(QStringLiteral("Windows Speech · installed voices"), QStringLiteral("windows-sapi"));
    voiceEngineCombo_->addItem(QStringLiteral("Kokoro ONNX · offline British voices"), QStringLiteral("kokoro-onnx"));
    voiceEngineCombo_->setCurrentIndex(voiceEngineCombo_->findData(voiceController_->ttsEngine()));
    voiceForm->addRow(QStringLiteral("Speech engine"), voiceEngineCombo_);
    voiceCombo_ = new QComboBox;
    voiceCombo_->setEnabled(false);
    voiceCombo_->setPlaceholderText(QStringLiteral("Checking installed voices…"));
    voiceForm->addRow(QStringLiteral("Voice"), voiceCombo_);

    auto *rateRow = new QHBoxLayout;
    voiceRateSlider_ = new QSlider(Qt::Horizontal);
    voiceRateSlider_->setRange(-10, 10);
    voiceRateSlider_->setValue(QSettings().value(QStringLiteral("voice/rate"), 0).toInt());
    auto *rateValue = makeLabel(QString::number(voiceRateSlider_->value()), QStringLiteral("muted"));
    connect(voiceRateSlider_, &QSlider::valueChanged, rateValue,
            [rateValue](int value) { rateValue->setText(QString::number(value)); });
    connect(voiceRateSlider_, &QSlider::valueChanged, this, [](int value) {
        QSettings().setValue(QStringLiteral("voice/rate"), value);
    });
    rateRow->addWidget(voiceRateSlider_, 1);
    rateRow->addWidget(rateValue);
    voiceForm->addRow(QStringLiteral("Speaking rate"), rateRow);

    auto *volumeRow = new QHBoxLayout;
    voiceVolumeSlider_ = new QSlider(Qt::Horizontal);
    voiceVolumeSlider_->setRange(0, 100);
    voiceVolumeSlider_->setValue(QSettings().value(QStringLiteral("voice/volume"), 85).toInt());
    auto *volumeValue = makeLabel(QStringLiteral("%1%").arg(voiceVolumeSlider_->value()), QStringLiteral("muted"));
    connect(voiceVolumeSlider_, &QSlider::valueChanged, volumeValue,
            [volumeValue](int value) { volumeValue->setText(QStringLiteral("%1%").arg(value)); });
    connect(voiceVolumeSlider_, &QSlider::valueChanged, this, [](int value) {
        QSettings().setValue(QStringLiteral("voice/volume"), value);
    });
    volumeRow->addWidget(voiceVolumeSlider_, 1);
    volumeRow->addWidget(volumeValue);
    voiceForm->addRow(QStringLiteral("Volume"), volumeRow);
    body->addLayout(voiceForm);

    auto *testRow = new QHBoxLayout;
    auto *testText = new QLineEdit(QStringLiteral("Good evening. At seven forty-five this evening, I’ll have the report ready."));
    testText->setPlaceholderText(QStringLiteral("Text to speak"));
    auto *speak = makeButton(QStringLiteral("SPEAK"), QStringLiteral("primaryButton"));
    stopSpeechButton_ = makeButton(QStringLiteral("STOP SPEAKING"), QStringLiteral("secondaryButton"));
    stopSpeechButton_->setEnabled(false);
    stopSpeechButtons_.append(stopSpeechButton_);
    testRow->addWidget(testText, 1);
    testRow->addWidget(speak);
    testRow->addWidget(stopSpeechButton_);
    body->addLayout(testRow);
    connect(speak, &QPushButton::clicked, this, [this, testText] { speakText(testText->text()); });
    connect(stopSpeechButton_, &QPushButton::clicked, this, &MainWindow::stopSpeech);
    connect(voiceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            QSettings().setValue(QStringLiteral("voice/name"), voiceCombo_->itemData(index).toString());
    });
    connect(voiceEngineCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0)
            return;
        const QString engine = voiceEngineCombo_->itemData(index).toString();
        QSettings().setValue(QStringLiteral("voice/engine"), engine);
        voiceController_->setTtsEngine(engine);
        refreshVoiceChoices();
        updateVoiceStatus();
    });

    speakRepliesCheck_ = new QCheckBox(QStringLiteral("Speak assistant responses automatically"));
    speakRepliesCheck_->setChecked(QSettings().value(QStringLiteral("voice/speakReplies"), true).toBool());
    connect(speakRepliesCheck_, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(QStringLiteral("voice/speakReplies"), enabled);
    });
    body->addWidget(speakRepliesCheck_);
    handsFreeVoiceCheck_ = new QCheckBox(QStringLiteral("Hands-free conversation: send recognised speech and listen again after each reply"));
    handsFreeVoiceCheck_->setObjectName(QStringLiteral("handsFreeVoiceCheck"));
    handsFreeVoiceCheck_->setChecked(QSettings().value(QStringLiteral("voice/handsFree"), false).toBool());
    handsFreeVoiceCheck_->setToolTip(QStringLiteral("This opt-in mode opens the microphone only after you start a voice session. It stops listening during model replies and resumes when the reply finishes."));
    connect(handsFreeVoiceCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("voice/handsFree"), enabled);
        if (!enabled)
            resumeVoiceAfterSpeech_ = false;
        if (!enabled && voiceController_->isListening())
            voiceController_->stopListening();
        for (QPushButton *button : voiceInputButtons_)
            button->setText(voiceController_->isListening() ? QStringLiteral("STOP LISTENING")
                                                            : (enabled ? QStringLiteral("START VOICE SESSION")
                                                                       : QStringLiteral("START PUSH TO TALK")));
    });
    body->addWidget(handsFreeVoiceCheck_);
    voiceStatusLabel_ = makeLabel(QStringLiteral("Speech output is checking the installed Windows voices…"), QStringLiteral("muted"));
    voiceStatusLabel_->setWordWrap(true);
    body->addWidget(voiceStatusLabel_);

    QVBoxLayout *inputBody = nullptr;
    QWidget *inputPanel = makePanel(QStringLiteral("PUSH TO TALK"), &inputBody);
    recognizerStatusLabel_ = makeLabel(QStringLiteral("Checking installed Windows speech recognizers…"), QStringLiteral("muted"));
    recognizerStatusLabel_->setWordWrap(true);
    inputBody->addWidget(recognizerStatusLabel_);
    auto *microphoneRow = new QHBoxLayout;
    microphoneRow->addWidget(makeLabel(QStringLiteral("INPUT MICROPHONE"), QStringLiteral("metricDetail")));
    microphoneCombo_ = new QComboBox;
    microphoneCombo_->setObjectName(QStringLiteral("microphoneInputCombo"));
    microphoneRow->addWidget(microphoneCombo_, 1);
    inputBody->addLayout(microphoneRow);
    microphoneLevelBar_ = new QProgressBar;
    microphoneLevelBar_->setObjectName(QStringLiteral("microphoneLevelMeter"));
    microphoneLevelBar_->setRange(0, 100);
    microphoneLevelBar_->setValue(0);
    microphoneLevelBar_->setFormat(QStringLiteral("Microphone input · %p%"));
    microphoneLevelBar_->setTextVisible(true);
    inputBody->addWidget(microphoneLevelBar_);
    connect(microphoneCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && voiceController_)
            voiceController_->setAudioInputId(microphoneCombo_->itemData(index).toString());
    });
    auto *notice = makeLabel(QStringLiteral("Push to talk places recognised words in the composer for review. Optional hands-free mode submits speech after you start a voice session, pauses the microphone while JARVIS replies, then listens again. No wake word or background recording is used."), QStringLiteral("muted"));
    notice->setWordWrap(true);
    inputBody->addWidget(notice);
    voiceInputButton_ = makeButton(handsFreeVoiceCheck_->isChecked() ? QStringLiteral("START VOICE SESSION")
                                                                     : QStringLiteral("START PUSH TO TALK"),
                                   QStringLiteral("secondaryButton"));
    voiceInputButton_->setEnabled(false);
    voiceInputButtons_.append(voiceInputButton_);
    connect(voiceInputButton_, &QPushButton::clicked, this, &MainWindow::showVoiceNotice);
    inputBody->addWidget(voiceInputButton_, 0, Qt::AlignLeft);
    layout->addWidget(panel);
    layout->addWidget(inputPanel);
    layout->addStretch(1);
    refreshVoiceChoices();
    refreshAudioInputs();
    return page;
}

QWidget *MainWindow::buildVoiceModelsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Voice / local model library"),
                                     QStringLiteral("Voice models"),
                                     QStringLiteral("Inspect and manage optional offline neural speech files. Downloads start only after you confirm their size.")));

    KokoroTtsProvider *kokoro = voiceController_->kokoroProvider();
    QVBoxLayout *body = nullptr;
    QWidget *panel = makePanel(QStringLiteral("KOKORO · BRITISH ENGLISH"), &body);
    auto *intro = makeLabel(QStringLiteral("Kokoro provides the British male bm_george voice and other British English voices. The selected INT8 model is 114 MB; its voice bank is 28 MB. The model is Apache 2.0 and the ONNX runtime wrapper is MIT licensed."), QStringLiteral("muted"));
    intro->setWordWrap(true);
    body->addWidget(intro);

    voiceRuntimeStatusLabel_ = makeLabel(QStringLiteral("Checking the Python runtime…"), QStringLiteral("sectionTitle"));
    voiceRuntimeStatusLabel_->setWordWrap(true);
    body->addWidget(voiceRuntimeStatusLabel_);
    voiceModelStatusLabel_ = makeLabel(QStringLiteral("Checking model files…"), QStringLiteral("muted"));
    voiceModelStatusLabel_->setWordWrap(true);
    body->addWidget(voiceModelStatusLabel_);
    auto *modelPathLabel = makeLabel(QStringLiteral("Model folder: %1").arg(kokoro->modelDirectory()), QStringLiteral("metricDetail"));
    modelPathLabel->setWordWrap(true);
    modelPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(modelPathLabel);

    auto *downloadRow = new QHBoxLayout;
    voiceDownloadButton_ = makeButton(QStringLiteral("DOWNLOAD / RETRY · 142 MB"), QStringLiteral("primaryButton"));
    voiceCancelDownloadButton_ = makeButton(QStringLiteral("CANCEL DOWNLOAD"), QStringLiteral("secondaryButton"));
    voiceCancelDownloadButton_->setEnabled(false);
    voiceRemoveModelButton_ = makeButton(QStringLiteral("REMOVE LOCAL FILES"), QStringLiteral("secondaryButton"));
    voiceRemoveModelButton_->setEnabled(kokoro->hasLocalModelFiles());
    auto *refresh = makeButton(QStringLiteral("REFRESH STATUS"), QStringLiteral("secondaryButton"));
    downloadRow->addWidget(voiceDownloadButton_);
    downloadRow->addWidget(voiceCancelDownloadButton_);
    downloadRow->addWidget(voiceRemoveModelButton_);
    downloadRow->addWidget(refresh);
    downloadRow->addStretch(1);
    body->addLayout(downloadRow);

    voiceDownloadProgress_ = new QProgressBar;
    voiceDownloadProgress_->setRange(0, 1000);
    voiceDownloadProgress_->setValue(0);
    voiceDownloadProgress_->setTextVisible(false);
    body->addWidget(voiceDownloadProgress_);
    voiceDownloadStatusLabel_ = makeLabel(QStringLiteral("No download is running."), QStringLiteral("metricDetail"));
    voiceDownloadStatusLabel_->setWordWrap(true);
    body->addWidget(voiceDownloadStatusLabel_);

    connect(refresh, &QPushButton::clicked, this, [this, kokoro] {
        kokoro->refreshStatus();
        updateVoiceModelStatus();
    });
    connect(voiceDownloadButton_, &QPushButton::clicked, this, [this, kokoro] {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this, QStringLiteral("Download Kokoro British voice files"),
            QStringLiteral("Download the official Kokoro INT8 model (114,119,327 bytes) and voice bank (28,214,398 bytes)?\n\n"
                           "Total: 142,333,725 bytes (about 142 MB / 136 MiB). The files are saved under the JARVIS application data folder. "
                           "The app checks both exact file sizes and SHA-256 hashes before installing them. This requires an internet connection."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes)
            return;
        if (voiceDownloadProgress_) {
            voiceDownloadProgress_->setRange(0, 1000);
            voiceDownloadProgress_->setValue(0);
        }
        if (voiceDownloadStatusLabel_)
            voiceDownloadStatusLabel_->setText(QStringLiteral("Preparing the verified Kokoro model download…"));
        voiceDownloadButton_->setEnabled(false);
        voiceCancelDownloadButton_->setEnabled(true);
        kokoro->downloadModels();
    });
    connect(voiceCancelDownloadButton_, &QPushButton::clicked, this, [kokoro] {
        kokoro->cancelDownload();
    });
    connect(voiceRemoveModelButton_, &QPushButton::clicked, this, [this, kokoro] {
        if (QMessageBox::question(this, QStringLiteral("Remove Kokoro model files"),
                                  QStringLiteral("Remove the optional Kokoro model and voice bank from the JARVIS data folder? This frees about 136 MiB and does not remove Python packages."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        if (!kokoro->removeModels(&error)) {
            QMessageBox::warning(this, QStringLiteral("Could not remove model"), error);
            return;
        }
        if (voiceDownloadStatusLabel_)
            voiceDownloadStatusLabel_->setText(QStringLiteral("Optional Kokoro model files removed."));
        updateVoiceModelStatus();
        refreshVoiceChoices();
    });

    auto *folderRow = new QHBoxLayout;
    auto *openFolder = makeButton(QStringLiteral("OPEN MODEL FOLDER"), QStringLiteral("secondaryButton"));
    auto *openRelease = makeButton(QStringLiteral("OFFICIAL MODEL RELEASE"), QStringLiteral("secondaryButton"));
    auto *openRuntimeDocs = makeButton(QStringLiteral("KOKORO RUNTIME SETUP"), QStringLiteral("secondaryButton"));
    folderRow->addWidget(openFolder);
    folderRow->addWidget(openRelease);
    folderRow->addWidget(openRuntimeDocs);
    folderRow->addStretch(1);
    body->addLayout(folderRow);
    connect(openFolder, &QPushButton::clicked, this, [this, kokoro] {
        QDir().mkpath(kokoro->modelDirectory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(kokoro->modelDirectory()));
    });
    connect(openRelease, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.1")));
    });
    connect(openRuntimeDocs, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/thewh1teagle/kokoro-onnx#setup")));
    });

    QVBoxLayout *runtimeBody = nullptr;
    QWidget *runtimePanel = makePanel(QStringLiteral("LOCAL RUNTIME"), &runtimeBody);
    auto *runtimeHint = makeLabel(QStringLiteral("The voice engine runs locally through Python 3.10–3.13. Install the lightweight runtime package with the command below, then refresh status. No speech audio is sent over the network."), QStringLiteral("muted"));
    runtimeHint->setWordWrap(true);
    runtimeBody->addWidget(runtimeHint);
    auto *pythonRow = new QHBoxLayout;
    voicePythonEdit_ = new QLineEdit(kokoro->pythonExecutable());
    voicePythonEdit_->setPlaceholderText(QStringLiteral("python or full path to python.exe"));
    auto *browsePython = makeButton(QStringLiteral("BROWSE"), QStringLiteral("secondaryButton"));
    auto *applyPython = makeButton(QStringLiteral("CHECK PYTHON"), QStringLiteral("secondaryButton"));
    pythonRow->addWidget(voicePythonEdit_, 1);
    pythonRow->addWidget(browsePython);
    pythonRow->addWidget(applyPython);
    runtimeBody->addLayout(pythonRow);
    voiceInstallCommand_ = new QLineEdit(kokoro->pythonInstallCommand());
    voiceInstallCommand_->setReadOnly(true);
    voiceInstallCommand_->setToolTip(QStringLiteral("This command targets the exact Python interpreter selected above."));
    runtimeBody->addWidget(voiceInstallCommand_);
    auto *runtimeActions = new QHBoxLayout;
    voiceCopyInstallCommandButton_ = makeButton(QStringLiteral("COPY INSTALL COMMAND"), QStringLiteral("secondaryButton"));
    runtimeActions->addWidget(voiceCopyInstallCommandButton_);
    runtimeActions->addStretch(1);
    runtimeBody->addLayout(runtimeActions);
    connect(browsePython, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select Python 3.10–3.13"),
                                                          voicePythonEdit_->text(),
                                                          QStringLiteral("Python executable (python.exe);;All files (*.*)"));
        if (!path.isEmpty())
            voicePythonEdit_->setText(path);
    });
    connect(applyPython, &QPushButton::clicked, this, [this, kokoro] {
        kokoro->setPythonExecutable(voicePythonEdit_->text());
        if (voiceRuntimeStatusLabel_)
            voiceRuntimeStatusLabel_->setText(QStringLiteral("Checking selected Python and Kokoro package…"));
    });
    connect(voicePythonEdit_, &QLineEdit::editingFinished, this, [this, kokoro] {
        kokoro->setPythonExecutable(voicePythonEdit_->text());
    });
    connect(voiceCopyInstallCommandButton_, &QPushButton::clicked, this, [this] {
        if (!voiceInstallCommand_ || voiceInstallCommand_->text().isEmpty())
            return;
        QGuiApplication::clipboard()->setText(voiceInstallCommand_->text());
        statusBar()->showMessage(QStringLiteral("Kokoro installation command copied."), 4000);
    });

    auto *voicesNote = makeLabel(QStringLiteral("Choose a voice on the Voice settings page and use its preview sentence before making it your default. Voice characteristics vary by listener; JARVIS does not claim to reproduce any film actor."), QStringLiteral("muted"));
    voicesNote->setWordWrap(true);
    runtimeBody->addWidget(voicesNote);
    layout->addWidget(panel);
    layout->addWidget(runtimePanel);
    layout->addStretch(1);
    updateVoiceModelStatus();
    return page;
}

QWidget *MainWindow::buildAiPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Reasoning / provider status"),
                                     QStringLiteral("AI model settings"),
                                     QStringLiteral("Connect a local Ollama model or an OpenAI-compatible endpoint. Prompts go only to the provider selected here.")));
    QVBoxLayout *body = nullptr;
    QWidget *panel = makePanel(QStringLiteral("AI PROVIDER"), &body);
    auto *form = new QFormLayout;
    providerCombo_ = new QComboBox;
    providerCombo_->addItem(QStringLiteral("Ollama (local)"), QStringLiteral("ollama"));
    providerCombo_->addItem(QStringLiteral("OpenAI-compatible"), QStringLiteral("openai-compatible"));
    endpointEdit_ = new QLineEdit;
    endpointEdit_->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434"));
    modelCombo_ = new QComboBox;
    modelCombo_->setEditable(true);
    modelCombo_->setInsertPolicy(QComboBox::NoInsert);
    modelCombo_->setPlaceholderText(QStringLiteral("Load models or enter a model name"));
    apiKeyEdit_ = new QLineEdit;
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(QStringLiteral("Optional for local endpoints; required by many hosted providers"));
    apiKeyEdit_->setText(qEnvironmentVariable("JARVIS_OPENAI_API_KEY"));
    apiKeyLabel_ = makeLabel(QStringLiteral("API key"), QStringLiteral("muted"));
    systemPromptEdit_ = new QPlainTextEdit;
    systemPromptEdit_->setMaximumHeight(110);
    systemPromptEdit_->setPlaceholderText(QStringLiteral("Instructions that shape JARVIS's responses"));
    aiToolsCheck_ = new QCheckBox(QStringLiteral("Allow structured AI tool calls for allowlisted apps and websites"));
    aiToolsCheck_->setObjectName(QStringLiteral("aiToolsCheck"));
    aiToolsCheck_->setChecked(QSettings().value(QStringLiteral("ai/toolsEnabled"), true).toBool());
    temperatureSpin_ = new QDoubleSpinBox;
    temperatureSpin_->setRange(0.0, 2.0);
    temperatureSpin_->setSingleStep(0.1);
    temperatureSpin_->setDecimals(1);
    temperatureSpin_->setValue(QSettings().value(QStringLiteral("ai/generation/temperature"), 0.7).toDouble());
    temperatureSpin_->setToolTip(QStringLiteral("Lower values tend to be more predictable; higher values allow more variation."));
    maxTokensSpin_ = new QSpinBox;
    maxTokensSpin_->setRange(64, 8192);
    maxTokensSpin_->setSingleStep(128);
    maxTokensSpin_->setValue(QSettings().value(QStringLiteral("ai/generation/maxTokens"), 1024).toInt());
    contextLengthSpin_ = new QSpinBox;
    contextLengthSpin_->setRange(512, 32768);
    contextLengthSpin_->setSingleStep(512);
    contextLengthSpin_->setValue(QSettings().value(QStringLiteral("ai/generation/contextLength"), 4096).toInt());
    contextLengthSpin_->setToolTip(QStringLiteral("Applied to local Ollama requests. Hosted providers set their own context window."));
    requestTimeoutSpin_ = new QSpinBox;
    requestTimeoutSpin_->setRange(5, 300);
    requestTimeoutSpin_->setSuffix(QStringLiteral(" seconds"));
    requestTimeoutSpin_->setValue(QSettings().value(QStringLiteral("ai/generation/timeoutSeconds"), 90).toInt());
    streamingCheck_ = new QCheckBox(QStringLiteral("Stream response as it is generated"));
    streamingCheck_->setChecked(QSettings().value(QStringLiteral("ai/generation/streaming"), true).toBool());
    form->addRow(QStringLiteral("Provider"), providerCombo_);
    form->addRow(QStringLiteral("Base URL"), endpointEdit_);
    form->addRow(QStringLiteral("Model"), modelCombo_);
    form->addRow(apiKeyLabel_, apiKeyEdit_);
    form->addRow(QStringLiteral("System instructions"), systemPromptEdit_);
    form->addRow(QStringLiteral("Local tools"), aiToolsCheck_);
    form->addRow(QStringLiteral("Creativity"), temperatureSpin_);
    form->addRow(QStringLiteral("Maximum response tokens"), maxTokensSpin_);
    form->addRow(QStringLiteral("Local context length"), contextLengthSpin_);
    form->addRow(QStringLiteral("Request timeout"), requestTimeoutSpin_);
    form->addRow(QStringLiteral("Streaming"), streamingCheck_);
    body->addLayout(form);

    providerHint_ = makeLabel(QStringLiteral("Local requests stay on this PC. The API key field is session-only; it is never saved to settings."), QStringLiteral("muted"));
    providerHint_->setWordWrap(true);
    body->addWidget(providerHint_);
    aiDetails_ = makeLabel(QStringLiteral("Checking provider connection…"), QStringLiteral("sectionTitle"));
    aiDetails_->setWordWrap(true);
    aiModels_ = makeLabel(QStringLiteral("Model list pending."), QStringLiteral("muted"));
    aiModels_->setWordWrap(true);
    body->addWidget(aiDetails_);
    body->addWidget(aiModels_);
    auto *buttonRow = new QHBoxLayout;
    auto *refresh = makeButton(QStringLiteral("LOAD AVAILABLE MODELS"), QStringLiteral("secondaryButton"));
    buttonRow->addWidget(refresh, 0, Qt::AlignLeft);
    buttonRow->addStretch(1);
    body->addLayout(buttonRow);
    connect(refresh, &QPushButton::clicked, this, &MainWindow::loadProviderModels);
    connect(providerCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { providerChanged(); });
    connect(endpointEdit_, &QLineEdit::editingFinished, this, [this] { saveProviderSettings(); });
    connect(modelCombo_->lineEdit(), &QLineEdit::editingFinished, this, [this] { saveProviderSettings(); });
    connect(modelCombo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        saveProviderSettings();
        updateOllamaModelManager();
        updateAiStatus();
    });
    connect(systemPromptEdit_, &QPlainTextEdit::textChanged, this, [this] { saveProviderSettings(); });
    connect(aiToolsCheck_, &QCheckBox::toggled, this, [this](bool) { saveProviderSettings(); });
    connect(temperatureSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { saveProviderSettings(); });
    connect(maxTokensSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { saveProviderSettings(); });
    connect(contextLengthSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { saveProviderSettings(); });
    connect(requestTimeoutSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { saveProviderSettings(); });
    connect(streamingCheck_, &QCheckBox::toggled, this, [this](bool) { saveProviderSettings(); });
    auto *storage = makeLabel(QStringLiteral("Conversation context and history are stored locally in this Windows account. The last 20 turns are sent as context; the API key is never included in history or logs."), QStringLiteral("muted"));
    storage->setWordWrap(true);
    body->addWidget(storage);
    body->addStretch(1);
    layout->addWidget(panel);

    QVBoxLayout *libraryBody = nullptr;
    QWidget *libraryPanel = makePanel(QStringLiteral("LOCAL OLLAMA MODEL LIBRARY"), &libraryBody);
    auto *recommendations = makeLabel(QStringLiteral("Recommendations for this PC (24 GB system RAM, GTX 1660 SUPER with 6 GB VRAM): qwen3.5:2b-q4_K_M is the lighter starting point (about 1.9 GB); qwen3.5:4b-q4_K_M is stronger but uses about 3.3 GB before runtime and context memory. Actual memory use grows with context length. These are recommendations only; the inventory below comes from Ollama."), QStringLiteral("muted"));
    recommendations->setWordWrap(true);
    libraryBody->addWidget(recommendations);
    ollamaInventoryLabel_ = makeLabel(QStringLiteral("Installed Ollama models have not been loaded yet."), QStringLiteral("sectionTitle"));
    ollamaInventoryLabel_->setWordWrap(true);
    libraryBody->addWidget(ollamaInventoryLabel_);

    auto *pullRow = new QHBoxLayout;
    ollamaModelNameEdit_ = new QLineEdit(QSettings().value(QStringLiteral("ai/ollama/modelToPull"),
                                                           QStringLiteral("qwen3.5:2b-q4_K_M")).toString());
    ollamaModelNameEdit_->setPlaceholderText(QStringLiteral("Ollama model tag, for example qwen3.5:2b-q4_K_M"));
    ollamaPullButton_ = makeButton(QStringLiteral("PULL MODEL"), QStringLiteral("primaryButton"));
    ollamaDeleteButton_ = makeButton(QStringLiteral("DELETE SELECTED"), QStringLiteral("secondaryButton"));
    ollamaCancelButton_ = makeButton(QStringLiteral("CANCEL PULL"), QStringLiteral("secondaryButton"));
    ollamaCancelButton_->setEnabled(false);
    pullRow->addWidget(ollamaModelNameEdit_, 1);
    pullRow->addWidget(ollamaPullButton_);
    pullRow->addWidget(ollamaDeleteButton_);
    pullRow->addWidget(ollamaCancelButton_);
    libraryBody->addLayout(pullRow);
    ollamaModelProgress_ = new QProgressBar;
    ollamaModelProgress_->setRange(0, 1000);
    ollamaModelProgress_->setValue(0);
    ollamaModelProgress_->setTextVisible(false);
    libraryBody->addWidget(ollamaModelProgress_);
    ollamaActionStatusLabel_ = makeLabel(QStringLiteral("Model pull and deletion require your confirmation. Ollama's model registry may download several gigabytes."), QStringLiteral("metricDetail"));
    ollamaActionStatusLabel_->setWordWrap(true);
    libraryBody->addWidget(ollamaActionStatusLabel_);

    connect(ollamaPullButton_, &QPushButton::clicked, this, [this] {
        if (activeProviderId() != QStringLiteral("ollama")) {
            statusBar()->showMessage(QStringLiteral("Select Ollama (local) before pulling a local model."), 6000);
            return;
        }
        const QString name = ollamaModelNameEdit_->text().trimmed();
        if (name.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Enter an Ollama model tag first."), 6000);
            return;
        }
        if (QMessageBox::warning(this, QStringLiteral("Download an Ollama model"),
                                 QStringLiteral("Pull '%1' from Ollama's model registry to this PC?\n\n"
                                                "The download size depends on the model and can be several gigabytes. Check available disk space and your connection before continuing. Ollama will report actual progress; JARVIS will not estimate it.")
                                     .arg(name),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QSettings().setValue(QStringLiteral("ai/ollama/modelToPull"), name);
        ollamaActionStatusLabel_->setText(QStringLiteral("Connecting to the local Ollama service…"));
        ollamaModelProgress_->setRange(0, 0);
        ollamaProvider_->pullModel(endpointEdit_->text(), name);
        updateOllamaModelManager();
    });
    connect(ollamaDeleteButton_, &QPushButton::clicked, this, [this] {
        if (activeProviderId() != QStringLiteral("ollama"))
            return;
        const QString name = modelCombo_->currentText().trimmed();
        if (!providerModels_.contains(name)) {
            statusBar()->showMessage(QStringLiteral("Choose a model returned by the local Ollama service."), 6000);
            return;
        }
        if (QMessageBox::warning(this, QStringLiteral("Delete a local Ollama model"),
                                 QStringLiteral("Delete the installed model '%1' from this PC? This frees its local model files; you can pull it again later.")
                                     .arg(name),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        ollamaActionStatusLabel_->setText(QStringLiteral("Deleting %1 from the local Ollama library…").arg(name));
        ollamaModelProgress_->setRange(0, 0);
        ollamaProvider_->deleteModel(endpointEdit_->text(), name);
        updateOllamaModelManager();
    });
    connect(ollamaCancelButton_, &QPushButton::clicked, this, [this] {
        ollamaProvider_->cancelModelAction();
    });
    connect(ollamaModelNameEdit_, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(QStringLiteral("ai/ollama/modelToPull"), ollamaModelNameEdit_->text().trimmed());
    });
    layout->addWidget(libraryPanel);
    updateOllamaModelManager();
    layout->addStretch(1);

    const QSettings settings;
    const QString savedProvider = settings.value(QStringLiteral("ai/provider"), QStringLiteral("ollama")).toString();
    const int providerIndex = providerCombo_->findData(savedProvider);
    if (providerIndex >= 0)
        providerCombo_->setCurrentIndex(providerIndex);
    lastProviderId_ = providerCombo_->currentData().toString();
    endpointEdit_->setText(settings.value(QStringLiteral("ai/%1/endpoint").arg(lastProviderId_),
                                          lastProviderId_ == QStringLiteral("ollama")
                                              ? QStringLiteral("http://127.0.0.1:11434")
                                              : QStringLiteral("https://api.openai.com/v1")).toString());
    const QString savedModel = settings.value(QStringLiteral("ai/%1/model").arg(lastProviderId_)).toString();
    if (!savedModel.isEmpty())
        modelCombo_->setEditText(savedModel);
    systemPromptEdit_->setPlainText(settings.value(QStringLiteral("ai/systemPrompt"),
                                                   QStringLiteral("You are JARVIS, a calm and capable personal assistant. Be concise by default, use British English, and be honest about uncertainty and actions you have not performed. When the user directly asks you to launch an application or open a website, use the matching available tool and report only the result it returns. Never claim that a tool action succeeded before you receive its result. Ask before opening websites; only launch applications the user explicitly requested.")).toString());
    const bool openAI = lastProviderId_ == QStringLiteral("openai-compatible");
    contextLengthSpin_->setEnabled(!openAI);
    contextLengthSpin_->setToolTip(openAI
                                       ? QStringLiteral("This endpoint controls its own context length; the local Ollama context setting does not apply.")
                                       : QStringLiteral("Applied to local Ollama requests as num_ctx."));
    apiKeyEdit_->setEnabled(openAI);
    apiKeyEdit_->setVisible(openAI);
    apiKeyLabel_->setVisible(openAI);
    updateAiStatus();
    return page;
}

QWidget *MainWindow::buildPersonalityPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Preferences / user control"),
                                     QStringLiteral("Personality & memory"),
                                     QStringLiteral("Edit personal context explicitly. JARVIS never saves facts from conversation automatically.")));
    QVBoxLayout *body = nullptr;
    QWidget *panel = makePanel(QStringLiteral("USER-MANAGED LOCAL MEMORY"), &body);
    auto *privacyNote = makeLabel(QStringLiteral("Memory is stored in this account's application settings. When enabled, these notes are included in prompts sent to the selected provider, including hosted services. Do not store passwords, API keys, or private credentials."), QStringLiteral("muted"));
    privacyNote->setWordWrap(true);
    body->addWidget(privacyNote);
    memoryEnabledCheck_ = new QCheckBox(QStringLiteral("Use saved memory as context for new conversations"));
    memoryEnabledCheck_->setObjectName(QStringLiteral("memoryEnabledCheck"));
    memoryEnabledCheck_->setChecked(QSettings().value(QStringLiteral("memory/enabled"), false).toBool());
    body->addWidget(memoryEnabledCheck_);
    memoryEdit_ = new QPlainTextEdit;
    memoryEdit_->setObjectName(QStringLiteral("memoryEdit"));
    memoryEdit_->setPlaceholderText(QStringLiteral("One preference or fact per line. Example: I prefer concise answers and British spelling."));
    memoryEdit_->setMinimumHeight(190);
    memoryEdit_->setPlainText(QSettings().value(QStringLiteral("memory/text")).toString());
    body->addWidget(memoryEdit_);
    auto *memoryStatus = makeLabel(QStringLiteral("Memory is opt-in and is not added to requests until enabled."), QStringLiteral("metricDetail"));
    memoryStatus->setObjectName(QStringLiteral("memoryStatusLabel"));
    memoryStatus->setText(memoryEnabledCheck_->isChecked()
                              ? QStringLiteral("Saved memory will be included with new requests to the selected provider.")
                              : QStringLiteral("Saved locally; excluded from requests while disabled."));
    body->addWidget(memoryStatus);
    auto *saveMemoryButton = makeButton(QStringLiteral("SAVE MEMORY"), QStringLiteral("primaryButton"));
    auto *clearMemoryButton = makeButton(QStringLiteral("DELETE ALL MEMORY"), QStringLiteral("secondaryButton"));
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(saveMemoryButton);
    buttonRow->addWidget(clearMemoryButton);
    buttonRow->addStretch(1);
    body->addLayout(buttonRow);
    connect(memoryEnabledCheck_, &QCheckBox::toggled, this, [memoryStatus](bool enabled) {
        QSettings().setValue(QStringLiteral("memory/enabled"), enabled);
        memoryStatus->setText(enabled
                                   ? QStringLiteral("Saved memory will be included with new requests to the selected provider.")
                                   : QStringLiteral("Saved locally; excluded from requests while disabled."));
    });
    connect(saveMemoryButton, &QPushButton::clicked, this, [this, memoryStatus] {
        QStringList items;
        for (const QString &line : memoryEdit_->toPlainText().split(QLatin1Char('\n'))) {
            const QString item = line.trimmed();
            if (!item.isEmpty())
                items.append(item);
        }
        const QString text = items.join(QLatin1Char('\n'));
        if (text.size() > 4096) {
            memoryStatus->setText(QStringLiteral("Memory is limited to 4,096 characters."));
            return;
        }
        static const QRegularExpression credentialPattern(
            QStringLiteral("(?i)\\b(?:api[_ -]?key|password|token|secret|credential)\\s*[:=]\\s*\\S+"));
        if (credentialPattern.match(text).hasMatch()) {
            memoryStatus->setText(QStringLiteral("Remove credential-like values before saving memory."));
            return;
        }
        QSettings().setValue(QStringLiteral("memory/text"), text);
        memoryEdit_->setPlainText(text);
        memoryStatus->setText(text.isEmpty() ? QStringLiteral("Saved memory is empty.")
                                             : QStringLiteral("Saved %1 user-managed memory item(s) locally.").arg(items.size()));
        statusBar()->showMessage(QStringLiteral("User-managed memory saved locally."), 5000);
        logEvent(QStringLiteral("User-managed local memory was saved."));
    });
    connect(clearMemoryButton, &QPushButton::clicked, this, [this, memoryStatus] {
        if (QMessageBox::question(this, QStringLiteral("Delete saved memory"),
                                  QStringLiteral("Delete all locally saved memory items?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QSettings().remove(QStringLiteral("memory/text"));
        memoryEdit_->clear();
        memoryStatus->setText(QStringLiteral("All saved memory was deleted."));
        logEvent(QStringLiteral("User-managed local memory was deleted."));
    });
    layout->addWidget(panel);
    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::buildDiagnosticsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(26, 16, 26, 28);
    layout->setSpacing(18);
    layout->addWidget(makePageHeader(QStringLiteral("Application / local events"),
                                     QStringLiteral("Logs & diagnostics"),
                                     QStringLiteral("Startup, provider checks and local UI actions are logged. Message text is excluded.")));
    QVBoxLayout *body = nullptr;
    QWidget *panel = makePanel(QStringLiteral("APPLICATION EVENTS"), &body);
    logViewer_ = new QPlainTextEdit;
    logViewer_->setReadOnly(true);
    logViewer_->setMinimumHeight(330);
    for (const QString &line : logLines_)
        logViewer_->appendPlainText(line);
    body->addWidget(logViewer_, 1);
    auto *openFolder = makeButton(QStringLiteral("OPEN LOG FOLDER"), QStringLiteral("secondaryButton"));
    connect(openFolder, &QPushButton::clicked, this, &MainWindow::openLogFolder);
    body->addWidget(openFolder, 0, Qt::AlignLeft);
    layout->addWidget(panel, 1);
    return page;
}

QTextBrowser *MainWindow::makeTranscript()
{
    auto *view = new QTextBrowser;
    view->setObjectName(QStringLiteral("transcript"));
    view->setOpenExternalLinks(false);
    view->setMinimumHeight(150);
    transcripts_.append(view);
    return view;
}

void MainWindow::appendMessage(const QString &speaker, const QString &message, bool userMessage)
{
    if (userMessage && !conversationWelcomeViews_.isEmpty()) {
        for (QTextBrowser *view : conversationWelcomeViews_)
            view->clear();
        conversationWelcomeViews_.clear();
    }
    for (QTextBrowser *view : transcripts_)
        appendMessageToView(view, speaker, message, userMessage);
}

void MainWindow::appendMessageToView(QTextBrowser *view, const QString &speaker,
                                     const QString &message, bool userMessage)
{
    if (!view)
        return;
    const QString color = userMessage ? QStringLiteral("#8edff0") : QStringLiteral("#77bfd1");
    const QString safeSpeaker = speaker.toHtmlEscaped();
    const QString safeMessage = message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    const QString html = QStringLiteral("<p style='margin:5px 0 12px 0; line-height:1.5;'>"
                                        "<span style='color:%1; font-size:9pt; font-weight:700; letter-spacing:1px;'>%2</span><br>"
                                        "<span style='color:#c7dce4;'>%3</span></p>")
                             .arg(color, safeSpeaker, safeMessage);
    view->append(html);
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
}

void MainWindow::connectComposer(QLineEdit *input)
{
    connect(input, &QLineEdit::returnPressed, this, [this, input] {
        if (submitText(input->text()))
            input->clear();
    });
}

bool MainWindow::submitText(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || chatRequestActive_)
        return false;

    QString configError;
    if (!validateProviderConfiguration(&configError)) {
        statusBar()->showMessage(configError, 7000);
        logEvent(QStringLiteral("A message was not sent because provider settings are incomplete."));
        return false;
    }

    if (voiceController_ && voiceController_->isSpeaking())
        voiceController_->stopSpeaking();

    appendMessage(QStringLiteral("YOU"), prompt, true);
    saveProviderSettings();
    pendingUserMessage_ = prompt;
    streamingAssistantText_.clear();
    chatRequestActive_ = true;
    if (cancelChatButton_)
        cancelChatButton_->setEnabled(true);
    replaceStreamingMessage(QString());

    activeRequestMessages_ = QJsonArray();
    activeTurnMessages_ = QJsonArray();
    toolCallRounds_ = 0;
    toolCallsExecuted_ = 0;
    QString systemPrompt = systemPromptEdit_ ? systemPromptEdit_->toPlainText().trimmed() : QString();
    const QSettings memorySettings;
    const QString userMemory = memorySettings.value(QStringLiteral("memory/text")).toString().trimmed();
    if (memorySettings.value(QStringLiteral("memory/enabled"), false).toBool() && !userMemory.isEmpty()) {
        if (!systemPrompt.isEmpty())
            systemPrompt += QStringLiteral("\n\n");
        systemPrompt += QStringLiteral("User-managed saved memories (use as personal context only; these may be out of date):\n%1")
                            .arg(userMemory.left(4096));
    }
    if (!systemPrompt.isEmpty())
        activeRequestMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                                  {QStringLiteral("content"), systemPrompt}});
    const int firstContextMessage = qMax(0, conversationMessages_.size() - 40);
    for (int index = firstContextMessage; index < conversationMessages_.size(); ++index)
        activeRequestMessages_.append(conversationMessages_.at(index));
    const QJsonObject userMessage{{QStringLiteral("role"), QStringLiteral("user")},
                                  {QStringLiteral("content"), prompt}};
    activeRequestMessages_.append(userMessage);
    activeTurnMessages_.append(userMessage);
    activeProvider()->streamChat(endpointEdit_->text(), activeApiKey(), modelCombo_->currentText(),
                                 activeRequestMessages_, activeToolDefinitions(), activeGenerationOptions(),
                                 streamingCheck_ ? streamingCheck_->isChecked() : true,
                                 activeRequestTimeoutMs());
    statusBar()->showMessage(QStringLiteral("Sending to %1…").arg(activeProvider()->displayName()));
    return true;
}

QJsonArray MainWindow::activeToolDefinitions() const
{
    if (!aiToolsCheck_ || !aiToolsCheck_->isChecked() || !toolRegistry_)
        return {};
    return toolRegistry_->aiToolDefinitions();
}

QJsonObject MainWindow::activeGenerationOptions() const
{
    QJsonObject options;
    if (temperatureSpin_)
        options.insert(QStringLiteral("temperature"), temperatureSpin_->value());
    if (maxTokensSpin_)
        options.insert(QStringLiteral("maxTokens"), maxTokensSpin_->value());
    if (activeProviderId() == QStringLiteral("ollama") && contextLengthSpin_)
        options.insert(QStringLiteral("contextLength"), contextLengthSpin_->value());
    return options;
}

int MainWindow::activeRequestTimeoutMs() const
{
    return requestTimeoutSpin_ ? requestTimeoutSpin_->value() * 1000 : 90000;
}

void MainWindow::executeToolCalls(const QVector<AIProvider::ToolCall> &calls)
{
    if (calls.isEmpty())
        return;
    if (toolCallRounds_ >= 3 || toolCallsExecuted_ + calls.size() > 4) {
        streamingAssistantText_ = QStringLiteral("I stopped before running more tools because this request reached JARVIS's tool-use limit.");
        replaceStreamingMessage(streamingAssistantText_);
        logEvent(QStringLiteral("AI tool calls were stopped at the per-turn safety limit."));
        finishChatRequest();
        return;
    }

    AIProvider *provider = activeProvider();
    if (!provider)
        return;
    const bool ollama = provider->id() == QStringLiteral("ollama");
    QJsonArray serializedCalls;
    QVector<AIProvider::ToolCall> callsWithIds = calls;
    int fallbackId = 1;
    for (AIProvider::ToolCall &call : callsWithIds) {
        if (call.id.isEmpty())
            call.id = QStringLiteral("jarvis-call-%1").arg(fallbackId++);
        if (ollama) {
            serializedCalls.append(QJsonObject{{QStringLiteral("function"),
                                               QJsonObject{{QStringLiteral("name"), call.name},
                                                           {QStringLiteral("arguments"), call.arguments}}}});
        } else {
            serializedCalls.append(QJsonObject{{QStringLiteral("id"), call.id},
                                               {QStringLiteral("type"), QStringLiteral("function")},
                                               {QStringLiteral("function"),
                                                QJsonObject{{QStringLiteral("name"), call.name},
                                                            {QStringLiteral("arguments"),
                                                             QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))}}}});
        }
    }
    const QJsonObject assistantToolMessage{{QStringLiteral("role"), QStringLiteral("assistant")},
                                           {QStringLiteral("content"), streamingAssistantText_},
                                           {QStringLiteral("tool_calls"), serializedCalls}};
    activeRequestMessages_.append(assistantToolMessage);
    activeTurnMessages_.append(assistantToolMessage);

    for (const AIProvider::ToolCall &call : callsWithIds) {
        QString details;
        bool succeeded = false;
        if (toolRegistry_->aiToolRequiresConfirmation(call.name)) {
            QMessageBox confirmation(this);
            confirmation.setIcon(QMessageBox::Question);
            confirmation.setWindowTitle(QStringLiteral("Confirm JARVIS website action"));
            confirmation.setTextFormat(Qt::PlainText);
            confirmation.setText(QStringLiteral("JARVIS wants to open this website in your default browser:\n\n%1\n\nContinue?")
                                     .arg(call.arguments.value(QStringLiteral("url")).toString()));
            confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            confirmation.setDefaultButton(QMessageBox::No);
            if (confirmation.exec() == QMessageBox::Yes)
                succeeded = toolRegistry_->executeAiTool(call.name, call.arguments, &details);
            else
                details = QStringLiteral("The user declined; the browser was not opened.");
        } else {
            succeeded = toolRegistry_->executeAiTool(call.name, call.arguments, &details);
        }
        if (details.isEmpty())
            details = succeeded ? QStringLiteral("Completed.") : QStringLiteral("The requested action did not complete.");
        const QJsonObject toolMessage = ollama
                                            ? QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                                          {QStringLiteral("content"), details},
                                                          {QStringLiteral("tool_name"), call.name}}
                                            : QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                                          {QStringLiteral("content"), details},
                                                          {QStringLiteral("tool_call_id"), call.id}};
        activeRequestMessages_.append(toolMessage);
        activeTurnMessages_.append(toolMessage);
        ++toolCallsExecuted_;
        statusBar()->showMessage(QStringLiteral("%1 %2.")
                                     .arg(call.name, succeeded ? QStringLiteral("completed")
                                                              : QStringLiteral("did not complete")), 6000);
        logEvent(QStringLiteral("AI tool %1 %2.")
                     .arg(call.name, succeeded ? QStringLiteral("completed") : QStringLiteral("failed or was declined")));
    }

    ++toolCallRounds_;
    streamingAssistantText_.clear();
    replaceStreamingMessage(QString());
    provider->streamChat(endpointEdit_->text(), activeApiKey(), modelCombo_->currentText(),
                         activeRequestMessages_, activeToolDefinitions(), activeGenerationOptions(),
                         streamingCheck_ ? streamingCheck_->isChecked() : true,
                         activeRequestTimeoutMs());
    statusBar()->showMessage(QStringLiteral("Waiting for JARVIS to report the verified tool results…"), 7000);
}

void MainWindow::showVoiceNotice()
{
    if (voiceController_->isListening()) {
        voiceController_->stopListening();
        return;
    }
    if (chatRequestActive_) {
        statusBar()->showMessage(QStringLiteral("Wait until the current AI reply is ready before opening the microphone."), 6000);
        return;
    }
    interruptListeningDuringSpeech_ = voiceController_->isSpeaking();
    voiceController_->startListening();
    statusBar()->showMessage(interruptListeningDuringSpeech_
                                 ? QStringLiteral("Listening for 'stop talking' to interrupt speech.")
                                 : QStringLiteral("Starting local voice commands…"), 5000);
    logEvent(interruptListeningDuringSpeech_
                 ? QStringLiteral("Voice interrupt listening started while speech output was active.")
                 : QStringLiteral("Local speech recognition started from a voice command control."));
}

void MainWindow::loadProviderModels()
{
    if (chatRequestActive_ || modelFetchRunning_ || !activeProvider())
        return;
    QString configError;
    if (endpointEdit_ && endpointEdit_->text().trimmed().isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Enter a provider URL first."), 6000);
        return;
    }
    if (!validateProviderConfiguration(&configError, false)) {
        statusBar()->showMessage(configError, 7000);
        return;
    }
    saveProviderSettings();
    modelFetchRunning_ = true;
    providerConnected_ = false;
    providerError_.clear();
    updateAiStatus();
    activeProvider()->fetchModels(endpointEdit_->text(), activeApiKey());
}

void MainWindow::saveProviderSettings(const QString &providerId)
{
    const QString key = providerId.isEmpty() ? activeProviderId() : providerId;
    if (key.isEmpty() || !endpointEdit_)
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("ai/provider"), activeProviderId());
    settings.setValue(QStringLiteral("ai/%1/endpoint").arg(key), endpointEdit_->text().trimmed());
    if (modelCombo_)
        settings.setValue(QStringLiteral("ai/%1/model").arg(key), modelCombo_->currentText().trimmed());
    if (systemPromptEdit_)
        settings.setValue(QStringLiteral("ai/systemPrompt"), systemPromptEdit_->toPlainText());
    if (aiToolsCheck_)
        settings.setValue(QStringLiteral("ai/toolsEnabled"), aiToolsCheck_->isChecked());
    if (temperatureSpin_)
        settings.setValue(QStringLiteral("ai/generation/temperature"), temperatureSpin_->value());
    if (maxTokensSpin_)
        settings.setValue(QStringLiteral("ai/generation/maxTokens"), maxTokensSpin_->value());
    if (contextLengthSpin_)
        settings.setValue(QStringLiteral("ai/generation/contextLength"), contextLengthSpin_->value());
    if (requestTimeoutSpin_)
        settings.setValue(QStringLiteral("ai/generation/timeoutSeconds"), requestTimeoutSpin_->value());
    if (streamingCheck_)
        settings.setValue(QStringLiteral("ai/generation/streaming"), streamingCheck_->isChecked());
}

void MainWindow::providerChanged()
{
    if (!providerCombo_ || !endpointEdit_ || !modelCombo_)
        return;
    const QString selectedId = activeProviderId();
    if (selectedId == lastProviderId_)
        return;
    AIProvider *previousProvider = lastProviderId_ == QStringLiteral("openai-compatible")
                                       ? static_cast<AIProvider *>(openAIProvider_)
                                       : static_cast<AIProvider *>(ollamaProvider_);
    if (lastProviderId_ == QStringLiteral("ollama") && ollamaProvider_->modelActionBusy())
        ollamaProvider_->cancelModelAction();
    if (previousProvider->isBusy())
        previousProvider->cancel();
    if (chatRequestActive_) {
        chatRequestActive_ = false;
        appendMessage(QStringLiteral("JARVIS"), QStringLiteral("Response cancelled when provider changed."), false);
        for (QLabel *label : streamingLabels_)
            label->hide();
        if (cancelChatButton_)
            cancelChatButton_->setEnabled(false);
    }
    saveProviderSettings(lastProviderId_);
    lastProviderId_ = selectedId;
    const QString defaultEndpoint = selectedId == QStringLiteral("ollama")
                                       ? QStringLiteral("http://127.0.0.1:11434")
                                       : QStringLiteral("https://api.openai.com/v1");
    const QSettings settings;
    endpointEdit_->setText(settings.value(QStringLiteral("ai/%1/endpoint").arg(selectedId), defaultEndpoint).toString());
    modelCombo_->clear();
    const QString savedModel = settings.value(QStringLiteral("ai/%1/model").arg(selectedId)).toString();
    if (!savedModel.isEmpty())
        modelCombo_->setEditText(savedModel);
    const bool openAI = selectedId == QStringLiteral("openai-compatible");
    if (contextLengthSpin_) {
        contextLengthSpin_->setEnabled(!openAI);
        contextLengthSpin_->setToolTip(openAI
                                           ? QStringLiteral("This endpoint controls its own context length; the local Ollama context setting does not apply.")
                                           : QStringLiteral("Applied to local Ollama requests as num_ctx."));
    }
    apiKeyEdit_->setEnabled(openAI);
    apiKeyEdit_->setVisible(openAI);
    if (apiKeyLabel_)
        apiKeyLabel_->setVisible(openAI);
    if (providerHint_)
        providerHint_->setText(openAI
                                   ? QStringLiteral("Hosted services may charge for use. The API key is used only in this session and is never saved. Use HTTPS when sending a key.")
                                   : QStringLiteral("Ollama requests stay on this PC. No API key is sent or stored."));
    providerModels_.clear();
    providerConnected_ = false;
    providerError_.clear();
    modelFetchRunning_ = false;
    updateAiStatus();
    updateOllamaModelManager();
}

AIProvider *MainWindow::activeProvider() const
{
    return activeProviderId() == QStringLiteral("openai-compatible")
               ? static_cast<AIProvider *>(openAIProvider_)
               : static_cast<AIProvider *>(ollamaProvider_);
}

QString MainWindow::activeProviderId() const
{
    return providerCombo_ ? providerCombo_->currentData().toString() : QStringLiteral("ollama");
}

QString MainWindow::activeApiKey() const
{
    return activeProviderId() == QStringLiteral("openai-compatible") && apiKeyEdit_
               ? apiKeyEdit_->text().trimmed()
               : QString();
}

bool MainWindow::validateProviderConfiguration(QString *error, bool requireModel) const
{
    if (!providerCombo_ || !endpointEdit_ || !modelCombo_) {
        *error = QStringLiteral("AI provider settings are not ready.");
        return false;
    }
    const QString endpoint = endpointEdit_->text().trimmed();
    const QUrl url(endpoint.contains(QStringLiteral("://")) ? endpoint : QStringLiteral("http://") + endpoint);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        *error = QStringLiteral("Enter a valid HTTP or HTTPS provider URL.");
        return false;
    }
    const QString model = modelCombo_->currentText().trimmed();
    if (requireModel && model.isEmpty()) {
        *error = QStringLiteral("Choose or enter a model on the AI models page before sending.");
        return false;
    }
    if (requireModel && providerConnected_ && !providerModels_.contains(model)) {
        *error = providerModels_.isEmpty()
                     ? QStringLiteral("%1 is connected but has no available models. Check the AI models page.")
                           .arg(activeProvider()->displayName())
                     : QStringLiteral("'%1' is not in the model list returned by %2. Select an available model on the AI models page.")
                           .arg(model, activeProvider()->displayName());
        return false;
    }
    const QString host = url.host().toLower();
    const bool localHost = host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")
                           || host == QStringLiteral("::1");
    if (!activeApiKey().isEmpty() && url.scheme() != QStringLiteral("https") && !localHost) {
        *error = QStringLiteral("Use HTTPS before sending an API key to a non-local provider.");
        return false;
    }
    return true;
}

void MainWindow::handleProviderError(const QString &message)
{
    if (chatRequestActive_) {
        chatRequestActive_ = false;
        providerConnected_ = false;
        providerError_ = message;
        if (streamingAssistantText_.isEmpty())
            streamingAssistantText_ = QStringLiteral("Provider error: %1").arg(message);
        else
            streamingAssistantText_ += QStringLiteral("\n\n[Provider error: %1]").arg(message);
        appendMessage(QStringLiteral("JARVIS"), streamingAssistantText_, false);
        for (const QJsonValue &turnMessage : activeTurnMessages_)
            conversationMessages_.append(turnMessage);
        conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                                 {QStringLiteral("content"), streamingAssistantText_}});
        saveConversationHistory();
        activeTurnMessages_ = QJsonArray();
        for (QLabel *label : streamingLabels_)
            label->hide();
        if (cancelChatButton_)
            cancelChatButton_->setEnabled(false);
        updateAiStatus();
        statusBar()->showMessage(QStringLiteral("AI request failed: %1").arg(message), 9000);
        logEvent(QStringLiteral("AI chat request failed: %1").arg(message));
        resumeHandsFreeListening();
        return;
    }

    modelFetchRunning_ = false;
    providerConnected_ = false;
    providerError_ = message;
    updateAiStatus();
    statusBar()->showMessage(QStringLiteral("Provider check failed: %1").arg(message), 9000);
    logEvent(QStringLiteral("AI provider check failed: %1").arg(message));
}

void MainWindow::finishChatRequest()
{
    if (!chatRequestActive_)
        return;
    const QVector<AIProvider::ToolCall> toolCalls = activeProvider()->takeToolCalls();
    if (!toolCalls.isEmpty()) {
        if (aiToolsCheck_ && aiToolsCheck_->isChecked()) {
            executeToolCalls(toolCalls);
            return;
        }
        streamingAssistantText_ += QStringLiteral("\n\nI ignored an unexpected tool request because AI tool use is disabled in settings.");
        logEvent(QStringLiteral("An AI tool request was rejected because tool use is disabled."));
    }
    chatRequestActive_ = false;
    if (cancelChatButton_)
        cancelChatButton_->setEnabled(false);
    for (QLabel *label : streamingLabels_)
        label->hide();
    const bool hasAssistantResponse = !streamingAssistantText_.trimmed().isEmpty();
    if (!hasAssistantResponse)
        streamingAssistantText_ = QStringLiteral("The provider completed without returning text.");
    appendMessage(QStringLiteral("JARVIS"), streamingAssistantText_, false);
    for (const QJsonValue &message : activeTurnMessages_)
        conversationMessages_.append(message);
    conversationMessages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                             {QStringLiteral("content"), streamingAssistantText_}});
    while (conversationMessages_.size() > 1000)
        conversationMessages_.removeAt(0);
    saveConversationHistory();
    activeTurnMessages_ = QJsonArray();
    providerConnected_ = true;
    providerError_.clear();
    updateAiStatus();
    statusBar()->showMessage(QStringLiteral("Response complete · %1 / %2")
                                 .arg(activeProvider()->displayName(), modelCombo_->currentText()), 6000);
    logEvent(QStringLiteral("AI response completed from %1.").arg(activeProvider()->displayName()));
    if (hasAssistantResponse && speakRepliesCheck_ && speakRepliesCheck_->isChecked())
        speakText(streamingAssistantText_);
    resumeHandsFreeListening();
}

void MainWindow::cancelChatRequest()
{
    if (!chatRequestActive_ || !activeProvider())
        return;
    activeProvider()->cancel();
}

void MainWindow::replaceStreamingMessage(const QString &message)
{
    for (QLabel *label : streamingLabels_) {
        label->setText(message.isEmpty() ? QStringLiteral("JARVIS  ·  THINKING")
                                         : QStringLiteral("JARVIS  ·  %1").arg(message));
        label->show();
    }
}

void MainWindow::loadConversationHistory()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                         + QStringLiteral("/conversations.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray())
        return;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if ((role == QStringLiteral("user") || role == QStringLiteral("assistant") || role == QStringLiteral("tool"))
            && message.value(QStringLiteral("content")).isString()) {
            conversationMessages_.append(message);
        }
    }
    while (conversationMessages_.size() > 1000)
        conversationMessages_.removeAt(0);
}

void MainWindow::saveConversationHistory()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(folder))
        return;
    QSaveFile file(folder + QStringLiteral("/conversations.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(conversationMessages_).toJson(QJsonDocument::Indented));
    file.commit();
}

void MainWindow::clearConversationHistory()
{
    if (QMessageBox::question(this, QStringLiteral("Clear conversation history"),
                              QStringLiteral("Delete the locally saved conversation history?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (chatRequestActive_) {
        chatRequestActive_ = false;
        if (activeProvider())
            activeProvider()->cancel();
        if (cancelChatButton_)
            cancelChatButton_->setEnabled(false);
        for (QLabel *label : streamingLabels_)
            label->hide();
    }
    conversationMessages_ = QJsonArray();
    saveConversationHistory();
    for (QTextBrowser *view : transcripts_)
        view->clear();
    conversationWelcomeViews_.clear();
    if (homeTranscript_)
        conversationWelcomeViews_.append(homeTranscript_);
    if (conversationTranscript_)
        conversationWelcomeViews_.append(conversationTranscript_);
    updateConversationWelcome();
    logEvent(QStringLiteral("Local conversation history was cleared."));
}

void MainWindow::updateVoiceStatus()
{
    if (!voiceStatusLabel_ || !voiceController_)
        return;
    QString status;
    if (voiceController_->isListening() && interruptListeningDuringSpeech_)
        status = QStringLiteral("Listening for 'stop talking' to interrupt JARVIS speech.");
    else if (voiceController_->isListening())
        status = handsFreeVoiceCheck_ && handsFreeVoiceCheck_->isChecked()
                     ? QStringLiteral("Hands-free conversation is listening through %1. It pauses while JARVIS replies.")
                           .arg(microphoneCombo_ ? microphoneCombo_->currentText() : QStringLiteral("the selected microphone"))
                     : QStringLiteral("Listening through %1. Pause to finish your sentence, or press Stop Listening.")
                           .arg(microphoneCombo_ ? microphoneCombo_->currentText() : QStringLiteral("the selected microphone"));
    else if (voiceController_->isSpeaking())
        status = QStringLiteral("Speaking with %1.").arg(voiceEngineCombo_ ? voiceEngineCombo_->currentText()
                                                                          : voiceController_->ttsEngine());
    else
        status = voiceController_->currentTtsEngineStatus();
    voiceStatusLabel_->setText(status);
}

void MainWindow::refreshVoiceChoices()
{
    if (!voiceController_ || !voiceCombo_)
        return;
    const QSignalBlocker blocker(voiceCombo_);
    const QString savedVoice = QSettings().value(QStringLiteral("voice/name")).toString();
    const QStringList voices = voiceController_->activeVoices();
    voiceCombo_->clear();
    const bool kokoroSelected = voiceController_->ttsEngine() == QStringLiteral("kokoro-onnx");
    for (const QString &voice : voices) {
        QString label = voice;
        if (kokoroSelected) {
            if (voice == QStringLiteral("bm_george"))
                label = QStringLiteral("George · British male");
            else if (voice == QStringLiteral("bm_fable"))
                label = QStringLiteral("Fable · British male");
            else if (voice == QStringLiteral("bm_lewis"))
                label = QStringLiteral("Lewis · British male");
            else if (voice == QStringLiteral("bm_daniel"))
                label = QStringLiteral("Daniel · British male");
        }
        voiceCombo_->addItem(label, voice);
    }
    int selected = voiceCombo_->findData(savedVoice);
    if (selected < 0 && kokoroSelected)
        selected = voiceCombo_->findData(QStringLiteral("bm_george"));
    if (selected < 0 && !voices.isEmpty())
        selected = 0;
    if (selected >= 0)
        voiceCombo_->setCurrentIndex(selected);
    voiceCombo_->setEnabled(!voices.isEmpty());
}

void MainWindow::refreshAudioInputs()
{
    if (!voiceController_ || !microphoneCombo_)
        return;
    const QSignalBlocker blocker(microphoneCombo_);
    const QString selectedId = voiceController_->selectedAudioInputId();
    const QStringList names = voiceController_->audioInputNames();
    const QStringList ids = voiceController_->audioInputIds();
    microphoneCombo_->clear();
    for (qsizetype i = 0; i < names.size() && i < ids.size(); ++i)
        microphoneCombo_->addItem(names.at(i), ids.at(i));
    int selected = microphoneCombo_->findData(selectedId);
    if (selected < 0 && microphoneCombo_->count() > 0)
        selected = 0;
    if (selected >= 0)
        microphoneCombo_->setCurrentIndex(selected);
    if (names.isEmpty())
        microphoneCombo_->addItem(QStringLiteral("No microphone detected"), QString());
    microphoneCombo_->setEnabled(!names.isEmpty() && !voiceController_->isListening());
}

void MainWindow::updateVoiceModelStatus()
{
    if (!voiceController_)
        return;
    KokoroTtsProvider *kokoro = voiceController_->kokoroProvider();
    if (voiceRuntimeStatusLabel_)
        voiceRuntimeStatusLabel_->setText(kokoro->runtimeStatusText());
    if (voiceModelStatusLabel_)
        voiceModelStatusLabel_->setText(kokoro->modelStatusText());
    if (voiceRemoveModelButton_)
        voiceRemoveModelButton_->setEnabled(kokoro->hasLocalModelFiles() && !kokoro->isDownloading());
    if (voiceDownloadButton_) {
        const bool filesVerified = kokoro->modelFilesVerified();
        voiceDownloadButton_->setText(filesVerified ? QStringLiteral("MODEL FILES VERIFIED")
                                                    : QStringLiteral("DOWNLOAD / RETRY · 142 MB"));
        voiceDownloadButton_->setEnabled(!kokoro->isDownloading() && !filesVerified);
    }
    if (voiceCancelDownloadButton_)
        voiceCancelDownloadButton_->setEnabled(kokoro->isDownloading());
    if (voiceInstallCommand_) {
        const QString command = kokoro->pythonInstallCommand();
        voiceInstallCommand_->setText(command.isEmpty()
                                          ? QStringLiteral("Kokoro is already available in the selected Python environment.")
                                          : command);
    }
    if (voiceCopyInstallCommandButton_)
        voiceCopyInstallCommandButton_->setEnabled(!kokoro->runtimeReady());
}

void MainWindow::updateOllamaModelManager()
{
    if (!ollamaProvider_)
        return;
    const bool ollamaSelected = activeProviderId() == QStringLiteral("ollama");
    const bool actionBusy = ollamaProvider_->modelActionBusy();
    if (ollamaInventoryLabel_) {
        QStringList rows;
        for (const QJsonValue &value : ollamaModelMetadata_) {
            const QJsonObject model = value.toObject();
            const QString name = model.value(QStringLiteral("name")).toString();
            const qint64 size = static_cast<qint64>(model.value(QStringLiteral("size")).toDouble(-1));
            const QJsonObject details = model.value(QStringLiteral("details")).toObject();
            QString detailsText;
            const QString parameterSize = details.value(QStringLiteral("parameter_size")).toString();
            const QString quantization = details.value(QStringLiteral("quantization_level")).toString();
            if (!parameterSize.isEmpty())
                detailsText = parameterSize;
            if (!quantization.isEmpty())
                detailsText += (detailsText.isEmpty() ? QString() : QStringLiteral(" · ")) + quantization;
            rows.append(detailsText.isEmpty()
                            ? QStringLiteral("%1 · %2").arg(name, humanSize(size))
                            : QStringLiteral("%1 · %2 · %3").arg(name, humanSize(size), detailsText));
        }
        ollamaInventoryLabel_->setText(rows.isEmpty()
                                           ? (providerConnected_ && activeProviderId() == QStringLiteral("ollama")
                                                  ? QStringLiteral("Ollama reports no installed models. Pull a model below; recommended tags are listed separately.")
                                                  : QStringLiteral("Actual installed models appear here when Ollama is selected and its model list has loaded."))
                                           : QStringLiteral("Installed models returned by Ollama:\n%1").arg(rows.join(QLatin1Char('\n'))));
    }
    if (ollamaPullButton_)
        ollamaPullButton_->setEnabled(ollamaSelected && !actionBusy);
    if (ollamaCancelButton_)
        ollamaCancelButton_->setEnabled(actionBusy);
    const QString selectedModel = modelCombo_ ? modelCombo_->currentText().trimmed() : QString();
    if (ollamaDeleteButton_)
        ollamaDeleteButton_->setEnabled(ollamaSelected && !actionBusy && providerModels_.contains(selectedModel));
}

void MainWindow::speakText(const QString &text)
{
    if (!voiceController_ || !voiceCombo_ || voiceCombo_->currentIndex() < 0) {
        statusBar()->showMessage(voiceController_ ? voiceController_->currentTtsEngineStatus()
                                                 : QStringLiteral("Speech output is unavailable."), 7000);
        return;
    }
    voiceController_->speak(text, voiceCombo_->currentData().toString(),
                            voiceRateSlider_ ? voiceRateSlider_->value() : 0,
                            voiceVolumeSlider_ ? voiceVolumeSlider_->value() : 85);
}

void MainWindow::resumeHandsFreeListening()
{
    if (!handsFreeVoiceCheck_ || !handsFreeVoiceCheck_->isChecked() || !voiceController_ || chatRequestActive_)
        return;
    QTimer::singleShot(1000, this, [this] {
        if (!handsFreeVoiceCheck_ || !handsFreeVoiceCheck_->isChecked() || !voiceController_
            || chatRequestActive_ || voiceController_->isListening())
            return;
        if (voiceController_->isSpeaking()) {
            resumeVoiceAfterSpeech_ = true;
            interruptListeningDuringSpeech_ = true;
            voiceController_->startListening();
            return;
        }
        resumeVoiceAfterSpeech_ = false;
        voiceController_->startListening();
    });
}

void MainWindow::stopSpeech()
{
    if (voiceController_)
        voiceController_->stopSpeaking();
}

void MainWindow::updateAiStatus()
{
    const QString providerName = activeProvider() ? activeProvider()->displayName() : QStringLiteral("AI provider");
    QString shortStatus;
    QString details;
    QString modelLine;
    if (modelFetchRunning_) {
        shortStatus = QStringLiteral("AI  ·  CONNECTING");
        details = QStringLiteral("Checking %1…").arg(providerName);
        modelLine = QStringLiteral("Waiting for the provider model list.");
    } else if (!providerError_.isEmpty()) {
        shortStatus = QStringLiteral("AI  ·  ERROR");
        details = QStringLiteral("%1 connection failed: %2").arg(providerName, providerError_);
        modelLine = QStringLiteral("Check the provider URL, service state and credentials.");
    } else if (providerConnected_ && providerModels_.isEmpty()) {
        shortStatus = QStringLiteral("AI  ·  NO MODEL");
        details = QStringLiteral("%1 is responding, but its model list is empty.").arg(providerName);
        modelLine = QStringLiteral("Install a local model or choose an available model name before chatting.");
    } else if (providerConnected_ && !providerModels_.contains(modelCombo_ ? modelCombo_->currentText().trimmed() : QString())) {
        shortStatus = QStringLiteral("AI  ·  SELECT MODEL");
        details = QStringLiteral("%1 is responding, but the current model selection is not in its available model list.")
                      .arg(providerName);
        modelLine = providerModels_.isEmpty()
                        ? QStringLiteral("No models were returned by the provider.")
                        : QStringLiteral("Select one of the available models: %1")
                              .arg(providerModels_.join(QStringLiteral(", ")));
    } else if (providerConnected_) {
        shortStatus = QStringLiteral("AI  ·  READY");
        details = QStringLiteral("%1 is responding and returned %2 model(s).").arg(providerName).arg(providerModels_.size());
        modelLine = QStringLiteral("Selected model: %1 · Available models: %2")
                        .arg(modelCombo_ ? modelCombo_->currentText().trimmed() : QString(),
                             providerModels_.join(QStringLiteral(", ")));
    } else {
        shortStatus = QStringLiteral("AI  ·  NOT CHECKED");
        details = QStringLiteral("Selected provider: %1. Load its models to check the connection.").arg(providerName);
        modelLine = QStringLiteral("No models have been checked yet.");
    }
    if (aiStatusLabel_)
        aiStatusLabel_->setText(shortStatus);
    if (sidebarAiStatus_)
        sidebarAiStatus_->setText(QStringLiteral("%1  ·  %2").arg(providerName, shortStatus.mid(6)));
    if (homeAiSummary_)
        homeAiSummary_->setText(shortStatus);
    if (aiDetails_)
        aiDetails_->setText(details);
    if (aiModels_)
        aiModels_->setText(modelLine);
    updateConversationWelcome();
}

void MainWindow::updateConversationWelcome()
{
    if (conversationWelcomeViews_.isEmpty())
        return;

    const QString providerName = activeProvider() ? activeProvider()->displayName() : QStringLiteral("AI provider");
    const QString model = modelCombo_ ? modelCombo_->currentText().trimmed() : QString();
    QString message;
    if (!providerError_.isEmpty()) {
        message = QStringLiteral("Could not connect to %1: %2 Open AI models to check its URL and status.")
                      .arg(providerName, providerError_);
    } else if (modelFetchRunning_) {
        message = QStringLiteral("Checking %1 for available models…")
                      .arg(providerName);
    } else if (providerConnected_ && providerModels_.contains(model)) {
        message = QStringLiteral("Ready to chat with %1 using %2. Type a message below to begin.")
                      .arg(providerName, model);
    } else if (providerConnected_ && providerModels_.isEmpty()) {
        message = QStringLiteral("%1 is responding, but it returned no available models. Check the AI models page.")
                      .arg(providerName);
    } else if (providerConnected_) {
        message = QStringLiteral("%1 is connected. Choose one of its available models on the AI models page.")
                      .arg(providerName);
    } else if (model.isEmpty()) {
        message = QStringLiteral("No model is selected. Open AI models to choose from the provider's available models.");
    } else {
        message = QStringLiteral("Saved selection: %1 · %2. Checking model availability…")
                      .arg(providerName, model);
    }

    for (QTextBrowser *view : conversationWelcomeViews_) {
        if (!view)
            continue;
        view->clear();
        appendMessageToView(view, QStringLiteral("JARVIS"), message, false);
    }
}

void MainWindow::updateMonitorUi()
{
    const QString cpuValue = monitor_.cpuReady()
                                 ? QStringLiteral("%1%").arg(monitor_.cpuPercent(), 0, 'f', 0)
                                 : QStringLiteral("—");
    const QString cpuDetail = monitor_.cpuReady()
                                  ? QStringLiteral("Live total CPU load")
                                  : QStringLiteral("Waiting for first sample");
    for (const MetricDisplay &display : cpuDisplays_) {
        display.value->setText(cpuValue);
        display.detail->setText(cpuDetail);
        display.bar->setValue(monitor_.cpuReady() ? qRound(monitor_.cpuPercent()) : 0);
    }

    const QString memoryValue = QStringLiteral("%1%").arg(monitor_.memoryPercent(), 0, 'f', 0);
    const QString memoryDetail = QStringLiteral("%1 / %2 GB used")
                                     .arg(monitor_.memoryUsedGb(), 0, 'f', 1)
                                     .arg(monitor_.memoryTotalGb(), 0, 'f', 1);
    for (const MetricDisplay &display : memoryDisplays_) {
        display.value->setText(memoryValue);
        display.detail->setText(memoryDetail);
        display.bar->setValue(qRound(monitor_.memoryPercent()));
    }
    for (QLabel *label : gpuDisplays_)
        label->setText(monitor_.gpuName());
}

void MainWindow::updateClock()
{
    clockLabel_->setText(QDateTime::currentDateTime().toString(QStringLiteral("ddd, dd MMM  ·  HH:mm:ss")));
}

void MainWindow::logEvent(const QString &event)
{
    const QString line = QStringLiteral("%1  %2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), event);
    logLines_.append(line);
    if (logLines_.size() > 500)
        logLines_.removeFirst();
    if (logViewer_)
        logViewer_->appendPlainText(line);

    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/logs");
    QDir().mkpath(folder);
    QFile file(folder + QStringLiteral("/jarvis.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((line + QLatin1Char('\n')).toUtf8());
        file.close();
    }
}

void MainWindow::openLogFolder()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/logs");
    QDir().mkpath(folder);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        statusBar()->showMessage(QStringLiteral("Could not open the log folder."), 5000);
}

void MainWindow::showPage(int index, const QString &title)
{
    pages_->setCurrentIndex(index);
    pageTitle_->setText(title);
    logEvent(QStringLiteral("Opened the %1 page.").arg(title));
}
