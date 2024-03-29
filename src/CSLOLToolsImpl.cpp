#include "CSLOLToolsImpl.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QStandardPaths>
#include <QThread>
#include <fstream>

CSLOLToolsImpl::CSLOLToolsImpl(QObject* parent) : QObject(parent), prog_(QCoreApplication::applicationDirPath()) {}

CSLOLToolsImpl::~CSLOLToolsImpl() {
    if (lockfile_) {
        delete lockfile_;
    }
}

CSLOLToolsImpl::CSLOLState CSLOLToolsImpl::getState() { return state_; }

void CSLOLToolsImpl::setState(CSLOLState value) {
    if (state_ != value) {
        state_ = value;
        emit stateChanged(value);
    }
}

void CSLOLToolsImpl::setStatus(QString status) {
    if (status_ != status) {
        status_ = status;
        emit statusChanged(status);
    }
}

QString CSLOLToolsImpl::getLeaguePath() { return game_; }

/// util

static QJsonObject modInfoFixup(QString modName, QJsonObject object) {
    if (!object.contains("Name") || !object["Name"].isString() || object["Name"].toString().isEmpty()) {
        object["Name"] = modName;
    }
    if (!object.contains("Version") || !object["Version"].isString()) {
        object["Version"] = "0.0.0";
    }
    if (!object.contains("Author") || !object["Author"].isString()) {
        object["Author"] = "UNKNOWN";
    }
    if (!object.contains("Description") || !object["Description"].isString()) {
        object["Description"] = "";
    }
    return object;
}

