/*
 * vostop — DiskMonitor / NetMonitor: GUI-thread property holders fed by the
 * worker's disk/net collectors (stolen code; phase 4).
 */
#pragma once

#include <QObject>
#include <QList>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "snapshots.h"

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class DiskMonitor : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//? Mount rows: dev/mount/fstype/used/total/ioRead/ioWrite
	Q_PROPERTY(QVariantList mounts READ mounts NOTIFY mountsChanged FINAL)
	//? io PSI (parity addition; valid=false → UI hides pressure rows)
	Q_PROPERTY(bool ioPressureValid READ ioPressureValid NOTIFY ioPressureChanged FINAL)
	Q_PROPERTY(QList<double> ioPressureSome READ ioPressureSome NOTIFY ioPressureChanged FINAL)
	Q_PROPERTY(QList<double> ioPressureFull READ ioPressureFull NOTIFY ioPressureChanged FINAL)

public:
	explicit DiskMonitor(QObject* parent = nullptr);

	static DiskMonitor& instance();
	static DiskMonitor* create(QQmlEngine* engine, QJSEngine* jsEngine);

	QVariantList mounts() const { return m_mounts; }
	bool ioPressureValid() const { return m_ioPressure.valid; }
	QList<double> ioPressureSome() const;
	QList<double> ioPressureFull() const;

public slots:
	void update(const DiskSnapshot& snapshot);

signals:
	void mountsChanged();
	void ioPressureChanged();

private:
	QVariantList m_mounts;
	PressureSnapshot m_ioPressure;
};

class NetMonitor : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	Q_PROPERTY(QString iface READ iface NOTIFY netChanged FINAL)
	Q_PROPERTY(QStringList ifaces READ ifaces NOTIFY ifacesChanged FINAL)
	Q_PROPERTY(QString ipv4 READ ipv4 NOTIFY netChanged FINAL)
	Q_PROPERTY(QString ipv6 READ ipv6 NOTIFY netChanged FINAL)
	Q_PROPERTY(bool connected READ connected NOTIFY netChanged FINAL)
	Q_PROPERTY(qint64 downSpeed READ downSpeed NOTIFY netChanged FINAL)
	Q_PROPERTY(qint64 upSpeed READ upSpeed NOTIFY netChanged FINAL)
	Q_PROPERTY(qint64 downTotal READ downTotal NOTIFY netChanged FINAL)
	Q_PROPERTY(qint64 upTotal READ upTotal NOTIFY netChanged FINAL)
	Q_PROPERTY(QList<double> downHistory READ downHistory NOTIFY historyChanged FINAL)
	Q_PROPERTY(QList<double> upHistory READ upHistory NOTIFY historyChanged FINAL)

public:
	explicit NetMonitor(QObject* parent = nullptr);

	static NetMonitor& instance();
	static NetMonitor* create(QQmlEngine* engine, QJSEngine* jsEngine);

	QString iface() const { return m_iface; }
	QStringList ifaces() const { return m_ifaces; }
	QString ipv4() const { return m_ipv4; }
	QString ipv6() const { return m_ipv6; }
	bool connected() const { return m_connected; }
	qint64 downSpeed() const { return m_downSpeed; }
	qint64 upSpeed() const { return m_upSpeed; }
	qint64 downTotal() const { return m_downTotal; }
	qint64 upTotal() const { return m_upTotal; }
	QList<double> downHistory() const { return m_downHistory; }
	QList<double> upHistory() const { return m_upHistory; }

	//* QML iface picker (empty string = auto)
	Q_INVOKABLE void selectIface(const QString& iface);

public slots:
	void update(const NetSnapshot& snapshot);

signals:
	void netChanged();
	void ifacesChanged();
	void historyChanged();

private:
	QString m_iface, m_ipv4, m_ipv6;
	QStringList m_ifaces;
	bool m_connected = false;
	qint64 m_downSpeed = 0, m_upSpeed = 0, m_downTotal = 0, m_upTotal = 0;
	QList<double> m_downHistory, m_upHistory;
};