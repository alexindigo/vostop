/*
 * vostop — CPU & memory collectors, naturalized from btop.
 *
 * Portions Copyright 2021 Aristocratos (jakob@qvantnet.com) — vendored from
 * btop src/linux/btop_collect.cpp @ 956050716f290f529cd51d271b3d7e36eb1a22f0
 * (Shared::init 333–431 trimmed, Cpu::collect 1104–1247, detect_active_cpus
 * 1079–1102, watts 1032–1071, get_cpuName 460–490, get_core_mapping 760–826,
 * get_totalMem 2319–2331, Mem::collect pure-memory 2333–2423, Tools::
 * system_uptime 3629–3642) and src/btop_shared.cpp (trim_name 38–80,
 * detect_container 313–331). Apache-2.0; see THIRD-PARTY-NOTICES.
 *
 * Naturalization (phase-2 checklist): Config::getS/getB → Settings::getS/getB;
 * Logger::* → QLoggingCategory("vostop.collect"); Runner::stopping/coreNum_reset
 * and no_update early-return removed (always collect on QTimer); width*2 deque
 * trims → fixed VostopHistoryDepth ring; fmt::format → QString; Tools file
 * helpers → tools_qt (QFile/QString). /proc & /sys math kept verbatim.
 */
#include "cpu_mem.h"

#include "tools_qt.h"
#include "../backend/Settings.h"

#include <QLoggingCategory>
#include <QString>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <unistd.h>
#include <sys/sysinfo.h>

Q_LOGGING_CATEGORY(vostopCollect, "vostop.collect")

namespace fs = std::filesystem;
namespace rng = std::ranges;
using namespace std;
using namespace std::string_literals;
using namespace tools;
using std::string, std::vector;

namespace Shared {
	fs::path procPath;
	fs::path passwd_path;
	long pageSize = 0, clkTck = 0, coreCount = 0;

	void init() {
		//? Shared global variables init (btop 336–363, Logger → qCWarning)
		procPath = (fs::is_directory(fs::path("/proc")) and access("/proc", R_OK) != -1) ? "/proc" : "";
		if (procPath.empty())
			throw std::runtime_error("Proc filesystem not found or no permission to read from it!");

		passwd_path = (fs::is_regular_file(fs::path("/etc/passwd")) and access("/etc/passwd", R_OK) != -1) ? "/etc/passwd" : "";
		if (passwd_path.empty())
			qCWarning(vostopCollect) << "Could not read /etc/passwd, will show UID instead of username.";

		coreCount = sysconf(_SC_NPROCESSORS_ONLN);
		if (coreCount < 1) {
			coreCount = sysconf(_SC_NPROCESSORS_CONF);
			if (coreCount < 1) {
				coreCount = 1;
				qCWarning(vostopCollect) << "Could not determine number of cores, defaulting to 1.";
			}
		}

		pageSize = sysconf(_SC_PAGE_SIZE);
		if (pageSize <= 0) {
			pageSize = 4096;
			qCWarning(vostopCollect) << "Could not get system page size. Defaulting to 4096, processes memory usage might be incorrect.";
		}

		clkTck = sysconf(_SC_CLK_TCK);
		if (clkTck <= 0) {
			clkTck = 100;
			qCWarning(vostopCollect) << "Could not get system clock ticks per second. Defaulting to 100, processes cpu usage might be incorrect.";
		}

		//? Init for namespace Cpu (btop 365–390; get_sensors deferred to phase 5)
		Cpu::current_cpu.core_percent.insert(Cpu::current_cpu.core_percent.begin(), Shared::coreCount, {});
		Cpu::current_cpu.temp.insert(Cpu::current_cpu.temp.begin(), Shared::coreCount + 1, {});
		Cpu::core_old_totals.insert(Cpu::core_old_totals.begin(), Shared::coreCount, 0);
		Cpu::core_old_idles.insert(Cpu::core_old_idles.begin(), Shared::coreCount, 0);

		for (int i = 0; i < Shared::coreCount; ++i) {
			Cpu::core_freq.push_back("/sys/devices/system/cpu/cpufreq/policy" + to_string(i) + "/scaling_cur_freq");
			if (not fs::exists(Cpu::core_freq.back()) or access(Cpu::core_freq.back().c_str(), R_OK) == -1) {
				Cpu::core_freq.pop_back();
			}
		}

		Cpu::collect();
		for (auto& [field, vec] : Cpu::current_cpu.cpu_percent) {
			if (not vec.empty() and not v_contains(Cpu::available_fields, field)) Cpu::available_fields.push_back(field);
		}
		Cpu::cpuName = Cpu::get_cpuName();
		Cpu::got_sensors = false; //? phase 5: Cpu::get_sensors()
		Cpu::core_mapping = Cpu::get_core_mapping();

		Cpu::container_engine = detect_container();
	}
}