QStringList CSLOLToolsImpl::modList() {
    auto result = QStringList();
    for (auto it = QDirIterator(prog_ + "/installed", QDir::Dirs); it.hasNext();) {
        auto path = it.next();
        if (path.endsWith(".tmp")) continue;
        auto name = QFileInfo(path).fileName();
        if (name == "." || name == "..") continue;
        if (auto meta = QFileInfo(path + "/META/info.json"); !meta.exists()) continue;
        result.push_back(name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CSLOLToolsImpl::modWadsList(QString modName) {
    auto result = QStringList();
    for (QDirIterator it(prog_ + "/installed/" + modName + "/WAD", {"*.wad.client"}, QDir::Files); it.hasNext();) {
        auto path = it.next();
        if (path.endsWith(".tmp")) continue;
        auto name = QFileInfo(path).fileName();
        result.push_back(name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QJsonObject CSLOLToolsImpl::modInfoRead(QString modName) {
    auto data = QByteArray("{}", 2);
    if (QFile file(prog_ + "/installed/" + modName + "/META/info.json"); file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
    }
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(data, &error);
    if (!document.isObject()) {
        return modInfoFixup(modName, QJsonObject());
    }
    return modInfoFixup(modName, document.object());
}

bool CSLOLToolsImpl::modInfoWrite(QString modName, QJsonObject object) {
    QDir(prog_ + "/installed/" + modName).mkpath("META");
    auto data = QJsonDocument(modInfoFixup(modName, object)).toJson();
    if (QFile file(prog_ + "/installed/" + modName + "/META/info.json"); file.open(QIODevice::WriteOnly)) {
        file.write(data);
        return true;
    }
    return false;
}

QString CSLOLToolsImpl::modImageGet(QString modName) {
    auto path = prog_ + "/installed/" + modName + "/META/image.png";
    if (QFileInfo info(prog_); !info.exists()) {
        return "";
    }
    return path;
}

QString CSLOLToolsImpl::modImageSet(QString modName, QString image) {
    QDir(prog_ + "/installed/" + modName).mkpath("META");
    auto path = prog_ + "/installed/" + modName + "/META/image.png";
    if (image.isEmpty()) {
        QFile::remove(path);
        return "";
    }
    if (path == image) return path;
    if (QFile src(image); src.open(QIODevice::ReadOnly)) {
        if (QFile dst(path); dst.open(QIODevice::WriteOnly)) {
            dst.write(src.readAll());
            return path;
        }
    }
    return "";
}

QStringList CSLOLToolsImpl::listProfiles() {
    if (QDir dir(prog_); !dir.exists()) {
        dir.mkpath("profiles");
    }
    QStringList profiles;
    for (QDirIterator it(prog_ + "/profiles", QDir::Dirs); it.hasNext();) {
        auto info = QFileInfo(it.next());
        auto name = info.fileName();
        if (name == "." || name == "..") continue;
        profiles.push_back(name);
    }
    if (!profiles.contains("Default Profile")) {
        profiles.push_front("Default Profile");
    }
    return profiles;
}

QJsonObject CSLOLToolsImpl::readProfile(QString profileName) {
    QJsonObject profile;
    auto data = QString("");
    if (QFile file(prog_ + "/profiles/" + profileName + ".profile"); file.open(QIODevice::ReadOnly)) {
        data = QString::fromUtf8(file.readAll());
    }
    for (auto line : data.split('\n', Qt::SkipEmptyParts)) {
        profile.insert(line.remove('\n'), true);
    }
    return profile;
}

void CSLOLToolsImpl::writeProfile(QString profileName, QJsonObject profile) {
    QDir profilesDir(prog_);
    profilesDir.mkpath("profiles");
    if (QFile file(prog_ + "/profiles/" + profileName + ".profile"); file.open(QIODevice::WriteOnly)) {
        for (auto mod : profile.keys()) {
            auto data = mod.toUtf8();
            if (data.size() == 0) {
                continue;
            }
            data.push_back('\n');
            file.write(data);
        }
    }
}

QString CSLOLToolsImpl::readCurrentProfile() {
    auto data = QString("");
    if (QFile file(prog_ + "/current.profile"); file.open(QIODevice::ReadOnly)) {
        data = QString::fromUtf8(file.readAll()).remove('\n');
    }
    if (data.isEmpty()) {
        data = "Default Profile";
    }
    return data;
}

void CSLOLToolsImpl::writeCurrentProfile(QString profile) {
    if (QFile file(prog_ + "/current.profile"); file.open(QIODevice::WriteOnly)) {
        auto data = profile.toUtf8();
        data.push_back('\n');
        file.write(data);
    }
}

/// impl

void CSLOLToolsImpl::changeLeaguePath(QString newLeaguePath) {
    if (state_ == CSLOLState::StateIdle || state_ == CSLOLState::StateUnitialized) {
        if (state_ != CSLOLState::StateUnitialized) {
            setState(CSLOLState::StateBusy);
            setStatus("Change League Path");
        }

        if (auto info = QFileInfo(newLeaguePath + "/League of Legends.exe"); info.exists()) {
            newLeaguePath = info.canonicalPath();
        }
        if (auto info = QFileInfo(newLeaguePath + "/League of Legends.app"); info.exists()) {
            newLeaguePath = info.canonicalPath();
        }
        if (game_ != newLeaguePath) {
            game_ = newLeaguePath;
            emit leaguePathChanged(newLeaguePath);
        }

        if (state_ != CSLOLState::StateUnitialized) {
            setState(CSLOLState::StateIdle);
        }
    }
}

void CSLOLToolsImpl::changeBlacklist(bool blacklist) {
    if (blacklist_ != blacklist) {
        blacklist_ = blacklist;
        emit blacklistChanged(blacklist);
    }
}

void CSLOLToolsImpl::changeIgnorebad(bool ignorebad) {
    if (ignorebad_ != ignorebad) {
        ignorebad_ = ignorebad;
        emit ignorebadChanged(ignorebad);
    }
}

void CSLOLToolsImpl::init() {
    if (state_ == CSLOLState::StateUnitialized) {
        setState(CSLOLState::StateBusy);

        patcherProcess_ = nullptr;
        setStatus("Acquire lock");
        lockfile_ = new QLockFile(prog_ + "/lockfile");
        if (!lockfile_->tryLock()) {
            auto lockerror = QString::number((int)lockfile_->error());
            emit reportError("Acquire lock", "Can not run multiple instances", lockerror);
            setState(CSLOLState::StateCriticalError);
            return;
        }

        setStatus("Check mod-tools");
        if (QFileInfo modtools(prog_ + "/cslol-tools/mod-tools.exe"); !modtools.exists()) {
            emit reportError("Check mod-tools",
                             "cslol-tools/mod-tools.exe is missing",
                             "Make sure you installed properly and that anti-virus isn't blocking any executables.");
            setState(CSLOLState::StateCriticalError);
            return;
        }

        setStatus("Load mods");
        QJsonObject mods;
        for (auto name : modList()) {
            auto info = modInfoRead(name);
            mods.insert(name, info);
        }

        setStatus("Load profiles");
        auto profiles = listProfiles();
        auto profileName = readCurrentProfile();
        if (!profiles.contains(profileName)) {
            profileName = "Default Profile";
            writeCurrentProfile(profileName);
        }

        setStatus("Read profile");
        auto profileMods = readProfile(profileName);

        emit initialized(mods, QJsonArray::fromStringList(profiles), profileName, profileMods);

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::deleteMod(QString name) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Delete mod");
        if (QDir dir(prog_ + "/installed/" + name); dir.removeRecursively()) {
            emit modDeleted(name);
        }

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::exportMod(QString name, QString dest) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Export mod");
        runTool(
            {
                "export",
                prog_ + "/installed/" + name,
                dest,
                "--game:" + game_,
                blacklist_ ? "--noTFT" : "",
            },
            [this](int code, QProcess* process) { setState(CSLOLState::StateIdle); });
    }
}

void CSLOLToolsImpl::installFantomeZip(QString path) {
    if (state_ == CSLOLState::StateIdle && !path.isEmpty()) {
        setState(CSLOLState::StateBusy);

        setStatus("Installing Mod");
        auto name = QFileInfo(path)
                        .fileName()
                        .replace(".zip", "")
                        .replace(".fantome", "")
                        .replace(".wad", "")
                        .replace(".client", "");
        auto dst = prog_ + "/installed/" + name;
        if (QDir old(dst); old.exists()) {
            auto info = modInfoRead(name);
            emit reportError("Install mod", "Already exists", "");
            setState(CSLOLState::StateIdle);
            return;
        }

        runTool(
            {
                "import",
                path,
                dst,
                "--game:" + game_,
                blacklist_ ? "--noTFT" : "",
            },
            [=, this](int code, QProcess* process) {
                if (code == 0) {
                    auto info = modInfoRead(name);
                    emit installedMod(name, info);
                }
                setState(CSLOLState::StateIdle);
            });
    }
}

void CSLOLToolsImpl::makeMod(QString fileName, QJsonObject infoData, QString image) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Make mod");
        if (!modInfoWrite(fileName, infoData)) {
            emit reportError("Make mod", "Failed to write mod info", "");
        } else {
            infoData = modInfoFixup(fileName, infoData);
            image = modImageSet(fileName, image);
            emit modCreated(fileName, infoData, image);
        }

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::saveProfile(QString name, QJsonObject mods, bool run, bool skipConflict) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Save profile");
        if (name.isEmpty() || name.isNull()) {
            name = "Default Profile";
        }
        writeCurrentProfile(name);
        writeProfile(name, mods);
        emit profileSaved(name, mods);

        setStatus("Write profile");
        runTool(
            {
                "mkoverlay",
                prog_ + "/installed",
                prog_ + "/profiles/" + name,
                "--game:" + game_,
                "--mods:" + mods.keys().join('/'),
                blacklist_ ? "--noTFT" : "",
                skipConflict ? "--ignoreConflict" : "",
            },
            [=, this](int code, QProcess* process) {
                if (run && code == 0) {
                    setStatus("Starting patcher...");
                    runPatcher({
                        "runoverlay",
                        prog_ + "/profiles/" + name,
                        prog_ + "/profiles/" + name + ".config",
                        "--game:" + game_,
                    });
                } else {
                    setState(CSLOLState::StateIdle);
                }
            });
    }
}

void CSLOLToolsImpl::loadProfile(QString name) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Save profile");
        if (name.isEmpty() || name.isNull()) {
            name = "Default Profile";
        }
        auto profileMods = readProfile(name);
        emit profileLoaded(name, profileMods);

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::deleteProfile(QString name) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Delete profile");
        if (QDir dir(prog_ + "/profiles/" + name); dir.removeRecursively()) {
            emit profileDeleted(name);
        }

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::stopProfile() {
    if (state_ == CSLOLState::StateRunning) {
        // setState(CSLOLState::StateStoping);
        if (patcherProcess_ != nullptr) {
            patcherProcess_->write("\n");
        }
    }
}

void CSLOLToolsImpl::startEditMod(QString fileName) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Edit mod");
        auto info = modInfoRead(fileName);
        auto image = modImageGet(fileName);
        auto wads = modWadsList(fileName);
        emit modEditStarted(fileName, info, image, QJsonArray::fromStringList(wads));

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::changeModInfo(QString fileName, QJsonObject infoData, QString image) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Change mod info");
        if (!modInfoWrite(fileName, infoData)) {
            emit reportError("Change mod info", "", "Failed to write mod info");
        } else {
            infoData = modInfoFixup(fileName, infoData);
            image = modImageSet(fileName, image);
            emit modInfoChanged(fileName, infoData, image);
        }

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::removeModWads(QString fileName, QJsonArray wads) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Remove mod wads");
        auto result = QStringList();
        for (auto wadName : wads) {
            auto name = wadName.toString();
            if (QFile::remove(prog_ + "/installed/" + fileName + "/WAD/" + name)) {
                result.push_back(name);
            }
        }
        emit modWadsRemoved(fileName, QJsonArray::fromStringList(result));

        setState(CSLOLState::StateIdle);
    }
}

void CSLOLToolsImpl::addModWad(QString fileName, QString wad, bool removeUnknownNames) {
    if (state_ == CSLOLState::StateIdle) {
        setState(CSLOLState::StateBusy);

        setStatus("Add mod wads");
        auto before = modWadsList(fileName);
        runTool(
            {
                "addwad",
                wad,
                prog_ + "/installed/" + fileName,
                "--game:" + game_,
                removeUnknownNames ? "--removeUNK" : "",
                blacklist_ ? "--noTFT" : "",
            },
            [=, this](int code, QProcess* process) {
                if (code == 0) {
                    auto after = modWadsList(fileName);
                    auto done = QStringList();
                    for (auto wad : after) {
                        if (!before.contains(wad, Qt::CaseInsensitive)) {
                            done.push_back(wad);
                        }
                    }
                    emit modWadsAdded(fileName, QJsonArray::fromStringList(done));
                }
                setState(CSLOLState::StateIdle);
            });
    }
}

void CSLOLToolsImpl::runTool(QStringList args, std::function<void(int code, QProcess*)> handle) {
    auto process = new QProcess(this);
    connect(process, &QProcess::readyReadStandardOutput, this, [=, this]() {
        process->setReadChannel(QProcess::ProcessChannel::StandardOutput);
        while (process->canReadLine()) {
            auto line = process->readLine().trimmed();
            emit processLog(line);
        }
    });
    connect(process,
            static_cast<void (QProcess::*)(int exitCode, QProcess::ExitStatus exitStatus)>(&QProcess::finished),
            this,
            [=, this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitCode != 0) {
                    QString message = "Run mod-tools";
                    QString stack_trace = process->readAllStandardError();
                    emit reportError("Exit with error", message, stack_trace);
                }
                handle(exitCode, process);
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QString message = process->errorString();
            QString path = process->program();
            if (QFileInfo pathInfo(path); !pathInfo.exists()) {
                message = "Make sure you installed properly and that anti-virus isn't blocking any executables.";
            }
            QString stack_trace = "arguments:\n  " + args.join("\n  ").replace('\\', '/') + "\n";
            emit reportError("Failed to run mod-tools", message, stack_trace);
            handle(-1, process);
            process->deleteLater();
        }
    });
    process->start(prog_ + "/cslol-tools/mod-tools.exe", args);
}

