/*
 * vostop — process collector (implementation), naturalized from btop.
 * See proc.h for the vendoring note and line references.
 */
#include "proc.h"

#include "cpu_mem.h"
#include "tools_qt.h"
#include "../backend/Settings.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <system_error>
#include <unistd.h>

#include <pwd.h>
#include <sys/resource.h>

namespace fs = std::filesystem;
namespace rng = std::ranges;
using namespace std;
using namespace std::string_literals;
using namespace tools;
using std::string, std::vector;
using Cpu::system_uptime;

namespace Proc {

	vector<proc_info> current_procs;
	std::unordered_map<string, string> uid_user;
	string current_sort;
	string current_filter;
	bool current_rev{};
	bool is_tree_mode;

	fs::file_time_type passwd_time;

	uint64_t cputimes;
	uint64_t old_cputimes{};
	std::atomic<int> numpids{0};
	int filter_found{};

	std::atomic<size_t> detailed_pid{0};

	detail_container detailed;
	constexpr size_t KTHREADD = 2;
	static std::unordered_set<size_t> kernels_procs = {KTHREADD};
	static std::unordered_set<size_t> dead_procs;

	const std::unordered_map<char, string> proc_states = {
		{'R', "Running"},
		{'S', "Sleeping"},
		{'D', "Waiting"},
		{'Z', "Zombie"},
		{'T', "Stopped"},
		{'t', "Tracing"},
		{'X', "Dead"},
		{'x', "Dead"},
		{'K', "Wakekill"},
		{'W', "Unknown"},
		{'P', "Parked"}
	};

	//* Get detailed info for selected process (btop _collect_details 3108–3203;
	//* show_detailed == detailed_pid != 0; redraw flags dropped — no TUI)
	static void _collect_details(const size_t pid, const uint64_t uptime, vector<proc_info>& procs) {
		fs::path pid_path = Shared::procPath / std::to_string(pid);

		if (pid != detailed.last_pid) {
			detailed = {};
			detailed.last_pid = pid;
			detailed.skip_smaps = not Settings::getB("proc_info_smaps");
		}

		//? Copy proc_info for process from proc vector
		auto p_info = rng::find(procs, pid, &proc_info::pid);
		detailed.entry = *p_info;

		//? Update cpu percent deque for process cpu graph
		if (not Settings::getB("proc_per_core")) detailed.entry.cpu_p *= Shared::coreCount;
		detailed.cpu_percent.push_back(clamp((long long)round(detailed.entry.cpu_p), 0ll, 100ll));
		while (cmp_greater(detailed.cpu_percent.size(), VostopHistoryDepth)) detailed.cpu_percent.pop_front();

		//? Process runtime
		if (detailed.entry.state != 'X') detailed.elapsed = sec_to_dhms(uptime - (detailed.entry.cpu_s / Shared::clkTck));
		else detailed.elapsed = sec_to_dhms(detailed.entry.death_time);
		if (detailed.elapsed.size() > 8) detailed.elapsed.resize(detailed.elapsed.size() - 3);

		//? Get parent process name
		if (detailed.parent.empty()) {
			auto p_entry = rng::find(procs, detailed.entry.ppid, &proc_info::pid);
			if (p_entry != procs.end()) detailed.parent = p_entry->name;
		}

		//? Expand process status from single char to explanative string
		detailed.status = (proc_states.contains(detailed.entry.state)) ? proc_states.at(detailed.entry.state) : "Unknown";

		ifstream d_read;
		string short_str;

		//? Try to get RSS mem from proc/[pid]/smaps
		detailed.memory.clear();
		if (not detailed.skip_smaps and fs::exists(pid_path / "smaps")) {
			d_read.open(pid_path / "smaps");
			uint64_t rss = 0;
			try {
				while (d_read.good()) {
					d_read.ignore(SSmax, 'R');
					if (d_read.peek() == 's') {
						d_read.ignore(SSmax, ':');
						getline(d_read, short_str, 'k');
						rss += stoull(short_str);
					}
				}
				if (rss == detailed.entry.mem >> 10)
					detailed.skip_smaps = true;
				else {
					detailed.mem_bytes.push_back(rss << 10);
					detailed.memory = floating_humanizer(rss << 10);
				}
			}
			catch (const std::invalid_argument&) {}
			catch (const std::out_of_range&) {}
			d_read.close();
		}
		if (detailed.memory.empty()) {
			detailed.mem_bytes.push_back(detailed.entry.mem);
			detailed.memory = floating_humanizer(detailed.entry.mem);
		}
		if (detailed.first_mem == -1 or detailed.first_mem < (long long)(detailed.mem_bytes.back() / 2) or detailed.first_mem > (long long)(detailed.mem_bytes.back() * 4)) {
			detailed.first_mem = min((uint64_t)detailed.mem_bytes.back() * 2, Mem::get_totalMem());
		}

		while (cmp_greater(detailed.mem_bytes.size(), VostopHistoryDepth)) detailed.mem_bytes.pop_front();

		//? Get bytes read and written from proc/[pid]/io
		if (fs::exists(pid_path / "io")) {
			d_read.open(pid_path / "io");
			try {
				string name;
				while (d_read.good()) {
					getline(d_read, name, ':');
					if (name.ends_with("read_bytes")) {
						getline(d_read, short_str);
						detailed.io_read = floating_humanizer(stoull(short_str));
					}
					else if (name.ends_with("write_bytes")) {
						getline(d_read, short_str);
						detailed.io_write = floating_humanizer(stoull(short_str));
						break;
					}
					else
						d_read.ignore(SSmax, '\n');
				}
			}
			catch (const std::invalid_argument&) {}
			catch (const std::out_of_range&) {}
			d_read.close();
		}
	}

