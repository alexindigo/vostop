/*
 * vostop — CpuMonitor / MemMonitor.
 *
 * Thin GUI-thread property holders. Slots receive snapshots from the
 * CollectorWorker over queued signals; NOTIFY fires only when a value
 * actually changed. History is a fixed 120-sample ring (plan.md §9 Q3).
 */
#pragma once

#include <QObject>
#include <QList>
#include <QtQmlIntegration/qqmlintegration.h>
QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

#include "snapshots.h"

class CpuMonitor : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//? Total usage percent (0–100)
	Q_PROPERTY(int usage READ usage NOTIFY usageChanged FINAL)
	//? Latest percent per logical core
	Q_PROPERTY(QList<double> perCore READ perCore NOTIFY perCoreChanged FINAL)
	//? Stolen get_cpuHz() string, e.g. "3.2 GHz" ("" when unavailable)
	Q_PROPERTY(QString freqText READ freqText NOTIFY freqTextChanged FINAL)
	//? 1/5/15-minute load averages
	Q_PROPERTY(double load1 READ load1 NOTIFY loadChanged FINAL)
	Q_PROPERTY(double load5 READ load5 NOTIFY loadChanged FINAL)
	Q_PROPERTY(double load15 READ load15 NOTIFY loadChanged FINAL)
	//? Total-usage history ring (oldest → newest, ≤120 samples)
	Q_PROPERTY(QList<double> history READ history NOTIFY historyChanged FINAL)
	//? Stolen Tools::system_uptime (seconds)
	Q_PROPERTY(double uptimeSec READ uptimeSec NOTIFY uptimeChanged FINAL)
	//? PSI pressure (parity addition); valid=false → UI hides pressure rows
	Q_PROPERTY(bool pressureValid READ pressureValid NOTIFY pressureChanged FINAL)
	Q_PROPERTY(QList<double> pressureSome READ pressureSome NOTIFY pressureChanged FINAL)
	Q_PROPERTY(QList<double> pressureFull READ pressureFull NOTIFY pressureChanged FINAL)
	//? Stolen get_cpuName()
	Q_PROPERTY(QString cpuName READ cpuName NOTIFY cpuNameChanged FINAL)

public:
	explicit CpuMonitor(QObject* parent = nullptr);

	//* Process-wide singleton instance (worker connects to it, QML displays it)
	static CpuMonitor& instance();
	//* QML singleton provider — returns the same instance the C++ side uses
	static CpuMonitor* create(QQmlEngine* engine, QJSEngine* jsEngine);

	int usage() const { return m_usage; }
	QList<double> perCore() const { return m_perCore; }
	QString freqText() const { return m_freqText; }
	double load1() const { return m_load[0]; }
	double load5() const { return m_load[1]; }
	double load15() const { return m_load[2]; }
	QList<double> history() const { return m_history; }
	double uptimeSec() const { return m_uptimeSec; }
	bool pressureValid() const { return m_pressure.valid; }
	QList<double> pressureSome() const;
	QList<double> pressureFull() const;
	QString cpuName() const { return m_cpuName; }

public slots:
	void update(const CpuSnapshot& snapshot);

signals:
	void usageChanged();
	void perCoreChanged();
	void freqTextChanged();
	void loadChanged();
	void historyChanged();
	void uptimeChanged();
	void pressureChanged();
	void cpuNameChanged();

private:
	int m_usage = 0;
	QList<double> m_perCore;
	QString m_freqText;
	double m_load[3] = {0.0, 0.0, 0.0};
	QList<double> m_history;
	double m_uptimeSec = 0.0;
	PressureSnapshot m_pressure;
	QString m_cpuName;
};

class MemMonitor : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	Q_PROPERTY(quint64 total READ total NOTIFY memChanged FINAL)
	Q_PROPERTY(quint64 used READ used NOTIFY memChanged FINAL)
	Q_PROPERTY(quint64 free READ free NOTIFY memChanged FINAL)
	Q_PROPERTY(quint64 available READ available NOTIFY memChanged FINAL)
	Q_PROPERTY(quint64 cached READ cached NOTIFY memChanged FINAL)
	Q_PROPERTY(quint64 swapTotal READ swapTotal NOTIFY memChanged FINAL)
	Q_PROPERTY(quint64 swapUsed READ swapUsed NOTIFY memChanged FINAL)
	Q_PROPERTY(bool hasSwap READ hasSwap NOTIFY memChanged FINAL)
	Q_PROPERTY(QList<double> history READ history NOTIFY historyChanged FINAL)
	Q_PROPERTY(bool pressureValid READ pressureValid NOTIFY pressureChanged FINAL)
	Q_PROPERTY(QList<double> pressureSome READ pressureSome NOTIFY pressureChanged FINAL)
	Q_PROPERTY(QList<double> pressureFull READ pressureFull NOTIFY pressureChanged FINAL)

public:
	explicit MemMonitor(QObject* parent = nullptr);

	//* Process-wide singleton instance (worker connects to it, QML displays it)
	static MemMonitor& instance();
	//* QML singleton provider — returns the same instance the C++ side uses
	static MemMonitor* create(QQmlEngine* engine, QJSEngine* jsEngine);

	quint64 total() const { return m_total; }
	quint64 used() const { return m_used; }
	quint64 free() const { return m_free; }
	quint64 available() const { return m_available; }
	quint64 cached() const { return m_cached; }
	quint64 swapTotal() const { return m_swapTotal; }
	quint64 swapUsed() const { return m_swapUsed; }
	bool hasSwap() const { return m_hasSwap; }
	QList<double> history() const { return m_history; }
	bool pressureValid() const { return m_pressure.valid; }
	QList<double> pressureSome() const;
	QList<double> pressureFull() const;

public slots:
	void update(const MemSnapshot& snapshot);

signals:
	void memChanged();
	void historyChanged();
	void pressureChanged();

private:
	quint64 m_total = 0, m_used = 0, m_free = 0, m_available = 0, m_cached = 0;
	quint64 m_swapTotal = 0, m_swapUsed = 0;
	bool m_hasSwap = false;
	QList<double> m_history;
	PressureSnapshot m_pressure;
};