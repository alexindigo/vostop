/*
 * vostop — disk & network collectors (implementation), naturalized from btop.
 * See disk_net.h for vendoring note and line references.
 */
#include "disk_net.h"

#include "cpu_mem.h"
#include "tools_qt.h"
#include "../backend/Settings.h"

#include <QLoggingCategory>
#include <QDateTime>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <system_error>

#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/statvfs.h>
#include <unistd.h>

Q_DECLARE_LOGGING_CATEGORY(vostopCollect)

namespace fs = std::filesystem;
namespace rng = std::ranges;
using namespace std;
using namespace std::chrono_literals;
using namespace tools;
using std::string, std::vector;

namespace {

	/// Convert ascii escapes like \040 into chars. (btop convert_ascii_escapes 2274–2302)
	auto convert_ascii_escapes(const std::string& input) -> std::string {
		std::string out;
		out.reserve(input.size());

		for (std::size_t i = 0; i < input.size(); ++i) {
			if (input[i] == '\\' &&
				// Peek the next three characters.
				i + 3 < input.size() &&
				std::isdigit(input[i + 1]) &&
				std::isdigit(input[i + 2]) &&
				std::isdigit(input[i + 3])) {

				// Convert octal chars to decimal int.
				int value = ((input[i + 1] - '0') * 64) + ((input[i + 2] - '0') * 8) + (input[i + 3] - '0');
				out.push_back(static_cast<char>(value));
				// Consume the three digits.
				i += 3;
			} else {
				out.push_back(input[i]);
			}
		}
		return out;
	}

	uint64_t time_ms() { //? btop Tools::time_ms
		return static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
	}
}

//? Vendored btop Tools::system_uptime lives in Cpu (cpu_mem.cpp)
using Cpu::system_uptime;

namespace Disk {

	vector<string> fstab;
	fs::file_time_type fstab_time;
	int disk_ios{};
	vector<string> last_found;
	std::unordered_map<string, disk_info> disks;
	vector<string> disks_order;
	double old_uptime = 0.0;

	namespace {
		vector<string> ignore_list;
		std::unordered_map<string, std::future<std::pair<disk_info, int>>> disks_stats_promises;

		//?* Find the filepath to the specified ZFS object's stat file (btop 2746–2798)
		fs::path get_zfs_stat_file(const string& device_name, size_t dataset_name_start, bool zfs_hide_datasets) {
			fs::path zfs_pool_stat_path;
			if (zfs_hide_datasets) {
				zfs_pool_stat_path = Shared::procPath / "spl/kstat/zfs" / device_name;
				if (access(zfs_pool_stat_path.c_str(), R_OK) == 0) {
					return zfs_pool_stat_path;
				} else {
					qCDebug(vostopCollect) << "Can't access folder:" << sstr(QString::fromStdString(zfs_pool_stat_path.string()));
					return "";
				}
			}

			ifstream filestream;
			string filename;
			string name_compare;

			if (dataset_name_start != std::string::npos) { // device is a dataset
				zfs_pool_stat_path = Shared::procPath / "spl/kstat/zfs" / device_name.substr(0, dataset_name_start);
			} else { // device is a pool
				zfs_pool_stat_path = Shared::procPath / "spl/kstat/zfs" / device_name;
			}

			// looking through all files that start with 'objset' to find the one containing `device_name` object stats
			try {
				for (const auto& file : fs::directory_iterator(zfs_pool_stat_path)) {
					filename = file.path().filename();
					if (filename.starts_with("objset")) {
						filestream.open(file.path());
						if (filestream.good()) {
							// skip first two lines
							for (int i = 0; i < 2; i++) filestream.ignore(numeric_limits<streamsize>::max(), '\n');
							// skip characters until '7' is reached, indicating data type 7, next value will be object name
							filestream.ignore(numeric_limits<streamsize>::max(), '7');
							filestream >> name_compare;
							if (name_compare == device_name) {
								filestream.close();
								if (access(file.path().c_str(), R_OK) == 0) {
									return file.path();
								} else {
									qCDebug(vostopCollect) << "Can't access file:" << sstr(QString::fromStdString(file.path().string()));
									return "";
								}
							}
						}
						filestream.close();
					}
				}
			}
			catch (fs::filesystem_error& e) {}

			qCDebug(vostopCollect) << "Could not read directory:" << sstr(QString::fromStdString(zfs_pool_stat_path.string()));
			return "";
		}