	bool set_priority(pid_t pid, int priority) { //? btop_shared.cpp 97–102
		if (setpriority(PRIO_PROCESS, pid, priority) == 0) {
			return true;
		}
		return false;
	}

	auto matches_filter_row(size_t pid, const string& name, const string& cmd,
							const string& user, const string& filter) -> bool {
		if (filter.starts_with("!")) {
			if (filter.size() == 1) {
				return true;
			}

			// An incomplete regex throws, see issue https://github.com/aristocratos/btop/issues/1133
			try {
				std::regex regex { filter.substr(1), std::regex::extended };
				return std::regex_search(std::to_string(pid), regex) || std::regex_search(name, regex) ||
							 std::regex_match(cmd, regex) || std::regex_search(user, regex);
			} catch (std::regex_error& /* unused */) {
				return false;
			}
		}

		return std::to_string(pid).find(filter) != std::string::npos || s_contains_ic(name, filter) ||
					 s_contains_ic(cmd, filter) || s_contains_ic(user, filter);
	}

	auto matches_filter(const proc_info& proc, const string& filter) -> bool { //? btop_shared.cpp 176–194
		return matches_filter_row(proc.pid, proc.name, proc.cmd, proc.user, filter);
	}

	//* Collects and sorts process information from /proc (btop 3206–3625; tree block
	//* 3537–3620 NOT stolen; Runner::stopping removed; Config → Settings)
	auto collect(bool no_update) -> vector<proc_info>& {
		const auto sorting = sstr(Settings::getS("proc_sorting"));
		auto reverse = Settings::getB("proc_reversed");
		const auto& filter = sstr(Settings::getS("proc_filter"));
		auto per_core = Settings::getB("proc_per_core");
		auto should_filter_kernel = Settings::getB("proc_filter_kernel");
		auto show_detailed = detailed_pid.load() != 0;
		const size_t v_detailed_pid = detailed_pid.load();
		const auto pause_proc_list = Settings::getB("pause_proc_list");
		bool should_filter = current_filter != filter;
		if (should_filter) current_filter = filter;
		bool sorted_change = (sorting != current_sort or reverse != current_rev or should_filter);
		if (sorted_change) {
			current_sort = sorting;
			current_rev = reverse;
		}
		ifstream pread;
		string long_string;
		string short_str;

		static vector<size_t> found;

		const double uptime = system_uptime();
		begin_tick(uptime);

		const int cmult = (per_core) ? Shared::coreCount : 1;
		bool got_detailed = false;

		static size_t proc_clear_count{};

		//* Use pids from last update if only changing filter, sorting or tree options
		if (no_update and not current_procs.empty()) {
			if (show_detailed and v_detailed_pid != detailed.last_pid) _collect_details(v_detailed_pid, round(uptime), current_procs);
		}
		//* ---------------------------------------------Collection start----------------------------------------------
		else {
			should_filter = true;
			found.clear();

			//? First make sure kernel proc cache is cleared.
			if (should_filter_kernel and ++proc_clear_count >= 256) {
				//? Clearing the cache is used in the event of a pid wrap around.
				//? In that event processes that acquire old kernel pids would also be filtered out so we need to manually clean the cache every now and then.
				kernels_procs.clear();
				kernels_procs.emplace(KTHREADD);
				proc_clear_count = 0;
			}

			auto totalMem = Mem::get_totalMem();
			int totalMem_len = to_string(totalMem >> 10).size();

			//? Update uid_user map if /etc/passwd changed since last run
			if (not Shared::passwd_path.empty() and fs::last_write_time(Shared::passwd_path) != passwd_time) {
				string r_uid, r_user;
				passwd_time = fs::last_write_time(Shared::passwd_path);
				uid_user.clear();
				pread.open(Shared::passwd_path);
				if (pread.good()) {
					while (pread.good()) {
						getline(pread, r_user, ':');
						pread.ignore(SSmax, ':');
						getline(pread, r_uid, ':');
						if (uid_user.contains(r_uid)) break;
						uid_user[r_uid] = r_user;
						pread.ignore(SSmax, '\n');
					}
				}
				else {
					Shared::passwd_path.clear();
				}
				pread.close();
			}

			//? Get cpu total times from /proc/stat up to the guest field
			cputimes = 0;
			pread.open(Shared::procPath / "stat");
			if (pread.good()) {
				pread.ignore(SSmax, ' ');
				int i = 0;
				for (uint64_t times; i < 8 and pread >> times; cputimes += times, i++);
			}
			else throw std::runtime_error("Failure to read /proc/stat");
			pread.close();

			//? Iterate over all pids in /proc
			for (const auto& d: fs::directory_iterator(Shared::procPath)) {
				if (pread.is_open()) pread.close();

				const string pid_str = d.path().filename();
				if (not isdigit(pid_str[0])) continue;

				const size_t pid = stoul(pid_str);

				if (should_filter_kernel and kernels_procs.contains(pid)) {
					continue;
				}

				found.push_back(pid);

				//? Check if pid already exists in current_procs
				auto find_old = rng::find(current_procs, pid, &proc_info::pid);
				bool no_cache{};
				//? Only add new processes if not paused
				if (find_old == current_procs.end()) {
					if (not pause_proc_list) {
						current_procs.push_back({pid});
						find_old = current_procs.end() - 1;
						no_cache = true;
					}
					else continue;
				}
				else if (dead_procs.contains(pid)) continue;

				auto& new_proc = *find_old;

				//? Get program name, command and username
				if (no_cache) {
					pread.open(d.path() / "comm");
					if (not pread.good()) continue;
					getline(pread, new_proc.name);
					pread.close();
					//? Check for whitespace characters in name and set offset to get correct fields from stat file
					new_proc.name_offset = rng::count(new_proc.name, ' ');

					pread.open(d.path() / "cmdline");
					if (not pread.good()) continue;
					long_string.clear();
					while(getline(pread, long_string, '\0')) {
						new_proc.cmd += long_string + ' ';
						if (new_proc.cmd.size() > 1000) {
							new_proc.cmd.resize(1000);
							break;
						}
					}
					pread.close();
					if (not new_proc.cmd.empty()) new_proc.cmd.pop_back();

					pread.open(d.path() / "status");
					if (not pread.good()) continue;
					string uid;
					string line;
					while (pread.good()) {
						getline(pread, line, ':');
						if (line == "Uid") {
							pread.ignore();
							getline(pread, uid, '\t');
							break;
						} else {
							pread.ignore(SSmax, '\n');
						}
					}
					pread.close();
					if (uid_user.contains(uid)) {
						new_proc.user = uid_user.at(uid);
					}
					else {
						try {
							struct passwd* udet;
							udet = getpwuid(stoi(uid));
							if (udet != nullptr and udet->pw_name != nullptr) {
								new_proc.user = string(udet->pw_name);
							}
							else {
								new_proc.user = uid;
							}
						}
						catch (...) { new_proc.user = uid; }
					}
				}

				//? Parse /proc/[pid]/stat
				pread.open(d.path() / "stat");
				if (not pread.good()) continue;

				const auto& offset = new_proc.name_offset;
				short_str.clear();
				int x = 0, next_x = 3;

				uint64_t cpu_t = 0;
				try {
					for (;;) {
						while (pread.good() and ++x < next_x + offset) pread.ignore(SSmax, ' ');
						if (not pread.good()) break;
						else getline(pread, short_str, ' ');

						switch (x-offset) {
							case 3: //? Process state
								new_proc.state = short_str.at(0);
								if (new_proc.ppid != 0) next_x = 14;
								continue;
							case 4: //? Parent pid
								new_proc.ppid = stoull(short_str);
								next_x = 14;
								continue;
							case 14: //? Process utime
								cpu_t = stoull(short_str);
								continue;
							case 15: //? Process stime
								cpu_t += stoull(short_str);
								next_x = 19;
								continue;
							case 19: //? Nice value
								new_proc.p_nice = stoll(short_str);
								continue;
							case 20: //? Number of threads
								new_proc.threads = stoull(short_str);
								if (new_proc.cpu_s == 0) {
									next_x = 22;
									new_proc.cpu_t = cpu_t;
								}
								else
									next_x = 24;
								continue;
							case 22: //? Get cpu seconds if missing
								new_proc.cpu_s = stoull(short_str);
								next_x = 24;
								continue;
							case 24: //? RSS memory (can be inaccurate, but parsing smaps increases total cpu usage by ~20x)
								if (cmp_greater(short_str.size(), totalMem_len))
									new_proc.mem = totalMem;
								else
									new_proc.mem = stoull(short_str) * Shared::pageSize;
						}
						break;
					}

				}
				catch (const std::invalid_argument&) { continue; }
				catch (const std::out_of_range&) { continue; }

				pread.close();

				if (should_filter_kernel and new_proc.ppid == KTHREADD) {
					kernels_procs.emplace(new_proc.pid);
					found.pop_back();
				}

				if (x-offset < 24) continue;

				//? Get RSS memory from /proc/[pid]/statm if value from /proc/[pid]/stat looks wrong
				if (new_proc.mem >= totalMem) {
					pread.open(d.path() / "statm");
					if (not pread.good()) continue;
					pread.ignore(SSmax, ' ');
					pread >> new_proc.mem;
					new_proc.mem *= Shared::pageSize;
					pread.close();
				}

				//? Process cpu usage since last update
				new_proc.cpu_p = clamp(round(cmult * 1000 * (cpu_t - new_proc.cpu_t) / max((uint64_t)1, cputimes - old_cputimes)) / 10.0, 0.0, 100.0 * Shared::coreCount);

				//? Process cumulative cpu usage since process start
				new_proc.cpu_c = (double)cpu_t / max(1.0, (uptime * Shared::clkTck) - new_proc.cpu_s);

				//? Update cached value with latest cpu times
				new_proc.cpu_t = cpu_t;

				if (show_detailed and not got_detailed and new_proc.pid == v_detailed_pid) {
					got_detailed = true;
				}
			}

			//? Clear dead processes from current_procs and remove kernel processes if enabled and not paused
			if (not pause_proc_list) {
				auto eraser = rng::remove_if(current_procs, [&](const auto& element){ return not v_contains(found, element.pid); });
				current_procs.erase(eraser.begin(), eraser.end());
				if (!dead_procs.empty()) dead_procs.clear();
			}
			//? Set correct state of dead processes if paused
			else {
				const bool keep_dead_proc_usage = Settings::getB("keep_dead_proc_usage");
				for (auto& r : current_procs) {
					if (rng::find(found, r.pid) == found.end()) {
						if (r.state != 'X') r.death_time = round(uptime) - (r.cpu_s / Shared::clkTck);
						r.state = 'X';
						dead_procs.emplace(r.pid);
						//? Reset cpu usage for dead processes if paused and option is set
						if (!keep_dead_proc_usage) {
							r.cpu_p = 0.0;
							r.mem = 0;
						}
					}
				}
			}

			prune_pids(found);

			//? Update the details info box for process if active
			if (show_detailed and got_detailed) {
				_collect_details(v_detailed_pid, round(uptime), current_procs);
			}

			old_cputimes = cputimes;
		}
		//* ---------------------------------------------Collection done-----------------------------------------------

		//* Match filter if defined
		if (should_filter) {
			filter_found = 0;
			for (auto& p : current_procs) {
				if (not filter.empty()) {
					if (!matches_filter(p, filter)) {
						p.filtered = true;
						filter_found++;
					} else {
						p.filtered = false;
					}
				} else {
					p.filtered = false;
				}
			}
		}

		//* Sort processes — Qt-side sort via ProcessModel/QSortFilterProxyModel owns the
		//* vostop UX; the stolen vector keeps scan order (tree mode cut, ppid is a column).

		numpids = (int)current_procs.size() - filter_found;

		return current_procs;
	}

