#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QThread>
#include <QLoggingCategory>

#include "backend/snapshots.h"
#include "backend/CollectorWorker.h"
#include "backend/Monitors.h"
#include "backend/DiskNetMonitors.h"
#include "backend/GpuSensorsMonitors.h"
#include "backend/ProcessModel.h"
#include "backend/Settings.h"

Q_LOGGING_CATEGORY(vostop, "vostop")

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("vostop"));
    QGuiApplication::setApplicationName(QStringLiteral("vostop"));

    qRegisterMetaType<CpuSnapshot>("CpuSnapshot");
    qRegisterMetaType<MemSnapshot>("MemSnapshot");
    qRegisterMetaType<ProcSnapshot>("ProcSnapshot");
    qRegisterMetaType<ProcDetailSnapshot>("ProcDetailSnapshot");
    qRegisterMetaType<OpenFilesSnapshot>("OpenFilesSnapshot");
    qRegisterMetaType<DiskSnapshot>("DiskSnapshot");
    qRegisterMetaType<NetSnapshot>("NetSnapshot");
    qRegisterMetaType<GpuSnapshot>("GpuSnapshot");
    qRegisterMetaType<SensorsSnapshot>("SensorsSnapshot");

    CpuMonitor& cpuMonitor = CpuMonitor::instance();
    MemMonitor& memMonitor = MemMonitor::instance();
    ProcessModel& procModel = ProcessModel::instance();
    DiskMonitor& diskMonitor = DiskMonitor::instance();
    NetMonitor& netMonitor = NetMonitor::instance();
    GpuMonitor& gpuMonitor = GpuMonitor::instance();
    SensorsMonitor& sensorsMonitor = SensorsMonitor::instance();

    QThread collectorThread;
    CollectorWorker* worker = new CollectorWorker;
    worker->moveToThread(&collectorThread);
    QObject::connect(&collectorThread, &QThread::started, worker, &CollectorWorker::start);
    QObject::connect(&collectorThread, &QThread::finished, worker, &QObject::deleteLater);
    //? Queued (auto) connections: worker emits from the worker thread → GUI-thread monitors
    QObject::connect(worker, &CollectorWorker::cpuUpdated, &cpuMonitor, &CpuMonitor::update);
    QObject::connect(worker, &CollectorWorker::memUpdated, &memMonitor, &MemMonitor::update);
    QObject::connect(worker, &CollectorWorker::procUpdated, &procModel, &ProcessModel::update);
    QObject::connect(worker, &CollectorWorker::procDetailUpdated, &procModel, &ProcessModel::detailUpdated);
    QObject::connect(worker, &CollectorWorker::openFilesUpdated, &procModel, &ProcessModel::openFilesUpdated);
    QObject::connect(worker, &CollectorWorker::diskUpdated, &diskMonitor, &DiskMonitor::update);
    QObject::connect(worker, &CollectorWorker::netUpdated, &netMonitor, &NetMonitor::update);
    QObject::connect(worker, &CollectorWorker::gpuUpdated, &gpuMonitor, &GpuMonitor::update);
    QObject::connect(worker, &CollectorWorker::sensorsUpdated, &sensorsMonitor, &SensorsMonitor::update);

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