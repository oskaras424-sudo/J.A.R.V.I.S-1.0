#include "ToolRegistry.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPair>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <iterator>

#include <Windows.h>
#include <TlHelp32.h>

namespace {

void hideProcessWindow(QProcess *process)
{
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
}

QString normaliseCommand(QString text)
{
    text = text.simplified().toLower();
    static const QRegularExpression prefix(
        QStringLiteral("^(?:(?:please|can you|could you|would you|will you)\\s+)+"));
    text.remove(prefix);
    text.remove(QRegularExpression(QStringLiteral("^hey\\s+jarvis[, :]?\\s*")));
    text.remove(QRegularExpression(QStringLiteral("^jarvis[, :]?\\s*")));
    while (text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(','))
           || text.endsWith(QLatin1Char('!')) || text.endsWith(QLatin1Char('?')))
        text.chop(1);
    return text.simplified();
}

QString normaliseTarget(QString text)
{
    text = text.simplified().toLower();
    text.remove(QRegularExpression(QStringLiteral("^(?:my|the)\\s+")));
    text.remove(QRegularExpression(QStringLiteral("\\s+(?:application|app|folder)$")));
    return text.simplified();
}

BOOL CALLBACK appWindowCallback(HWND window, LPARAM parameter)
{
    auto *target = reinterpret_cast<QPair<DWORD, QVector<HWND> *> *>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == target->first && IsWindowVisible(window))
        target->second->append(window);
    return TRUE;
}

} // namespace

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{
}

ToolRegistry::~ToolRegistry()
{
    cancelFileSearch();
    if (searchThread_) {
        // Never destroy a QThread while its directory iterator is still running.
        searchThread_->wait();
        delete searchThread_;
        searchThread_ = nullptr;
    }
}

