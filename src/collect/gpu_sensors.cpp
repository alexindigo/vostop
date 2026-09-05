/*
 * vostop — GPU, sensor and battery collectors (implementation), naturalized
 * from btop. See gpu_sensors.h for vendoring note and line references.
 */
#include "gpu_sensors.h"

#include "cpu_mem.h"
#include "tools_qt.h"
#include "../backend/Settings.h"

#include <QLoggingCategory>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <numeric>
#include <ranges>
#include <system_error>
#include <tuple>

#include <unistd.h>

Q_DECLARE_LOGGING_CATEGORY(vostopCollect)

namespace fs = std::filesystem;
namespace rng = std::ranges;
using namespace std;
using namespace std::string_literals;
using namespace tools;
using std::string, std::vector;
using Cpu::system_uptime;

namespace Gpu {

	vector<gpu_info> gpus;
	vector<string> gpu_names;
	long long gpu_pwr_total_max = 0;

	//? NVIDIA data collection (btop Nvml defines/typedefs 179–224; dlfcn → QLibrary)
	namespace Nvml {
		//? NVML defines, structs & typedefs
		#define NVML_DEVICE_NAME_BUFFER_SIZE        64
		#define NVML_SUCCESS                         0
		#define NVML_TEMPERATURE_THRESHOLD_SHUTDOWN  0
		#define NVML_CLOCK_GRAPHICS                  0
		#define NVML_CLOCK_MEM                       2
		#define NVML_TEMPERATURE_GPU                 0
		#define NVML_PCIE_UTIL_TX_BYTES              0
		#define NVML_PCIE_UTIL_RX_BYTES              1

		typedef void* nvmlDevice_t; // we won't be accessing any of the underlying struct's properties, so this is fine
		typedef int nvmlReturn_t, // enums are basically ints
					nvmlTemperatureThresholds_t,
					nvmlClockType_t,
					nvmlPstates_t,
					nvmlTemperatureSensors_t,
					nvmlPcieUtilCounter_t;

		struct nvmlUtilization_t {unsigned int gpu, memory;};
		struct nvmlMemory_t {unsigned long long total, free, used;};

		//? Function pointers
		const char* (*nvmlErrorString)(nvmlReturn_t);
		nvmlReturn_t (*nvmlInit)();
		nvmlReturn_t (*nvmlShutdown)();
		nvmlReturn_t (*nvmlDeviceGetCount)(unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*);
		nvmlReturn_t (*nvmlDeviceGetName)(nvmlDevice_t, char*, unsigned int);
		nvmlReturn_t (*nvmlDeviceGetPowerManagementLimit)(nvmlDevice_t, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetTemperatureThreshold)(nvmlDevice_t, nvmlTemperatureThresholds_t, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*);
		nvmlReturn_t (*nvmlDeviceGetClockInfo)(nvmlDevice_t, nvmlClockType_t, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetPowerState)(nvmlDevice_t, nvmlPstates_t*);
		nvmlReturn_t (*nvmlDeviceGetTemperature)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*);
		nvmlReturn_t (*nvmlDeviceGetPcieThroughput)(nvmlDevice_t, nvmlPcieUtilCounter_t, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetEncoderUtilization)(nvmlDevice_t, unsigned int*, unsigned int*);
		nvmlReturn_t (*nvmlDeviceGetDecoderUtilization)(nvmlDevice_t, unsigned int*, unsigned int*);

		//? Data
		QLibrary nvml_lib;
		bool initialized = false;
		unsigned int device_count = 0;
		vector<nvmlDevice_t> devices;
		template <bool is_init> bool collect(gpu_info* gpus_slice);

		bool init() {
			if (initialized) return false;

			//? Dynamic loading & linking (btop 1259–1273; dlopen → QLibrary)
			const array libNvAlts = {
				"libnvidia-ml.so",
				"libnvidia-ml.so.1",
			};

			for (const auto& l : libNvAlts) {
				nvml_lib.setFileName(QString::fromStdString(l));
				if (nvml_lib.load()) break;
			}
			if (not nvml_lib.isLoaded()) {
				qInfo(vostopCollect) << "backend skipped: libnvidia-ml not loadable, NVIDIA GPUs will not be detected:"
					<< nvml_lib.errorString();
				return false;
			}

			auto load_nvml_sym = [&](const char sym_name[]) -> void* {
				auto sym = nvml_lib.resolve(sym_name);
				if (sym == nullptr) {
					qCWarning(vostopCollect) << "NVML: Couldn't find function" << sym_name << ":" << nvml_lib.errorString();
					return nullptr;
				}
				return reinterpret_cast<void*>(sym);
			};

			#define LOAD_SYM(NAME)  if ((NAME = (decltype(NAME))load_nvml_sym(#NAME)) == nullptr) return false

			LOAD_SYM(nvmlErrorString);
			LOAD_SYM(nvmlInit);
			LOAD_SYM(nvmlShutdown);
			LOAD_SYM(nvmlDeviceGetCount);
			LOAD_SYM(nvmlDeviceGetHandleByIndex);
			LOAD_SYM(nvmlDeviceGetName);
			LOAD_SYM(nvmlDeviceGetPowerManagementLimit);
			LOAD_SYM(nvmlDeviceGetTemperatureThreshold);
			LOAD_SYM(nvmlDeviceGetUtilizationRates);
			LOAD_SYM(nvmlDeviceGetClockInfo);
			LOAD_SYM(nvmlDeviceGetPowerUsage);
			LOAD_SYM(nvmlDeviceGetPowerState);
			LOAD_SYM(nvmlDeviceGetTemperature);
			LOAD_SYM(nvmlDeviceGetMemoryInfo);
			LOAD_SYM(nvmlDeviceGetPcieThroughput);
			LOAD_SYM(nvmlDeviceGetEncoderUtilization);
			LOAD_SYM(nvmlDeviceGetDecoderUtilization);

			#undef LOAD_SYM

			//? Function calls
			nvmlReturn_t result = nvmlInit();
			if (result != NVML_SUCCESS) {
				qCDebug(vostopCollect) << "backend skipped: failed to initialize NVML, NVIDIA GPUs will not be detected:" << nvmlErrorString(result);
				return false;
			}

			//? Device count
			result = nvmlDeviceGetCount(&device_count);
			if (result != NVML_SUCCESS) {
				qCWarning(vostopCollect) << "NVML: Failed to get device count:" << nvmlErrorString(result);
				return false;
			}

			if (device_count > 0) {
				devices.resize(device_count);
				gpus.resize(device_count);
				gpu_names.resize(device_count);

				initialized = true;

				//? Check supported functions & get maximums
				Nvml::collect<1>(gpus.data());

				return true;
			} else {initialized = true; shutdown(); return false;}
		}

		bool shutdown() {
			if (!initialized) return false;
			nvmlReturn_t result = nvmlShutdown();
			if (NVML_SUCCESS == result) {
				initialized = false;
				nvml_lib.unload();
			} else qCWarning(vostopCollect) << "Failed to shutdown NVML:" << nvmlErrorString(result);

			return !initialized;
		}

