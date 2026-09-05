#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(vostop, "vostop")

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Vostop/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}