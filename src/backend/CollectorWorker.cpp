/*
 * vostop — CollectorWorker (implementation).
 */
#include "CollectorWorker.h"

#include "../collect/cpu_mem.h"
#include "../collect/proc.h"
#include "../collect/disk_net.h"
#include "../collect/gpu_sensors.h"
#include "../collect/gpu_procs.h"
#include "Settings.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(vostopWorker, "vostop.worker")

CollectorWorker* CollectorWorker::instance() {
	static CollectorWorker inst;
	return &inst;
}

CollectorWorker::CollectorWorker(QObject* parent)
	: QObject(parent),
	  m_timer(this) { //? child QObject → moves with the worker into the collector thread
	m_timer.setSingleShot(false);
	connect(&m_timer, &QTimer::timeout, this, &CollectorWorker::tick);
}

void CollectorWorker::start() {
	try {
		Shared::init();
	} catch (const std::exception& e) {
		qCCritical(vostopWorker) << "collector init failed:" << e.what();
		emit cpuUpdated(CpuSnapshot{}); //? empty snapshot → GUI stays empty
		return;
	}
	m_psiAvailable = Psi::available();
	m_timer.setInterval(Settings::instance()->pollIntervalMs());
	m_timer.start();
	tick(); //? first sample immediately (phase-2 warm-up already primed deltas)

	//? Phase 5: flip sensor/battery/watts flags ON wherever backends probed
	//? successfully (phase-2 decision — the flags were OFF until this phase).
	//? Queued to the Settings object (GUI thread) to keep the notifier thread-safe.
	auto* settings = Settings::instance();
	if (Cpu::got_sensors)
		QMetaObject::invokeMethod(settings, [settings] { settings->set_checkTemp(true); }, Qt::QueuedConnection);
	if (Cpu::has_battery) {
		Cpu::get_battery(); //? probe; sets has_battery=false when no battery conforms
		if (Cpu::has_battery)
			QMetaObject::invokeMethod(settings, [settings] { settings->set_showBattery(true); }, Qt::QueuedConnection);
	}
	if (Cpu::get_cpuConsumptionUJoules() > 0)
		QMetaObject::invokeMethod(settings, [settings] { settings->set_showCpuWatts(true); }, Qt::QueuedConnection);
}

void CollectorWorker::stop() {
	m_timer.stop();
}

void CollectorWorker::applyInterval() {
	m_timer.setInterval(Settings::instance()->pollIntervalMs());
}