		template <bool is_init>
		bool collect(gpu_info* gpus_slice) {
			if (!initialized) return false;

			nvmlReturn_t result;
			for (unsigned int i = 0; i < device_count; ++i) {
				if constexpr(is_init) {
					//? Device Handle
					result = nvmlDeviceGetHandleByIndex(i, devices.data() + i);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get device handle:" << nvmlErrorString(result);
						gpus[i].supported_functions = {false, false, false, false, false, false, false, false, false, false};
						continue;
					}

					//? Device name
					char name[NVML_DEVICE_NAME_BUFFER_SIZE];
					result = nvmlDeviceGetName(devices[i], name, NVML_DEVICE_NAME_BUFFER_SIZE);
					if (result != NVML_SUCCESS)
						qCWarning(vostopCollect) << "NVML: Failed to get device name:" << nvmlErrorString(result);
					else {
						gpu_names[i] = string(name);
						for (const auto& brand : {"NVIDIA", "Nvidia", "(R)", "(TM)"}) {
							gpu_names[i] = s_replace(gpu_names[i], brand, "");
						}
						gpu_names[i] = trim(gpu_names[i]);
					}

					//? Power usage
					unsigned int max_power;
					result = nvmlDeviceGetPowerManagementLimit(devices[i], &max_power);
					if (result != NVML_SUCCESS)
						qCWarning(vostopCollect) << "NVML: Failed to get maximum GPU power draw, defaulting to 225W:" << nvmlErrorString(result);
					else {
						gpus[i].pwr_max_usage = max_power; // RSMI reports power in microWatts
						gpu_pwr_total_max += max_power;
					}

					//? Get temp_max
					unsigned int temp_max;
					result = nvmlDeviceGetTemperatureThreshold(devices[i], NVML_TEMPERATURE_THRESHOLD_SHUTDOWN, &temp_max);
					if (result != NVML_SUCCESS)
						qCWarning(vostopCollect) << "NVML: Failed to get maximum GPU temperature, defaulting to 110°C:" << nvmlErrorString(result);
					else gpus[i].temp_max = (long long)temp_max;
				}

				//? PCIe link speeds (btop ran these on threads ≥20 ms; sequential here, default OFF)
				if (gpus_slice[i].supported_functions.pcie_txrx and (Settings::getB("nvml_measure_pcie_speeds") or is_init)) {
					unsigned int tx;
					result = nvmlDeviceGetPcieThroughput(devices[i], NVML_PCIE_UTIL_TX_BYTES, &tx);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get PCIe TX throughput:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.pcie_txrx = false;
					} else gpus_slice[i].pcie_tx = (long long)tx;

					unsigned int rx;
					result = nvmlDeviceGetPcieThroughput(devices[i], NVML_PCIE_UTIL_RX_BYTES, &rx);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get PCIe RX throughput:" << nvmlErrorString(result);
					} else gpus_slice[i].pcie_rx = (long long)rx;
				} else {
					gpus_slice[i].pcie_tx = -1;
					gpus_slice[i].pcie_rx = -1;
				}

				//? GPU & memory utilization
				if (gpus_slice[i].supported_functions.gpu_utilization) {
					nvmlUtilization_t utilization;
					result = nvmlDeviceGetUtilizationRates(devices[i], &utilization);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get GPU utilization:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.gpu_utilization = false;
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_utilization = false;
					} else {
						gpus_slice[i].gpu_percent.at("gpu-totals").push_back((long long)utilization.gpu);
						gpus_slice[i].mem_utilization_percent.push_back((long long)utilization.memory);
					}
				}

				//? Clock speeds
				if (gpus_slice[i].supported_functions.gpu_clock) {
					unsigned int gpu_clock;
					result = nvmlDeviceGetClockInfo(devices[i], NVML_CLOCK_GRAPHICS, &gpu_clock);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get GPU clock speed:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.gpu_clock = false;
					} else gpus_slice[i].gpu_clock_speed = (long long)gpu_clock;
				}

				if (gpus_slice[i].supported_functions.mem_clock) {
					unsigned int mem_clock;
					result = nvmlDeviceGetClockInfo(devices[i], NVML_CLOCK_MEM, &mem_clock);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get VRAM clock speed:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_clock = false;
					} else gpus_slice[i].mem_clock_speed = (long long)mem_clock;
				}

				//? Power usage & state
				if (gpus_slice[i].supported_functions.pwr_usage) {
					unsigned int power;
					result = nvmlDeviceGetPowerUsage(devices[i], &power);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get GPU power usage:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.pwr_usage = false;
					} else {
						gpus_slice[i].pwr_usage = (long long)power;
						if (gpus_slice[i].pwr_usage > gpus_slice[i].pwr_max_usage)
								gpus_slice[i].pwr_max_usage = gpus_slice[i].pwr_usage;
						gpus_slice[i].gpu_percent.at("gpu-pwr-totals").push_back(clamp((long long)round((double)gpus_slice[i].pwr_usage * 100.0 / (double)gpus_slice[i].pwr_max_usage), 0ll, 100ll));
					}
				}

				if (gpus_slice[i].supported_functions.pwr_state) {
					nvmlPstates_t pState;
					result = nvmlDeviceGetPowerState(devices[i], &pState);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get GPU power state:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.pwr_state = false;
					} else gpus_slice[i].pwr_state = static_cast<int>(pState);
				}

				//? GPU temperature
				if (gpus_slice[i].supported_functions.temp_info) {
					if (Settings::getB("check_temp")) {
						unsigned int temp;
						nvmlReturn_t result = nvmlDeviceGetTemperature(devices[i], NVML_TEMPERATURE_GPU, &temp);
						if (result != NVML_SUCCESS) {
							qCWarning(vostopCollect) << "NVML: Failed to get GPU temperature:" << nvmlErrorString(result);
							if constexpr(is_init) gpus_slice[i].supported_functions.temp_info = false;
						} else gpus_slice[i].temp.push_back((long long)temp);
					}
				}

				//? Memory info
				if (gpus_slice[i].supported_functions.mem_total) {
					nvmlMemory_t memory;
					result = nvmlDeviceGetMemoryInfo(devices[i], &memory);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get VRAM info:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_total = false;
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_used = false;
					} else {
						gpus_slice[i].mem_total = memory.total;
						gpus_slice[i].mem_used = memory.used;

						auto used_percent = (long long)round((double)memory.used * 100.0 / (double)memory.total);
						gpus_slice[i].gpu_percent.at("gpu-vram-totals").push_back(used_percent);
					}
				}

				//? Encoder info
				if (gpus_slice[i].supported_functions.encoder_utilization) {
					unsigned int utilization;
					unsigned int samplingPeriodUs;
					result = nvmlDeviceGetEncoderUtilization(devices[i], &utilization, &samplingPeriodUs);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get encoder utilization:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.encoder_utilization = false;
					} else gpus_slice[i].encoder_utilization = (long long)utilization;
				}

				//? Decoder info
				if (gpus_slice[i].supported_functions.decoder_utilization) {
					unsigned int utilization;
					unsigned int samplingPeriodUs;
					result = nvmlDeviceGetDecoderUtilization(devices[i], &utilization, &samplingPeriodUs);
					if (result != NVML_SUCCESS) {
						qCWarning(vostopCollect) << "NVML: Failed to get decoder utilization:" << nvmlErrorString(result);
						if constexpr(is_init) gpus_slice[i].supported_functions.decoder_utilization = false;
					} else gpus_slice[i].decoder_utilization = (long long)utilization;
				}
			}

			return true;
		}

		//? Explicit instantiation for the aggregate collector
		template bool collect<0>(gpu_info*);
		template bool collect<1>(gpu_info*);
	}

	//? AMD data collection (btop Rsmi defines/typedefs 226–300; dlfcn → QLibrary)
	namespace Rsmi {
		#define RSMI_DEVICE_NAME_BUFFER_SIZE 128
		#define RSMI_MAX_NUM_FREQUENCIES_V5  32
		#define RSMI_MAX_NUM_FREQUENCIES_V6  33
		#define RSMI_STATUS_SUCCESS           0
		#define RSMI_MEM_TYPE_VRAM            0
		#define RSMI_TEMP_CURRENT             0
		#define RSMI_TEMP_TYPE_EDGE           0
		#define RSMI_CLK_TYPE_MEM             4
		#define RSMI_CLK_TYPE_SYS             0
		#define RSMI_TEMP_MAX                 1

		typedef int rsmi_status_t,
					rsmi_temperature_metric_t,
					rsmi_clk_type_t,
					rsmi_memory_type_t;

		struct rsmi_version_t {uint32_t major,  minor,  patch; const char* build;};
		struct rsmi_frequencies_t_v5 {uint32_t num_supported, current; uint64_t frequency[RSMI_MAX_NUM_FREQUENCIES_V5];};
		struct rsmi_frequencies_t_v6 {bool has_deep_sleep; uint32_t num_supported, current; uint64_t frequency[RSMI_MAX_NUM_FREQUENCIES_V6];};

		//? Function pointers
		rsmi_status_t (*rsmi_init)(uint64_t);
		rsmi_status_t (*rsmi_shut_down)();
		rsmi_status_t (*rsmi_version_get)(rsmi_version_t*);
		rsmi_status_t (*rsmi_num_monitor_devices)(uint32_t*);
		rsmi_status_t (*rsmi_dev_name_get)(uint32_t, char*, size_t);
		rsmi_status_t (*rsmi_dev_power_cap_get)(uint32_t, uint32_t, uint64_t*);
		rsmi_status_t (*rsmi_dev_temp_metric_get)(uint32_t, uint32_t, rsmi_temperature_metric_t, int64_t*);
		rsmi_status_t (*rsmi_dev_busy_percent_get)(uint32_t, uint32_t*);
		rsmi_status_t (*rsmi_dev_memory_busy_percent_get)(uint32_t, uint32_t*);
		rsmi_status_t (*rsmi_dev_gpu_clk_freq_get_v5)(uint32_t, rsmi_clk_type_t, rsmi_frequencies_t_v5*);
		rsmi_status_t (*rsmi_dev_gpu_clk_freq_get_v6)(uint32_t, rsmi_clk_type_t, rsmi_frequencies_t_v6*);
		rsmi_status_t (*rsmi_dev_power_ave_get)(uint32_t, uint32_t, uint64_t*);
		rsmi_status_t (*rsmi_dev_memory_total_get)(uint32_t, rsmi_memory_type_t, uint64_t*);
		rsmi_status_t (*rsmi_dev_memory_usage_get)(uint32_t, rsmi_memory_type_t, uint64_t*);
		rsmi_status_t (*rsmi_dev_pci_throughput_get)(uint32_t, uint64_t*, uint64_t*, uint64_t*);

		uint32_t version_major = 0;

		//? Data
		QLibrary rsmi_lib;
		bool initialized = false;
		unsigned int device_count = 0;
		template <bool is_init> bool collect(gpu_info* gpus_slice);

		bool init() {
			if (initialized) return false;

			//? Try possible library paths and names for librocm_smi64.so
			const array libRocAlts = {
				"/opt/rocm/lib/librocm_smi64.so",
				"librocm_smi64.so",
				"librocm_smi64.so.5", // fedora
				"librocm_smi64.so.1.0", // debian
				"librocm_smi64.so.6",
				"librocm_smi64.so.7" // rocm 7 support / Ubuntu 26.04
			};

			for (const auto& l : libRocAlts) {
				rsmi_lib.setFileName(QString::fromStdString(l));
				if (rsmi_lib.load()) break;
			}

			if (not rsmi_lib.isLoaded()) {
				qInfo(vostopCollect) << "backend skipped: librocm_smi64 not loadable, AMD GPUs will not be detected:"
					<< rsmi_lib.errorString();
				return false;
			}

			auto load_rsmi_sym = [&](const char sym_name[]) -> void* {
				auto sym = rsmi_lib.resolve(sym_name);
				if (sym == nullptr) {
					qCWarning(vostopCollect) << "ROCm SMI: Couldn't find function" << sym_name << ":" << rsmi_lib.errorString();
					return nullptr;
				}
				return reinterpret_cast<void*>(sym);
			};

			#define LOAD_SYM(NAME)  if ((NAME = (decltype(NAME))load_rsmi_sym(#NAME)) == nullptr) return false

			LOAD_SYM(rsmi_init);
			LOAD_SYM(rsmi_shut_down);
			LOAD_SYM(rsmi_version_get);
			LOAD_SYM(rsmi_num_monitor_devices);
			LOAD_SYM(rsmi_dev_name_get);
			LOAD_SYM(rsmi_dev_power_cap_get);
			LOAD_SYM(rsmi_dev_temp_metric_get);
			LOAD_SYM(rsmi_dev_busy_percent_get);
			LOAD_SYM(rsmi_dev_memory_busy_percent_get);
			LOAD_SYM(rsmi_dev_power_ave_get);
			LOAD_SYM(rsmi_dev_memory_total_get);
			LOAD_SYM(rsmi_dev_memory_usage_get);
			LOAD_SYM(rsmi_dev_pci_throughput_get);

			#undef LOAD_SYM

			//? Function calls
			rsmi_status_t result = rsmi_init(0);
			if (result != RSMI_STATUS_SUCCESS) {
				qCDebug(vostopCollect) << "backend skipped: failed to initialize ROCm SMI, AMD GPUs will not be detected";
				return false;
			}

			//? Check version (btop 1626–1658)
			rsmi_version_t version;
			result = rsmi_version_get(&version);
			if (result != RSMI_STATUS_SUCCESS) {
				qCWarning(vostopCollect) << "ROCm SMI: Failed to get version";
				return false;
			}

			// Two distinct real-world libraries report version.major == 1:
			//   - ROCm 7.2 ships the v6 ABI (see upstream PR #1566).
			//   - Debian/Ubuntu's librocm-smi64 is built from 5.x sources but
			//     rocm_smi64Config.h reports version 1.0.0, so the ABI is v5.
			// Probe a 6.x-only symbol to disambiguate instead of guessing.
			uint32_t effective_major = version.major;
			if (version.major == 1) {
				bool has_v6_symbol = (rsmi_lib.resolve("rsmi_dev_activity_metric_get") != nullptr);
				effective_major = has_v6_symbol ? 6 : 5;
				qCWarning(vostopCollect) << "ROCm SMI: library reports version 1.x; assuming" << (has_v6_symbol ? 6 : 5) << "ABI based on symbol probe";
			}

			if (effective_major == 5) {
				if ((rsmi_dev_gpu_clk_freq_get_v5 = (decltype(rsmi_dev_gpu_clk_freq_get_v5))load_rsmi_sym("rsmi_dev_gpu_clk_freq_get")) == nullptr)
					return false;
			// In the release tarballs of rocm 6.0.0 and 6.0.2 the version queried with rsmi_version_get is 7.0.0.0
			} else if (effective_major == 6 || effective_major == 7) {
				if ((rsmi_dev_gpu_clk_freq_get_v6 = (decltype(rsmi_dev_gpu_clk_freq_get_v6))load_rsmi_sym("rsmi_dev_gpu_clk_freq_get")) == nullptr)
					return false;
			} else {
				qCWarning(vostopCollect) << "ROCm SMI: Dynamic loading only supported for version 5 and 6";
				return false;
			}
			version_major = effective_major;

			//? Device count
			result = rsmi_num_monitor_devices(&device_count);
			if (result != RSMI_STATUS_SUCCESS) {
				qCWarning(vostopCollect) << "ROCm SMI: Failed to fetch number of devices";
				return false;
			}

			if (device_count > 0) {
				gpus.resize(gpus.size() + device_count);
				gpu_names.resize(gpus.size() + device_count);

				initialized = true;

				//? Check supported functions & get maximums
				Rsmi::collect<1>(gpus.data() + Nvml::device_count);

				return true;
			} else {initialized = true; shutdown(); return false;}
		}

		bool shutdown() {
			if (!initialized) return false;
			if (rsmi_shut_down() == RSMI_STATUS_SUCCESS) {
				initialized = false;
				rsmi_lib.unload();
			} else qCWarning(vostopCollect) << "Failed to shutdown ROCm SMI";

			return true;
		}

		template <bool is_init>
		bool collect(gpu_info* gpus_slice) {
			if (!initialized) return false;
			rsmi_status_t result;

			for (uint32_t i = 0; i < device_count; ++i) {
				if constexpr(is_init) {
					//? Device name
					char name[RSMI_DEVICE_NAME_BUFFER_SIZE];
					result = rsmi_dev_name_get(i, name, RSMI_DEVICE_NAME_BUFFER_SIZE);
					if (result != RSMI_STATUS_SUCCESS)
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get device name";
					else gpu_names[Nvml::device_count + i] = string(name);

					//? Power usage
					uint64_t max_power;
					result = rsmi_dev_power_cap_get(i, 0, &max_power);
					if (result != RSMI_STATUS_SUCCESS)
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get maximum GPU power draw, defaulting to 225W";
					else {
						gpus_slice[i].pwr_max_usage = (long long)(max_power/1000); // RSMI reports power in microWatts
						gpu_pwr_total_max += gpus_slice[i].pwr_max_usage;
					}

					//? Get temp_max
					int64_t temp_max;
					result = rsmi_dev_temp_metric_get(i, RSMI_TEMP_TYPE_EDGE, RSMI_TEMP_MAX, &temp_max);
					if (result != RSMI_STATUS_SUCCESS)
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get maximum GPU temperature, defaulting to 110°C";
					else gpus_slice[i].temp_max = (long long)temp_max;

					//? Disable encoder and decoder utilisation on AMD
					gpus_slice[i].supported_functions.encoder_utilization = false;
					gpus_slice[i].supported_functions.decoder_utilization = false;
				}

				//? GPU utilization
				if (gpus_slice[i].supported_functions.gpu_utilization) {
					uint32_t utilization;
					result = rsmi_dev_busy_percent_get(i, &utilization);
					if (result != RSMI_STATUS_SUCCESS) {
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get GPU utilization";
						if constexpr(is_init) gpus_slice[i].supported_functions.gpu_utilization = false;
					} else gpus_slice[i].gpu_percent.at("gpu-totals").push_back((long long)utilization);
				}

				//? Memory utilization
				if (gpus_slice[i].supported_functions.mem_utilization) {
					uint32_t utilization;
					result = rsmi_dev_memory_busy_percent_get(i, &utilization);
					if (result != RSMI_STATUS_SUCCESS) {
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get VRAM utilization";
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_utilization = false;
					} else gpus_slice[i].mem_utilization_percent.push_back((long long)utilization);
				}

				//? Clock speeds (bound-check before indexing; btop 1749–1805)
				if (gpus_slice[i].supported_functions.gpu_clock) {
					if (version_major == 5) {
						rsmi_frequencies_t_v5 frequencies{};
						result = rsmi_dev_gpu_clk_freq_get_v5(i, RSMI_CLK_TYPE_SYS, &frequencies);
						if (result != RSMI_STATUS_SUCCESS) {
							qCWarning(vostopCollect) << "ROCm SMI: Failed to get GPU clock speed:";
							if constexpr(is_init) gpus_slice[i].supported_functions.gpu_clock = false;
						} else if (frequencies.num_supported == 0 || frequencies.current >= frequencies.num_supported
								|| frequencies.num_supported > RSMI_MAX_NUM_FREQUENCIES_V5) {
							qCWarning(vostopCollect) << "ROCm SMI: GPU clock speed unsupported on this device";
							if constexpr(is_init) gpus_slice[i].supported_functions.gpu_clock = false;
						} else gpus_slice[i].gpu_clock_speed = (long long)frequencies.frequency[frequencies.current]/1000000; // Hz to MHz
					}
					else if (version_major == 6 || version_major == 7) {
						rsmi_frequencies_t_v6 frequencies{};
						result = rsmi_dev_gpu_clk_freq_get_v6(i, RSMI_CLK_TYPE_SYS, &frequencies);
						if (result != RSMI_STATUS_SUCCESS) {
							qCWarning(vostopCollect) << "ROCm SMI: Failed to get GPU clock speed:";
							if constexpr(is_init) gpus_slice[i].supported_functions.gpu_clock = false;
						} else if (frequencies.num_supported == 0 || frequencies.current >= frequencies.num_supported
								|| frequencies.num_supported > RSMI_MAX_NUM_FREQUENCIES_V6) {
							qCWarning(vostopCollect) << "ROCm SMI: GPU clock speed unsupported on this device";
							if constexpr(is_init) gpus_slice[i].supported_functions.gpu_clock = false;
						} else gpus_slice[i].gpu_clock_speed = (long long)frequencies.frequency[frequencies.current]/1000000; // Hz to MHz
					}
				}

				if (gpus_slice[i].supported_functions.mem_clock) {
					if (version_major == 5) {
						rsmi_frequencies_t_v5 frequencies{};
						result = rsmi_dev_gpu_clk_freq_get_v5(i, RSMI_CLK_TYPE_MEM, &frequencies);
						if (result != RSMI_STATUS_SUCCESS) {
							qCWarning(vostopCollect) << "ROCm SMI: Failed to get VRAM clock speed:";
							if constexpr(is_init) gpus_slice[i].supported_functions.mem_clock = false;
						} else if (frequencies.num_supported == 0 || frequencies.current >= frequencies.num_supported
								|| frequencies.num_supported > RSMI_MAX_NUM_FREQUENCIES_V5) {
							qCWarning(vostopCollect) << "ROCm SMI: VRAM clock speed unsupported on this device";
							if constexpr(is_init) gpus_slice[i].supported_functions.mem_clock = false;
						} else gpus_slice[i].mem_clock_speed = (long long)frequencies.frequency[frequencies.current]/1000000; // Hz to MHz
					}
					else if (version_major == 6 || version_major == 7) {
						rsmi_frequencies_t_v6 frequencies{};
						result = rsmi_dev_gpu_clk_freq_get_v6(i, RSMI_CLK_TYPE_MEM, &frequencies);
						if (result != RSMI_STATUS_SUCCESS) {
							qCWarning(vostopCollect) << "ROCm SMI: Failed to get VRAM clock speed:";
							if constexpr(is_init) gpus_slice[i].supported_functions.mem_clock = false;
						} else if (frequencies.num_supported == 0 || frequencies.current >= frequencies.num_supported
								|| frequencies.num_supported > RSMI_MAX_NUM_FREQUENCIES_V6) {
							qCWarning(vostopCollect) << "ROCm SMI: VRAM clock speed unsupported on this device";
							if constexpr(is_init) gpus_slice[i].supported_functions.mem_clock = false;
						} else gpus_slice[i].mem_clock_speed = (long long)frequencies.frequency[frequencies.current]/1000000; // Hz to MHz
					}
				}

				//? Power usage & state
				if (gpus_slice[i].supported_functions.pwr_usage) {
					uint64_t power;
					result = rsmi_dev_power_ave_get(i, 0, &power);
					if (result != RSMI_STATUS_SUCCESS) {
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get GPU power usage";
						if constexpr(is_init) gpus_slice[i].supported_functions.pwr_usage = false;
					} else {
							gpus_slice[i].pwr_usage = (long long)power / 1000;
							if (gpus_slice[i].pwr_usage > gpus_slice[i].pwr_max_usage)
								gpus_slice[i].pwr_max_usage = gpus_slice[i].pwr_usage;
							gpus_slice[i].gpu_percent.at("gpu-pwr-totals").push_back(clamp((long long)round((double)gpus_slice[i].pwr_usage * 100.0 / (double)gpus_slice[i].pwr_max_usage), 0ll, 100ll));
						}

					if constexpr(is_init) gpus_slice[i].supported_functions.pwr_state = false;
				}

				//? GPU temperature
				if (gpus_slice[i].supported_functions.temp_info) {
					if (Settings::getB("check_temp") or is_init) {
						int64_t temp;
						result = rsmi_dev_temp_metric_get(i, RSMI_TEMP_TYPE_EDGE, RSMI_TEMP_CURRENT, &temp);
						if (result != RSMI_STATUS_SUCCESS) {
							qCWarning(vostopCollect) << "ROCm SMI: Failed to get GPU temperature";
							if constexpr(is_init) gpus_slice[i].supported_functions.temp_info = false;
						} else gpus_slice[i].temp.push_back((long long)temp/1000);
					}
				}

				//? Memory info
				if (gpus_slice[i].supported_functions.mem_total) {
					uint64_t total;
					result = rsmi_dev_memory_total_get(i, RSMI_MEM_TYPE_VRAM, &total);
					if (result != RSMI_STATUS_SUCCESS) {
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get total VRAM";
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_total = false;
					} else gpus_slice[i].mem_total = total;
				}

				if (gpus_slice[i].supported_functions.mem_used) {
					uint64_t used;
					result = rsmi_dev_memory_usage_get(i, RSMI_MEM_TYPE_VRAM, &used);
					if (result != RSMI_STATUS_SUCCESS) {
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get VRAM usage";
						if constexpr(is_init) gpus_slice[i].supported_functions.mem_used = false;
					} else {
						gpus_slice[i].mem_used = used;
						if (gpus_slice[i].supported_functions.mem_total)
							gpus_slice[i].gpu_percent.at("gpu-vram-totals").push_back((long long)round((double)used * 100.0 / (double)gpus_slice[i].mem_total));
					}
				}

				//? PCIe link speeds (sequential; default OFF)
				if ((gpus_slice[i].supported_functions.pcie_txrx and Settings::getB("rsmi_measure_pcie_speeds")) or is_init) {
					uint64_t tx, rx;
					result = rsmi_dev_pci_throughput_get(i, &tx, &rx, nullptr);
					if (result != RSMI_STATUS_SUCCESS) {
						qCWarning(vostopCollect) << "ROCm SMI: Failed to get PCIe throughput";
						if constexpr(is_init) gpus_slice[i].supported_functions.pcie_txrx = false;
					} else {
						gpus_slice[i].pcie_tx = (long long)tx;
						gpus_slice[i].pcie_rx = (long long)rx;
					}
				} else {
					gpus_slice[i].pcie_tx = -1;
					gpus_slice[i].pcie_rx = -1;
				}
			}

			return true;
		}

		//? Explicit instantiation for the aggregate collector
		template bool collect<0>(gpu_info*);
		template bool collect<1>(gpu_info*);
	}

	//? AMD sysfs (consumer GPU / iGPU) fallback (btop Asysfs 2029–2212)
	namespace Asysfs {

		struct device_paths {
			std::filesystem::path device;
			std::filesystem::path hwmon;
			std::filesystem::path power;            //? empty if no power sensor
			uint32_t pci_device_id;
			bool has_temp;
			bool has_freq;
			bool has_busy;
			bool has_vram;
		};

		bool initialized = false;
		unsigned int device_count = 0;
		vector<device_paths> devices;
		template <bool is_init> bool collect(gpu_info* gpus_slice);

		//? Read a sysfs node containing a single integer; return fallback on missing/parse error.
		static long long read_ll(const std::filesystem::path& path, long long fallback = 0) {
			try {
				return std::stoll(readfile(path, std::to_string(fallback)));
			} catch (const std::exception&) {
				return fallback;
			}
		}

		//? Match /sys/class/drm/cardN (no '-', all digits after "card"). Skips card1-DP-1, renderD*, etc.
		static bool is_card_node(const string& fname) {
			if (not fname.starts_with("card") or fname.size() <= 4) return false;
			return std::ranges::all_of(fname.begin() + 4, fname.end(),
				[](char c) { return c >= '0' and c <= '9'; });
		}

		//? Pick the first hwmon* subdirectory under <device>/hwmon, or empty path if none.
		static std::filesystem::path find_hwmon(const std::filesystem::path& device) {
			const auto hwmon_dir = device / "hwmon";
			std::error_code ec;
			if (not std::filesystem::is_directory(hwmon_dir, ec)) return {};
			for (const auto& h : std::filesystem::directory_iterator(hwmon_dir, ec)) {
				if (h.is_directory()) return h.path();
			}
			return {};
		}

		bool init() {
			if (initialized) return false;
			//? Self-skip when rocm-smi already enumerated AMD devices — avoids double-counting on
			//? systems where both backends would succeed.
			if (Rsmi::device_count > 0) return false;
			devices.clear();

			const std::filesystem::path drm_root("/sys/class/drm");
			std::error_code ec;
			if (not std::filesystem::is_directory(drm_root, ec)) {
				qCDebug(vostopCollect) << "backend skipped: amdgpu sysfs /sys/class/drm not present";
				return false;
			}

			for (const auto& entry : std::filesystem::directory_iterator(drm_root, ec)) {
				if (not is_card_node(entry.path().filename().string())) continue;

				const auto device_link = entry.path() / "device";
				if (not std::filesystem::exists(device_link)) continue;

				//? Vendor must be exactly 0x1002 (AMD). The sysfs node ends with a newline,
				//? so trim before comparing.
				string vendor = readfile(device_link / "vendor", "");
				while (not vendor.empty() and (vendor.back() == '\n' or vendor.back() == ' ')) vendor.pop_back();
				if (vendor != "0x1002") continue;

				//? Driver must be amdgpu (rules out radeon-driver-only legacy GPUs which use a
				//? different sysfs layout).
				const auto driver_link = std::filesystem::read_symlink(device_link / "driver", ec);
				if (ec or driver_link.filename().string() != "amdgpu") continue;

				device_paths d{};
				d.device = device_link;
				try {
					//? "device" file holds e.g. "0x150e\n" — base 0 lets stoul autodetect the 0x prefix.
					d.pci_device_id = (uint32_t)std::stoul(readfile(device_link / "device", "0"), nullptr, 0);
				} catch (const std::exception&) {
					d.pci_device_id = 0;
				}
				d.hwmon = find_hwmon(device_link);

				d.has_busy = std::filesystem::exists(device_link / "gpu_busy_percent");
				d.has_vram = std::filesystem::exists(device_link / "mem_info_vram_total");

				if (not d.hwmon.empty()) {
					d.has_temp = std::filesystem::exists(d.hwmon / "temp1_input");
					d.has_freq = std::filesystem::exists(d.hwmon / "freq1_input");
					//? Prefer power1_average (filtered, EMA) over power1_input (instantaneous).
					const auto avg = d.hwmon / "power1_average";
					const auto inst = d.hwmon / "power1_input";
					if (std::filesystem::exists(avg)) d.power = avg;
					else if (std::filesystem::exists(inst)) d.power = inst;
				}

				//? Skip cards with no readable signals — virtual GPUs, fresh-bound devices,
				//? or driver states where nothing useful is exposed yet.
				if (not (d.has_busy or d.has_vram or d.has_temp or d.has_freq or not d.power.empty())) {
					qCDebug(vostopCollect) << "amdgpu sysfs: skipping" << sstr(QString::fromStdString(device_link.string())) << "— no readable metrics";
					continue;
				}

				devices.push_back(std::move(d));
			}

			device_count = (uint32_t)devices.size();
			if (device_count == 0) {
				qCDebug(vostopCollect) << "backend skipped: amdgpu sysfs no AMD cards found in /sys/class/drm";
				return false;
			}

			gpus.resize(gpus.size() + device_count);
			gpu_names.resize(Nvml::device_count + Rsmi::device_count + device_count);
			for (uint32_t i = 0; i < device_count; ++i) {
				gpu_names[Nvml::device_count + Rsmi::device_count + i] =
					sstr(QString("AMD GPU (1002:%1)").arg(devices[i].pci_device_id, 4, 16, QChar(u'0')));
			}

			initialized = true;
			qInfo(vostopCollect) << "Using amdgpu sysfs for" << device_count << "AMD GPU(s)";
			Asysfs::collect<1>(gpus.data() + Nvml::device_count + Rsmi::device_count);
			return true;
		}

		bool shutdown() {
			if (not initialized) return false;
			devices.clear();
			device_count = 0;
			initialized = false;
			return true;
		}

		template <bool is_init> bool collect(gpu_info* gpus_slice) {
			if (not initialized) return false;

			for (uint32_t i = 0; i < device_count; ++i) {
				gpu_info& gpu = gpus_slice[i];
				const device_paths& d = devices[i];

				if constexpr (is_init) {
					gpu.supported_functions = {
						.gpu_utilization = d.has_busy,
						.mem_utilization = false,
						.gpu_clock = d.has_freq,
						.mem_clock = false, //? only DPM table is exposed via pp_dpm_mclk, not the current frequency
						.pwr_usage = not d.power.empty(),
						.pwr_state = false,
						.temp_info = d.has_temp,
						.mem_total = d.has_vram,
						.mem_used = d.has_vram,
						.pcie_txrx = false,
						.encoder_utilization = false,
						.decoder_utilization = false,
					};
					//? Start at zero and let the observed peak set the scale, like Intel does.
					gpu.pwr_max_usage = 0;
				}

				if (d.has_busy) {
					gpu.gpu_percent.at("gpu-totals").push_back(
						std::clamp(read_ll(d.device / "gpu_busy_percent"), 0LL, 100LL));
				}

				if (d.has_temp) {
					gpu.temp.push_back(read_ll(d.hwmon / "temp1_input") / 1000); //? millidegrees → degrees
				}

				if (not d.power.empty()) {
					gpu.pwr_usage = read_ll(d.power) / 1000; //? microwatts → milliwatts
					gpu.pwr_max_usage = std::max(gpu.pwr_max_usage, gpu.pwr_usage);
					if (gpu.pwr_max_usage > 0) {
						gpu.gpu_percent.at("gpu-pwr-totals").push_back(
							std::clamp((long long)std::round((double)gpu.pwr_usage * 100.0 / (double)gpu.pwr_max_usage), 0LL, 100LL));
					}
				}

				if (d.has_freq) {
					gpu.gpu_clock_speed = (unsigned int)(read_ll(d.hwmon / "freq1_input") / 1'000'000); //? Hz → MHz
				}

				if (d.has_vram) {
					gpu.mem_total = read_ll(d.device / "mem_info_vram_total");
					gpu.mem_used = read_ll(d.device / "mem_info_vram_used");
					if (gpu.mem_total > 0) {
						gpu.gpu_percent.at("gpu-vram-totals").push_back(
							std::clamp((long long)std::round((double)gpu.mem_used * 100.0 / (double)gpu.mem_total), 0LL, 100LL));
					}
				}
			}
			return true;
		}

		//? Explicit template instantiations referenced from Shared::init and Gpu::collect.
		template bool collect<0>(gpu_info*);
		template bool collect<1>(gpu_info*);
	}

	//? Collect data from GPU-specific backends (btop 2215–2270; Intel call cut — no Intel backend in MVP)
	auto collect() -> std::vector<gpu_info>& {
		//* Collect data
		Nvml::collect<0>(gpus.data());
		if (not gpus.empty()) {
			Rsmi::collect<0>(gpus.data() + Nvml::device_count);
			Asysfs::collect<0>(gpus.data() + Nvml::device_count + Rsmi::device_count);
		}

		//* Calculate average usage (btop 2226–2265; width trims → VostopHistoryDepth)
		long long avg = 0;
		long long mem_usage_total = 0;
		long long mem_total = 0;
		long long pwr_total = 0;
		for (auto& gpu : gpus) {
			if (gpu.supported_functions.gpu_utilization and not gpu.gpu_percent.at("gpu-totals").empty())
				avg += gpu.gpu_percent.at("gpu-totals").back();
			if (gpu.supported_functions.mem_used)
				mem_usage_total += gpu.mem_used;
			if (gpu.supported_functions.mem_total)
				mem_total += gpu.mem_total;
			if (gpu.supported_functions.pwr_usage)
				pwr_total += gpu.pwr_usage;

			//* Trim vectors if there are more values than needed for graphs
			while (cmp_greater(gpu.gpu_percent.at("gpu-totals").size(), VostopHistoryDepth)) gpu.gpu_percent.at("gpu-totals").pop_front();
			while (cmp_greater(gpu.mem_utilization_percent.size(), VostopHistoryDepth)) gpu.mem_utilization_percent.pop_front();
			while (cmp_greater(gpu.gpu_percent.at("gpu-pwr-totals").size(), VostopHistoryDepth)) gpu.gpu_percent.at("gpu-pwr-totals").pop_front();
			while (cmp_greater(gpu.temp.size(), 18)) gpu.temp.pop_front();
			while (cmp_greater(gpu.gpu_percent.at("gpu-vram-totals").size(), VostopHistoryDepth / 2)) gpu.gpu_percent.at("gpu-vram-totals").pop_front();
		}

		if (not gpus.empty()) {
			long long count = (long long)gpus.size();
			long long supported = 0;
			for (auto& gpu : gpus)
				if (gpu.supported_functions.gpu_utilization) supported++;
			shared_gpu_percent.at("gpu-average").push_back(supported > 0 ? avg / supported : 0);
			if (mem_total != 0)
				shared_gpu_percent.at("gpu-vram-total").push_back(static_cast<long long>(round(mem_usage_total * 100.0 / mem_total)));
			if (gpu_pwr_total_max != 0)
				shared_gpu_percent.at("gpu-pwr-total").push_back(clamp(static_cast<long long>(round(pwr_total * 100.0 / gpu_pwr_total_max)), 0ll, 100ll));

			while (cmp_greater(shared_gpu_percent.at("gpu-average").size(), VostopHistoryDepth)) shared_gpu_percent.at("gpu-average").pop_front();
			while (cmp_greater(shared_gpu_percent.at("gpu-pwr-total").size(), VostopHistoryDepth)) shared_gpu_percent.at("gpu-pwr-total").pop_front();
			while (cmp_greater(shared_gpu_percent.at("gpu-vram-total").size(), VostopHistoryDepth)) shared_gpu_percent.at("gpu-vram-total").pop_front();
		}

		return gpus;
	}

	std::unordered_map<std::string, std::deque<long long>> shared_gpu_percent = {
		{"gpu-average", {}},
		{"gpu-vram-total", {}},
		{"gpu-pwr-total", {}},
	};
}

