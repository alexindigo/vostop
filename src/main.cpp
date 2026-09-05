#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QThread>
#include <QLoggingCategory>

#include "backend/snapshots.h"
#include "backend/CollectorWorker.h"
#include "backend/Monitors.h"
#include "backend/Settings.h"

Q_LOGGING_CATEGORY(vostop, "vostop")

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("vostop"));
    QGuiApplication::setApplicationName(QStringLiteral("vostop"));

    qRegisterMetaType<CpuSnapshot>("CpuSnapshot");
    qRegisterMetaType<MemSnapshot>("MemSnapshot");

    CpuMonitor& cpuMonitor = CpuMonitor::instance();
    MemMonitor& memMonitor = MemMonitor::instance();

    QThread collectorThread;
    CollectorWorker* worker = new CollectorWorker;
    worker->moveToThread(&collectorThread);
    QObject::connect(&collectorThread, &QThread::started, worker, &CollectorWorker::start);
    QObject::connect(&collectorThread, &QThread::finished, worker, &QObject::deleteLater);
    //? Queued (auto) connections: worker emits from the worker thread → GUI-thread monitors
    QObject::connect(worker, &CollectorWorker::cpuUpdated, &cpuMonitor, &CpuMonitor::update);
    QObject::connect(worker, &CollectorWorker::memUpdated, &memMonitor, &MemMonitor::update);

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Vostop/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    collectorThread.start();
    const int rc = app.exec();
    QMetaObject::invokeMethod(worker, &CollectorWorker::stop, Qt::QueuedConnection);
    collectorThread.quit();
    collectorThread.wait();
    return rc;
}