namespace Cpu {
	string cpuName;
	string cpuHz;
	bool got_sensors = false, cpu_temp_only = false, has_battery = true, supports_watts = false;
	cpu_info current_cpu;
	std::unordered_map<int, int> core_mapping;
	std::optional<std::string> container_engine;

	vector<string> core_freq;
	vector<string> available_fields;
	vector<long long> core_old_totals;
	vector<long long> core_old_idles;

	const std::array time_names {
		"user"s, "nice"s, "system"s, "idle"s, "iowait"s,
		"irq"s, "softirq"s, "steal"s, "guest"s, "guest_nice"s
	};

	std::unordered_map<string, long long> cpu_old = {
			{"totals", 0}, {"idles", 0}, {"user", 0}, {"nice", 0}, {"system", 0},
			{"idle", 0}, {"iowait", 0}, {"irq", 0}, {"softirq", 0}, {"steal", 0},
			{"guest", 0}, {"guest_nice", 0}
	};

	string get_cpuName() { //? btop 460–490
		string name;
		ifstream cpuinfo(Shared::procPath / "cpuinfo");
		if (cpuinfo.good()) {
			for (string instr; getline(cpuinfo, instr, ':') and not instr.starts_with("model name");)
				cpuinfo.ignore(SSmax, '\n');
			if (cpuinfo.bad()) return name;
			else if (not cpuinfo.eof()) {
				cpuinfo.ignore(1);
				getline(cpuinfo, name);
			}
			else if (fs::exists("/sys/devices")) {
				for (const auto& d : fs::directory_iterator("/sys/devices")) {
					if (string(d.path().filename()).starts_with("arm")) {
						name = d.path().filename();
						break;
					}
				}
				if (not name.empty()) {
					auto name_vec = ssplit(name, '_');
					if (name_vec.size() < 2) return capitalize(name);
					else return capitalize(name_vec.at(1)) + (name_vec.size() > 2 ? ' ' + capitalize(name_vec.at(2)) : "");
				}
			}

			name = trim_name(name);
		}

		return name;
	}

	auto get_core_mapping() -> std::unordered_map<int, int> { //? btop 760–826
		std::unordered_map<int, int> core_map;
		if (cpu_temp_only) return core_map;

		const int num_sensors = core_sensors.size();

		//? Try to get core mapping from /proc/cpuinfo
		ifstream cpuinfo(Shared::procPath / "cpuinfo");
		if (cpuinfo.good()) {
			int cpu{};
			int core{};
			int max_core{};
			std::unordered_map<int, int> cpu_to_core;
			for (string instr; cpuinfo >> instr;) {
				if (instr == "processor") {
					cpuinfo.ignore(SSmax, ':');
					cpuinfo >> cpu;
				}
				else if (instr.starts_with("core")) {
					cpuinfo.ignore(SSmax, ':');
					cpuinfo >> core;
					cpu_to_core[cpu] = core;
					max_core = std::max(max_core, core);
				}
				cpuinfo.ignore(SSmax, '\n');
			}
			if (not cpu_to_core.empty())
				//? Divide core ID space evenly across sensors (handles AMD multi-CCD, e.g. 5950x: IDs 0-7 -> Tccd1, 8-15 -> Tccd2)
				for (const auto& [cpu_id, core_id] : cpu_to_core) {
					core_map[cpu_id] = core_id * num_sensors / (max_core + 1);
				}
		}

		//? If core mapping from cpuinfo was incomplete try to guess remainder, if missing completely, map 0-0 1-1 2-2 etc.
		if (cmp_less(core_map.size(), Shared::coreCount)) {
			if (Shared::coreCount % 2 == 0 and (long)core_map.size() == Shared::coreCount / 2) {
				//? SMT siblings mirror their first half of cores.
				for (int i = 0; i < Shared::coreCount / 2; i++) {
					core_map[Shared::coreCount / 2 + i] = core_map.count(i) ? core_map.at(i) : (i % num_sensors);
				}
			}
			else {
				core_map.clear();
				for (int i = 0; i < Shared::coreCount; i++) {
					core_map[i] = (i * num_sensors) / Shared::coreCount;
				}
			}
		}

		//? Apply user set custom mapping if any
		const auto& custom_map = Settings::getS("cpu_core_map");
		if (not custom_map.isEmpty()) {
			try {
				for (const auto& split : ssplit(sstr(custom_map))) {
					const auto vals = ssplit(split, ':');
					if (vals.size() != 2) continue;
					int change_id = std::stoi(vals.at(0));
					int new_id = std::stoi(vals.at(1));
					if (not core_map.contains(change_id) or cmp_greater(new_id, num_sensors)) continue;
					core_map.at(change_id) = new_id;
				}
			}
			catch (...) {}
		}

		return core_map;
	}