		//?* Collect total ZFS pool io stats (btop 2800–2876)
		bool zfs_collect_pool_total_stats(struct disk_info& disk) {
			ifstream diskread;

			int64_t bytes_read;
			int64_t bytes_write;
			int64_t io_ticks;
			int64_t bytes_read_total{};
			int64_t bytes_write_total{};
			int64_t io_ticks_total{};
			int64_t objects_read{};

			// looking through all files that start with 'objset'
			for (const auto& file : fs::directory_iterator(disk.stat)) {
				if ((file.path().filename()).string().starts_with("objset")) {
					diskread.open(file.path());
					if (diskread.good()) {
						try {
							// skip first three lines
							for (int i = 0; i < 3; i++) diskread.ignore(numeric_limits<streamsize>::max(), '\n');
							// skip characters until '4' is reached, indicating data type 4, next value will be out target
							diskread.ignore(numeric_limits<streamsize>::max(), '4');
							diskread >> io_ticks;
							io_ticks_total += io_ticks;

							// skip characters until '4' is reached, indicating data type 4, next value will be out target
							diskread.ignore(numeric_limits<streamsize>::max(), '4');
							diskread >> bytes_write;
							bytes_write_total += bytes_write;

							// skip characters until '4' is reached, indicating data type 4, next value will be out target
							diskread.ignore(numeric_limits<streamsize>::max(), '4');
							diskread >> io_ticks;
							io_ticks_total += io_ticks;

							// skip characters until '4' is reached, indicating data type 4, next value will be out target
							diskread.ignore(numeric_limits<streamsize>::max(), '4');
							diskread >> bytes_read;
							bytes_read_total += bytes_read;
						} catch (const std::exception& e) {
							continue;
						}

						// increment read objects counter if no errors were encountered
						objects_read++;
					} else {
						qCDebug(vostopCollect) << "Could not read file:" << sstr(QString::fromStdString(file.path().string()));
					}
					diskread.close();
				}
			}

			// if for some reason no objects were read
			if (objects_read == 0) return false;

			if (disk.io_write.empty())
				disk.io_write.push_back(0);
			else
				disk.io_write.push_back(max((int64_t)0, (bytes_write_total - disk.old_io.at(1))));
			disk.old_io.at(1) = bytes_write_total;
			while (cmp_greater(disk.io_write.size(), VostopHistoryDepth)) disk.io_write.pop_front();

			if (disk.io_read.empty())
				disk.io_read.push_back(0);
			else
				disk.io_read.push_back(max((int64_t)0, (bytes_read_total - disk.old_io.at(0))));
			disk.old_io.at(0) = bytes_read_total;
			while (cmp_greater(disk.io_read.size(), VostopHistoryDepth)) disk.io_read.pop_front();

			if (disk.io_activity.empty())
				disk.io_activity.push_back(0);
			else
				disk.io_activity.push_back(max((int64_t)0, (io_ticks_total - disk.old_io.at(2))));
			disk.old_io.at(2) = io_ticks_total;
			while (cmp_greater(disk.io_activity.size(), VostopHistoryDepth)) disk.io_activity.pop_front();

			return true;
		}
	}