	//? ---------------- Parity additions (original vostop code, GPLv3-or-later) ----------------

	namespace {

		struct IoCache {
			uint64_t read = 0, write = 0;   //? last absolute byte counters
			double uptime = 0.0;            //? tick timestamp
			bool valid = false;
		};
		std::unordered_map<size_t, IoCache> io_cache;

		struct CgroupCache {
			uint64_t starttime = 0;         //? pid-reuse detector
			Category category = Category::Background;
			bool valid = false;
		};
		std::unordered_map<size_t, CgroupCache> cgroup_cache;

		double tick_uptime = 0.0;
	}

	void begin_tick(double uptime) {
		tick_uptime = uptime;
	}

	Category classify_category(size_t pid, uint64_t ppid, uint64_t starttime) {
		auto cache = cgroup_cache.find(pid);
		if (cache != cgroup_cache.end() and cache->second.valid and cache->second.starttime == starttime)
			return cache->second.category;

		Category cat = Category::Background;
		//? Kernel threads: parent is kthreadd (2); pid 1 is systemd/init
		if (pid == 1 or (ppid == 2 and pid != 1))
			cat = Category::System;
		else {
			std::ifstream cg("/proc/" + std::to_string(pid) + "/cgroup");
			if (cg.good()) {
				//? Machines can emit v1 lines (e.g. "1:net_cls:/") before the v2
				//? unified line — scan for "0::<path>" instead of trusting line 1.
				string line, path;
				while (getline(cg, line)) {
					if (line.starts_with("0::")) {
						path = line.substr(3);
						break;
					}
				}
				if (path.find("app.slice") != string::npos)
					cat = Category::Apps;       //? app.slice/app-*.scope → Apps
				else if (path.find("system.slice") != string::npos)
					cat = Category::System;
				else if (path.find("user.slice") != string::npos)
					cat = Category::Background;
				else
					cat = Category::Background; //? unknown/unclassified → user domain
			}
		}

		CgroupCache entry;
		entry.starttime = starttime;
		entry.category = cat;
		entry.valid = true;
		cgroup_cache[pid] = entry;
		return cat;
	}

