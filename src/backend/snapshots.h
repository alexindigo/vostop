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

struct ProcDetailSnapshot {
	quint64 pid = 0;
	QString name, user, status, elapsed, parent;
	quint64 memBytes = 0;
	double cpuPct = 0.0;
	quint64 threads = 0;
	quint64 ppid = 0;
	char state = '0';
	quint64 ioRead = 0, ioWrite = 0; //? cumulative bytes
	bool valid = false;
};

struct ProcSnapshot {
	struct Row {
		quint64 pid = 0;
		QString name, cmd, user;
		quint64 memBytes = 0;
		double cpuPct = 0.0;
		quint64 threads = 0;
		char state = '0';
		qint64 nice = 0;
		quint64 ppid = 0;
		//? Parity additions
		double ioReadRate = 0.0, ioWriteRate = 0.0;
		bool ioKnown = false;
		int category = 1; //? 0 Apps, 1 Background, 2 System
		double gpuPct = -1.0; //? phase 5: per-process GPU (−1 → "—")
	};
	QList<Row> rows;
	int numpids = 0;      //? stolen accounting: total − filtered
	int totalProcs = 0;   //? all scanned processes
	quint64 threadsTotal = 0;
};

struct OpenFilesSnapshot {
	quint64 pid = 0;
	QStringList files;
};

Q_DECLARE_METATYPE(ProcSnapshot)
Q_DECLARE_METATYPE(ProcDetailSnapshot)
Q_DECLARE_METATYPE(OpenFilesSnapshot)

struct DiskSnapshot {
	struct Mount {
		QString dev, name, fstype, mountpoint;
		qint64 total = 0, used = 0, free = 0;
		int usedPercent = 0, freePercent = 0;
		qint64 ioRead = 0, ioWrite = 0;   //? last rate sample (bytes/s)
		int ioActivity = 0;               //? busy % from io_ticks
	};
	QList<Mount> mounts;
	bool ioPressureValid = false;
	double ioPressureSome[3] = {0.0, 0.0, 0.0};
	double ioPressureFull[3] = {0.0, 0.0, 0.0};
	bool ioPressureHasFull = false;
};

struct NetSnapshot {
	QString iface;                       //? selected interface
	QStringList ifaces;                  //? all interfaces
	QString ipv4, ipv6;
	bool connected = false;
	qint64 downSpeed = 0, upSpeed = 0;   //? bytes/s
	qint64 downTotal = 0, upTotal = 0;
	QList<double> downHistory;           //? ring, oldest → newest
	QList<double> upHistory;
};
Q_DECLARE_METATYPE(DiskSnapshot)
Q_DECLARE_METATYPE(NetSnapshot)

struct GpuSnapshot {
	struct Device {
		QString name;
		double util = 0.0;             //? gpu-totals latest %
		double memUtil = 0.0;          //? mem_utilization_percent latest
		quint64 memTotal = 0, memUsed = 0;
		long long temp = 0, tempMax = 110;
		long long powerMw = 0, powerMaxMw = 0;   //? milliwatts
		unsigned clockMhz = 0, memClockMhz = 0;
		long long encUtil = 0, decUtil = 0;
		bool approximate = false;      //? fdinfo-aggregate device card (Intel MVP path)
		QString pdev;
	};
	QList<Device> devices;
	bool available = false;
};

struct SensorsSnapshot {
	bool sensorsAvailable = false;
	QString cpuSensorName;
	double cpuTemp = 0.0;
	QList<double> coreTemps;       //? per-core temp (empty when cpu_temp_only)
	bool batteryAvailable = false;
	int batteryPct = 0;
	double batteryWatts = 0.0;
	long batterySeconds = 0;
	QString batteryStatus;
	bool cpuWattsAvailable = false;
	double cpuWatts = 0.0;
};

Q_DECLARE_METATYPE(GpuSnapshot)
Q_DECLARE_METATYPE(SensorsSnapshot)