namespace Cpu {

	std::unordered_map<std::string, Sensor> found_sensors;
	string cpu_sensor;
	vector<string> available_sensors;

	bool get_sensors() { //? btop 492–622; fmt::format → QString
		bool got_cpu = false, got_coretemp = false;
		vector<fs::path> search_paths;
		try {
			//? Setup up paths to search for sensors
			if (fs::exists(fs::path("/sys/class/hwmon")) and access("/sys/class/hwmon", R_OK) != -1) {
				for (const auto& dir : fs::directory_iterator(fs::path("/sys/class/hwmon"))) {
					fs::path add_path = fs::canonical(dir.path());
					if (v_contains(search_paths, add_path) or v_contains(search_paths, add_path / "device")) continue;

					if (add_path.string().find("coretemp") != string::npos)
						got_coretemp = true;

					for (const auto & file : fs::directory_iterator(add_path)) {
						if (file.path().filename() == "device") {
							for (const auto & dev_file : fs::directory_iterator(file.path())) {
								string dev_filename = dev_file.path().filename();
								if (dev_filename.starts_with("temp") and dev_filename.ends_with("_input")) {
									search_paths.push_back(file.path());
									break;
								}
							}
						}

						string filename = file.path().filename();
						if (filename.starts_with("temp") and filename.ends_with("_input")) {
							search_paths.push_back(add_path);
							break;
						}
					}
				}
			}
			if (not got_coretemp and fs::exists(fs::path("/sys/devices/platform/coretemp.0/hwmon"))) {
				for (auto& d : fs::directory_iterator(fs::path("/sys/devices/platform/coretemp.0/hwmon"))) {
					fs::path add_path = fs::canonical(d.path());

					for (const auto & file : fs::directory_iterator(add_path)) {
						string filename = file.path().filename();
						if (filename.starts_with("temp") and filename.ends_with("_input") and not v_contains(search_paths, add_path)) {
								search_paths.push_back(add_path);
								got_coretemp = true;
								break;
						}
					}
				}
			}
			//? Scan any found directories for temperature sensors
			if (not search_paths.empty()) {
				for (const auto& path : search_paths) {
					const string pname = readfile(path / "name", path.filename());
					for (const auto & file : fs::directory_iterator(path)) {
						const string file_suffix = "input";
						const int file_id = atoi(file.path().filename().c_str() + 4); // skip "temp" prefix
						string file_path = file.path();

						if (file_path.find(file_suffix) == string::npos or file_path.find("nvme") != string::npos) {
							continue;
						}

						const string basepath = file_path.erase(file_path.find(file_suffix), file_suffix.length());
						const string label = readfile(fs::path(basepath + "label"), "temp" + to_string(file_id));
						const string sensor_name = pname + "/" + label;
						const int64_t temp = stol(readfile(fs::path(basepath + "input"), "0")) / 1000;
						const int64_t crit = stol(readfile(fs::path(basepath + "crit"), "95000")) / 1000;

						found_sensors[sensor_name] = Sensor { fs::path(basepath + "input"), temp, crit };

						if (not got_cpu and (label.starts_with("Package id") or label.starts_with("Tdie") or label.starts_with("SoC Temperature"))) {
							got_cpu = true;
							cpu_sensor = sensor_name;
						}
						else if (label.starts_with("Core") or label.starts_with("Tccd")) {
							got_coretemp = true;
							if (not v_contains(core_sensors, sensor_name)) core_sensors.push_back(sensor_name);
						}
					}
				}
			}
			//? If no good candidate for cpu temp has been found scan /sys/class/thermal
			if (not got_cpu and fs::exists(fs::path("/sys/class/thermal"))) {
				const string rootpath = fs::path("/sys/class/thermal/thermal_zone");
				for (int i = 0; fs::exists(fs::path(rootpath + to_string(i))); i++) {
					const fs::path basepath = rootpath + to_string(i);
					if (not fs::exists(basepath / "temp")) continue;
					const string label = readfile(basepath / "type", "temp" + to_string(i));
					const string sensor_name = "thermal" + to_string(i) + "/" + label;
					const int64_t temp = stol(readfile(basepath / "temp", "0")) / 1000;

					int64_t high = 0;
					int64_t crit = 0;
					for (int ii = 0; fs::exists(fs::path(sstr(QString("%1/trip_point_%2_temp").arg(sstr(QString::fromStdString(basepath.string()))).arg(ii)))); ii++) {
						const string trip_type = readfile(sstr(QString("%1/trip_point_%2_type").arg(sstr(QString::fromStdString(basepath.string()))).arg(ii)));
						if (not is_in(trip_type, "high", "critical")) continue;
						auto& val = (trip_type == "high" ? high : crit);
						val = stol(readfile(sstr(QString("%1/trip_point_%2_temp").arg(sstr(QString::fromStdString(basepath.string()))).arg(ii)), "0")) / 1000;
					}
					if (high < 1) high = 80;
					if (crit < 1) crit = 95;

					found_sensors[sensor_name] = Sensor { basepath / "temp", temp, crit };
				}
			}

		}
		catch (...) {}

		if (not got_coretemp or core_sensors.empty()) {
			cpu_temp_only = true;
		}
		else {
			rng::sort(core_sensors, rng::less{});
			rng::stable_sort(core_sensors, [](const auto& a, const auto& b){
				return a.size() < b.size();
			});
		}

		if (cpu_sensor.empty() and not found_sensors.empty()) {
			for (const auto& [name, sensor] : found_sensors) {
				if (str_to_lower(name).find("cpu") != string::npos or str_to_lower(name).find("k10temp") != string::npos) {
					cpu_sensor = name;
					break;
				}
			}
			if (cpu_sensor.empty()) {
				cpu_sensor = found_sensors.begin()->first;
				qCWarning(vostopCollect) << "No good candidate for cpu sensor found, using random from all found sensors.";
			}
		}

		return not found_sensors.empty();
	}

