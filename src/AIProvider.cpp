#include "AIProvider.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace {

QJsonArray messageArray(const QJsonArray &messages)
{
    QJsonArray result;
    for (const QJsonValue &value : messages) {
        if (!value.isObject())
            continue;
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("role")).isString()
            && message.value(QStringLiteral("content")).isString()) {
            result.append(message);
        }
    }
    return result;
}

QString jsonErrorMessage(const QJsonObject &object)
{
    const QJsonValue error = object.value(QStringLiteral("error"));
    if (error.isString())
        return error.toString();
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    return QString();
}

} // namespace

AIProvider::AIProvider(QObject *parent)
    : QObject(parent), network_(new QNetworkAccessManager(this))
{
}

void AIProvider::fetchModels(const QString &endpoint, const QString &apiKey)
{
    const QUrl url(requestEndpoint(endpoint, modelsPath()));
    if (!url.isValid() || url.host().isEmpty()) {
        emit requestFailed(QStringLiteral("Enter a valid provider URL."));
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(15000);
    if (!apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.trimmed().toUtf8());

    beginRequest(network_->get(request), Operation::Models);
}

void AIProvider::streamChat(const QString &endpoint, const QString &apiKey,
                            const QString &model, const QJsonArray &messages, const QJsonArray &tools,
                            const QJsonObject &generationOptions, bool streaming, int timeoutMs)
{
    if (model.trimmed().isEmpty()) {
        emit requestFailed(QStringLiteral("Choose or enter a model before sending a message."));
        return;
    }

    const QUrl url(requestEndpoint(endpoint, chatPath()));
    if (!url.isValid() || url.host().isEmpty()) {
        emit requestFailed(QStringLiteral("Enter a valid provider URL."));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setTransferTimeout(qBound(5000, timeoutMs, 300000));
    if (!apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.trimmed().toUtf8());

    const QJsonObject payload = makeChatPayload(model.trimmed(), messageArray(messages), tools,
                                                generationOptions, streaming);
    beginRequest(network_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact)), Operation::Chat);
}

QVector<AIProvider::ToolCall> AIProvider::takeToolCalls()
{
    const QVector<ToolCall> calls = toolCalls_;
    toolCalls_.clear();
    return calls;
}

void AIProvider::cancel()
{
    if (reply_)
        reply_->abort();
}

bool AIProvider::isBusy() const
{
    return !reply_.isNull();
}

QString AIProvider::requestEndpoint(const QString &endpoint, const QString &path) const
{
    QString base = endpoint.trimmed();
    if (!base.contains(QStringLiteral("://")))
        base.prepend(QStringLiteral("http://"));
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QString suffix = path;
    if (!suffix.startsWith(QLatin1Char('/')))
        suffix.prepend(QLatin1Char('/'));
    return base + suffix;
}

void AIProvider::setStreamError(const QString &message)
{
    if (streamError_.isEmpty())
        streamError_ = message;
}

QNetworkReply *AIProvider::activeReply() const
{
    return reply_.data();
}

