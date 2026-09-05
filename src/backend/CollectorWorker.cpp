/*
 * vostop — CollectorWorker (implementation).
 */
#include "CollectorWorker.h"

#include "../collect/cpu_mem.h"
#include "Settings.h"

#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(vostopWorker, "vostop.worker")

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

	emit cpuUpdated(cs);
	emit memUpdated(ms);
	qCDebug(vostopWorker) << "tick: cpu" << cs.usage << "%" << cs.perCore.size() << "cores"
		<< "mem used" << ms.used / 1048576 << "MiB /" << ms.total / 1048576 << "MiB"
		<< "swap" << ms.swapUsed / 1048576 << "/" << ms.swapTotal / 1048576
		<< "psi cpu some" << cs.pressure.some[0] << "mem some" << ms.pressure.some[0];
}