QVector<ToolRegistry::Application> ToolRegistry::availableApplications() const
{
    struct Candidate {
        QString id;
        QString label;
        QStringList startMenuNames;
        QString processName;
        QString fallbackExecutable;
        QStringList aliases;
    };
    const QVector<Candidate> candidates = {
        {QStringLiteral("notepad"), QStringLiteral("Notepad"), {QStringLiteral("Notepad")}, QStringLiteral("Notepad.exe"), QStringLiteral("notepad.exe"), {QStringLiteral("notepad")}},
        {QStringLiteral("calculator"), QStringLiteral("Calculator"), {QStringLiteral("Calculator")}, QStringLiteral("CalculatorApp.exe"), {}, {QStringLiteral("calculator"), QStringLiteral("calc")}},
        {QStringLiteral("explorer"), QStringLiteral("File Explorer"), {QStringLiteral("File Explorer")}, QStringLiteral("explorer.exe"), QStringLiteral("explorer.exe"), {QStringLiteral("explorer"), QStringLiteral("file explorer")}},
        {QStringLiteral("paint"), QStringLiteral("Paint"), {QStringLiteral("Paint")}, QStringLiteral("mspaint.exe"), QStringLiteral("mspaint.exe"), {QStringLiteral("paint")}},
        {QStringLiteral("vscode"), QStringLiteral("Visual Studio Code"), {QStringLiteral("Visual Studio Code"), QStringLiteral("VS Code")}, QStringLiteral("Code.exe"), QStringLiteral("Code.exe"), {QStringLiteral("visual studio code"), QStringLiteral("vs code"), QStringLiteral("code")}},
        {QStringLiteral("visual-studio"), QStringLiteral("Visual Studio"), {QStringLiteral("Visual Studio")}, QStringLiteral("devenv.exe"), QStringLiteral("devenv.exe"), {QStringLiteral("visual studio"), QStringLiteral("vs")}},
        {QStringLiteral("discord"), QStringLiteral("Discord"), {QStringLiteral("Discord")}, QStringLiteral("Discord.exe"), QStringLiteral("Discord.exe"), {QStringLiteral("discord")}},
        {QStringLiteral("steam"), QStringLiteral("Steam"), {QStringLiteral("Steam")}, QStringLiteral("steam.exe"), QStringLiteral("Steam.exe"), {QStringLiteral("steam")}},
        {QStringLiteral("firefox"), QStringLiteral("Firefox"), {QStringLiteral("Firefox")}, QStringLiteral("firefox.exe"), QStringLiteral("firefox.exe"), {QStringLiteral("firefox")}},
        {QStringLiteral("spotify"), QStringLiteral("Spotify"), {QStringLiteral("Spotify")}, QStringLiteral("Spotify.exe"), QStringLiteral("Spotify.exe"), {QStringLiteral("spotify")}},
        {QStringLiteral("chrome"), QStringLiteral("Google Chrome"), {QStringLiteral("Google Chrome"), QStringLiteral("Chrome")}, QStringLiteral("chrome.exe"), QStringLiteral("chrome.exe"), {QStringLiteral("chrome"), QStringLiteral("google chrome")}},
        {QStringLiteral("edge"), QStringLiteral("Microsoft Edge"), {QStringLiteral("Microsoft Edge"), QStringLiteral("Edge")}, QStringLiteral("msedge.exe"), QStringLiteral("msedge.exe"), {QStringLiteral("edge"), QStringLiteral("microsoft edge")}},
        {QStringLiteral("terminal"), QStringLiteral("Windows Terminal"), {QStringLiteral("Terminal"), QStringLiteral("Windows Terminal")}, QStringLiteral("WindowsTerminal.exe"), QStringLiteral("wt.exe"), {QStringLiteral("terminal"), QStringLiteral("windows terminal")}}
    };

    QHash<QString, QString> startApps;
    QProcess inventory;
    hideProcessWindow(&inventory);
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop'; [Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false); "
        "Get-StartApps | Select-Object Name,AppID | ConvertTo-Json -Compress -Depth 2");
    inventory.start(QStringLiteral("powershell.exe"),
                    {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
                     QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script});
    if (inventory.waitForStarted(1000) && inventory.waitForFinished(4000)
        && inventory.exitStatus() == QProcess::NormalExit && inventory.exitCode() == 0) {
        const QJsonDocument document = QJsonDocument::fromJson(inventory.readAllStandardOutput());
        QJsonArray rows;
        if (document.isArray())
            rows = document.array();
        else if (document.isObject())
            rows.append(document.object());
        for (const QJsonValue &value : rows) {
            const QJsonObject row = value.toObject();
            const QString name = row.value(QStringLiteral("Name")).toString().trimmed();
            const QString appId = row.value(QStringLiteral("AppID")).toString().trimmed();
            if (!name.isEmpty() && !appId.isEmpty())
                startApps.insert(name.toCaseFolded(), appId);
        }
    } else if (inventory.state() != QProcess::NotRunning) {
        inventory.kill();
        inventory.waitForFinished(250);
    }

    QVector<Application> applications;
    for (const Candidate &candidate : candidates) {
        QString launchTarget;
        bool appFolderId = false;
        for (const QString &name : candidate.startMenuNames) {
            const QString appId = startApps.value(name.toCaseFolded());
            if (appId.isEmpty())
                continue;
            const QFileInfo appInfo(appId);
            if (appInfo.isAbsolute() && appInfo.isFile() && appInfo.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0) {
                launchTarget = appInfo.absoluteFilePath();
            } else if (QRegularExpression(QStringLiteral("^[A-Za-z0-9_.!:-]+$")).match(appId).hasMatch()) {
                launchTarget = appId;
                appFolderId = true;
            }
            if (!launchTarget.isEmpty())
                break;
        }
        if (launchTarget.isEmpty() && !candidate.fallbackExecutable.isEmpty())
            launchTarget = QStandardPaths::findExecutable(candidate.fallbackExecutable);
        if (launchTarget.isEmpty())
            continue;
        applications.append({candidate.id, candidate.label, launchTarget,
                             candidate.processName, appFolderId});
    }
    return applications;
}

QJsonArray ToolRegistry::aiToolDefinitions() const
{
    QJsonArray tools;
    const QVector<Application> applications = availableApplications();
    if (!applications.isEmpty()) {
        QJsonArray ids;
        QStringList labels;
        for (const Application &application : applications) {
            ids.append(application.id);
            labels.append(QStringLiteral("%1 (%2)").arg(application.label, application.id));
        }
        const QJsonObject properties{{QStringLiteral("application_id"),
                                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                  {QStringLiteral("enum"), ids},
                                                  {QStringLiteral("description"), labels.join(QStringLiteral(", "))}}}};
        tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                 {QStringLiteral("function"),
                                  QJsonObject{{QStringLiteral("name"), QStringLiteral("launch_application")},
                                              {QStringLiteral("description"), QStringLiteral("Launch one installed application from the JARVIS allowlist. Only call when the user directly asked to open or launch that application.")},
                                              {QStringLiteral("parameters"),
                                               QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                           {QStringLiteral("properties"), properties},
                                                           {QStringLiteral("required"), QJsonArray{QStringLiteral("application_id")}},
                                                           {QStringLiteral("additionalProperties"), false}}}}}});
    }
    const QJsonObject websiteProperties{{QStringLiteral("url"),
                                        QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                    {QStringLiteral("description"), QStringLiteral("A complete HTTP or HTTPS website address requested by the user.")}}}};
    tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                             {QStringLiteral("function"),
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("open_website")},
                                          {QStringLiteral("description"), QStringLiteral("Ask to open a website the user directly requested in the default browser. The user must confirm before the browser is opened.")},
                                          {QStringLiteral("parameters"),
                                           QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                                       {QStringLiteral("properties"), websiteProperties},
                                                       {QStringLiteral("required"), QJsonArray{QStringLiteral("url")}},
                                                       {QStringLiteral("additionalProperties"), false}}}}}});
    return tools;
}

