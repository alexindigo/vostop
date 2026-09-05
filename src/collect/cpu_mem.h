/*
 * vostop — CPU & memory collectors, naturalized from btop.
 *
 * Portions Copyright 2021 Aristocratos (jakob@qvantnet.com) — vendored from
 * btop src/linux/btop_collect.cpp / src/btop_shared.hpp @ 956050716f290f529cd51d271b3d7e36eb1a22f0,
 * licensed under the Apache License, Version 2.0; /proc & /sys collection math
 * kept verbatim, TUI couplings replaced with Qt equivalents. Original
 * copyright headers preserved; see THIRD-PARTY-NOTICES.
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <QList>

//? Fixed history depth for all collectors (plan.md §9 Q3: fixed 120-sample ring, no setting)
inline constexpr int VostopHistoryDepth = 120;

namespace Shared {
	extern std::filesystem::path procPath;
	extern std::filesystem::path passwd_path;
	extern long pageSize, clkTck, coreCount;

	//* Naturalized init: coreCount/pageSize/clkTck/procPath, cpufreq paths,
	//* Cpu::collect() warm-up, cpuName, core_mapping, container engine.
	//* (btop Shared::init trimmed: GPU init block and Mem::collect() pre-call removed)
	void init();
}

namespace Cpu {
	struct cpu_info {
		std::unordered_map<std::string, std::deque<long long>> cpu_percent = {
			{"total", {}}, {"user", {}}, {"nice", {}}, {"system", {}}, {"idle", {}},
			{"iowait", {}}, {"irq", {}}, {"softirq", {}}, {"steal", {}}, {"guest", {}}, {"guest_nice", {}}
		};
		std::vector<std::deque<long long>> core_percent;
		std::vector<std::deque<long long>> temp;
		long long temp_max = 0;
		std::array<double, 3> load_avg = {};
		float usage_watts = 0;
		std::optional<std::vector<std::int32_t>> active_cpus;
	};

	extern std::string cpuName;
	extern std::string cpuHz;
	extern bool got_sensors, cpu_temp_only, has_battery, supports_watts;
	extern cpu_info current_cpu;
	extern std::unordered_map<int, int> core_mapping;
	extern std::optional<std::string> container_engine;

	extern std::vector<std::string> core_freq;
	extern std::vector<std::string> available_fields;
	extern std::vector<long long> core_old_totals;
	extern std::vector<long long> core_old_idles;
	extern std::vector<std::string> core_sensors; //? filled by phase-5 get_sensors
	extern std::tuple<int, float, long, std::string> current_bat;

	auto collect() -> cpu_info&;
	auto get_core_mapping() -> std::unordered_map<int, int>;
	std::string get_cpuName();
	std::string get_cpuHz();
	std::string trim_name(std::string name);
	void update_sensors();   //? phase-5 vendor; flag-gated no-op stub until then
	auto get_battery() -> std::tuple<int, float, long, std::string>; //? phase-5 vendor
	double system_uptime(); //? stolen Tools::system_uptime
	long long get_cpuConsumptionUJoules(); //? RAPL probe (btop 1032–1042)
	float get_cpuConsumptionWatts();       //? RAPL watts (btop 1044–1071)
}

auto detect_container() -> std::optional<std::string>; //? stolen btop_shared.cpp 313–331

namespace Mem {
	const std::array<std::string, 4> mem_names { "used", "available", "cached", "free" };
	const std::array<std::string, 2> swap_names { "swap_used", "swap_free" };
	extern bool has_swap;

	struct mem_info {
		std::unordered_map<std::string, uint64_t> stats = {
			{"used", 0}, {"available", 0}, {"cached", 0}, {"free", 0},
			{"swap_total", 0}, {"swap_used", 0}, {"swap_free", 0}};
		std::unordered_map<std::string, std::deque<long long>> percent = {
			{"used", {}}, {"available", {}}, {"cached", {}}, {"free", {}},
			{"swap_total", {}}, {"swap_used", {}}, {"swap_free", {}}};
	};

	extern mem_info current_mem;

	uint64_t get_totalMem();
	auto collect() -> mem_info&; //? pure-memory section only; disk section attaches at the PHASE-4-SEAM in phase 4
}