	void collect(uint64_t swap_total, uint64_t swap_used, uint64_t swap_free,
				 double swap_used_pct, double swap_free_pct, bool has_swap) {
		auto show_disks = Settings::getB("show_disks");
		if (not show_disks) return; //? stolen gate (btop 2425)

		try {
			const auto& disks_filter = Settings::getS("disks_filter");
			bool filter_exclude = false;
			auto use_fstab = Settings::getB("use_fstab");
			auto only_physical = Settings::getB("only_physical");
			auto zfs_hide_datasets = Settings::getB("zfs_hide_datasets");
			ifstream diskread;

			vector<string> filter;
			if (not disks_filter.isEmpty()) {
				filter = ssplit(sstr(disks_filter));
				if (filter.at(0).starts_with("exclude=")) {
					filter_exclude = true;
					filter.at(0) = filter.at(0).substr(8);
				}
			}

			//? Get list of "real" filesystems from /proc/filesystems
			vector<string> fstypes;
			if (only_physical and not use_fstab) {
				fstypes = {"zfs", "wslfs", "drvfs"};
				diskread.open(Shared::procPath / "filesystems");
				if (diskread.good()) {
					for (string fstype; diskread >> fstype;) {
						if (not is_in(fstype, "nodev", "squashfs", "nullfs"))
							fstypes.push_back(fstype);
						diskread.ignore(SSmax, '\n');
					}
				}
				else
					throw std::runtime_error("Failed to read /proc/filesystems");
				diskread.close();
			}

			//? Get disk list to use from fstab if enabled
			if (use_fstab and fs::last_write_time("/etc/fstab") != fstab_time) {
				fstab.clear();
				fstab_time = fs::last_write_time("/etc/fstab");
				diskread.open("/etc/fstab");
				if (diskread.good()) {
					for (string instr; diskread >> instr;) {
						if (not instr.starts_with('#')) {
							diskread >> instr;
							if (not is_in(instr, "none", "swap")) fstab.push_back(instr);
						}
						diskread.ignore(SSmax, '\n');
					}
				}
				else
					throw std::runtime_error("Failed to read /etc/fstab");
				diskread.close();
			}

			//? Get mounts from /etc/mtab or /proc/self/mounts
			diskread.open((fs::exists("/etc/mtab") ? fs::path("/etc/mtab") : Shared::procPath / "self/mounts"));
			if (diskread.good()) {
				vector<string> found;
				found.reserve(last_found.size());
				string dev, mountpoint, fstype;
				while (not diskread.eof()) {
					std::error_code ec;
					diskread >> dev >> mountpoint >> fstype;
					diskread.ignore(SSmax, '\n');

					// A mountpoint can contain ascii escape codes, which will not work with `statvfs`.
					mountpoint = convert_ascii_escapes(mountpoint);

					if (v_contains(ignore_list, mountpoint) or v_contains(found, mountpoint)) continue;

					//? Match filter if not empty
					if (not filter.empty()) {
						bool match = v_contains(filter, mountpoint);
						if ((filter_exclude and match) or (not filter_exclude and not match))
							continue;
					}

					//? Skip ZFS datasets if zfs_hide_datasets option is enabled
					size_t zfs_dataset_name_start = 0;
					if (fstype == "zfs" && (zfs_dataset_name_start = dev.find('/')) != std::string::npos && zfs_hide_datasets) continue;

					if ((not use_fstab and not only_physical)
					or (use_fstab and v_contains(fstab, mountpoint))
					or (not use_fstab and only_physical and v_contains(fstypes, fstype))) {
						found.push_back(mountpoint);

						//? Save mountpoint, name, fstype, dev path and path to /sys/block stat file
						if (not disks.contains(mountpoint)) {
							disks[mountpoint] = disk_info{fs::canonical(dev, ec), fs::path(mountpoint).filename(), fstype};
							if (disks.at(mountpoint).dev.empty()) disks.at(mountpoint).dev = dev;
							if (disks.at(mountpoint).name.empty()) disks.at(mountpoint).name = (mountpoint == "/" ? "root" : mountpoint);
							string devname = disks.at(mountpoint).dev.filename();
							int c = 0;
							while (devname.size() >= 2) {
								//? fmt::format → QString (btop 2533, 2535)
								const auto stat = sstr(QString("/sys/block/%1/stat").arg(sstr(QString::fromStdString(devname))));
								if (fs::exists(stat, ec) and access(stat.c_str(), R_OK) == 0) {
									const auto mount_stat = sstr(QString("/sys/block/%1/%2/stat").arg(
										sstr(QString::fromStdString(devname)),
										sstr(QString::fromStdString(disks.at(mountpoint).dev.filename()))));
									if (c > 0 and fs::exists(mount_stat, ec))
										disks.at(mountpoint).stat = std::move(mount_stat);
									else
										disks.at(mountpoint).stat = std::move(stat);
									break;
								//? Set ZFS stat filepath
								} else if (fstype == "zfs") {
									disks.at(mountpoint).stat = get_zfs_stat_file(dev, zfs_dataset_name_start, zfs_hide_datasets);
									if (disks.at(mountpoint).stat.empty()) {
										qCDebug(vostopCollect) << "Failed to get ZFS stat file for device" << sstr(QString::fromStdString(dev));
									}
									break;
								}
								devname.resize(devname.size() - 1);
								c++;
							}
						}

						//? If zfs_hide_datasets option was switched, refresh stat filepath
						if (fstype == "zfs" && ((zfs_hide_datasets && !is_directory(disks.at(mountpoint).stat))
							|| (!zfs_hide_datasets && is_directory(disks.at(mountpoint).stat)))) {
							disks.at(mountpoint).stat = get_zfs_stat_file(dev, zfs_dataset_name_start, zfs_hide_datasets);
							if (disks.at(mountpoint).stat.empty()) {
								qCDebug(vostopCollect) << "Failed to get ZFS stat file for device" << sstr(QString::fromStdString(dev));
							}
						}
					}
				}

				//? Remove disks no longer mounted or filtered out
				if (Settings::getB("swap_disk") and has_swap) found.push_back("swap");
				for (auto it = disks.begin(); it != disks.end();) {
					if (not v_contains(found, it->first))
						it = disks.erase(it);
					else
						it++;
				}
				last_found = std::move(found);
			}
			else
				throw std::runtime_error("Failed to get mounts from /etc/mtab and /proc/self/mounts");
			diskread.close();

			//? Get disk/partition stats (statvfs async pool, kept stolen btop 2580–2627)
			for (auto it = disks.begin(); it != disks.end(); ) {
				auto& [mountpoint, disk] = *it;
				if (v_contains(ignore_list, mountpoint) or disk.name == "swap") {
					it = disks.erase(it);
					continue;
				}
				if (auto promises_it = disks_stats_promises.find(mountpoint); promises_it != disks_stats_promises.end()) {
					auto& promise = promises_it->second;
					if (promise.valid() &&
					   promise.wait_for(0s) == std::future_status::timeout) {
						++it;
						continue;
					}
					auto promise_res = promises_it->second.get();
					if (promise_res.second != -1) {
						ignore_list.push_back(mountpoint);
						qCWarning(vostopCollect) << "Failed to get disk/partition stats for mount" << sstr(QString::fromStdString(mountpoint))
							<< "with statvfs error code:" << promise_res.second << ". Ignoring...";
						it = disks.erase(it);
						continue;
					}
					auto& updated_stats = promise_res.first;
					disk.total = updated_stats.total;
					disk.free = updated_stats.free;
					disk.used = updated_stats.used;
					disk.used_percent = updated_stats.used_percent;
					disk.free_percent = updated_stats.free_percent;
				}
				const auto free_priv = Settings::getB("disk_free_priv");
				disks_stats_promises[mountpoint] = std::async(std::launch::async, [mountpoint, free_priv]() -> std::pair<disk_info, int> {
					struct statvfs vfs;
					disk_info disk;
					if (statvfs(mountpoint.c_str(), &vfs) < 0) {
						return {disk, errno};
					}
					disk.total = vfs.f_blocks * vfs.f_frsize;
					disk.free = (free_priv ? vfs.f_bfree : vfs.f_bavail) * vfs.f_frsize;
					disk.used = disk.total - disk.free;
					if (disk.total != 0) {
						disk.used_percent = round((double)disk.used * 100 / disk.total);
						disk.free_percent = 100 - disk.used_percent;
					} else {
						disk.used_percent = 0;
						disk.free_percent = 0;
					}
					return {disk, -1};
				});
				++it;
			}

			//? Setup disks order in UI and add swap if enabled
			disks_order.clear();
			if (disks.contains("/")) disks_order.push_back("/");
			if (Settings::getB("swap_disk") and has_swap) {
				disks_order.push_back("swap");
				if (not disks.contains("swap")) disks["swap"] = {"", "swap", "swap"};
				disks.at("swap").total = swap_total;
				disks.at("swap").used = swap_used;
				disks.at("swap").free = swap_free;
				disks.at("swap").used_percent = (int)swap_used_pct;
				disks.at("swap").free_percent = (int)swap_free_pct;
			}
			for (const auto& name : last_found)
				if (not is_in(name, "/", "swap")) disks_order.push_back(name);

			//? Get disks IO
			double uptime = system_uptime();
			int64_t sectors_read, sectors_write, io_ticks, io_ticks_temp;
			disk_ios = 0;
			for (auto& [ignored, disk] : disks) {
				if (disk.stat.empty() or access(disk.stat.c_str(), R_OK) != 0) continue;
				if (disk.fstype == "zfs" && zfs_hide_datasets && zfs_collect_pool_total_stats(disk)) {
					disk_ios++;
					continue;
				}
				diskread.open(disk.stat);
				if (diskread.good()) {
					disk_ios++;
					//? ZFS Pool Support
					if (disk.fstype == "zfs") {
						// skip first three lines
						for (int i = 0; i < 3; i++) diskread.ignore(numeric_limits<streamsize>::max(), '\n');
						// skip characters until '4' is reached, indicating data type 4, next value will be out target
						diskread.ignore(numeric_limits<streamsize>::max(), '4');
						diskread >> io_ticks;

						// skip characters until '4' is reached, indicating data type 4, next value will be out target
						diskread.ignore(numeric_limits<streamsize>::max(), '4');
						diskread >> sectors_write; // nbytes written
						if (disk.io_write.empty())
							disk.io_write.push_back(0);
						else
							disk.io_write.push_back(max((int64_t)0, (sectors_write - disk.old_io.at(1))));
						disk.old_io.at(1) = sectors_write;
						while (cmp_greater(disk.io_write.size(), VostopHistoryDepth)) disk.io_write.pop_front();

						// skip characters until '4' is reached, indicating data type 4, next value will be out target
						diskread.ignore(numeric_limits<streamsize>::max(), '4');
						diskread >> io_ticks_temp;
						io_ticks += io_ticks_temp;

						// skip characters until '4' is reached, indicating data type 4, next value will be out target
						diskread.ignore(numeric_limits<streamsize>::max(), '4');
						diskread >> sectors_read; // nbytes read
						if (disk.io_read.empty())
							disk.io_read.push_back(0);
						else
							disk.io_read.push_back(max((int64_t)0, (sectors_read - disk.old_io.at(0))));
						disk.old_io.at(0) = sectors_read;
						while (cmp_greater(disk.io_read.size(), VostopHistoryDepth)) disk.io_read.pop_front();

						if (disk.io_activity.empty())
							disk.io_activity.push_back(0);
						else
							disk.io_activity.push_back(max((int64_t)0, (io_ticks - disk.old_io.at(2))));
						disk.old_io.at(2) = io_ticks;
						while (cmp_greater(disk.io_activity.size(), VostopHistoryDepth)) disk.io_activity.pop_front();
					} else {
						for (int i = 0; i < 2; i++) { diskread >> std::ws; diskread.ignore(SSmax, ' '); }
						diskread >> sectors_read;
						if (disk.io_read.empty())
							disk.io_read.push_back(0);
						else
							disk.io_read.push_back(max((int64_t)0, (sectors_read - disk.old_io.at(0)) * 512));
						disk.old_io.at(0) = sectors_read;
						while (cmp_greater(disk.io_read.size(), VostopHistoryDepth)) disk.io_read.pop_front();

						for (int i = 0; i < 3; i++) { diskread >> std::ws; diskread.ignore(SSmax, ' '); }
						diskread >> sectors_write;
						if (disk.io_write.empty())
							disk.io_write.push_back(0);
						else
							disk.io_write.push_back(max((int64_t)0, (sectors_write - disk.old_io.at(1)) * 512));
						disk.old_io.at(1) = sectors_write;
						while (cmp_greater(disk.io_write.size(), VostopHistoryDepth)) disk.io_write.pop_front();

						for (int i = 0; i < 2; i++) { diskread >> std::ws; diskread.ignore(SSmax, ' '); }
						diskread >> io_ticks;
						if (uptime == old_uptime || disk.io_activity.empty())
							disk.io_activity.push_back(0);
						else
							disk.io_activity.push_back(clamp((long)round((double)(io_ticks - disk.old_io.at(2)) / (uptime - old_uptime) / 10), 0l, 100l));
						disk.old_io.at(2) = io_ticks;
						while (cmp_greater(disk.io_activity.size(), VostopHistoryDepth)) disk.io_activity.pop_front();
					}
				} else {
					qCDebug(vostopCollect) << "Error in Mem::collect() : when opening" << sstr(QString::fromStdString(disk.stat.string()));
				}
				diskread.close();
			}
			old_uptime = uptime;
		}
		catch (const std::exception& e) {
			qCWarning(vostopCollect) << "Error in Mem::collect() :" << e.what();
		}
	}
}

