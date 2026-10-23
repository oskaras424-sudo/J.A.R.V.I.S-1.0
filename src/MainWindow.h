#pragma once

#include "AIProvider.h"
#include "SystemMonitor.h"
#include "ToolRegistry.h"
#include "VoiceController.h"

#include <QJsonArray>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QSlider;
class QSpinBox;
class QTextBrowser;
class QTimer;
class QWidget;
class QVBoxLayout;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    struct MetricDisplay {
        QLabel *value = nullptr;
        QLabel *detail = nullptr;
        QProgressBar *bar = nullptr;
    };

    QWidget *makePanel(const QString &title, QVBoxLayout **bodyLayout = nullptr) const;
    QWidget *makePageHeader(const QString &eyebrow, const QString &title, const QString &subtitle) const;
    QWidget *makeMetricCard(const QString &title, MetricDisplay *display);
    QWidget *makeGpuCard(const QString &title);
    QWidget *makeReadinessCard(const QString &title, const QString &phase, const QString &description) const;
    QWidget *wrapPage(QWidget *content) const;

    QWidget *buildHomePage();
    QWidget *buildConversationPage();
    QWidget *buildMonitorPage();
    QWidget *buildToolsPage();
    QWidget *buildVoicePage();
    QWidget *buildVoiceModelsPage();
    QWidget *buildAiPage();
    QWidget *buildPersonalityPage();
    QWidget *buildDiagnosticsPage();

    QTextBrowser *makeTranscript();
    void appendMessage(const QString &speaker, const QString &message, bool userMessage);
    void appendMessageToView(QTextBrowser *view, const QString &speaker, const QString &message, bool userMessage);
    void connectComposer(QLineEdit *input);
    bool submitText(const QString &text);
    QJsonObject activeGenerationOptions() const;
    int activeRequestTimeoutMs() const;
    QJsonArray activeToolDefinitions() const;
    void executeToolCalls(const QVector<AIProvider::ToolCall> &calls);
    void resumeHandsFreeListening();
    void showVoiceNotice();
    void loadProviderModels();
    void saveProviderSettings(const QString &providerId = QString());
    void providerChanged();
    AIProvider *activeProvider() const;
    QString activeProviderId() const;
    QString activeApiKey() const;
    bool validateProviderConfiguration(QString *error, bool requireModel = true) const;
    void handleProviderError(const QString &message);
    void finishChatRequest();
    void cancelChatRequest();
    void replaceStreamingMessage(const QString &message);
    void loadConversationHistory();
    void saveConversationHistory();
    void clearConversationHistory();
    void updateVoiceStatus();
    void refreshVoiceChoices();
    void refreshAudioInputs();
    void updateVoiceModelStatus();
    void updateOllamaModelManager();
    void updateConversationWelcome();
    void speakText(const QString &text);
    void stopSpeech();
    void updateAiStatus();
    void updateMonitorUi();
    void updateClock();
    void logEvent(const QString &event);
    void openLogFolder();
    void showPage(int index, const QString &title);

    SystemMonitor monitor_;
    QStackedWidget *pages_ = nullptr;
    QButtonGroup *navigation_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QLabel *clockLabel_ = nullptr;
    QLabel *aiStatusLabel_ = nullptr;
    QLabel *sidebarAiStatus_ = nullptr;
    QLabel *homeAiSummary_ = nullptr;
    QLabel *aiDetails_ = nullptr;
    QLabel *aiModels_ = nullptr;
    QLabel *providerHint_ = nullptr;
    QLabel *ollamaInventoryLabel_ = nullptr;
    QLabel *ollamaActionStatusLabel_ = nullptr;
    QPlainTextEdit *logViewer_ = nullptr;
    QPushButton *voiceInputButton_ = nullptr;
    QPushButton *cancelChatButton_ = nullptr;
    QPushButton *stopSpeechButton_ = nullptr;
    QComboBox *providerCombo_ = nullptr;
    QComboBox *modelCombo_ = nullptr;
    QComboBox *voiceCombo_ = nullptr;
    QComboBox *voiceEngineCombo_ = nullptr;
    QComboBox *microphoneCombo_ = nullptr;
    QLineEdit *endpointEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;
    QLabel *apiKeyLabel_ = nullptr;
    QLineEdit *chatInput_ = nullptr;
    QPlainTextEdit *systemPromptEdit_ = nullptr;
    QCheckBox *aiToolsCheck_ = nullptr;
    QCheckBox *streamingCheck_ = nullptr;
    QDoubleSpinBox *temperatureSpin_ = nullptr;
    QSpinBox *maxTokensSpin_ = nullptr;
    QSpinBox *contextLengthSpin_ = nullptr;
    QSpinBox *requestTimeoutSpin_ = nullptr;
    QCheckBox *speakRepliesCheck_ = nullptr;
    QCheckBox *handsFreeVoiceCheck_ = nullptr;
    QCheckBox *memoryEnabledCheck_ = nullptr;
    QPlainTextEdit *memoryEdit_ = nullptr;
    QSlider *voiceRateSlider_ = nullptr;
    QSlider *voiceVolumeSlider_ = nullptr;
    QLabel *voiceStatusLabel_ = nullptr;
    QLabel *recognizerStatusLabel_ = nullptr;
    QLabel *voiceModelStatusLabel_ = nullptr;
    QLabel *voiceRuntimeStatusLabel_ = nullptr;
    QLabel *voiceDownloadStatusLabel_ = nullptr;
    QProgressBar *voiceDownloadProgress_ = nullptr;
    QProgressBar *microphoneLevelBar_ = nullptr;
    QPushButton *voiceDownloadButton_ = nullptr;
    QPushButton *voiceCopyInstallCommandButton_ = nullptr;
    QPushButton *voiceCancelDownloadButton_ = nullptr;
    QPushButton *voiceRemoveModelButton_ = nullptr;
    QLineEdit *voicePythonEdit_ = nullptr;
    QLineEdit *voiceInstallCommand_ = nullptr;
    QLineEdit *ollamaModelNameEdit_ = nullptr;
    QProgressBar *ollamaModelProgress_ = nullptr;
    QPushButton *ollamaPullButton_ = nullptr;
    QPushButton *ollamaCancelButton_ = nullptr;
    QPushButton *ollamaDeleteButton_ = nullptr;
    QVector<QLabel *> streamingLabels_;
    QTextBrowser *conversationTranscript_ = nullptr;
    QTextBrowser *homeTranscript_ = nullptr;
    QVector<QTextBrowser *> conversationWelcomeViews_;
    QTimer *clockTimer_ = nullptr;
    QTimer *monitorTimer_ = nullptr;
    OllamaProvider *ollamaProvider_ = nullptr;
    OpenAICompatibleProvider *openAIProvider_ = nullptr;
    VoiceController *voiceController_ = nullptr;
    ToolRegistry *toolRegistry_ = nullptr;
    QStringList providerModels_;
    QJsonArray ollamaModelMetadata_;
    QString providerError_;
    QString lastProviderId_;
    QString pendingUserMessage_;
    QString streamingAssistantText_;
    QJsonArray conversationMessages_;
    QJsonArray activeRequestMessages_;
    QJsonArray activeTurnMessages_;
    int toolCallRounds_ = 0;
    int toolCallsExecuted_ = 0;
    bool resumeVoiceAfterSpeech_ = false;
    bool interruptListeningDuringSpeech_ = false;
    bool providerConnected_ = false;
    bool modelFetchRunning_ = false;
    bool chatRequestActive_ = false;
    QVector<QTextBrowser *> transcripts_;
    QVector<QPushButton *> voiceInputButtons_;
    QVector<QPushButton *> stopSpeechButtons_;
    QVector<MetricDisplay> cpuDisplays_;
    QVector<MetricDisplay> memoryDisplays_;
    QVector<QLabel *> gpuDisplays_;
    QStringList logLines_;
};