void AIProvider::recordToolCall(const QString &name, const QJsonObject &arguments, const QString &id)
{
    const QString callId = id.isEmpty()
                               ? QStringLiteral("jarvis-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                               : id;
    toolCalls_.append({callId, name, arguments});
}

void AIProvider::recordToolCallFragment(int index, const QString &id, const QString &name,
                                        const QByteArray &argumentsFragment)
{
    ToolCallFragments &fragments = toolCallFragments_[index];
    if (!id.isEmpty())
        fragments.id = id;
    fragments.name += name;
    constexpr qsizetype maxArgumentsBytes = 16384;
    if (fragments.arguments.size() + argumentsFragment.size() > maxArgumentsBytes) {
        fragments.invalidArguments = true;
        return;
    }
    fragments.arguments.append(argumentsFragment);
}

void AIProvider::finalizeToolCallFragments()
{
    QList<int> indexes = toolCallFragments_.keys();
    std::sort(indexes.begin(), indexes.end());
    for (const int index : indexes) {
        const ToolCallFragments fragments = toolCallFragments_.value(index);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(fragments.arguments, &parseError);
        if (fragments.invalidArguments || parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
            recordToolCall(fragments.name, {}, fragments.id);
            continue;
        }
        recordToolCall(fragments.name, document.object(), fragments.id);
    }
    toolCallFragments_.clear();
}

void AIProvider::beginRequest(QNetworkReply *reply, Operation operation)
{
    if (reply_)
        reply_->abort();

    reply_ = reply;
    operation_ = operation;
    streamBuffer_.clear();
    streamError_.clear();
    toolCallFragments_.clear();
    toolCalls_.clear();
    cancelled_ = false;

    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        if (reply_ == reply && operation_ == Operation::Chat)
            consumeStream(false);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply_ != reply) {
            reply->deleteLater();
            return;
        }

        const Operation operation = operation_;
        const bool wasCancelled = cancelled_;
        if (operation == Operation::Chat) {
            streamBuffer_.append(reply->readAll());
            consumeStream(true);
            finalizeToolCallFragments();
        }

        const QByteArray body = operation == Operation::Models ? reply->readAll() : QByteArray();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkError = reply->error() == QNetworkReply::NoError
                                         ? QString()
                                         : reply->errorString();
        reply_.clear();
        operation_ = Operation::None;
        reply->deleteLater();

        if (wasCancelled) {
            emit requestCancelled();
            return;
        }
        if (!streamError_.isEmpty()) {
            emit requestFailed(streamError_);
            return;
        }
        if (statusCode >= 400 || !networkError.isEmpty()) {
            emit requestFailed(responseError(body, statusCode, networkError));
            return;
        }

        if (operation == Operation::Models) {
            QString parseError;
            const QStringList models = parseModels(body, &parseError);
            if (!parseError.isEmpty())
                emit requestFailed(parseError);
            else
                emit modelsLoaded(models);
        } else if (operation == Operation::Chat) {
            emit chatFinished();
        }
    });
}

void AIProvider::consumeStream(bool finalChunk)
{
    while (true) {
        const qsizetype newline = streamBuffer_.indexOf('\n');
        if (newline < 0)
            break;
        QByteArray line = streamBuffer_.left(newline);
        streamBuffer_.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (!line.trimmed().isEmpty())
            parseStreamLine(line);
    }

    if (finalChunk && !streamBuffer_.trimmed().isEmpty()) {
        QByteArray line = streamBuffer_.trimmed();
        if (line.endsWith('\r'))
            line.chop(1);
        parseStreamLine(line);
        streamBuffer_.clear();
    }
}

QString AIProvider::responseError(const QByteArray &body, int statusCode, const QString &networkError) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    QString details;
    if (document.isObject())
        details = jsonErrorMessage(document.object());
    if (details.isEmpty())
        details = QString::fromUtf8(body).trimmed();

    if (!details.isEmpty())
        return statusCode > 0 ? QStringLiteral("Provider returned HTTP %1: %2").arg(statusCode).arg(details)
                             : details;
    if (!networkError.isEmpty())
        return QStringLiteral("Provider connection failed: %1").arg(networkError);
    return QStringLiteral("Provider returned HTTP %1.").arg(statusCode);
}

OllamaProvider::OllamaProvider(QObject *parent)
    : AIProvider(parent), modelNetwork_(new QNetworkAccessManager(this))
{
    connect(modelNetwork_, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
        if (reply == modelReply_)
            finishModelAction(reply);
        else
            reply->deleteLater();
    });
}

QString OllamaProvider::id() const { return QStringLiteral("ollama"); }
QString OllamaProvider::displayName() const { return QStringLiteral("Ollama (local)"); }

void OllamaProvider::pullModel(const QString &endpoint, const QString &modelName)
{
    startModelAction(endpoint, modelName, QStringLiteral("pull"));
}

void OllamaProvider::deleteModel(const QString &endpoint, const QString &modelName)
{
    startModelAction(endpoint, modelName, QStringLiteral("delete"));
}

bool OllamaProvider::modelActionBusy() const
{
    return !modelReply_.isNull();
}

void OllamaProvider::cancelModelAction()
{
    if (!modelReply_)
        return;
    modelActionError_ = QStringLiteral("Ollama model action was cancelled.");
    modelReply_->abort();
}