namespace Net {

	std::unordered_map<string, net_info> current_net;
	net_info empty_net = {};
	vector<string> interfaces;
	string selected_iface;
	int errors{};
	std::unordered_map<string, uint64_t> graph_max = { {"download", {}}, {"upload", {}} };
	std::unordered_map<string, std::array<int, 2>> max_count = { {"download", {}}, {"upload", {}} };
	bool rescale{true};
	uint64_t timestamp{};

	void set_selected_iface(const string& iface) {
		selected_iface = iface;
	}

	auto collect() -> net_info& { //? btop 2891–3082; Runner::stopping removed
		auto& net = current_net;
		const auto& config_iface = Settings::getS("net_iface");
		auto net_sync = Settings::getB("net_sync");
		auto net_auto = Settings::getB("net_auto");
		auto new_timestamp = time_ms();

		if (errors < 3) {
			//? Get interface list using getifaddrs() wrapper
			IfAddrsPtr if_addrs {};
			if (if_addrs.get_status() != 0) {
				errors++;
				qCCritical(vostopCollect) << "Net::collect() -> getifaddrs() failed with id" << if_addrs.get_status();
				return empty_net;
			}
			int family = 0;
			static_assert(INET6_ADDRSTRLEN >= INET_ADDRSTRLEN); // 46 >= 16, compile-time assurance.
			enum { IPBUFFER_MAXSIZE = INET6_ADDRSTRLEN }; // manually using the known biggest value, guarded by the above static_assert
			char ip[IPBUFFER_MAXSIZE];
			interfaces.clear();
			string ipv4, ipv6;

			//? Iteration over all items in getifaddrs() list
			for (auto* ifa = if_addrs.get(); ifa != nullptr; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr == nullptr) continue;
				family = ifa->ifa_addr->sa_family;
				const auto& iface = ifa->ifa_name;

				//? Update available interfaces vector and get status of interface
				if (not v_contains(interfaces, iface)) {
					interfaces.push_back(iface);
					net[iface].connected = (ifa->ifa_flags & IFF_RUNNING);

					// An interface can have more than one IP of the same family associated with it,
					// but we pick only the first one to show in the NET box.
					// Note: Interfaces without any IPv4 and IPv6 set are still valid and monitorable!
					net[iface].ipv4.clear();
					net[iface].ipv6.clear();
				}

				//? Get IPv4 address
				if (family == AF_INET) {
					if (net[iface].ipv4.empty()) {
						if (nullptr != inet_ntop(family, &(reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr), ip, IPBUFFER_MAXSIZE)) {
							net[iface].ipv4 = ip;
						} else {
							int errsv = errno;
							qCCritical(vostopCollect) << "Net::collect() -> Failed to convert IPv4 to string for iface" << iface << "errno:" << strerror(errsv);
						}
					}
				}
				//? Get IPv6 address
				else if (family == AF_INET6) {
					if (net[iface].ipv6.empty()) {
						if (nullptr != inet_ntop(family, &(reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr)->sin6_addr), ip, IPBUFFER_MAXSIZE)) {
							net[iface].ipv6 = ip;
						} else {
							int errsv = errno;
							qCCritical(vostopCollect) << "Net::collect() -> Failed to convert IPv6 to string for iface" << iface << "errno:" << strerror(errsv);
						}
					}
				} //else, ignoring family==AF_PACKET (see man 3 getifaddrs) which is the first one in the `for` loop.
			}