	void update_sensors() { //? btop 624–647
		if (cpu_sensor.empty()) return;

		const auto cfg_sensor = sstr(Settings::getS("cpu_sensor"));
		const string& sel_sensor = (not cfg_sensor.empty() and found_sensors.contains(cfg_sensor)) ? cfg_sensor : Cpu::cpu_sensor;

		found_sensors.at(sel_sensor).temp = stol(readfile(found_sensors.at(sel_sensor).path, "0")) / 1000;
		current_cpu.temp.at(0).push_back(found_sensors.at(sel_sensor).temp);
		current_cpu.temp_max = found_sensors.at(sel_sensor).crit;
		if (current_cpu.temp.at(0).size() > 20) current_cpu.temp.at(0).pop_front();

		if (Settings::getB("show_coretemp") and not cpu_temp_only) {
			for (vector<string_view> done; const auto& sensor : core_sensors) {
				if (v_contains(done, sensor)) continue;
				found_sensors.at(sensor).temp = stol(readfile(found_sensors.at(sensor).path, "0")) / 1000;
				done.push_back(sensor);
			}
			for (const auto& [core, temp] : core_mapping) {
				if (cmp_less(core + 1, current_cpu.temp.size()) and cmp_less(temp, core_sensors.size())) {
					current_cpu.temp.at(core + 1).push_back(found_sensors.at(core_sensors.at(temp)).temp);
					if (current_cpu.temp.at(core + 1).size() > 20) current_cpu.temp.at(core + 1).pop_front();
				}
			}
		}
	}

