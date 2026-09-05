/*
 * vostop — process collector, naturalized from btop.
 *
 * Portions Copyright 2021 Aristocratos (jakob@qvantnet.com) — vendored from
 * btop src/linux/btop_collect.cpp @ 956050716f290f529cd51d271b3d7e36eb1a22f0
 * (Proc state block 3087–3105, _collect_details 3108–3203, Proc::collect
 * 3206–3625 minus the tree block 3537–3620) and src/btop_shared.{hpp,cpp}
 * (proc_info 382–404, detail_container 407–415, set_priority 97–102,
 * matches_filter 176–194). Apache-2.0; see THIRD-PARTY-NOTICES.
 *
 * Naturalization: Config::getS/getB/getI → Settings::getS/getB; Runner::stopping
 * removed; tree block/`tree_sort`/`_tree_gen`/`_collect_prefixes`/
 * `toggle_tree_collapse`/`_auto_collapse_oversized` NOT stolen (tree cut from
 * MVP, ppid kept as a plain column); show_detailed/detailed_pid →
 * Proc::detailed_pid driven by the UI selection. Sort/filter live in
 * ProcessModel/QSortFilterProxyModel per the plan's steal mapping; the
 * "cpu lazy" special order is expressed as plain cpu-descending there.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/types.h>

namespace Proc {
	extern std::atomic<int> numpids;
	extern int filter_found;
	extern std::atomic<size_t> detailed_pid; //? 0 = no detail target

	//* Translation from process state char to explanative string (btop proc_states)
	extern const std::unordered_map<char, std::string> proc_states;

	//* Container for process information (btop proc_info verbatim; tree-only
	//* fields prefix/depth/tree_index/collapsed retained but unused — tree cut)
	struct proc_info {
		size_t pid{};
		std::string name{};
		std::string cmd{};
		std::string short_cmd{};
		size_t threads{};
		int name_offset{};
		std::string user{};
		uint64_t mem{};
		double cpu_p{};
		double cpu_c{};
		char state = '0';
		int64_t p_nice{};
		uint64_t ppid{};
		uint64_t cpu_s{};
		uint64_t cpu_t{};
		uint64_t death_time{};
		std::string prefix{};
		size_t depth{};
		size_t tree_index{};
		bool collapsed{};
		bool filtered{};
	};

	//* Container for process info box (btop detail_container verbatim)
	struct detail_container {
		size_t last_pid{};
		bool skip_smaps{};
		proc_info entry;
		std::string elapsed, parent, status, io_read, io_write, memory;
		long long first_mem = -1;
		std::deque<long long> cpu_percent;
		std::deque<long long> mem_bytes;
	};

	//* All info for the detail readout
	extern detail_container detailed;

	//* Collect and sort process information from /proc
	auto collect(bool no_update = false) -> std::vector<proc_info>&;

	//* Change priority (nice) of pid, returns true on success otherwise false
	bool set_priority(pid_t pid, int priority);

	//* btop matches_filter (verbatim; substring on pid/name/cmd/user or !extended-regex)
	auto matches_filter(const proc_info& proc, const std::string& filter) -> bool;
	//* Same core for row-shaped data (ProcessModel proxy)
	auto matches_filter_row(size_t pid, const std::string& name, const std::string& cmd,
							const std::string& user, const std::string& filter) -> bool;

	//? ---- Parity additions (original vostop code, GPLv3-or-later) ----

	//* Process category from /proc/<pid>/cgroup (cgroup-v2) + kernel-thread heuristics:
	//* 0 = Apps (app.slice/app-*.scope), 1 = Background (other user.slice),
	//* 2 = System (system.slice, pid 1, kernel threads). Cached per pid; re-read on pid reuse.
	enum class Category : int { Apps = 0, Background = 1, System = 2 };
	Category classify_category(size_t pid, uint64_t ppid, uint64_t starttime);

	//* Per-tick disk-I/O rates from /proc/<pid>/io (read_bytes/write_bytes, Δ/Δt).
	//* Returns false when unreadable (EACCES — other users' processes → "—").
	bool io_rates(size_t pid, double& readRate, double& writeRate);

	//* Open files of pid via readlink of /proc/<pid>/fd/* (own-user only; capped list)
	std::vector<std::string> open_files(size_t pid, size_t cap = 100);

	//* Call at the start of each collect tick to reset rate caches' Δt window
	void begin_tick(double uptime);

	//* Drop cached state for pids that disappeared (call after the scan)
	void prune_pids(const std::vector<size_t>& found);
}