			//? Get total received and transmitted bytes + device address if no ip was found
			for (const auto& iface : interfaces) {
				auto& netif = net.at(iface);
				if (netif.ipv4.empty() and netif.ipv6.empty())
					netif.ipv4 = readfile("/sys/class/net/" + iface + "/address");

				for (const string dir : {"download", "upload"}) {
					const fs::path sys_file = "/sys/class/net/" + iface + "/statistics/" + (dir == "download" ? "rx_bytes" : "tx_bytes");
					auto& saved_stat = netif.stat.at(dir);
					auto& bandwidth = netif.bandwidth.at(dir);

					uint64_t val{};
					try { val = stoull(readfile(sys_file, "0")); }
					catch (const std::invalid_argument&) {}
					catch (const std::out_of_range&) {}

					//? Update speed, total and top values
					if (val < saved_stat.last) {
						saved_stat.rollover += saved_stat.last;
						saved_stat.last = 0;
					}
					if (cmp_greater((unsigned long long)saved_stat.rollover + (unsigned long long)val, numeric_limits<uint64_t>::max())) {
						saved_stat.rollover = 0;
						saved_stat.last = 0;
					}
					saved_stat.speed = round((double)(val - saved_stat.last) / ((double)(new_timestamp - timestamp) / 1000));
					if (saved_stat.speed > saved_stat.top) saved_stat.top = saved_stat.speed;
					if (saved_stat.offset > val + saved_stat.rollover) saved_stat.offset = 0;
					saved_stat.total = (val + saved_stat.rollover) - saved_stat.offset;
					saved_stat.last = val;

					//? Add values to graph
					bandwidth.push_back(saved_stat.speed);
					while (cmp_greater(bandwidth.size(), VostopHistoryDepth)) bandwidth.pop_front();

					//? Set counters for auto scaling
					if (net_auto and selected_iface == iface) {
						if (net_sync and saved_stat.speed < netif.stat.at(dir == "download" ? "upload" : "download").speed) continue;
						if (saved_stat.speed > graph_max[dir]) {
							++max_count[dir][0];
							if (max_count[dir][1] > 0) --max_count[dir][1];
						}
						else if (graph_max[dir] > 10 << 10 and saved_stat.speed < graph_max[dir] / 10) {
							++max_count[dir][1];
							if (max_count[dir][0] > 0) --max_count[dir][0];
						}
					}
				}
			}