void OllamaProvider::startModelAction(const QString &endpoint, const QString &modelName, const QString &action)
{
    if (modelActionBusy()) {
        emit modelActionFailed(QStringLiteral("An Ollama model action is already running."));
        return;
    }
    const QString name = modelName.trimmed();
    static const QRegularExpression namePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$"));
    if (!namePattern.match(name).hasMatch()) {
        emit modelActionFailed(QStringLiteral("Enter a model tag using letters, numbers, '.', '_', ':', '/', or '-'."));
        return;
    }

    QString base = endpoint.trimmed();
    if (!base.contains(QStringLiteral("://")))
        base.prepend(QStringLiteral("http://"));
    const QUrl baseUrl(base);
    const QString host = baseUrl.host().toLower();
    if (!baseUrl.isValid() || (baseUrl.scheme() != QStringLiteral("http")
                               && baseUrl.scheme() != QStringLiteral("https"))
        || (host != QStringLiteral("localhost") && host != QStringLiteral("127.0.0.1")
            && host != QStringLiteral("::1"))) {
        emit modelActionFailed(QStringLiteral("Model pull and delete are limited to a local Ollama service."));
        return;
    }

    const QString path = action == QStringLiteral("pull") ? QStringLiteral("/api/pull")
                                                          : QStringLiteral("/api/delete");
    const QUrl url(requestEndpoint(endpoint, path));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "JARVIS-Assistant/0.2");
    request.setTransferTimeout(action == QStringLiteral("pull") ? 900000 : 30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    const QJsonObject payload{{QStringLiteral("name"), name},
                              {QStringLiteral("stream"), action == QStringLiteral("pull")}};

    activeModelName_ = name;
    modelAction_ = action;
    modelActionError_.clear();
    modelActionSuccess_ = false;
    modelStreamBuffer_.clear();
    if (action == QStringLiteral("pull"))
        modelReply_ = modelNetwork_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    else
        modelReply_ = modelNetwork_->sendCustomRequest(request, "DELETE",
                                                        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(modelReply_, &QIODevice::readyRead, this, [this, reply = modelReply_] {
        if (reply == modelReply_ && modelAction_ == QStringLiteral("pull")) {
            modelStreamBuffer_.append(reply->readAll());
            consumeModelAction(false);
        }
    });
}

void OllamaProvider::consumeModelAction(bool finalChunk)
{
    while (true) {
        const qsizetype newline = modelStreamBuffer_.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = modelStreamBuffer_.left(newline).trimmed();
        modelStreamBuffer_.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            modelActionError_ = QStringLiteral("Ollama returned an invalid model-download event.");
            continue;
        }
        const QJsonObject object = document.object();
        const QString apiError = jsonErrorMessage(object);
        if (!apiError.isEmpty()) {
            modelActionError_ = apiError;
            continue;
        }
        emit modelActionProgress(object.value(QStringLiteral("status")).toString(),
                                 static_cast<qint64>(object.value(QStringLiteral("completed")).toDouble()),
                                 static_cast<qint64>(object.value(QStringLiteral("total")).toDouble()));
        if (object.value(QStringLiteral("status")).toString() == QStringLiteral("success"))
            modelActionSuccess_ = true;
    }
    if (finalChunk && !modelStreamBuffer_.trimmed().isEmpty()) {
        const QByteArray line = modelStreamBuffer_.trimmed();
        modelStreamBuffer_.clear();
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            modelActionError_ = QStringLiteral("Ollama returned an invalid final model-download event.");
        } else {
            const QJsonObject object = document.object();
            const QString apiError = jsonErrorMessage(object);
            if (!apiError.isEmpty())
                modelActionError_ = apiError;
            else
                emit modelActionProgress(object.value(QStringLiteral("status")).toString(),
                                         static_cast<qint64>(object.value(QStringLiteral("completed")).toDouble()),
                                         static_cast<qint64>(object.value(QStringLiteral("total")).toDouble()));
            if (object.value(QStringLiteral("status")).toString() == QStringLiteral("success"))
                modelActionSuccess_ = true;
        }
    }
}