bool ToolRegistry::aiToolRequiresConfirmation(const QString &toolName) const
{
    return toolName == QStringLiteral("open_website");
}

bool ToolRegistry::executeAiTool(const QString &toolName, const QJsonObject &arguments,
                                 QString *details) const
{
    if (toolName == QStringLiteral("launch_application")) {
        if (arguments.size() != 1 || !arguments.value(QStringLiteral("application_id")).isString()) {
            if (details)
                *details = QStringLiteral("Invalid arguments. Supply only an allowlisted application_id string.");
            return false;
        }
        return launchApplication(arguments.value(QStringLiteral("application_id")).toString(), details);
    }
    if (toolName == QStringLiteral("open_website")) {
        if (arguments.size() != 1 || !arguments.value(QStringLiteral("url")).isString()) {
            if (details)
                *details = QStringLiteral("Invalid arguments. Supply only a website url string.");
            return false;
        }
        return openWebsite(arguments.value(QStringLiteral("url")).toString(), details);
    }
    if (details)
        *details = QStringLiteral("This tool name is not available in JARVIS.");
    return false;
}

bool ToolRegistry::launchApplication(const QString &applicationId, QString *details) const
{
    const QVector<Application> applications = availableApplications();
    const auto app = std::find_if(applications.cbegin(), applications.cend(),
                                  [&applicationId](const Application &entry) { return entry.id == applicationId; });
    if (app == applications.cend()) {
        if (details)
            *details = QStringLiteral("That application is not in the available application list.");
        return false;
    }
    QString program = app->executable;
    QStringList arguments;
    if (app->appFolderId) {
        program = QStandardPaths::findExecutable(QStringLiteral("explorer.exe"));
        if (program.isEmpty())
            program = QStringLiteral("explorer.exe");
        arguments.append(QStringLiteral("shell:AppsFolder\\") + app->executable);
    }
    qint64 processId = 0;
    if (!QProcess::startDetached(program, arguments, QString(), &processId)) {
        if (details)
            *details = QStringLiteral("Windows could not start %1.").arg(app->label);
        return false;
    }
    if (details)
        *details = app->appFolderId
                       ? QStringLiteral("Windows accepted the launch request for %1.").arg(app->label)
                       : QStringLiteral("Started %1 (process %2). Windows created the application process.")
                             .arg(app->label).arg(processId);
    return true;
}

bool ToolRegistry::closeApplication(const QString &applicationId, QString *details) const
{
    const QVector<Application> applications = availableApplications();
    const auto app = std::find_if(applications.cbegin(), applications.cend(),
                                  [&applicationId](const Application &entry) { return entry.id == applicationId; });
    if (app == applications.cend()) {
        if (details)
            *details = QStringLiteral("That application is not in the available application list.");
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (details)
            *details = QStringLiteral("Windows could not inspect running applications.");
        return false;
    }

    int windowsRequestedToClose = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (QString::fromWCharArray(entry.szExeFile).compare(app->processName, Qt::CaseInsensitive) != 0)
                continue;
            QVector<HWND> windows;
            QPair<DWORD, QVector<HWND> *> target(entry.th32ProcessID, &windows);
            EnumWindows(appWindowCallback, reinterpret_cast<LPARAM>(&target));
            for (HWND window : windows) {
                DWORD_PTR result = 0;
                if (SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                        2500, &result))
                    ++windowsRequestedToClose;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (windowsRequestedToClose == 0) {
        if (details)
            *details = QStringLiteral("No open %1 window was found.").arg(app->label);
        return false;
    }
    if (details)
        *details = QStringLiteral("Sent a close request to %1 %2 window(s). Windows may ask you to save unsaved work.")
                       .arg(app->label).arg(windowsRequestedToClose);
    return true;
}

