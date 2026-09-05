/*
 * vostop — per-process GPU collector (implementation). Original vostop code,
 * GPLv3-or-later. The nvtop approach: DRM fdinfo walk (drm-client-id dedupe,
 * Δdrm-engine-* / Δwall → busy% max across engines, drm-resident-* → memory),
 * vendor-neutral across i915/xe/amdgpu/msm; NVML per-pid fallback for NVIDIA.
 */
#include "gpu_procs.h"

#include "cpu_mem.h"
#include "gpu_sensors.h"
#include "tools_qt.h"

#include <QFile>
#include <QDir>
#include <QSet>
#include <QLoggingCategory>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>

#include <unistd.h>

Q_DECLARE_LOGGING_CATEGORY(vostopCollect)

namespace fs = std::filesystem;
using namespace tools;
using std::string;

namespace GpuProcs {

namespace {

	struct ClientState {
		quint64 client = 0;                       //? drm-client-id
		QHash<QString, quint64> engines;          //? engine name → cumulative ns
		quint64 mem = 0;                          //? sum of drm-resident-*
	};

	struct PidState {
		bool hasDrm = false;                      //? cached: pid holds DRM fds (full re-scan only for new pids)
		QString driver;
		QString pdev;
		QHash<quint64, ClientState> clients;
		QHash<QString, quint64> lastEngines;      //? per-engine last cumulative ns
		double lastBusy = 0.0;
		quint64 lastMem = 0;
		double wall = 0.0;                        //? last sample wall clock (monotonic s)
	};

	std::unordered_map<quint64, PidState> pid_cache;
	std::chrono::steady_clock::time_point last_scan{};
	std::chrono::steady_clock::time_point last_rate{};
	double wall_now = 0.0;
	double wall_prev = 0.0;
	QHash<QString, DeviceApprox> device_approx;
	QStringList drivers_seen;

	double monoSec() {
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}
}

QHash<quint64, ProcGpu> collect(bool newTick) {
	QHash<quint64, ProcGpu> out;
	if (not newTick) {
		//? Between fdinfo walks reuse the previous rates (slow cadence per plan)
		for (const auto& [pid, st] : pid_cache) {
			if (not st.hasDrm or st.lastBusy <= 0.0) continue;
			out.insert(pid, ProcGpu{st.lastBusy, st.lastMem, true});
		}
		return out;
	}

	const double now = monoSec();
	wall_prev = wall_now;
	wall_now = now;
	const double dt = (wall_prev > 0.0) ? (now - wall_prev) : 0.0;

	device_approx.clear();
	drivers_seen.clear();

	//? Walk /proc — full client re-scan only for pids not yet known to hold DRM fds
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(Shared::procPath, ec)) {
		const string pid_str = entry.path().filename();
		if (pid_str.empty() or not std::isdigit(pid_str[0])) continue;
		const quint64 pid = std::stoull(pid_str);

		auto pit = pid_cache.find(pid);
		if (pit != pid_cache.end() and pit->second.hasDrm) {
			//? Known DRM holder — re-read its fdinfo this tick
		} else {
			//? Cheap pre-check: does this pid hold a DRM fd? (readlink /proc/<pid>/fd/*)
			bool hasDrm = false;
			const fs::path fdDir = Shared::procPath / pid_str / "fd";
			if (fs::is_directory(fdDir, ec)) {
				for (const auto& fd : fs::directory_iterator(fdDir, ec)) {
					char buf[256];
					const ssize_t len = readlink(fd.path().c_str(), buf, sizeof(buf) - 1);
					if (len > 0) {
						buf[len] = '\0';
						if (std::string_view(buf).find("renderD") != std::string_view::npos
							or std::string_view(buf).find("/drm") != std::string_view::npos) {
							hasDrm = true;
							break;
						}
					}
				}
			}
			if (not hasDrm) continue;
			pit = pid_cache.emplace(pid, PidState{}).first;
			pit->second.hasDrm = true;
			qCDebug(vostopCollect) << "gpu_procs: pid" << pid << "holds DRM fds";
		}

		PidState& st = pit->second;

		//? Scan fdinfo files of this pid (std::ifstream — QFile misbehaves on 0-size /proc files)
		const fs::path fdi = Shared::procPath / pid_str / "fdinfo";
		QHash<QString, quint64> clientEngines;
		QHash<quint64, quint64> clientMem;
		QString driver, pdev;
		QSet<quint64> clients;
		std::error_code fec;
		for (const auto& f : fs::directory_iterator(fdi, fec)) {
			std::ifstream file(f.path());
			if (not file.good()) continue;
			quint64 client = 0;
			for (string line; std::getline(file, line);) {
				const std::string_view sv(line);
				if (sv.starts_with("drm-driver:")) {
					driver = QString::fromStdString(std::string(sv.substr(11))).trimmed();
				} else if (sv.starts_with("drm-pdev:")) {
					pdev = QString::fromStdString(std::string(sv.substr(9))).trimmed();
				} else if (sv.starts_with("drm-client-id:")) {
					client = std::strtoull(sv.substr(14).data(), nullptr, 10);
					clients.insert(client);
				} else if (sv.starts_with("drm-engine-") and client != 0) {
					const QString engine = QString::fromStdString(std::string(sv.substr(11, sv.find(':') - 11))).trimmed();
					const quint64 ns = std::strtoull(sv.substr(sv.find(':') + 1).data(), nullptr, 10);
					//? fdinfo prints cumulative ns per engine per fd; same client on many fds
					//? repeats the counter — keep the max (they are equal or stale copies).
					const QString key = QString::number(client) + "/" + engine;
					auto it = clientEngines.find(key);
					if (it == clientEngines.end() or ns > it.value())
						clientEngines.insert(key, ns);
				} else if (sv.starts_with("drm-resident-") and client != 0) {
					clientMem[client] += std::strtoull(sv.substr(sv.find(':') + 1).data(), nullptr, 10);
				}
			}
		}

		if (clients.isEmpty()) {
			//? DRM fds closed since the pre-check
			st.hasDrm = false;
			qCDebug(vostopCollect) << "gpu_procs: pid" << pid << "no fdinfo clients found";
			continue;
		}

		if (not driver.isEmpty() and not drivers_seen.contains(driver))
			drivers_seen.append(driver);

		//? Aggregate per-pid engines from per-client counters
		QHash<QString, quint64> newEngines;
		quint64 mem = 0;
		for (auto it = clientEngines.constBegin(); it != clientEngines.constEnd(); ++it) {
			const QString engine = it.key().section(u'/', 1);
			newEngines[engine] = qMax(newEngines.value(engine, 0), it.value());
		}
		for (auto it = clientMem.constBegin(); it != clientMem.constEnd(); ++it)
			mem += it.value();

		double busy = 0.0;
		if (dt > 0.0 and not st.lastEngines.isEmpty()) {
			//? busy% = Δ(engine ns)/Δwall, max across engines (Win TM default)
			for (auto it = newEngines.constBegin(); it != newEngines.constEnd(); ++it) {
				const quint64 prev = st.lastEngines.value(it.key(), 0);
				const double pct = std::min(100.0, std::max(0.0,
					static_cast<double>(it.value() - prev) / (dt * 1e7))); //? ns/s → % of one engine
				busy = std::max(busy, pct);
			}
		}
		st.lastEngines = newEngines;
		st.lastBusy = busy;
		st.lastMem = mem;
		st.driver = driver;
		st.pdev = pdev;

		if (busy > 0.0 or mem > 0)
			out.insert(pid, ProcGpu{busy, mem, true});

		//? Device-level aggregate for the approximate Intel card
		if (not pdev.isEmpty()) {
			auto& dev = device_approx[pdev];
			dev.pdev = pdev;
			dev.busyPct = std::min(100.0, dev.busyPct + busy);
			dev.memBytes += mem;
		}
	}