	bool io_rates(size_t pid, double& readRate, double& writeRate) {
		readRate = writeRate = 0.0;
		std::ifstream io("/proc/" + std::to_string(pid) + "/io");
		if (not io.good())
			return false; //? EACCES or gone → "—"

		uint64_t read_bytes = 0, write_bytes = 0;
		try {
			string name, value;
			while (io.good()) {
				getline(io, name, ':');
				if (name.ends_with("read_bytes")) {
					getline(io, value);
					read_bytes = stoull(value);
				}
				else if (name.ends_with("write_bytes")) {
					getline(io, value);
					write_bytes = stoull(value);
					break;
				}
				else
					io.ignore(SSmax, '\n');
			}
		}
		catch (const std::invalid_argument&) { return false; }
		catch (const std::out_of_range&) { return false; }

		auto cache = io_cache.find(pid);
		if (cache == io_cache.end() or not cache->second.valid) {
			io_cache[pid] = {read_bytes, write_bytes, tick_uptime, true};
			return false; //? first observation — no rate yet
		}

		const double dt = tick_uptime - cache->second.uptime;
		if (dt <= 0.0)
			return false;
		readRate = static_cast<double>(read_bytes - cache->second.read) / dt;
		writeRate = static_cast<double>(write_bytes - cache->second.write) / dt;
		//? Rollover/underflow guard (counters never decrease; clamp negatives)
		if (readRate < 0.0) readRate = 0.0;
		if (writeRate < 0.0) writeRate = 0.0;
		cache->second.read = read_bytes;
		cache->second.write = write_bytes;
		cache->second.uptime = tick_uptime;
		return true;
	}

	void prune_pids(const vector<size_t>& found) {
		//? Drop parity caches for dead pids
		for (auto it = io_cache.begin(); it != io_cache.end();) {
			if (std::find(found.begin(), found.end(), it->first) == found.end())
				it = io_cache.erase(it);
			else
				++it;
		}
		for (auto it = cgroup_cache.begin(); it != cgroup_cache.end();) {
			if (std::find(found.begin(), found.end(), it->first) == found.end())
				it = cgroup_cache.erase(it);
			else
				++it;
		}
	}

	vector<string> open_files(size_t pid, size_t cap) {
		vector<string> out;
		const fs::path fd_dir = Shared::procPath / std::to_string(pid) / "fd";
		std::error_code ec;
		if (not fs::is_directory(fd_dir, ec))
			return out;
		for (const auto& entry : fs::directory_iterator(fd_dir, ec)) {
			if (out.size() >= cap) break;
			char buf[4096];
			const ssize_t len = readlink(entry.path().c_str(), buf, sizeof(buf) - 1);
			if (len > 0) {
				buf[len] = '\0';
				out.emplace_back(buf);
			}
		}
		return out;
	}

} // namespace Proc