	string get_cpuHz() { //? btop 669–758 (freq_mode key at 677)
		static int failed{};

		if (failed > 4)
			return ""s;

		string cpuhz;

		const auto& freq_mode = Settings::getS("freq_mode");

		//? fmt → QString (btop normalize_frequency 649–667)
		auto normalize_frequency = [](double hz) -> string {
			string str;
			if (hz > 999999) {
				str = sstr(QString::number(hz / 1'000'000, 'f', 1));
				str.resize(3);
				if (str.back() == '.') str.pop_back();
				str += " THz";
			}
			else if (hz > 999) {
				str = sstr(QString::number(hz / 1'000, 'f', 1));
				str.resize(3);
				if (str.back() == '.') str.pop_back();
				str += " GHz";
			}
			else {
				str = sstr(QString::number(hz, 'f', 0) + " MHz");
			}
			return str;
		};

		try {
			double hz = 0.0;
			// Read frequencies from all CPU cores
			vector<double> frequencies;
			for (auto it = Cpu::core_freq.begin(); it != Cpu::core_freq.end(); ) {
				if (it->empty()) {
					it = Cpu::core_freq.erase(it);
					continue;
				}

				double core_hz = stod(readfile(*it, "0.0")) / 1000;
				if (core_hz <= 0.0 and ++failed >= 2) {
					it = Cpu::core_freq.erase(it);
				} else {
					frequencies.push_back(core_hz);
					if (freq_mode == "first") break;
					++it;
				}
			}

			if (not frequencies.empty()) {
				if (freq_mode == "first") {
					hz = frequencies.front();
				}
				if (freq_mode == "average") {
					hz = std::accumulate(frequencies.begin(), frequencies.end(), 0.0) / static_cast<double>(frequencies.size());
				}
				else if (freq_mode == "highest") {
					hz = *std::max_element(frequencies.begin(), frequencies.end());
				}
				else if (freq_mode == "lowest") {
					hz = *std::min_element(frequencies.begin(), frequencies.end());
				}
				else if (freq_mode == "range") {
					auto [min_hz,max_hz] = std::minmax_element(frequencies.begin(), frequencies.end());

					// Format as range
					string min_str, max_str;
					min_str = normalize_frequency(*min_hz);
					max_str = normalize_frequency(*max_hz);

					return min_str + " - " + max_str;
				}
			}
			//? If freq from /sys failed or is missing try to use /proc/cpuinfo
			if (hz <= 0.0) {
				ifstream cpufreq(Shared::procPath / "cpuinfo");
				if (cpufreq.good()) {
					while (cpufreq.ignore(SSmax, '\n')) {
						// peek is caps sensitive so it was skipping 'CPU MHz'. This aims to fix it.
						if (cpufreq.peek() == 'c' || cpufreq.peek() == 'C') {
							cpufreq.ignore(SSmax, ' ');
							if (cpufreq.peek() == 'M') {
								cpufreq.ignore(SSmax, ':');
								cpufreq.ignore(1);
								cpufreq >> hz;
								break;
							}
						}
					}
				}
			}

			if (hz <= 1 or hz >= 999999999)
				throw std::runtime_error("Failed to read /sys/devices/system/cpu/cpufreq/policy and /proc/cpuinfo.");

			cpuhz = normalize_frequency(hz);

		}
		catch (const std::exception& e) {
			if (++failed < 5)
				return ""s;
			else {
				qCWarning(vostopCollect) << "get_cpuHz() :" << e.what();
				return ""s;
			}
		}

		return cpuhz;
	}

	long long get_cpuConsumptionUJoules() { //? btop 1032–1042
		long long consumption = -1;
		const auto rapl_power_usage_path = "/sys/class/powercap/intel-rapl:0/energy_uj";
		std::ifstream file(rapl_power_usage_path);
		if(file.good())
		{
			file >> consumption;
		}
		return consumption;
	}

	float get_cpuConsumptionWatts() { //? btop 1044–1071
		static long long previous_usage = 0;
		static long long previous_timestamp = 0;

		if (previous_usage == 0)
		{
			previous_usage = get_cpuConsumptionUJoules();
			previous_timestamp = get_monotonicTimeUSec();
			supports_watts = (previous_usage > 0);
			return 0;
		}

		if (!supports_watts)
		{
			return -1;
		}

		auto current_timestamp = get_monotonicTimeUSec();
		auto current_usage = get_cpuConsumptionUJoules();

		auto watts = (float)(current_usage - previous_usage) / (float)(current_timestamp - previous_timestamp);

		previous_timestamp = current_timestamp;
		previous_usage = current_usage;

		return watts;
	}

	static auto to_int(std::string_view view) { //? btop 1073–1077 (constexpr dropped: from_chars not constexpr on GCC 16)
		std::uint32_t value {};
		std::from_chars(view.data(), view.data() + view.size(), value);
		return value;
	}

	//? btop 1079–1102; C++23 ranges::to pipeline flattened to a loop (same element set)
	static auto detect_active_cpus() {
		auto stream = std::ifstream { "/sys/fs/cgroup/cpuset.cpus.effective" };
		auto buf = std::string { std::istreambuf_iterator<char> { stream }, {} };

		std::vector<std::int32_t> out;
		if (buf.empty()) {
			out.reserve(Shared::coreCount);
			for (int i = 0; i < Shared::coreCount; ++i) out.push_back(i);
			return out;
		}

		size_t pos = 0;
		while (pos <= buf.size()) {
			auto comma = buf.find(',', pos);
			if (comma == string::npos) comma = buf.size();
			std::string_view tok { buf.data() + pos, comma - pos };
			if (not tok.empty()) {
				auto dash = tok.find('-');
				if (dash == std::string_view::npos) {
					// Single CPU, single element (upstream: iota of one)
					out.push_back(static_cast<std::int32_t>(to_int(tok)));
				} else {
					auto start = to_int(tok.substr(0, dash));
					auto end = to_int(tok.substr(dash + 1));
					for (auto v = start; v <= end; ++v) out.push_back(static_cast<std::int32_t>(v));
				}
			}
			pos = comma + 1;
		}
		return out;
	}

	auto collect() -> cpu_info& { //? btop 1104–1247; no_update/Runner::stopping removed
		auto& cpu = current_cpu;

		if (Settings::getB("show_cpu_freq"))
			cpuHz = get_cpuHz();

		if (getloadavg(cpu.load_avg.data(), cpu.load_avg.size()) < 0) {
			qCWarning(vostopCollect) << "failed to get load averages";
		}

		ifstream cread;

		try {
			//? Get cpu total times for all cores from /proc/stat
			string cpu_name;
			cread.open(Shared::procPath / "stat");
			int i = 0;
			int target = Shared::coreCount;
			for (; i <= target or (cread.good() and cread.peek() == 'c'); i++) {
				//? Make sure to add zero value for missing core values if at end of file
				if ((not cread.good() or cread.peek() != 'c') and i <= target) {
					if (i == 0) throw std::runtime_error("Failed to parse /proc/stat");
					else {
						//? Fix container sizes if new cores are detected
						while (cmp_less(cpu.core_percent.size(), i)) {
							core_old_totals.push_back(0);
							core_old_idles.push_back(0);
							cpu.core_percent.emplace_back();
						}
						cpu.core_percent.at(i-1).push_back(0);
					}
				}
				else {
					if (i == 0) cread.ignore(SSmax, ' ');
					else {
						cread >> cpu_name;
						int cpuNum = std::stoi(cpu_name.substr(3));
						if (cpuNum >= target - 1) target = cpuNum + (cread.peek() == 'c' ? 2 : 1);

						//? Add zero value for core if core number is missing from /proc/stat
						while (i - 1 < cpuNum) {
							//? Fix container sizes if new cores are detected
							while (cmp_less(cpu.core_percent.size(), i)) {
								core_old_totals.push_back(0);
								core_old_idles.push_back(0);
								cpu.core_percent.emplace_back();
							}
							cpu.core_percent[i-1].push_back(0);
							if (cmp_greater(cpu.core_percent.at(i-1).size(), VostopHistoryDepth)) cpu.core_percent.at(i-1).pop_front();
							i++;
						}
					}

					//? Expected on kernel 2.6.3> : 0=user, 1=nice, 2=system, 3=idle, 4=iowait, 5=irq, 6=softirq, 7=steal, 8=guest, 9=guest_nice
					vector<long long> times;
					long long total_sum = 0;

					for (uint64_t val; cread >> val; total_sum += val) {
						times.push_back(val);
					}
					cread.clear();
					if (times.size() < 4) throw std::runtime_error("Malformed /proc/stat");

					//? Subtract fields 8-9 and any future unknown fields
					const long long totals = std::max(0ll, total_sum - (times.size() > 8 ? std::accumulate(times.begin() + 8, times.end(), 0ll) : 0));

					//? Add iowait field if present
					const long long idles = std::max(0ll, times.at(3) + (times.size() > 4 ? times.at(4) : 0));

					//? Calculate values for totals from first line of stat
					if (i == 0) {
						const long long calc_totals = std::max(1ll, totals - cpu_old.at("totals"));
						const long long calc_idles = std::max(0ll, idles - cpu_old.at("idles"));
						cpu_old.at("totals") = totals;
						cpu_old.at("idles") = idles;

						//? Total usage of cpu — stolen formula: round((Δtotal − Δidle) × 100 / Δtotal)
						cpu.cpu_percent.at("total").push_back(std::clamp((long long)round((double)(calc_totals - calc_idles) * 100 / calc_totals), 0ll, 100ll));

						//? width*2 trim → fixed ring (plan.md §9 Q3)
						while (cmp_greater(cpu.cpu_percent.at("total").size(), VostopHistoryDepth)) cpu.cpu_percent.at("total").pop_front();

						//? Populate cpu.cpu_percent with all fields from stat
						for (int ii = 0; const auto& val : times) {
							cpu.cpu_percent.at(time_names.at(ii)).push_back(std::clamp((long long)round((double)(val - cpu_old.at(time_names.at(ii))) * 100 / calc_totals), 0ll, 100ll));
							cpu_old.at(time_names.at(ii)) = val;

							while (cmp_greater(cpu.cpu_percent.at(time_names.at(ii)).size(), VostopHistoryDepth)) cpu.cpu_percent.at(time_names.at(ii)).pop_front();

							if (++ii == 10) break;
						}
						continue;
					}
					//? Calculate cpu total for each core
					else {
						//? Fix container sizes if new cores are detected
						while (cmp_less(cpu.core_percent.size(), i)) {
							core_old_totals.push_back(0);
							core_old_idles.push_back(0);
							cpu.core_percent.emplace_back();
						}
						const long long calc_totals = std::max(1ll, totals - core_old_totals.at(i-1));
						const long long calc_idles = std::max(0ll, idles - core_old_idles.at(i-1));
						core_old_totals.at(i-1) = totals;
						core_old_idles.at(i-1) = idles;

						cpu.core_percent.at(i-1).push_back(std::clamp((long long)round((double)(calc_totals - calc_idles) * 100 / calc_totals), 0ll, 100ll));
					}
				}

				//? Reduce size if there are more values than needed for graph
				if (cmp_greater(cpu.core_percent.at(i-1).size(), VostopHistoryDepth)) cpu.core_percent.at(i-1).pop_front();
			}

			//? New cores detected → widen Shared::coreCount (btop 1221–1226; Runner::coreNum_reset → no-op, vostop has no TUI boxes)
			if (cmp_greater(cpu.core_percent.size(), Shared::coreCount)) {
				qCDebug(vostopCollect) << "Changing CPU max corecount from" << Shared::coreCount << "to" << cpu.core_percent.size();
				Shared::coreCount = cpu.core_percent.size();
				while (cmp_less(current_cpu.temp.size(), cpu.core_percent.size() + 1)) current_cpu.temp.push_back({0});
			}

		}
		catch (const std::exception& e) {
			qCDebug(vostopCollect) << "Cpu::collect() :" << e.what();
			if (cread.bad()) throw std::runtime_error("Failed to read /proc/stat");
			else throw std::runtime_error(sstr(QString("Cpu::collect() : %1").arg(e.what())));
		}

		//? Sensor/battery/watts triggers stay flag-gated here (phase-2 decision; defaults OFF until phase 5)
		if (Settings::getB("check_temp") and got_sensors)
			update_sensors();

		if (Settings::getB("show_battery") and has_battery)
			current_bat = get_battery();

		if (Settings::getB("show_cpu_watts") and supports_watts)
			cpu.usage_watts = get_cpuConsumptionWatts();

		cpu.active_cpus = std::make_optional(detect_active_cpus());

		return cpu;
	}

	double system_uptime() { //? stolen Tools::system_uptime (btop 3629–3642)
		string upstr;
		ifstream pread(Shared::procPath / "uptime");
		if (pread.good()) {
			try {
				getline(pread, upstr, ' ');
				pread.close();
				return stod(upstr);
			}
			catch (const std::invalid_argument&) {}
			catch (const std::out_of_range&) {}
		}
		throw std::runtime_error(sstr(QString("Failed to get uptime from %1").arg(sstr(QString::fromStdString(Shared::procPath.string())))));
	}
}

namespace Mem {
	bool has_swap{};
	mem_info current_mem;

	uint64_t get_totalMem() { //? btop 2319–2331
		ifstream meminfo(Shared::procPath / "meminfo");
		int64_t totalMem = 0;
		if (meminfo.good()) {
			meminfo.ignore(SSmax, ':');
			meminfo >> totalMem;
			totalMem <<= 10;
		}
		if (not meminfo.good() or totalMem == 0)
			throw std::runtime_error("Could not get total memory size from /proc/meminfo");

		return totalMem;
	}

	auto collect() -> mem_info& { //? btop 2333–2423 (pure-memory section; show_swap/swap_disk/zfs_arc_cached → Settings)
		auto show_swap = Settings::getB("show_swap");
		auto swap_disk = Settings::getB("swap_disk");
		auto zfs_arc_cached = Settings::getB("zfs_arc_cached");
		auto totalMem = get_totalMem();
		auto& mem = current_mem;

		mem.stats.at("swap_total") = 0;

		//? Read ZFS ARC info from /proc/spl/kstat/zfs/arcstats (btop 2344–2360)
		uint64_t arc_size = 0, arc_min_size = 0;
		if (zfs_arc_cached) {
			ifstream arcstats(Shared::procPath / "spl/kstat/zfs/arcstats");
			if (arcstats.good()) {
				for (string label; arcstats >> label;) {
					if (label == "c_min") {
						arcstats >> arc_min_size >> arc_min_size; // double read skips type column
					}
					else if (label == "size") {
						arcstats >> arc_size >> arc_size;
						break;
					}
				}
			}
			arcstats.close();
		}

		//? Read memory info from /proc/meminfo (btop 2362–2404)
		ifstream meminfo(Shared::procPath / "meminfo");
		if (meminfo.good()) {
			bool got_avail = false;
			for (string label; meminfo.peek() != 'D' and meminfo >> label;) {
				if (label == "MemFree:") {
					meminfo >> mem.stats.at("free");
					mem.stats.at("free") <<= 10;
				}
				else if (label == "MemAvailable:") {
					meminfo >> mem.stats.at("available");
					mem.stats.at("available") <<= 10;
					got_avail = true;
				}
				else if (label == "Cached:") {
					meminfo >> mem.stats.at("cached");
					mem.stats.at("cached") <<= 10;
					if (not show_swap and not swap_disk) break;
				}
				else if (label == "SwapTotal:") {
					meminfo >> mem.stats.at("swap_total");
					mem.stats.at("swap_total") <<= 10;
				}
				else if (label == "SwapFree:") {
					meminfo >> mem.stats.at("swap_free");
					mem.stats.at("swap_free") <<= 10;
					break;
				}
				meminfo.ignore(SSmax, '\n');
			}
			if (not got_avail) mem.stats.at("available") = mem.stats.at("free") + mem.stats.at("cached");
			if (zfs_arc_cached) {
				mem.stats.at("cached") += arc_size;
				// The ARC will not shrink below arc_min_size, so that memory is not available
				if (arc_size > arc_min_size)
					mem.stats.at("available") += arc_size - arc_min_size;
			}
			mem.stats.at("used") = totalMem - (mem.stats.at("available") <= totalMem ? mem.stats.at("available") : mem.stats.at("free"));

			if (mem.stats.at("swap_total") > 0) mem.stats.at("swap_used") = mem.stats.at("swap_total") - mem.stats.at("swap_free");
		}
		else
			throw std::runtime_error("Failed to read /proc/meminfo");

		meminfo.close();

		//? Calculate percentages (btop 2408–2412; width*2 → VostopHistoryDepth)
		for (const auto& name : mem_names) {
			mem.percent.at(name).push_back(round((double)mem.stats.at(name) * 100 / totalMem));
			while (cmp_greater(mem.percent.at(name).size(), VostopHistoryDepth)) mem.percent.at(name).pop_front();
		}

		if (show_swap and mem.stats.at("swap_total") > 0) { //? btop 2414–2422
			for (const auto& name : swap_names) {
				mem.percent.at(name).push_back(round((double)mem.stats.at(name) * 100 / mem.stats.at("swap_total")));
				while (cmp_greater(mem.percent.at(name).size(), VostopHistoryDepth)) mem.percent.at(name).pop_front();
			}
			has_swap = true;
		}
		else
			has_swap = false;

		//? PHASE-4-SEAM: disk section (btop 2424–2741) attaches here in phase 4

		return mem;
	}
}

//? PHASE-5: real update_sensors()/get_battery() vendor here (btop 624–647, 835–1030).
//? Until then: flag-gated no-ops so the phase-2 trigger calls in Cpu::collect() compile.
namespace Cpu {
	void update_sensors() {}

	auto get_battery() -> std::tuple<int, float, long, std::string> {
		has_battery = false; //? no vendor path yet — treat as battery-less until phase 5
		return {0, 0, 0, ""};
	}
}

//? Vendored from src/btop_shared.cpp 38–80 (Cpu::trim_name) — helper state
namespace Cpu {
	vector<string> core_sensors; //? filled in phase 5; kept so get_core_mapping compiles against shared decl
	std::tuple<int, float, long, string> current_bat;

	string trim_name(string name) {
		auto name_vec = ssplit(name);

		if ((name.find("Xeon") != std::string::npos or v_contains(name_vec, "Duo"s)) and v_contains(name_vec, "CPU"s)) {
			auto cpu_pos = v_index(name_vec, "CPU"s);
			if (cpu_pos < name_vec.size() - 1 and not name_vec.at(cpu_pos + 1).ends_with(')'))
				name = name_vec.at(cpu_pos + 1);
			else
				name.clear();
		} else if (v_contains(name_vec, "Ryzen"s)) {
			auto ryz_pos = v_index(name_vec, "Ryzen"s);
			name = "Ryzen";
			int tokens = 0;
			for (auto i = ryz_pos + 1; i < name_vec.size() && tokens < 2; i++) {
				const std::string& p = name_vec.at(i);
				if (p != "AI" && p != "PRO" && p != "H" && p != "HX")
					tokens++;
				name += " " + p;
			}
		} else if (name.find("Intel") != std::string::npos and v_contains(name_vec, "CPU"s)) {
			auto cpu_pos = v_index(name_vec, "CPU"s);
			if (cpu_pos < name_vec.size() - 1 and not name_vec.at(cpu_pos + 1).ends_with(')') and name_vec.at(cpu_pos + 1) != "@")
				name = name_vec.at(cpu_pos + 1);
			else
				name.clear();
		} else
			name.clear();

		if (name.empty() and not name_vec.empty()) {
			for (const auto &n : name_vec) {
				if (n == "@") break;
				name += n + ' ';
			}
			name.pop_back();
			for (const auto& replace : {"Processor", "CPU", "(R)", "(TM)", "Intel", "AMD", "Apple", "Core"}) {
				name = s_replace(name, replace, "");
				name = s_replace(name, "  ", " ");
			}
			name = trim(name);
		}

		return name;
	}
}

//? Vendored from src/btop_shared.cpp 313–331
auto detect_container() -> std::optional<std::string> {
	std::error_code err;

	if (fs::exists(fs::path("/run/.containerenv"), err)) {
		return std::make_optional(std::string { "podman" });
	}
	if (fs::exists(fs::path("/.dockerenv"), err)) {
		return std::make_optional(std::string { "docker" });
	}
	auto systemd_container = fs::path("/run/systemd/container");
	if (fs::exists(systemd_container, err)) {
		auto stream = std::ifstream { systemd_container };
		auto buf = std::string {};
		stream >> buf;
		return std::make_optional(buf);
	}

	return std::nullopt;
}