void OllamaProvider::finishModelAction(QNetworkReply *reply)
{
    if (reply != modelReply_) {
        reply->deleteLater();
        return;
    }
    const QByteArray remainder = reply->readAll();
    if (modelAction_ == QStringLiteral("pull")) {
        modelStreamBuffer_.append(remainder);
        consumeModelAction(true);
    }
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    QString failure = modelActionError_;
    if (failure.isEmpty() && modelAction_ == QStringLiteral("pull") && !modelActionSuccess_)
        failure = QStringLiteral("Ollama stopped the pull stream before reporting completion.");
    if (failure.isEmpty() && (!networkError.isEmpty() || statusCode < 200 || statusCode >= 300)) {
        const QString details = QString::fromUtf8(remainder).trimmed();
        failure = details.isEmpty()
                      ? QStringLiteral("Ollama model action failed: %1")
                            .arg(networkError.isEmpty() ? QStringLiteral("HTTP %1").arg(statusCode) : networkError)
                      : QStringLiteral("Ollama model action failed: %1").arg(details.left(500));
    }
    const QString action = modelAction_;
    const QString name = activeModelName_;
    modelReply_.clear();
    modelAction_.clear();
    activeModelName_.clear();
    modelActionError_.clear();
    modelActionSuccess_ = false;
    modelStreamBuffer_.clear();
    reply->deleteLater();
    if (!failure.isEmpty())
        emit modelActionFailed(failure);
    else
        emit modelActionFinished(action, name);
}
QString OllamaProvider::modelsPath() const { return QStringLiteral("/api/tags"); }
QString OllamaProvider::chatPath() const { return QStringLiteral("/api/chat"); }

QJsonObject OllamaProvider::makeChatPayload(const QString &model, const QJsonArray &messages,
                                            const QJsonArray &tools, const QJsonObject &generationOptions,
                                            bool streaming) const
{
    QJsonObject payload{{QStringLiteral("model"), model},
                        {QStringLiteral("messages"), messages},
                        {QStringLiteral("stream"), streaming},
                        {QStringLiteral("think"), false}};
    if (!tools.isEmpty())
        payload.insert(QStringLiteral("tools"), tools);
    QJsonObject options;
    if (generationOptions.contains(QStringLiteral("temperature")))
        options.insert(QStringLiteral("temperature"), generationOptions.value(QStringLiteral("temperature")));
    if (generationOptions.contains(QStringLiteral("maxTokens")))
        options.insert(QStringLiteral("num_predict"), generationOptions.value(QStringLiteral("maxTokens")));
    if (generationOptions.contains(QStringLiteral("contextLength")))
        options.insert(QStringLiteral("num_ctx"), generationOptions.value(QStringLiteral("contextLength")));
    if (!options.isEmpty())
        payload.insert(QStringLiteral("options"), options);
    return payload;
}

QStringList OllamaProvider::parseModels(const QByteArray &body, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("Ollama returned an invalid model list.");
        return {};
    }

    QStringList models;
    QJsonArray metadata;
    const QJsonArray rows = document.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &row : rows) {
        const QJsonObject model = row.toObject();
        const QString name = model.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            models.append(name);
            metadata.append(QJsonObject{{QStringLiteral("name"), name},
                                        {QStringLiteral("size"), model.value(QStringLiteral("size"))},
                                        {QStringLiteral("modified_at"), model.value(QStringLiteral("modified_at"))},
                                        {QStringLiteral("details"), model.value(QStringLiteral("details"))}});
        }
    }
    emit const_cast<OllamaProvider *>(this)->modelsMetadataLoaded(metadata);
    return models;
}

void OllamaProvider::parseStreamLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject object = document.object();
    const QString error = jsonErrorMessage(object);
    if (!error.isEmpty()) {
        setStreamError(error);
        return;
    }
    const QString token = object.value(QStringLiteral("message")).toObject()
                              .value(QStringLiteral("content")).toString();
    if (!token.isEmpty())
        emit tokenReceived(token);
    const QJsonArray calls = object.value(QStringLiteral("message")).toObject()
                                 .value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &value : calls) {
        const QJsonObject function = value.toObject().value(QStringLiteral("function")).toObject();
        const QJsonValue arguments = function.value(QStringLiteral("arguments"));
        recordToolCall(function.value(QStringLiteral("name")).toString(),
                       arguments.isObject() ? arguments.toObject() : QJsonObject());
    }
}