bool ToolRegistry::parseSpokenCommand(const QString &text, QVector<SpokenAction> *actions,
                                     QString *response) const
{
    if (!actions)
        return false;
    actions->clear();
    if (response)
        response->clear();
    const QString command = normaliseCommand(text);
    if (command.isEmpty())
        return false;

    const QStringList parts = command.split(QRegularExpression(QStringLiteral("\\s+(?:and then|then)\\s+")),
                                            Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = normaliseCommand(part);
        if (part == QStringLiteral("stop talking") || part == QStringLiteral("stop speaking")
            || part == QStringLiteral("stop output")) {
            actions->append({SpokenAction::Type::StopSpeaking, {}, QStringLiteral("speech output")});
            continue;
        }

        const QRegularExpression searchPattern(
            QStringLiteral("^(?:search (?:google|the web) for|google search for)\\s+(.+)$"));
        const QRegularExpressionMatch searchMatch = searchPattern.match(part);
        if (searchMatch.hasMatch()) {
            const QString query = searchMatch.captured(1).trimmed();
            if (query.isEmpty()) {
                if (response)
                    *response = QStringLiteral("Tell me what you want to search for.");
                return true;
            }
            QUrl url(QStringLiteral("https://www.google.com/search"));
            QUrlQuery urlQuery;
            urlQuery.addQueryItem(QStringLiteral("q"), query);
            url.setQuery(urlQuery);
            actions->append({SpokenAction::Type::OpenWebsite, url.toString(QUrl::FullyEncoded),
                             QStringLiteral("Google search for %1").arg(query)});
            continue;
        }

        const QRegularExpression actionPattern(
            QStringLiteral("^(open|launch|start|show|go to|visit|close|quit|exit)\\s+(.+)$"));
        const QRegularExpressionMatch actionMatch = actionPattern.match(part);
        if (!actionMatch.hasMatch())
            return false;

        const QString verb = actionMatch.captured(1);
        const QString target = normaliseTarget(actionMatch.captured(2));
        if (target.isEmpty()) {
            if (response)
                *response = QStringLiteral("Say what you want me to %1.").arg(verb);
            return true;
        }
        if (target == QStringLiteral("downloads") || target == QStringLiteral("downloads folder")) {
            const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            if (downloads.isEmpty() || !QFileInfo(downloads).isDir()) {
                if (response)
                    *response = QStringLiteral("Windows did not provide an existing Downloads folder path.");
                return true;
            }
            actions->append({SpokenAction::Type::OpenPath, downloads, QStringLiteral("Downloads folder")});
            continue;
        }

        if (verb == QStringLiteral("close") || verb == QStringLiteral("quit") || verb == QStringLiteral("exit")) {
            const QVector<Application> applications = availableApplications();
            const auto app = std::find_if(applications.cbegin(), applications.cend(), [&target](const Application &entry) {
                return entry.id == target || entry.label.toLower() == target;
            });
            if (app == applications.cend()) {
                if (response)
                    *response = QStringLiteral("I could not find an installed supported app called %1.").arg(target);
                return true;
            }
            actions->append({SpokenAction::Type::CloseApplication, app->id, app->label});
            continue;
        }

        if (verb == QStringLiteral("open") || verb == QStringLiteral("go to") || verb == QStringLiteral("visit")) {
            const struct Website {
                const char *alias;
                const char *url;
            } websites[] = {
                {"youtube", "https://www.youtube.com"},
                {"google", "https://www.google.com"},
                {"github", "https://github.com"},
                {"wikipedia", "https://www.wikipedia.org"},
                {"gmail", "https://mail.google.com"}
            };
            const auto website = std::find_if(std::begin(websites), std::end(websites), [&target](const Website &entry) {
                return QString::fromLatin1(entry.alias) == target;
            });
            if (website != std::end(websites)) {
                actions->append({SpokenAction::Type::OpenWebsite, QString::fromLatin1(website->url),
                                 QString::fromLatin1(website->alias)});
                continue;
            }
            const QVector<Application> applications = availableApplications();
            const auto app = std::find_if(applications.cbegin(), applications.cend(), [&target](const Application &entry) {
                if (entry.id == target || entry.label.toLower() == target)
                    return true;
                const QHash<QString, QStringList> aliases = {
                    {QStringLiteral("notepad"), {QStringLiteral("notepad")}},
                    {QStringLiteral("calculator"), {QStringLiteral("calculator"), QStringLiteral("calc")}},
                    {QStringLiteral("explorer"), {QStringLiteral("file explorer"), QStringLiteral("explorer")}},
                    {QStringLiteral("paint"), {QStringLiteral("paint")}},
                    {QStringLiteral("vscode"), {QStringLiteral("vs code"), QStringLiteral("visual studio code"), QStringLiteral("code")}},
                    {QStringLiteral("visual-studio"), {QStringLiteral("visual studio"), QStringLiteral("vs")}},
                    {QStringLiteral("discord"), {QStringLiteral("discord")}},
                    {QStringLiteral("steam"), {QStringLiteral("steam")}},
                    {QStringLiteral("firefox"), {QStringLiteral("firefox")}},
                    {QStringLiteral("spotify"), {QStringLiteral("spotify")}},
                    {QStringLiteral("chrome"), {QStringLiteral("chrome"), QStringLiteral("google chrome")}},
                    {QStringLiteral("edge"), {QStringLiteral("edge"), QStringLiteral("microsoft edge")}},
                    {QStringLiteral("terminal"), {QStringLiteral("terminal"), QStringLiteral("windows terminal")}}
                };
                return aliases.value(entry.id).contains(target);
            });
            if (app != applications.cend()) {
                actions->append({SpokenAction::Type::LaunchApplication, app->id, app->label});
                continue;
            }
            const QUrl possibleUrl = QUrl::fromUserInput(actionMatch.captured(2).trimmed());
            if (!target.contains(QLatin1Char(' ')) && possibleUrl.isValid() && !possibleUrl.host().isEmpty()
                && (possibleUrl.scheme() == QStringLiteral("http") || possibleUrl.scheme() == QStringLiteral("https"))) {
                actions->append({SpokenAction::Type::OpenWebsite, possibleUrl.toString(), target});
                continue;
            }
            if (response)
                *response = QStringLiteral("I could not find an installed supported app or known website called %1.").arg(target);
            return true;
        }
    }
    return !actions->isEmpty();
}

