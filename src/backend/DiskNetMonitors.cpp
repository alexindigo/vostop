/*
 * vostop — DiskMonitor / NetMonitor (implementation).
 */
#include "DiskNetMonitors.h"

#include "CollectorWorker.h"
#include "Settings.h"
#include "../collect/disk_net.h"

#include <QQmlEngine>

DiskMonitor::DiskMonitor(QObject* parent) : QObject(parent) {}

DiskMonitor& DiskMonitor::instance() {
	static DiskMonitor inst;
	return inst;
}

DiskMonitor* DiskMonitor::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	DiskMonitor* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

QList<double> DiskMonitor::ioPressureSome() const {
	return {m_ioPressure.some[0], m_ioPressure.some[1], m_ioPressure.some[2]};
}

QList<double> DiskMonitor::ioPressureFull() const {
	return {m_ioPressure.full[0], m_ioPressure.full[1], m_ioPressure.full[2]};
}

void DiskMonitor::update(const DiskSnapshot& snapshot) {
	//? Rebuild the mount list (slow-changing data; stolen per-mount stats)
	QVariantList newList;
	for (const auto& m : snapshot.mounts) {
		QVariantMap row;
		row["dev"] = m.dev;
		row["name"] = m.name;
		row["fstype"] = m.fstype;
		row["mountpoint"] = m.mountpoint;
		row["total"] = m.total;
		row["used"] = m.used;
		row["free"] = m.free;
		row["usedPercent"] = m.usedPercent;
		row["freePercent"] = m.freePercent;
		row["ioRead"] = m.ioRead;
		row["ioWrite"] = m.ioWrite;
		row["ioActivity"] = m.ioActivity;
		newList.append(row);
	}
	if (newList != m_mounts) {
		m_mounts = newList;
		emit mountsChanged();
	}

	if (m_ioPressure.valid != snapshot.ioPressureValid
		or m_ioPressure.some[0] != snapshot.ioPressureSome[0]
		or m_ioPressure.some[1] != snapshot.ioPressureSome[1]
		or m_ioPressure.some[2] != snapshot.ioPressureSome[2]
		or m_ioPressure.full[0] != snapshot.ioPressureFull[0]
		or m_ioPressure.full[1] != snapshot.ioPressureFull[1]
		or m_ioPressure.full[2] != snapshot.ioPressureFull[2]) {
		m_ioPressure.some[0] = snapshot.ioPressureSome[0];
		m_ioPressure.some[1] = snapshot.ioPressureSome[1];
		m_ioPressure.some[2] = snapshot.ioPressureSome[2];
		m_ioPressure.full[0] = snapshot.ioPressureFull[0];
		m_ioPressure.full[1] = snapshot.ioPressureFull[1];
		m_ioPressure.full[2] = snapshot.ioPressureFull[2];
		m_ioPressure.hasFull = snapshot.ioPressureHasFull;
		m_ioPressure.valid = snapshot.ioPressureValid;
		m_ioPressureHistory = snapshot.ioPressureHistory;
		emit ioPressureChanged();
	}
}

NetMonitor::NetMonitor(QObject* parent) : QObject(parent) {}

NetMonitor& NetMonitor::instance() {
	static NetMonitor inst;
	return inst;
}

NetMonitor* NetMonitor::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	NetMonitor* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

void NetMonitor::selectIface(const QString& iface) {
	Net::set_selected_iface(iface.toStdString());
	Settings::instance()->set_netIface(iface); //? persist the picker choice (phase-4 settings key)
}

void NetMonitor::update(const NetSnapshot& snapshot) {
	bool changed = m_iface != snapshot.iface or m_ipv4 != snapshot.ipv4 or m_ipv6 != snapshot.ipv6
		or m_connected != snapshot.connected
		or m_downSpeed != snapshot.downSpeed or m_upSpeed != snapshot.upSpeed
		or m_downTotal != snapshot.downTotal or m_upTotal != snapshot.upTotal;
	if (changed) {
		m_iface = snapshot.iface;
		m_ipv4 = snapshot.ipv4;
		m_ipv6 = snapshot.ipv6;
		m_connected = snapshot.connected;
		m_downSpeed = snapshot.downSpeed;
		m_upSpeed = snapshot.upSpeed;
		m_downTotal = snapshot.downTotal;
		m_upTotal = snapshot.upTotal;
		emit netChanged();
	}
	if (m_ifaces != snapshot.ifaces) {
		m_ifaces = snapshot.ifaces;
		emit ifacesChanged();
	}
	if (m_downHistory != snapshot.downHistory or m_upHistory != snapshot.upHistory) {
		m_downHistory = snapshot.downHistory;
		m_upHistory = snapshot.upHistory;
		emit historyChanged();
	}
}