QString OpenAICompatibleProvider::id() const { return QStringLiteral("openai-compatible"); }
QString OpenAICompatibleProvider::displayName() const { return QStringLiteral("OpenAI-compatible"); }
QString OpenAICompatibleProvider::modelsPath() const { return QStringLiteral("/models"); }
QString OpenAICompatibleProvider::chatPath() const { return QStringLiteral("/chat/completions"); }

QJsonObject OpenAICompatibleProvider::makeChatPayload(const QString &model, const QJsonArray &messages,
                                                      const QJsonArray &tools,
                                                      const QJsonObject &generationOptions,
                                                      bool streaming) const
{
    QJsonObject payload{{QStringLiteral("model"), model},
                        {QStringLiteral("messages"), messages},
                        {QStringLiteral("stream"), streaming}};
    if (!tools.isEmpty())
        payload.insert(QStringLiteral("tools"), tools);
    if (generationOptions.contains(QStringLiteral("temperature")))
        payload.insert(QStringLiteral("temperature"), generationOptions.value(QStringLiteral("temperature")));
    if (generationOptions.contains(QStringLiteral("maxTokens")))
        payload.insert(QStringLiteral("max_tokens"), generationOptions.value(QStringLiteral("maxTokens")));
    return payload;
}

QStringList OpenAICompatibleProvider::parseModels(const QByteArray &body, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("The provider returned an invalid model list.");
        return {};
    }

    const QJsonObject root = document.object();
    const QString apiError = jsonErrorMessage(root);
    if (!apiError.isEmpty()) {
        *error = apiError;
        return {};
    }

    QStringList models;
    const QJsonArray rows = root.value(QStringLiteral("data")).toArray();
    for (const QJsonValue &row : rows) {
        const QString name = row.toObject().value(QStringLiteral("id")).toString();
        if (!name.isEmpty())
            models.append(name);
    }
    return models;
}

void OpenAICompatibleProvider::parseStreamLine(const QByteArray &line)
{
    QByteArray data = line.trimmed();
    if (!data.startsWith("data:")) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject root = document.object();
            const QString error = jsonErrorMessage(root);
            if (!error.isEmpty())
                setStreamError(error);
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty())
                return;
            const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
            const QString content = message.value(QStringLiteral("content")).toString();
            if (!content.isEmpty())
                emit tokenReceived(content);
            const QJsonArray calls = message.value(QStringLiteral("tool_calls")).toArray();
            for (const QJsonValue &value : calls) {
                const QJsonObject call = value.toObject();
                const QJsonObject function = call.value(QStringLiteral("function")).toObject();
                QJsonObject arguments;
                const QJsonValue rawArguments = function.value(QStringLiteral("arguments"));
                if (rawArguments.isObject()) {
                    arguments = rawArguments.toObject();
                } else if (rawArguments.isString()) {
                    const QJsonDocument argumentDocument = QJsonDocument::fromJson(rawArguments.toString().toUtf8());
                    if (argumentDocument.isObject())
                        arguments = argumentDocument.object();
                }
                recordToolCall(function.value(QStringLiteral("name")).toString(), arguments,
                               call.value(QStringLiteral("id")).toString());
            }
        }
        return;
    }
    data.remove(0, 5);
    data = data.trimmed();
    if (data.isEmpty() || data == "[DONE]")
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject object = document.object();
    const QString error = jsonErrorMessage(object);
    if (!error.isEmpty()) {
        setStreamError(error);
        return;
    }
    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
    const QString token = delta.value(QStringLiteral("content")).toString();
    if (!token.isEmpty())
        emit tokenReceived(token);
    const QJsonArray calls = delta.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &value : calls) {
        const QJsonObject call = value.toObject();
        const QJsonObject function = call.value(QStringLiteral("function")).toObject();
        recordToolCallFragment(call.value(QStringLiteral("index")).toInt(),
                               call.value(QStringLiteral("id")).toString(),
                               function.value(QStringLiteral("name")).toString(),
                               function.value(QStringLiteral("arguments")).toString().toUtf8());
    }
}