	//? Battery struct + collector (btop 828–1030)
	struct battery {
		fs::path base_dir, energy_now, charge_now, energy_full, charge_full, power_now, current_now, voltage_now, status, online;
		string device_type;
		bool use_energy_or_charge = true;
		bool use_power = true;
	};

	auto get_battery() -> tuple<int, float, long, string> {
		if (not Cpu::has_battery) return {0, 0, 0, ""};
		static string auto_sel;
		static std::unordered_map<string, battery> batteries;

		//? Get paths to needed files and check for valid values on first run
		if (batteries.empty() and Cpu::has_battery) {
			try {
				if (fs::exists("/sys/class/power_supply")) {
					for (const auto& d : fs::directory_iterator("/sys/class/power_supply")) {
						//? Only consider online power supplies of type Battery or UPS
						//? see kernel docs for details on the file structure and contents
						//? https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-class-power
						battery new_bat;
						fs::path bat_dir;
						try {
							if (not d.is_directory()
								or not fs::exists(d.path() / "type")
								or not fs::exists(d.path() / "present")
								or stoi(readfile(d.path() / "present")) != 1)
								continue;
							string dev_type = readfile(d.path() / "type");
							if (is_in(dev_type, "Battery", "UPS")) {
								bat_dir = d.path();
								new_bat.base_dir = d.path();
								new_bat.device_type = dev_type;
							}
						} catch (...) {
							//? skip power supplies not conforming to the kernel standard
							continue;
						}

						if (fs::exists(bat_dir / "energy_now")) new_bat.energy_now = bat_dir / "energy_now";
						else if (fs::exists(bat_dir / "charge_now")) new_bat.charge_now = bat_dir / "charge_now";
						else new_bat.use_energy_or_charge = false;

						if (fs::exists(bat_dir / "energy_full")) new_bat.energy_full = bat_dir / "energy_full";
						else if (fs::exists(bat_dir / "charge_full")) new_bat.charge_full = bat_dir / "charge_full";
						else new_bat.use_energy_or_charge = false;

						if (not new_bat.use_energy_or_charge and not fs::exists(bat_dir / "capacity")) {
							continue;
						}

						if (fs::exists(bat_dir / "power_now")) {
							new_bat.power_now = bat_dir / "power_now";
						}
						else if ((fs::exists(bat_dir / "current_now")) and (fs::exists(bat_dir / "voltage_now"))) {
							 new_bat.current_now = bat_dir / "current_now";
							 new_bat.voltage_now = bat_dir / "voltage_now";
						}
						else {
							new_bat.use_power = false;
						}

						if (fs::exists(bat_dir / "AC0/online")) new_bat.online = bat_dir / "AC0/online";
						else if (fs::exists(bat_dir / "AC/online")) new_bat.online = bat_dir / "AC/online";

						batteries[bat_dir.filename()] = new_bat;
					}
				}
			}
			catch (...) {
				batteries.clear();
			}
			if (batteries.empty()) {
				Cpu::has_battery = false;
				return {0, 0, 0, ""};
			}
		}

		const auto battery_sel = sstr(Settings::getS("selected_battery"));

		if (auto_sel.empty()) {
			for (auto& [name, bat] : batteries) {
				if (bat.device_type == "Battery") {
					auto_sel = name;
					break;
				}
			}
			if (auto_sel.empty()) auto_sel = batteries.begin()->first;
		}

		auto& b = (battery_sel != "Auto" and batteries.contains(battery_sel)) ? batteries.at(battery_sel) : batteries.at(auto_sel);

		int percent = -1;
		long seconds = -1;
		float watts = -1;

		//? Try to get battery percentage
		if (percent < 0) {
			try {
				percent = stoi(readfile(b.base_dir / "capacity", "-1"));
			}
			catch (const std::invalid_argument&) { }
			catch (const std::out_of_range&) { }
		}
		if (b.use_energy_or_charge and percent < 0) {
			try {
				percent = round(100.0 * stod(readfile(b.energy_now, "-1")) / stod(readfile(b.energy_full, "1")));
			}
			catch (const std::invalid_argument&) { }
			catch (const std::out_of_range&) { }
		}
		if (b.use_energy_or_charge and percent < 0) {
			try {
				percent = round(100.0 * stod(readfile(b.charge_now, "-1")) / stod(readfile(b.charge_full, "1")));
			}
			catch (const std::invalid_argument&) { }
			catch (const std::out_of_range&) { }
		}
		if (percent < 0) {
			Cpu::has_battery = false;
			return {0, 0, 0, ""};
		}

		//? Get charging/discharging status
		string status = str_to_lower(readfile(b.base_dir / "status", "unknown"));
		if (status == "unknown" and not b.online.empty()) {
			const auto online = readfile(b.online, "0");
			if (online == "1" and percent < 100) status = "charging";
			else if (online == "1") status = "full";
			else status = "discharging";
		}

		//? Get seconds to empty
		if (not is_in(status, "charging", "full")) {
			if (b.use_energy_or_charge ) {
				if (not b.power_now.empty()) {
					try {
						seconds = abs(round(stod(readfile(b.energy_now, "0")) / stod(readfile(b.power_now, "1")) * 3600));
					}
					catch (const std::invalid_argument&) { }
					catch (const std::out_of_range&) { }
				}
				else if (not b.current_now.empty()) {
					try {
						seconds = abs(round(stod(readfile(b.charge_now, "0")) / stod(readfile(b.current_now, "1")) * 3600));
					}
					catch (const std::invalid_argument&) { }
					catch (const std::out_of_range&) { }
				}
			}

			if (seconds < 0 and fs::exists(b.base_dir / "time_to_empty")) {
				try {
					seconds = stoll(readfile(b.base_dir / "time_to_empty", "0")) * 60;
				}
				catch (const std::invalid_argument&) { }
				catch (const std::out_of_range&) { }
			}
		}
		//? Or get seconds to full
		else if(is_in(status, "charging")) {
			if (b.use_energy_or_charge ) {
				if (not b.power_now.empty()) {
					try {
						seconds = (round(stod(readfile(b.energy_full , "0")) - round(stod(readfile(b.energy_now, "0"))))
									/ abs(stod(readfile(b.power_now, "1"))) * 3600);
					}
					catch (const std::invalid_argument&) { }
					catch (const std::out_of_range&) { }
				}
				else if (not b.current_now.empty()) {
					try {
						seconds = (round(stod(readfile(b.charge_full , "0")) - stod(readfile(b.charge_now, "0")))
									/ std::abs(stod(readfile(b.current_now, "1"))) * 3600);
					}
					catch (const std::invalid_argument&) { }
					catch (const std::out_of_range&) { }
				}
			}
		}

		//? Get power draw
		if (b.use_power) {
			if (not b.power_now.empty()) {
				try {
					watts = stof(readfile(b.power_now, "-1")) / 1000000.0F;
				}
				catch (const std::invalid_argument&) { }
				catch (const std::out_of_range&) { }
			}
			else if (not b.voltage_now.empty() and not b.current_now.empty()) {
				try {
					watts = stof(readfile(b.current_now, "-1")) / 1000000.0F * stof(readfile(b.voltage_now, "1")) / 1000000.0F;
				}
				catch (const std::invalid_argument&) { }
				catch (const std::out_of_range&) { }
			}
		}

		return {percent, watts, seconds, status};
	}
}