/*
 * vostop — collector snapshot types crossing the worker→GUI thread boundary
 * via queued signals (qRegisterMetaType'd).
 */
#pragma once

#include <QMetaType>
#include <QList>

struct PressureSnapshot {
	double some[3] = {0.0, 0.0, 0.0}; //? avg10/60/300
	double full[3] = {0.0, 0.0, 0.0};
	bool hasFull = false;
	bool valid = false;
};

struct CpuSnapshot {
	int usage = 0;                 //? latest total percent
	QList<double> perCore;         //? latest percent per core
	double freqMHz = 0.0;          //? parsed from Cpu::cpuHz ("x.xx GHz")
	QString cpuName;
	QString freqText;              //? stolen get_cpuHz() string
	double loadAvg[3] = {0.0, 0.0, 0.0};
	QList<double> history;         //? total-usage ring, oldest → newest
	double uptimeSec = 0.0;
	PressureSnapshot pressure;
};

struct MemSnapshot {
	quint64 total = 0, used = 0, free = 0, available = 0, cached = 0;
	quint64 swapTotal = 0, swapUsed = 0;
	QList<double> history;         //? used-percent ring, oldest → newest
	bool hasSwap = false;
	PressureSnapshot pressure;
};

Q_DECLARE_METATYPE(CpuSnapshot)
Q_DECLARE_METATYPE(MemSnapshot)