bool ToolRegistry::spokenActionRequiresConfirmation(const SpokenAction &action) const
{
    return action.type == SpokenAction::Type::CloseApplication;
}

bool ToolRegistry::executeSpokenAction(const SpokenAction &action, QString *details) const
{
    switch (action.type) {
    case SpokenAction::Type::LaunchApplication:
        return launchApplication(action.target, details);
    case SpokenAction::Type::CloseApplication:
        return closeApplication(action.target, details);
    case SpokenAction::Type::OpenWebsite:
        return openWebsite(action.target, details);
    case SpokenAction::Type::OpenPath:
        return openPath(action.target, details);
    case SpokenAction::Type::StopSpeaking:
        if (details)
            *details = QStringLiteral("Stopped current speech output.");
        return true;
    }
    if (details)
        *details = QStringLiteral("This spoken action is not available.");
    return false;
}

bool ToolRegistry::openWebsite(const QString &input, QString *details) const
{
    QString address = input.trimmed();
    if (!address.contains(QStringLiteral("://")))
        address.prepend(QStringLiteral("https://"));
    const QUrl url(address);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))
        || !url.userInfo().isEmpty()) {
        if (details)
            *details = QStringLiteral("Enter a valid HTTP or HTTPS website without embedded credentials.");
        return false;
    }
    if (!QDesktopServices::openUrl(url)) {
        if (details)
            *details = QStringLiteral("Windows could not ask the default browser to open %1.").arg(url.toString());
        return false;
    }
    if (details)
        *details = QStringLiteral("Asked the default browser to open %1.").arg(url.toString());
    return true;
}

bool ToolRegistry::openPath(const QString &path, QString *details) const
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (details)
            *details = QStringLiteral("The selected file or folder no longer exists.");
        return false;
    }
    if (info.isFile()) {
        static const QSet<QString> blockedExtensions = {
            QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("bat"), QStringLiteral("cmd"),
            QStringLiteral("ps1"), QStringLiteral("psm1"), QStringLiteral("msi"), QStringLiteral("msp"),
            QStringLiteral("scr"), QStringLiteral("lnk"), QStringLiteral("url"), QStringLiteral("vbs"),
            QStringLiteral("js"), QStringLiteral("jse"), QStringLiteral("wsf"), QStringLiteral("hta"),
            QStringLiteral("reg"), QStringLiteral("jar")
        };
        if (blockedExtensions.contains(info.suffix().toLower())) {
            if (details)
                *details = QStringLiteral("Opening executable, script, installer, or shortcut files is disabled from this tool.");
            return false;
        }
    }
    const QString absolutePath = info.absoluteFilePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))) {
        if (details)
            *details = QStringLiteral("Windows could not open the selected path.");
        return false;
    }
    if (details)
        *details = QStringLiteral("Asked Windows to open %1.").arg(absolutePath);
    return true;
}

