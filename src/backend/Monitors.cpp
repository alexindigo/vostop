/*
 * vostop — CpuMonitor / MemMonitor (implementation).
 */
#include "Monitors.h"

#include <QQmlEngine>

namespace {
template <typename T>
bool changed(const T& a, const T& b) { return a != b; }
}CpuMonitor::CpuMonitor(QObject* parent) : QObject(parent) {}

CpuMonitor& CpuMonitor::instance() {
	static CpuMonitor inst;
	return inst;
}

CpuMonitor* CpuMonitor::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	CpuMonitor* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

QList<double> CpuMonitor::pressureSome() const {
	return {m_pressure.some[0], m_pressure.some[1], m_pressure.some[2]};
}

QList<double> CpuMonitor::pressureFull() const {
	return {m_pressure.full[0], m_pressure.full[1], m_pressure.full[2]};
}

void CpuMonitor::update(const CpuSnapshot& snapshot) {
	if (changed(m_usage, snapshot.usage)) {
		m_usage = snapshot.usage;
		emit usageChanged();
	}
	if (changed(m_perCore, snapshot.perCore) or changed(m_coreHistories.size(), snapshot.coreHistories.size())) {
		m_perCore = snapshot.perCore;
		m_coreHistories.clear();
		for (const auto& ring : snapshot.coreHistories)
			m_coreHistories.append(QVariant::fromValue(ring));
		emit perCoreChanged();
	}
	if (changed(m_freqText, snapshot.freqText)) {
		m_freqText = snapshot.freqText;
		emit freqTextChanged();
	}
	if (changed(m_load[0], snapshot.loadAvg[0]) or changed(m_load[1], snapshot.loadAvg[1])
		or changed(m_load[2], snapshot.loadAvg[2])) {
		m_load[0] = snapshot.loadAvg[0];
		m_load[1] = snapshot.loadAvg[1];
		m_load[2] = snapshot.loadAvg[2];
		emit loadChanged();
	}
	if (changed(m_history, snapshot.history)) {
		m_history = snapshot.history;
		emit historyChanged();
	}
	if (changed(m_uptimeSec, snapshot.uptimeSec)) {
		m_uptimeSec = snapshot.uptimeSec;
		emit uptimeChanged();
	}
	if (changed(m_pressure.valid, snapshot.pressure.valid)
		or changed(m_pressure.some[0], snapshot.pressure.some[0])
		or changed(m_pressure.some[1], snapshot.pressure.some[1])
		or changed(m_pressure.some[2], snapshot.pressure.some[2])
		or changed(m_pressure.full[0], snapshot.pressure.full[0])
		or changed(m_pressure.full[1], snapshot.pressure.full[1])
		or changed(m_pressure.full[2], snapshot.pressure.full[2])
		or changed(m_pressureHistory, snapshot.pressureHistory)) {
		m_pressure = snapshot.pressure;
		m_pressureHistory = snapshot.pressureHistory;
		emit pressureChanged();
	}
	if (changed(m_cpuName, snapshot.cpuName)) {
		m_cpuName = snapshot.cpuName;
		emit cpuNameChanged();
	}
}

MemMonitor::MemMonitor(QObject* parent) : QObject(parent) {}

MemMonitor& MemMonitor::instance() {
	static MemMonitor inst;
	return inst;
}

MemMonitor* MemMonitor::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	MemMonitor* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

QList<double> MemMonitor::pressureSome() const {
	return {m_pressure.some[0], m_pressure.some[1], m_pressure.some[2]};
}

QList<double> MemMonitor::pressureFull() const {
	return {m_pressure.full[0], m_pressure.full[1], m_pressure.full[2]};
}

void MemMonitor::update(const MemSnapshot& snapshot) {
	bool memDiff = changed(m_total, snapshot.total) or changed(m_used, snapshot.used)
		or changed(m_free, snapshot.free) or changed(m_available, snapshot.available)
		or changed(m_cached, snapshot.cached) or changed(m_swapTotal, snapshot.swapTotal)
		or changed(m_swapUsed, snapshot.swapUsed) or changed(m_hasSwap, snapshot.hasSwap);
	if (memDiff) {
		m_total = snapshot.total;
		m_used = snapshot.used;
		m_free = snapshot.free;
		m_available = snapshot.available;
		m_cached = snapshot.cached;
		m_swapTotal = snapshot.swapTotal;
		m_swapUsed = snapshot.swapUsed;
		m_hasSwap = snapshot.hasSwap;
		emit memChanged();
	}
	if (changed(m_history, snapshot.history)) {
		m_history = snapshot.history;
		emit historyChanged();
	}
	if (changed(m_pressure.valid, snapshot.pressure.valid)
		or changed(m_pressure.some[0], snapshot.pressure.some[0])
		or changed(m_pressure.some[1], snapshot.pressure.some[1])
		or changed(m_pressure.some[2], snapshot.pressure.some[2])
		or changed(m_pressure.full[0], snapshot.pressure.full[0])
		or changed(m_pressure.full[1], snapshot.pressure.full[1])
		or changed(m_pressure.full[2], snapshot.pressure.full[2])
		or changed(m_pressureHistory, snapshot.pressureHistory)) {
		m_pressure = snapshot.pressure;
		m_pressureHistory = snapshot.pressureHistory;
		emit pressureChanged();
	}
}