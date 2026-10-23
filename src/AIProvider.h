#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QNetworkReply;
class QNetworkAccessManager;

class AIProvider : public QObject
{
    Q_OBJECT

public:
    struct ToolCall {
        QString id;
        QString name;
        QJsonObject arguments;
    };

    explicit AIProvider(QObject *parent = nullptr);
    ~AIProvider() override = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    void fetchModels(const QString &endpoint, const QString &apiKey = QString());
    void streamChat(const QString &endpoint, const QString &apiKey,
                    const QString &model, const QJsonArray &messages, const QJsonArray &tools,
                    const QJsonObject &generationOptions, bool streaming, int timeoutMs);
    QVector<ToolCall> takeToolCalls();
    void cancel();
    bool isBusy() const;

signals:
    void modelsLoaded(const QStringList &models);
    void tokenReceived(const QString &token);
    void chatFinished();
    void requestFailed(const QString &message);
    void requestCancelled();

protected:
    virtual QString modelsPath() const = 0;
    virtual QString chatPath() const = 0;
    virtual QJsonObject makeChatPayload(const QString &model, const QJsonArray &messages,
                                        const QJsonArray &tools, const QJsonObject &generationOptions,
                                        bool streaming) const = 0;
    virtual QStringList parseModels(const QByteArray &body, QString *error) const = 0;
    virtual void parseStreamLine(const QByteArray &line) = 0;

    QString requestEndpoint(const QString &endpoint, const QString &path) const;
    void setStreamError(const QString &message);
    QNetworkReply *activeReply() const;
    void recordToolCall(const QString &name, const QJsonObject &arguments, const QString &id = QString());
    void recordToolCallFragment(int index, const QString &id, const QString &name,
                                const QByteArray &argumentsFragment);
    void finalizeToolCallFragments();

private:
    enum class Operation { None, Models, Chat };

    void beginRequest(QNetworkReply *reply, Operation operation);
    void consumeStream(bool finalChunk);
    QString responseError(const QByteArray &body, int statusCode, const QString &networkError) const;

    QNetworkAccessManager *network_ = nullptr;
    QPointer<QNetworkReply> reply_;
    Operation operation_ = Operation::None;
    QByteArray streamBuffer_;
    QString streamError_;
    struct ToolCallFragments {
        QString id;
        QString name;
        QByteArray arguments;
        bool invalidArguments = false;
    };
    QHash<int, ToolCallFragments> toolCallFragments_;
    QVector<ToolCall> toolCalls_;
    bool cancelled_ = false;
};

class OllamaProvider final : public AIProvider
{
    Q_OBJECT

public:
    explicit OllamaProvider(QObject *parent = nullptr);
    QString id() const override;
    QString displayName() const override;
    void pullModel(const QString &endpoint, const QString &modelName);
    void deleteModel(const QString &endpoint, const QString &modelName);
    void cancelModelAction();
    bool modelActionBusy() const;

signals:
    void modelsMetadataLoaded(const QJsonArray &models);
    void modelActionProgress(const QString &status, qint64 completed, qint64 total);
    void modelActionFinished(const QString &action, const QString &modelName);
    void modelActionFailed(const QString &message);

protected:
    QString modelsPath() const override;
    QString chatPath() const override;
    QJsonObject makeChatPayload(const QString &model, const QJsonArray &messages,
                                const QJsonArray &tools, const QJsonObject &generationOptions,
                                bool streaming) const override;
    QStringList parseModels(const QByteArray &body, QString *error) const override;
    void parseStreamLine(const QByteArray &line) override;

private:
    void startModelAction(const QString &endpoint, const QString &modelName, const QString &action);
    void consumeModelAction(bool finalChunk);
    void finishModelAction(QNetworkReply *reply);

    QNetworkAccessManager *modelNetwork_ = nullptr;
    QPointer<QNetworkReply> modelReply_;
    QByteArray modelStreamBuffer_;
    QString modelAction_;
    QString activeModelName_;
    QString modelActionError_;
    bool modelActionSuccess_ = false;
};

class OpenAICompatibleProvider final : public AIProvider
{
    Q_OBJECT

public:
    using AIProvider::AIProvider;
    QString id() const override;
    QString displayName() const override;

protected:
    QString modelsPath() const override;
    QString chatPath() const override;
    QJsonObject makeChatPayload(const QString &model, const QJsonArray &messages,
                                const QJsonArray &tools, const QJsonObject &generationOptions,
                                bool streaming) const override;
    QStringList parseModels(const QByteArray &body, QString *error) const override;
    void parseStreamLine(const QByteArray &line) override;
};
