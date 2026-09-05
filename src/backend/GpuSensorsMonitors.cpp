/*
 * vostop — GpuMonitor / SensorsMonitor (implementation).
 */
#include "GpuSensorsMonitors.h"

#include <QQmlEngine>

GpuMonitor::GpuMonitor(QObject* parent) : QObject(parent) {}

GpuMonitor& GpuMonitor::instance() {
	static GpuMonitor inst;
	return inst;
}

GpuMonitor* GpuMonitor::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	GpuMonitor* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

void GpuMonitor::update(const GpuSnapshot& snapshot) {
	QVariantList newList;
	for (const auto& d : snapshot.devices) {
		QVariantMap row;
		row["name"] = d.name;
		row["util"] = d.util;
		row["memUtil"] = d.memUtil;
		row["memTotal"] = d.memTotal;
		row["memUsed"] = d.memUsed;
		row["temp"] = d.temp;
		row["tempMax"] = d.tempMax;
		row["powerMw"] = d.powerMw;
		row["powerMaxMw"] = d.powerMaxMw;
		row["clockMhz"] = d.clockMhz;
		row["memClockMhz"] = d.memClockMhz;
		row["encUtil"] = d.encUtil;
		row["decUtil"] = d.decUtil;
		row["approximate"] = d.approximate;
		row["pdev"] = d.pdev;
		newList.append(row);
	}
	if (newList != m_gpus) {
		m_gpus = newList;
		emit gpusChanged();
	}
	if (m_available != snapshot.available) {
		m_available = snapshot.available;
		emit gpusChanged();
	}
}

SensorsMonitor::SensorsMonitor(QObject* parent) : QObject(parent) {}

SensorsMonitor& SensorsMonitor::instance() {
	static SensorsMonitor inst;
	return inst;
}

SensorsMonitor* SensorsMonitor::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	SensorsMonitor* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

void SensorsMonitor::update(const SensorsSnapshot& snapshot) {
	if (m_sensorsAvailable != snapshot.sensorsAvailable
		or m_cpuSensorName != snapshot.cpuSensorName
		or m_cpuTemp != snapshot.cpuTemp
		or m_coreTemps != snapshot.coreTemps) {
		m_sensorsAvailable = snapshot.sensorsAvailable;
		m_cpuSensorName = snapshot.cpuSensorName;
		m_cpuTemp = snapshot.cpuTemp;
		m_coreTemps = snapshot.coreTemps;
		emit sensorsChanged();
	}
	if (m_batteryAvailable != snapshot.batteryAvailable
		or m_batteryPct != snapshot.batteryPct
		or m_batteryWatts != snapshot.batteryWatts
		or m_batterySeconds != snapshot.batterySeconds
		or m_batteryStatus != snapshot.batteryStatus) {
		m_batteryAvailable = snapshot.batteryAvailable;
		m_batteryPct = snapshot.batteryPct;
		m_batteryWatts = snapshot.batteryWatts;
		m_batterySeconds = snapshot.batterySeconds;
		m_batteryStatus = snapshot.batteryStatus;
		emit batteryChanged();
	}
	if (m_cpuWattsAvailable != snapshot.cpuWattsAvailable or m_cpuWatts != snapshot.cpuWatts) {
		m_cpuWattsAvailable = snapshot.cpuWattsAvailable;
		m_cpuWatts = snapshot.cpuWatts;
		emit wattsChanged();
	}
}