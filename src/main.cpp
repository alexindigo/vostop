#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QLoggingCategory>

#include "backend/Settings.h"

Q_LOGGING_CATEGORY(vostop, "vostop")

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    //? Org/app names: vostop's Settings persistence (QSettings) keys off these
    QGuiApplication::setOrganizationName(QStringLiteral("vostop"));
    QGuiApplication::setApplicationName(QStringLiteral("vostop"));

    //? The collector engine (topqml's TopEngine) starts lazily on the first
    //? Top singleton touch — no wiring here. Snapshot metatypes, the worker
    //? thread, and the monitor connections all live in the library now.

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Vostop/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
