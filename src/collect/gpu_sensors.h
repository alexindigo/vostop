/*
 * vostop — GPU, sensor and battery collectors, naturalized from btop.
 *
 * Portions Copyright 2021 Aristocratos (jakob@qvantnet.com) — vendored from
 * btop src/linux/btop_collect.cpp @ 956050716f290f529cd51d271b3d7e36eb1a22f0
 * (Cpu::get_sensors 492–622, update_sensors 624–647, get_battery 835–1030,
 * Gpu::Nvml 1254–1556, Gpu::Rsmi 1561–1907, Gpu::Asysfs 2031–2207, Gpu::
 * collect 2215–2270; NVIDIA/ROCm defines and typedefs 176–300) and
 * src/btop_shared.hpp (gpu_info_supported 136–149, gpu_info 152–182).
 * Apache-2.0; see THIRD-PARTY-NOTICES.
 *
 * Naturalization: dlfcn → QLibrary (errorString replaces dlerror); Config →
 * Settings; Logger → QLoggingCategory (init failures are qInfo "backend
 * skipped", never errors); PCIe-speed threads → sequential calls on the
 * slow cadence (nvml_measure_pcie_speeds/rsmi_measure_pcie_speeds default
 * OFF); width*2 trims → VostopHistoryDepth; Intel::* NOT stolen (cut from
 * MVP — the fdinfo approximation in gpu_procs covers Intel instead);
 * RSMI_STATIC not defined (dynamic path only, local typedefs).
 */
#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <QLibrary>

namespace Gpu {
	//* Per-device container for supported Gpu::*::collect() functions
	//* (btop gpu_info_supported verbatim)
	struct gpu_info_supported {
		bool gpu_utilization = true,
			 mem_utilization = true,
			 gpu_clock = true,
			 mem_clock = true,
			 pwr_usage = true,
			 pwr_state = true,
			 temp_info = true,
			 mem_total = true,
			 mem_used = true,
			 pcie_txrx = true,
			 encoder_utilization = true,
			 decoder_utilization = true;
	};

	//* Per-device container for GPU info (btop gpu_info; deque history capped at VostopHistoryDepth)
	struct gpu_info {
		std::unordered_map<std::string, std::deque<long long>> gpu_percent = {
			{"gpu-totals", {}},
			{"gpu-vram-totals", {}},
			{"gpu-pwr-totals", {}},
		};
		unsigned int gpu_clock_speed = 0; // MHz

		long long pwr_usage = 0; // mW
		long long pwr_max_usage = 255000;
		long long pwr_state = 0;

		std::deque<long long> temp = {0};
		long long temp_max = 110;

		long long mem_total = 0;
		long long mem_used = 0;
		std::deque<long long> mem_utilization_percent = {0};
		long long mem_clock_speed = 0; // MHz

		long long pcie_tx = 0; // KB/s
		long long pcie_rx = 0;

		long long encoder_utilization = 0;
		long long decoder_utilization = 0;

		gpu_info_supported supported_functions;
	};

	extern std::vector<gpu_info> gpus;
	extern std::vector<std::string> gpu_names;
	extern long long gpu_pwr_total_max;

	//* Averages / totals across devices (btop shared_gpu_percent)
	extern std::unordered_map<std::string, std::deque<long long>> shared_gpu_percent;

	//* Backend init/shutdown (probe returns false when absent)
	namespace Nvml {
		bool init();
		bool shutdown();
		extern unsigned int device_count;
		//? Exposed for the per-pid NVML fallback (gpu_procs)
		extern QLibrary nvml_lib;
		extern std::vector<void*> devices;
	}
	namespace Rsmi {
		bool init();
		bool shutdown();
		extern unsigned int device_count;
	}
	namespace Asysfs {
		bool init();
		bool shutdown();
		extern unsigned int device_count;
	}

	//* Collect from all initialized backends
	auto collect() -> std::vector<gpu_info>&;
}

namespace Cpu {
	//* Sensor/battery state (vendored declarations)
	struct Sensor {
		std::string path;
		int64_t temp = 0;
		int64_t crit = 0;
	};

	extern std::unordered_map<std::string, Sensor> found_sensors;
	extern std::vector<std::string> core_sensors;
	extern std::string cpu_sensor;
	extern std::vector<std::string> available_sensors;

	//* hwmon walk + coretemp fallback + thermal-zone fallback (btop 492–622)
	bool get_sensors();
	//* Per-tick re-read (btop 624–647)
	void update_sensors();
	//* Real battery collector (btop 835–1030)
	auto get_battery() -> std::tuple<int, float, long, std::string>;
}