			//? Clean up net map if needed
			if (net.size() > interfaces.size()) {
				for (auto it = net.begin(); it != net.end();) {
					if (not v_contains(interfaces, it->first))
						it = net.erase(it);
					else
						it++;
				}
			}

			timestamp = new_timestamp;
		}

		//? Return empty net_info struct if no interfaces was found
		if (net.empty())
			return empty_net;

		//? Find an interface to display if selected isn't set or valid
		if (selected_iface.empty() or not v_contains(interfaces, selected_iface)) {
			max_count["download"][0] = max_count["download"][1] = max_count["upload"][0] = max_count["upload"][1] = 0;
			if (net_auto) rescale = true;
			const auto cfg_iface = sstr(config_iface);
			if (not cfg_iface.empty() and v_contains(interfaces, cfg_iface)) selected_iface = cfg_iface;
			else {
				//? Sort interfaces by total upload + download bytes
				auto sorted_interfaces = interfaces;
				rng::sort(sorted_interfaces, [&](const auto& a, const auto& b){
					return 	cmp_greater(net.at(a).stat["download"].total + net.at(a).stat["upload"].total,
										net.at(b).stat["download"].total + net.at(b).stat["upload"].total);
				});
				selected_iface.clear();
				//? Try to set to a connected interface
				for (const auto& iface : sorted_interfaces) {
					if (net.at(iface).connected) {
						selected_iface = iface;
						break;
					}
				}
				//? If no interface is connected set to first available
				if (selected_iface.empty() and not sorted_interfaces.empty()) selected_iface = sorted_interfaces.at(0);
				else if (sorted_interfaces.empty()) return empty_net;
			}
		}

		//? Calculate max scale for graphs if needed
		if (net_auto) {
			bool sync = false;
			for (const auto& dir : {"download", "upload"}) {
				for (const auto& sel : {0, 1}) {
					if (rescale or max_count[dir][sel] >= 5) {
						const long long avg_speed = (net[selected_iface].bandwidth[dir].size() > 5
							? std::accumulate(net.at(selected_iface).bandwidth.at(dir).rbegin(), net.at(selected_iface).bandwidth.at(dir).rbegin() + 5, 0ll) / 5
							: net[selected_iface].stat[dir].speed);
						graph_max[dir] = max(uint64_t(avg_speed * (sel == 0 ? 1.3 : 3.0)), (uint64_t)10 << 10);
						max_count[dir][0] = max_count[dir][1] = 0;
						if (net_sync) sync = true;
						break;
					}
				}
				//? Sync download/upload graphs if enabled
				if (sync) {
					const auto other = (string(dir) == "upload" ? "download" : "upload");
					graph_max[other] = graph_max[dir];
					max_count[other][0] = max_count[other][1] = 0;
					break;
				}
			}
		}

		rescale = false;
		return net.at(selected_iface);
	}
}