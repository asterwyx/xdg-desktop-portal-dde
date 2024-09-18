// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screenshotportal.h"

#include "common.h"
#include "treelandcapture.h"

#include <private/qwaylandscreen_p.h>

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QRegion>
#include <QScreen>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(portalWayland, "dde.portal.wayland");

struct ScreenCaptureInfo
{
    QtWaylandClient::QWaylandScreen *screen{ nullptr };
    QPointer<ScreenshotFrame> capturedFrame{ nullptr };
    QImage capturedImage{};
};

static inline QString fullShotFileName(QString format)
{
    auto getXdgPicturesDir = [=]() -> QString {
        QString homeDir = qgetenv("HOME");
        if (homeDir.isEmpty()) {
            return "";
        }
        QString configHome = qgetenv("XDG_CONFIG_HOME");
        if (configHome.isEmpty()) {
            configHome = homeDir + QDir::separator() + ".config";
        }
        QFile dirsFile(configHome + QDir::separator() + "user-dirs.dirs");
        if (dirsFile.open(QIODevice::ReadOnly)) {
            QTextStream in(&dirsFile);
            const QString prefix("XDG_PICTURES_DIR=");
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#')) {
                    continue;
                }
                if (line.startsWith(prefix)) {
                    line.replace(prefix, "");
                    line.replace("\"", "");
                    if (line.contains("$HOME")) {
                        line.replace("$HOME", homeDir);
                    }
                    return line;
                }
            }
        }
        return "";
    };

    auto saveBasePath = getXdgPicturesDir();
    QDir saveBaseDir(saveBasePath);
    if (!saveBaseDir.exists())
        return "";
    QString picName;
    if (format == "PNG") {
        picName = "portal screenshot - " + QDateTime::currentDateTime().toString() + ".png";
    } else {
        return "";
    }
    return saveBaseDir.absoluteFilePath(picName);
}

ScreenshotPortalWayland::ScreenshotPortalWayland(PortalWaylandContext *context)
    : AbstractWaylandPortal(context)
{
}

uint ScreenshotPortalWayland::PickColor(
        const QDBusObjectPath &handle,
        const QString &app_id,
        const QString &parent_window, // might just ignore this argument now
        const QVariantMap &options,
        QVariantMap &results)
{
    // TODO Implement PickColor
    return 0;
}

QString ScreenshotPortalWayland::fullScreenShot()
{
    std::list<std::shared_ptr<ScreenCaptureInfo>> captureList;
    int pendingCapture = 0;
    auto screenCopyManager = context()->screenCopyManager();
    QEventLoop eventLoop;
    QRegion outputRegion;
    QImage::Format formatLast;
    // Capture each output
    for (auto screen : waylandDisplay()->screens()) {
        auto info = std::make_shared<ScreenCaptureInfo>();
        outputRegion += screen->geometry();
        auto output = screen->output();
        info->capturedFrame = screenCopyManager->captureOutput<ScreenshotFrame>(false, output);
        info->screen = screen;
        ++pendingCapture;
        captureList.push_back(info);
        connect(info->capturedFrame,
                &ScreenshotFrame::ready,
                this,
                [&formatLast, info, &pendingCapture, &eventLoop, this](QImage image) {
                    info->capturedImage = image;
                    formatLast = info->capturedImage.format();
                    if (--pendingCapture == 0) {
                        eventLoop.quit();
                    }
                });
        connect(info->capturedFrame, &ScreenshotFrame::failed, this, [&pendingCapture, &eventLoop] {
            if (--pendingCapture == 0) {
                eventLoop.quit();
            }
        });
    }
    eventLoop.exec();
    // Cat them according to layout
    QImage image(outputRegion.boundingRect().size(), formatLast);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto &info : std::as_const(captureList)) {
        if (!info->capturedImage.isNull()) {
            QRect targetRect = info->screen->geometry();
            // Convert to screen image local coordinates
            auto sourceRect = targetRect;
            sourceRect.moveTo(sourceRect.topLeft() - info->screen->geometry().topLeft());
            p.drawImage(targetRect, info->capturedImage, sourceRect);
        } else {
            qCWarning(portalWayland) << "image is null!!!";
        }
    }
    static const char *SaveFormat = "PNG";
    auto saveBasePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir saveBaseDir(saveBasePath);
    if (!saveBaseDir.exists())
        return "";
    QString picName = "portal screenshot - " + QDateTime::currentDateTime().toString() + ".png";
    if (image.save(saveBaseDir.absoluteFilePath(picName), SaveFormat)) {
        return saveBaseDir.absoluteFilePath(picName);
    } else {
        return "";
    }
}

QString ScreenshotPortalWayland::captureInteractively()
{
    auto captureManager = context()->treelandCaptureManager();
    auto captureContext = captureManager->getContext();
    if (!captureContext) {
        return "";
    }
    captureContext->selectSource(
            QtWayland::treeland_capture_context_v1::source_type_output
                    | QtWayland::treeland_capture_context_v1::source_type_window
                    | QtWayland::treeland_capture_context_v1::source_type_region,
            true,
            false,
            nullptr);
    QEventLoop loop;
    connect(captureContext, &TreeLandCaptureContext::sourceReady, &loop, &QEventLoop::quit);
    loop.exec();
    auto frame = captureContext->frame();
    QImage result;
    connect(frame, &TreeLandCaptureFrame::ready, this, [this, &result, &loop](QImage image) {
        result = image;
        loop.quit();
    });
    connect(frame, &TreeLandCaptureFrame::failed, &loop, &QEventLoop::quit);
    loop.exec();
    if (result.isNull())
        return "";
    auto saveBasePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir saveBaseDir(saveBasePath);
    if (!saveBaseDir.exists())
        return "";
    QString picName = "portal screenshot - " + QDateTime::currentDateTime().toString() + ".png";
    if (result.save(saveBaseDir.absoluteFilePath(picName), "PNG")) {
        return saveBaseDir.absoluteFilePath(picName);
    } else {
        return "";
    }
}

uint ScreenshotPortalWayland::Screenshot(const QDBusObjectPath &handle,
                                         const QString &app_id,
                                         const QString &parent_window,
                                         const QVariantMap &options,
                                         QVariantMap &results)
{
    if (options["modal"].toBool()) {
        // TODO if modal, we should block parent_window
    }
    QString filePath;
    if (options["interactive"].toBool()) {
        filePath = captureInteractively();
    } else {
        filePath = fullScreenShot();
    }
    if (filePath.isEmpty()) {
        return 1;
    }
    results.insert(QStringLiteral("uri"),
                   QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded));
    return 0;
}