	//? Drop dead pids from the cache
	for (auto it = pid_cache.begin(); it != pid_cache.end();) {
		if (not out.contains(it->first) and it->second.lastBusy <= 0.0)
			it = pid_cache.erase(it);
		else
			++it;
	}
	return out;
}

QHash<QString, DeviceApprox> deviceApprox() {
	return device_approx;
}

QStringList drmDrivers() {
	return drivers_seen;
}

QHash<quint64, ProcGpu> nvmlPerPid() {
	//? NVML per-pid fallback (original vostop code): use nvmlDeviceGetProcessUtilization
	//? / GetComputeRunningProcesses via the already-loaded QLibrary. Resolved lazily.
	QHash<quint64, ProcGpu> out;
	if (not Gpu::Nvml::device_count) return out;

	typedef int nvmlReturn_t;
	struct nvmlProcessUtilizationSample_t { unsigned int pid; unsigned long long timeStamp; unsigned long long smUtil; unsigned long long memUtil; unsigned long long encUtil; unsigned long long decUtil; };
	struct nvmlProcessInfo_t { unsigned int pid; unsigned long long usedGpuMemory; };

	static auto nvmlGetProcUtil = reinterpret_cast<nvmlReturn_t(*)(void*, nvmlProcessUtilizationSample_t*, unsigned int, unsigned int*)>(
		Gpu::Nvml::nvml_lib.resolve("nvmlDeviceGetProcessUtilization"));
	static auto nvmlGetComputeProcs = reinterpret_cast<nvmlReturn_t(*)(void*, nvmlProcessInfo_t*, unsigned int*)>(
		Gpu::Nvml::nvml_lib.resolve("nvmlDeviceGetComputeRunningProcesses_v3"));
	if (not nvmlGetProcUtil) return out;

	for (unsigned int i = 0; i < Gpu::Nvml::device_count; ++i) {
		nvmlProcessUtilizationSample_t samples[128];
		unsigned int count = 128;
		if (nvmlGetProcUtil(Gpu::Nvml::devices[i], samples, count, &count) == 0) {
			for (unsigned int s = 0; s < count; ++s) {
				ProcGpu g;
				g.busyPct = static_cast<double>(samples[s].smUtil);
				g.valid = true;
				out.insert(samples[s].pid, g);
			}
		}
		if (nvmlGetComputeProcs) {
			nvmlProcessInfo_t infos[128];
			unsigned int n = 128;
			if (nvmlGetComputeProcs(Gpu::Nvml::devices[i], infos, &n) == 0) {
				for (unsigned int s = 0; s < n; ++s) {
					auto& g = out[infos[s].pid];
					g.memBytes = infos[s].usedGpuMemory;
					g.valid = true;
				}
			}
		}
	}
	return out;
}

} // namespace GpuProcs