void CollectorWorker::tick() { //? tick counter for slow cadences (fdinfo walk every 2nd tick)
	++m_tickCounter;
	QElapsedTimer tickTimer;
	tickTimer.start();
	tickTimer.start();

	CpuSnapshot cs;
	MemSnapshot ms;
	try {
		auto& cpu = Cpu::collect();
		cs.usage = cpu.cpu_percent.at("total").empty() ? 0 : static_cast<int>(cpu.cpu_percent.at("total").back());
		cs.perCore.reserve(cpu.core_percent.size());
		for (const auto& core : cpu.core_percent)
			cs.perCore.append(core.empty() ? 0.0 : static_cast<double>(core.back()));
		cs.cpuName = QString::fromStdString(Cpu::cpuName);
		cs.freqText = QString::fromStdString(Cpu::cpuHz);
		cs.loadAvg[0] = cpu.load_avg[0];
		cs.loadAvg[1] = cpu.load_avg[1];
		cs.loadAvg[2] = cpu.load_avg[2];
		for (const long long v : cpu.cpu_percent.at("total"))
			cs.history.append(static_cast<double>(v));
		//? Phase 6: per-core rings for the Win-TM-style grid view
		for (const auto& core : cpu.core_percent) {
			QList<double> ring;
			ring.reserve(core.size());
			for (const long long v : core)
				ring.append(static_cast<double>(v));
			cs.coreHistories.append(ring);
		}
		try {
			cs.uptimeSec = Cpu::system_uptime();
		} catch (const std::exception&) {}

		auto& mem = Mem::collect();
		const auto& stats = mem.stats;
		ms.total = stats.at("used") + stats.at("available"); //? totalMem == used + available by the stolen formula
		ms.used = stats.at("used");
		ms.free = stats.at("free");
		ms.available = stats.at("available");
		ms.cached = stats.at("cached");
		ms.swapTotal = stats.at("swap_total");
		ms.swapUsed = stats.at("swap_used");
		ms.hasSwap = Mem::has_swap and ms.swapTotal > 0;
		for (const long long v : mem.percent.at("used"))
			ms.history.append(static_cast<double>(v));
	} catch (const std::exception& e) {
		qCWarning(vostopWorker) << "collect tick failed:" << e.what();
		return;
	}

	if (m_psiAvailable) {
		//? Parity addition: PSI (original vostop collector)
		Psi::Pressure p;
		if (Psi::read("cpu", p)) {
			cs.pressure.valid = true;
			for (int i = 0; i < 3; ++i) { cs.pressure.some[i] = p.some[i]; cs.pressure.full[i] = p.full[i]; }
			cs.pressure.hasFull = p.hasFull;
			cs.pressureHistory.append(p.some[0]);
			if (cs.pressureHistory.size() > VostopHistoryDepth) cs.pressureHistory.removeFirst();
		}
		if (Psi::read("memory", p)) {
			ms.pressure.valid = true;
			for (int i = 0; i < 3; ++i) { ms.pressure.some[i] = p.some[i]; ms.pressure.full[i] = p.full[i]; }
			ms.pressure.hasFull = p.hasFull;
			ms.pressureHistory.append(p.some[0]);
			if (ms.pressureHistory.size() > VostopHistoryDepth) ms.pressureHistory.removeFirst();
		}
	}

	//? Stolen proc scan (same worker tick, plan: "same worker thread, same snapshot pattern")
	ProcSnapshot ps;
	QElapsedTimer procTimer;
	procTimer.start();
	try {
		const auto& procs = Proc::collect(false);
		ps.rows.reserve(static_cast<qsizetype>(procs.size()));
		ps.totalProcs = static_cast<int>(procs.size());
		ps.numpids = Proc::numpids.load();
		quint64 threadsTotal = 0;
		for (const auto& p : procs) {
			ProcSnapshot::Row row;
			row.pid = p.pid;
			row.name = QString::fromStdString(p.name);
			row.cmd = QString::fromStdString(p.cmd);
			row.user = QString::fromStdString(p.user);
			row.memBytes = p.mem;
			row.cpuPct = p.cpu_p;
			row.threads = p.threads;
			row.state = p.state;
			row.nice = p.p_nice;
			row.ppid = p.ppid;
			//? Parity: per-process disk I/O rates (EACCES → ioKnown=false → "—")
			row.ioKnown = Proc::io_rates(p.pid, row.ioReadRate, row.ioWriteRate);
			//? Parity: Apps/Background/System category (cached per pid in the collector)
			row.category = static_cast<int>(Proc::classify_category(p.pid, p.ppid, p.cpu_s));
			threadsTotal += p.threads;
			ps.rows.append(row);
		}
		ps.threadsTotal = threadsTotal;
		m_procScanMs = m_procScanMs == 0.0 ? procTimer.elapsed()
			: 0.9 * m_procScanMs + 0.1 * procTimer.elapsed();

		//? Detail readout for the selected pid (stolen _collect_details)
		if (Proc::detailed_pid.load() != 0 and not procs.empty()) {
			ProcDetailSnapshot ds;
			const auto& e = Proc::detailed.entry;
			ds.pid = e.pid;
			ds.name = QString::fromStdString(e.name);
			ds.user = QString::fromStdString(e.user);
			ds.status = QString::fromStdString(Proc::detailed.status);
			ds.elapsed = QString::fromStdString(Proc::detailed.elapsed);
			ds.parent = QString::fromStdString(Proc::detailed.parent);
			ds.memBytes = e.mem;
			ds.cpuPct = e.cpu_p;
			ds.threads = e.threads;
			ds.ppid = e.ppid;
			ds.state = e.state;
			ds.ioRead = 0; ds.ioWrite = 0; //? raw io shown via model roles; detail pane shows rates
			ds.valid = true;
			emit procDetailUpdated(ds);
		}
	} catch (const std::exception& e) {
		qCWarning(vostopWorker) << "proc collect failed:" << e.what();
	}

	//? Disk + net snapshots (stolen collectors; Mem::collect() already ran Disk::collect at the seam)
	DiskSnapshot ds;
	try {
		for (const auto& name : Disk::disks_order) {
			auto it = Disk::disks.find(name);
			if (it == Disk::disks.end()) continue;
			const auto& d = it->second;
			DiskSnapshot::Mount m;
			m.dev = QString::fromStdString(d.dev.string());
			m.name = QString::fromStdString(d.name);
			m.fstype = QString::fromStdString(d.fstype);
			m.mountpoint = QString::fromStdString(name);
			m.total = d.total;
			m.used = d.used;
			m.free = d.free;
			m.usedPercent = d.used_percent;
			m.freePercent = d.free_percent;
			m.ioRead = d.io_read.empty() ? 0 : d.io_read.back();
			m.ioWrite = d.io_write.empty() ? 0 : d.io_write.back();
			m.ioActivity = d.io_activity.empty() ? 0 : static_cast<int>(d.io_activity.back());
			ds.mounts.append(m);
		}
	} catch (const std::exception& e) {
		qCWarning(vostopWorker) << "disk snapshot failed:" << e.what();
	}

	//? Parity: io PSI on the disk card (same psi collector; phase-6 sparkline ring)
	if (m_psiAvailable) {
		Psi::Pressure p;
		if (Psi::read("io", p)) {
			ds.ioPressureValid = true;
			for (int i = 0; i < 3; ++i) { ds.ioPressureSome[i] = p.some[i]; ds.ioPressureFull[i] = p.full[i]; }
			ds.ioPressureHasFull = p.hasFull;
			ds.ioPressureHistory.append(p.some[0]);
			if (ds.ioPressureHistory.size() > VostopHistoryDepth) ds.ioPressureHistory.removeFirst();
		}
	}

	NetSnapshot ns;
	try {
		auto& net = Net::collect();
		if (not Net::selected_iface.empty()) {
			ns.iface = QString::fromStdString(Net::selected_iface);
			for (const auto& iface : Net::interfaces)
				ns.ifaces.append(QString::fromStdString(iface));
			auto& n = net;
			ns.ipv4 = QString::fromStdString(n.ipv4);
			ns.ipv6 = QString::fromStdString(n.ipv6);
			ns.connected = n.connected;
			ns.downSpeed = static_cast<qint64>(n.stat.at("download").speed);
			ns.upSpeed = static_cast<qint64>(n.stat.at("upload").speed);
			ns.downTotal = static_cast<qint64>(n.stat.at("download").total);
			ns.upTotal = static_cast<qint64>(n.stat.at("upload").total);
			for (const long long v : n.bandwidth.at("download"))
				ns.downHistory.append(static_cast<double>(v));
			for (const long long v : n.bandwidth.at("upload"))
				ns.upHistory.append(static_cast<double>(v));
		}
	} catch (const std::exception& e) {
		qCWarning(vostopWorker) << "net collect failed:" << e.what();
	}

	//? Phase 5: GPU backends (slow-cadence fdinfo walk every 2nd tick)
	GpuSnapshot gs;
	QElapsedTimer fdinfoTimer;
	fdinfoTimer.start();
	bool fdinfoRan = false;
	try {
		auto& gpus = Gpu::collect();
		for (size_t i = 0; i < gpus.size(); ++i) {
			const auto& g = gpus[i];
			GpuSnapshot::Device dev;
			dev.name = QString::fromStdString(i < Gpu::gpu_names.size() ? Gpu::gpu_names[i] : "GPU " + std::to_string(i));
			dev.util = g.supported_functions.gpu_utilization and not g.gpu_percent.at("gpu-totals").empty()
				? static_cast<double>(g.gpu_percent.at("gpu-totals").back()) : -1.0;
			dev.memUtil = g.mem_utilization_percent.empty() ? -1.0 : static_cast<double>(g.mem_utilization_percent.back());
			dev.memTotal = g.mem_total;
			dev.memUsed = g.mem_used;
			dev.temp = g.temp.empty() ? 0 : g.temp.back();
			dev.tempMax = g.temp_max;
			dev.powerMw = g.pwr_usage;
			dev.powerMaxMw = g.pwr_max_usage;
			dev.clockMhz = g.gpu_clock_speed;
			dev.memClockMhz = g.mem_clock_speed;
			dev.encUtil = g.encoder_utilization;
			dev.decUtil = g.decoder_utilization;
			gs.devices.append(dev);
		}
	} catch (const std::exception& e) {
		qCWarning(vostopWorker) << "gpu collect failed:" << e.what();
	}

	//? Per-process GPU (parity): fdinfo walk on the slow cadence, NVML per-pid fallback
	QHash<quint64, GpuProcs::ProcGpu> procGpu;
	try {
		procGpu = GpuProcs::collect(m_tickCounter % 2 == 0);
		if (procGpu.isEmpty())
			procGpu = GpuProcs::nvmlPerPid();
		fdinfoRan = true;
	} catch (const std::exception& e) {
		qCDebug(vostopWorker) << "gpu_procs walk failed:" << e.what();
	}
	if (fdinfoRan)
		m_fdinfoMs = m_fdinfoMs == 0.0 ? fdinfoTimer.elapsed()
			: 0.9 * m_fdinfoMs + 0.1 * fdinfoTimer.elapsed();

	//? Intel approximate device card: no real backend found a card, but fdinfo
	//? clients exist on this machine → device-level aggregate, labeled approximate.
	if (gs.devices.isEmpty()) {
		const auto approx = GpuProcs::deviceApprox();
		for (auto it = approx.constBegin(); it != approx.constEnd(); ++it) {
			const GpuProcs::DeviceApprox& dev = it.value();
			GpuSnapshot::Device d;
			d.name = QStringLiteral("Intel iGPU (approximate, %1)").arg(dev.pdev);
			d.util = dev.busyPct;
			d.memUsed = dev.memBytes;
			d.approximate = true;
			d.pdev = dev.pdev;
			gs.devices.append(d);
		}
	}
	gs.available = not gs.devices.isEmpty();

	//? Merge per-pid GPU into the proc snapshot rows (reserved gpuPct role, phase 3)
	for (auto& row : ps.rows) {
		if (const auto it = procGpu.constFind(row.pid); it != procGpu.constEnd() and it->valid)
			row.gpuPct = it->busyPct;
		else
			row.gpuPct = -1.0; //? "—" (own-user only; same rule as /proc/<pid>/io)
	}

	//? Sensors + battery + RAPL watts snapshot
	SensorsSnapshot ss;
	ss.sensorsAvailable = Cpu::got_sensors and not Cpu::found_sensors.empty();
	if (ss.sensorsAvailable) {
		if (Cpu::found_sensors.contains(Cpu::cpu_sensor))
			ss.cpuTemp = static_cast<double>(Cpu::found_sensors.at(Cpu::cpu_sensor).temp);
		ss.cpuSensorName = QString::fromStdString(Cpu::cpu_sensor);
		for (size_t c = 1; c < Cpu::current_cpu.temp.size() and c <= static_cast<size_t>(Shared::coreCount); ++c) {
			const auto& dq = Cpu::current_cpu.temp[c];
			ss.coreTemps.append(dq.empty() ? 0.0 : static_cast<double>(dq.back()));
		}
	}
	ss.batteryAvailable = Cpu::has_battery and Settings::getB("show_battery");
	if (Cpu::has_battery and Settings::getB("show_battery")) {
		const auto& [pct, watts, secs, status] = Cpu::current_bat;
		ss.batteryPct = pct;
		ss.batteryWatts = watts;
		ss.batterySeconds = secs;
		ss.batteryStatus = QString::fromStdString(status);
	}
	ss.cpuWattsAvailable = Cpu::supports_watts and Settings::getB("show_cpu_watts");
	ss.cpuWatts = Cpu::current_cpu.usage_watts;

	emit cpuUpdated(cs);
	emit memUpdated(ms);
	if (not ps.rows.isEmpty())
		emit procUpdated(ps);
	emit diskUpdated(ds);
	emit netUpdated(ns);
	emit gpuUpdated(gs);
	emit sensorsUpdated(ss);
	//? Acceptance diagnostics (phase 3): top-cpu row, category counts, top io writer
	{
		int apps = 0, bg = 0, sys = 0;
		const ProcSnapshot::Row* top = nullptr;
		const ProcSnapshot::Row* topIo = nullptr;
		for (const auto& r : ps.rows) {
			if (r.category == 0) ++apps; else if (r.category == 1) ++bg; else ++sys;
			if (not top or r.cpuPct > top->cpuPct) top = &r;
			if (r.ioKnown and (not topIo or r.ioWriteRate > topIo->ioWriteRate)) topIo = &r;
		}
		qCDebug(vostopWorker) << "tick: cpu" << cs.usage << "%" << cs.perCore.size() << "cores"
			<< "mem used" << ms.used / 1048576 << "MiB /" << ms.total / 1048576 << "MiB"
			<< "psi cpu some" << cs.pressure.some[0] << "mem some" << ms.pressure.some[0];
		qCDebug(vostopWorker) << "tick: procs" << ps.rows.size() << "numpids" << ps.numpids
			<< "threads" << ps.threadsTotal
			<< "categories apps/bg/sys" << apps << "/" << bg << "/" << sys;
		if (top)
			qCDebug(vostopWorker) << "tick: top-cpu" << top->name << top->cpuPct << "%" << "state" << QChar(top->state);
		if (topIo)
			qCDebug(vostopWorker) << "tick: top-io" << topIo->name << "write" << topIo->ioWriteRate / 1048576 << "MiB/s"
				<< "read" << topIo->ioReadRate / 1048576 << "MiB/s";
		//? Phase-4 acceptance diagnostics: first mounts + net
		if (not ds.mounts.isEmpty()) {
			const auto& m0 = ds.mounts.first();
			qCDebug(vostopWorker) << "tick: disk" << m0.name << "used" << m0.used / 1073741824 << "GiB /" << m0.total / 1073741824 << "GiB"
				<< m0.usedPercent << "%" << "mounts" << ds.mounts.size()
				<< "io W" << m0.ioWrite / 1048576 << "MiB/s R" << m0.ioRead / 1048576 << "MiB/s busy" << m0.ioActivity << "%";
		} else {
			qCDebug(vostopWorker) << "tick: disk no mounts";
		}
		qCDebug(vostopWorker) << "tick: net iface" << ns.iface << "down" << ns.downSpeed / 1024 << "KiB/s up" << ns.upSpeed / 1024
			<< "KiB/s ifaces" << ns.ifaces.size();
		//? Phase-5 diagnostics: gpus, sensors, per-pid gpu
		QString topGpuName;
		double topGpuPct = -1.0;
		for (const auto& row : ps.rows)
			if (row.gpuPct >= 0.0 and row.gpuPct > topGpuPct) {
				topGpuPct = row.gpuPct;
				topGpuName = row.name;
			}
		qCDebug(vostopWorker) << "tick: gpu devices" << gs.devices.size() << "available" << gs.available
			<< "procGpu" << procGpu.size() << "drivers" << GpuProcs::drmDrivers().join(u",")
			<< "top" << topGpuName << topGpuPct << "%";
		qCDebug(vostopWorker) << "tick: sensors" << ss.sensorsAvailable << "cpuTemp" << ss.cpuTemp
			<< "cores" << ss.coreTemps.size() << "battery" << ss.batteryAvailable << ss.batteryPct << "%"
			<< ss.batteryStatus << "watts" << ss.cpuWattsAvailable << ss.cpuWatts;
		//? Phase-6 perf gate: per-pid io + fdinfo overhead (post-MVP perf pass input)
		qCDebug(vostopWorker) << "tick: ms total" << tickTimer.elapsed()
			<< "| proc scan" << procTimer.elapsed() << "| fdinfo walk" << (m_tickCounter % 2 == 0 ? fdinfoTimer.elapsed() : -1);
	}
}

void CollectorWorker::gatherOpenFiles(quint64 pid) {
	OpenFilesSnapshot snap;
	snap.pid = pid;
	const auto files = Proc::open_files(pid);
	for (const auto& f : files)
		snap.files.append(QString::fromStdString(f));
	emit openFilesUpdated(snap);
}