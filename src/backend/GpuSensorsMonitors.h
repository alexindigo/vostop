/*
 * vostop — GpuMonitor / SensorsMonitor: GUI-thread property holders for the
 * phase-5 GPU/sensor collectors. Cards set `visible` from the available flags.
 */
#pragma once

#include <QObject>
#include <QList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "snapshots.h"

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class GpuMonitor : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//? Per-device rows; empty when no supported GPU → card hides itself
	Q_PROPERTY(QVariantList gpus READ gpus NOTIFY gpusChanged FINAL)
	Q_PROPERTY(bool available READ available NOTIFY gpusChanged FINAL)

public:
	explicit GpuMonitor(QObject* parent = nullptr);

	static GpuMonitor& instance();
	static GpuMonitor* create(QQmlEngine* engine, QJSEngine* jsEngine);

	QVariantList gpus() const { return m_gpus; }
	bool available() const { return m_available; }

public slots:
	void update(const GpuSnapshot& snapshot);

signals:
	void gpusChanged();

private:
	QVariantList m_gpus;
	bool m_available = false;
};

class SensorsMonitor : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//? Temps: hidden when no sensor found (empty-state instead)
	Q_PROPERTY(bool sensorsAvailable READ sensorsAvailable NOTIFY sensorsChanged FINAL)
	Q_PROPERTY(QString cpuSensorName READ cpuSensorName NOTIFY sensorsChanged FINAL)
	Q_PROPERTY(double cpuTemp READ cpuTemp NOTIFY sensorsChanged FINAL)
	Q_PROPERTY(QList<double> coreTemps READ coreTemps NOTIFY sensorsChanged FINAL)
	//? Battery
	Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryChanged FINAL)
	Q_PROPERTY(int batteryPct READ batteryPct NOTIFY batteryChanged FINAL)
	Q_PROPERTY(double batteryWatts READ batteryWatts NOTIFY batteryChanged FINAL)
	Q_PROPERTY(long batterySeconds READ batterySeconds NOTIFY batteryChanged FINAL)
	Q_PROPERTY(QString batteryStatus READ batteryStatus NOTIFY batteryChanged FINAL)
	//? RAPL package watts
	Q_PROPERTY(bool cpuWattsAvailable READ cpuWattsAvailable NOTIFY wattsChanged FINAL)
	Q_PROPERTY(double cpuWatts READ cpuWatts NOTIFY wattsChanged FINAL)

public:
	explicit SensorsMonitor(QObject* parent = nullptr);

	static SensorsMonitor& instance();
	static SensorsMonitor* create(QQmlEngine* engine, QJSEngine* jsEngine);

	bool sensorsAvailable() const { return m_sensorsAvailable; }
	QString cpuSensorName() const { return m_cpuSensorName; }
	double cpuTemp() const { return m_cpuTemp; }
	QList<double> coreTemps() const { return m_coreTemps; }
	bool batteryAvailable() const { return m_batteryAvailable; }
	int batteryPct() const { return m_batteryPct; }
	double batteryWatts() const { return m_batteryWatts; }
	long batterySeconds() const { return m_batterySeconds; }
	QString batteryStatus() const { return m_batteryStatus; }
	bool cpuWattsAvailable() const { return m_cpuWattsAvailable; }
	double cpuWatts() const { return m_cpuWatts; }

public slots:
	void update(const SensorsSnapshot& snapshot);

signals:
	void sensorsChanged();
	void batteryChanged();
	void wattsChanged();

private:
	bool m_sensorsAvailable = false;
	QString m_cpuSensorName;
	double m_cpuTemp = 0.0;
	QList<double> m_coreTemps;
	bool m_batteryAvailable = false;
	int m_batteryPct = 0;
	double m_batteryWatts = 0.0;
	long m_batterySeconds = 0;
	QString m_batteryStatus;
	bool m_cpuWattsAvailable = false;
	double m_cpuWatts = 0.0;
};