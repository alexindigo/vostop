/*
 * vostop — CollectorWorker (implementation).
 */
#include "CollectorWorker.h"

#include "../collect/cpu_mem.h"
#include "../collect/proc.h"
#include "Settings.h"

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
}

void CollectorWorker::stop() {
	m_timer.stop();
}

void CollectorWorker::tick() {
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
		}
		if (Psi::read("memory", p)) {
			ms.pressure.valid = true;
			for (int i = 0; i < 3; ++i) { ms.pressure.some[i] = p.some[i]; ms.pressure.full[i] = p.full[i]; }
			ms.pressure.hasFull = p.hasFull;
		}
	}

	//? Stolen proc scan (same worker tick, plan: "same worker thread, same snapshot pattern")
	ProcSnapshot ps;
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

	emit cpuUpdated(cs);
	emit memUpdated(ms);
	if (not ps.rows.isEmpty())
		emit procUpdated(ps);
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