bool ToolRegistry::createNote(const QString &title, const QString &content, QString *savedPath,
                              QString *error) const
{
    QString safeTitle = title.trimmed();
    safeTitle.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}._-]+")), QStringLiteral("-"));
    safeTitle.remove(QRegularExpression(QStringLiteral("^[.-]+|[.-]+$")));
    if (safeTitle.isEmpty())
        safeTitle = QStringLiteral("JARVIS-note");
    if (safeTitle.size() > 80)
        safeTitle.truncate(80);

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        if (error)
            *error = QStringLiteral("Windows did not provide a Documents folder path.");
        return false;
    }
    const QString folder = QDir(documents).filePath(QStringLiteral("JARVIS/Notes"));
    if (!QDir().mkpath(folder)) {
        if (error)
            *error = QStringLiteral("Could not create the JARVIS Notes folder under Documents.");
        return false;
    }
    QString filePath = QDir(folder).filePath(safeTitle + QStringLiteral(".txt"));
    if (QFileInfo::exists(filePath)) {
        const QString suffix = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        filePath = QDir(folder).filePath(safeTitle + QLatin1Char('-') + suffix + QStringLiteral(".txt"));
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not create the note: %1").arg(file.errorString());
        return false;
    }
    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Could not save the note: %1").arg(file.errorString());
        return false;
    }
    if (savedPath)
        *savedPath = filePath;
    return true;
}

bool ToolRegistry::startFileSearch(const QString &rootDirectory, const QString &query, QString *error)
{
    const QFileInfo root(rootDirectory);
    if (!root.exists() || !root.isDir()) {
        if (error)
            *error = QStringLiteral("Choose an existing folder to search.");
        return false;
    }
    const QString searchTerm = query.trimmed();
    if (searchTerm.isEmpty()) {
        if (error)
            *error = QStringLiteral("Enter part of a file name to search for.");
        return false;
    }
    if (fileSearchRunning()) {
        if (error)
            *error = QStringLiteral("A file search is already running.");
        return false;
    }
    if (searchThread_) {
        searchThread_->deleteLater();
        searchThread_ = nullptr;
    }

    cancelSearch_.store(false, std::memory_order_relaxed);
    const QString startPath = root.absoluteFilePath();
    searchThread_ = QThread::create([this, startPath, searchTerm] {
        QStringList matches;
        qint64 scanned = 0;
        QString resultMessage;
        constexpr qsizetype maximumResults = 5000;
        QDirIterator iterator(startPath, QDir::Files | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            if (cancelSearch_.load(std::memory_order_relaxed))
                break;
            const QString path = iterator.next();
            ++scanned;
            if (QFileInfo(path).fileName().contains(searchTerm, Qt::CaseInsensitive)) {
                matches.append(path);
                if (matches.size() >= maximumResults) {
                    resultMessage = QStringLiteral("Search stopped after reaching the 5,000-result display limit.");
                    break;
                }
            }
            if (scanned % 1000 == 0) {
                const qint64 progressScanned = scanned;
                const int matchCount = matches.size();
                QMetaObject::invokeMethod(this, [this, progressScanned, matchCount] {
                    emit fileSearchProgress(progressScanned, matchCount);
                }, Qt::QueuedConnection);
            }
        }
        const bool cancelled = cancelSearch_.load(std::memory_order_relaxed);
        QMetaObject::invokeMethod(this, [this, matches, scanned, cancelled, resultMessage] {
            emit fileSearchFinished(matches, scanned, cancelled, resultMessage);
        }, Qt::QueuedConnection);
    });
    connect(searchThread_, &QThread::finished, this, [this, thread = searchThread_] {
        if (searchThread_ == thread)
            searchThread_ = nullptr;
        thread->deleteLater();
    });
    searchThread_->start();
    return true;
}

bool ToolRegistry::fileSearchRunning() const
{
    return searchThread_ && searchThread_->isRunning();
}

void ToolRegistry::cancelFileSearch()
{
    if (fileSearchRunning())
        cancelSearch_.store(true, std::memory_order_relaxed);
}