void CSLOLToolsImpl::runPatcher(QStringList args) {
    if (patcherProcess_ == nullptr) {
        patcherProcess_ = new QProcess(this);
        connect(patcherProcess_, &QProcess::readyReadStandardOutput, this, [=, this]() {
            patcherProcess_->setReadChannel(QProcess::ProcessChannel::StandardOutput);
            while (patcherProcess_->canReadLine()) {
                auto line = patcherProcess_->readLine().trimmed();
                if (auto idx = line.indexOf("Status: "); idx >= 0) {
                    line.remove(0, idx + 8);
                    setStatus(line);
                } else {
                    emit processLog(line);
                }
            }
        });
        connect(patcherProcess_, &QProcess::started, this, [this] { setState(CSLOLState::StateRunning); });
        connect(patcherProcess_,
                static_cast<void (QProcess::*)(int exitCode, QProcess::ExitStatus exitStatus)>(&QProcess::finished),
                this,
                [=, this](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (exitCode != 0) {
                        auto message = "Run mod-tools";
                        QString path = patcherProcess_->program();
                        if (QFileInfo pathInfo(path); !pathInfo.exists()) {
                            message =
                                "Make sure you installed properly and that anti-virus isn't blocking any executables.";
                        }
                        auto stack_trace = patcherProcess_->readAllStandardError();
                        emit reportError("Exit with error", message, stack_trace);
                    }
                    setState(CSLOLState::StateIdle);
                });
        connect(patcherProcess_, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                auto message = patcherProcess_->errorString();
                auto stack_trace =
                    "arguments:\n  " + patcherProcess_->arguments().join("\n  ").replace('\\', '/') + "\n";
                emit reportError("Failed to run mod-tools", message, stack_trace);
                setState(CSLOLState::StateIdle);
            }
        });
    }
    patcherProcess_->start(prog_ + "/cslol-tools/mod-tools.exe", args);
}
