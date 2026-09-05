/*
 * vostop — disk & network collectors, naturalized from btop.
 *
 * Portions Copyright 2021 Aristocratos (jakob@qvantnet.com) — vendored from
 * btop src/linux/btop_collect.cpp @ 956050716f290f529cd51d271b3d7e36eb1a22f0
 * (disk section of Mem::collect 2424–2741, mount filtering 2448–2463, fstab
 * 2465–2487, mount iteration 2489–2578, /sys/block stat resolution 2530–2551,
 * statvfs async pool 2580–2627, disks_order + swap pseudo-disk 2629–2650,
 * I/O parse 2703–2730, ZFS objset parse 2664–2702, get_zfs_stat_file 2746–2798,
 * zfs_collect_pool_total_stats 2800–2876, convert_ascii_escapes 2274–2302,
 * Net::collect 2891–3082) and src/btop_shared.hpp (disk_info 273–288,
 * net_stat 321–328, net_info 330–336, IfAddrsPtr 338–351). Apache-2.0; see
 * THIRD-PARTY-NOTICES.
 *
 * Naturalization: Config::getS/getB → Settings; Logger → QLoggingCategory;
 * width*2 trims → VostopHistoryDepth ring; fmt::format → QString; redraw flags
 * dropped (no TUI); Runner::stopping removed; std::async statvfs pool kept
 * (collection runs on the worker thread); selected_iface settable for the QML
 * iface picker.
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include <ifaddrs.h>

namespace Disk {

	struct disk_info {
		std::filesystem::path dev;
		std::string name;
		std::string fstype{};
		std::filesystem::path stat{};
		int64_t total{};
		int64_t used{};
		int64_t free{};
		int used_percent{};
		int free_percent{};

		std::array<int64_t, 3> old_io = {0, 0, 0};
		std::deque<long long> io_read = {};
		std::deque<long long> io_write = {};
		std::deque<long long> io_activity = {};
	};

	extern std::unordered_map<std::string, disk_info> disks;
	extern std::vector<std::string> disks_order;
	extern int disk_ios;

	//* Disk section of Mem::collect, attached at the phase-2 PHASE-4-SEAM.
	//* Needs the mem side's swap numbers for the swap pseudo-disk.
	void collect(uint64_t swap_total, uint64_t swap_used, uint64_t swap_free,
				 double swap_used_pct, double swap_free_pct, bool has_swap);

	//* Old-uptime anchor for io_activity % (btop Mem::old_uptime)
	extern double old_uptime;
}

namespace Net {

	struct net_stat {
		uint64_t speed{};
		uint64_t top{};
		uint64_t total{};
		uint64_t last{};
		uint64_t offset{};
		uint64_t rollover{};
	};

	struct net_info {
		std::unordered_map<std::string, std::deque<long long>> bandwidth = { {"download", {}}, {"upload", {}} };
		std::unordered_map<std::string, net_stat> stat = { {"download", {}}, {"upload", {}} };
		std::string ipv4{};
		std::string ipv6{};
		bool connected{};
	};

	class IfAddrsPtr {
		struct ifaddrs* ifaddr;
		int status;
	public:
		IfAddrsPtr() { status = getifaddrs(&ifaddr); }
		~IfAddrsPtr() noexcept { freeifaddrs(ifaddr); }
		IfAddrsPtr(const IfAddrsPtr&) = delete;
		IfAddrsPtr& operator=(IfAddrsPtr& other) = delete;
		IfAddrsPtr(IfAddrsPtr&&) = delete;
		IfAddrsPtr& operator=(IfAddrsPtr&& other) = delete;
		[[nodiscard]] auto operator()() -> struct ifaddrs* { return ifaddr; }
		[[nodiscard]] auto get() -> struct ifaddrs* { return ifaddr; }
		[[nodiscard]] auto get_status() const noexcept -> int { return status; }
	};

	extern std::unordered_map<std::string, net_info> current_net;
	extern std::vector<std::string> interfaces;
	extern std::string selected_iface;

	auto collect() -> net_info&;

	//* QML iface picker: select an interface (empty = auto)
	void set_selected_iface(const std::string& iface);
}