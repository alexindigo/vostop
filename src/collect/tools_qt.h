/*
 * vostop — trimmed helper utilities, Qt-backed.
 *
 * Replaces the small subset of btop's Tools helpers used by the vendored
 * collectors (readfile/ssplit/v_contains/is_in/str_to_lower/capitalize/
 * trim/s_replace). Original logic by Aristocratos (btop, Apache-2.0),
 * re-implemented on QFile/QString; see THIRD-PARTY-NOTICES.
 */
#pragma once

#include <QString>
#include <QStringList>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>

namespace tools {

//? btop's SSmax: maximum streamsize for istream::ignore()
constexpr std::streamsize SSmax = std::numeric_limits<std::streamsize>::max();

//? Read whole file, strip trailing newline; returns def on any error (btop Tools::readfile semantics)
std::string readfile(const std::string& path, const std::string& def = "");
inline std::string readfile(const std::filesystem::path& path, const std::string& def = "") {
	return readfile(path.string(), def);
}

//? Split on delim, skipping empty parts (btop Tools::ssplit semantics)
std::vector<std::string> ssplit(const std::string& str, char delim = ' ');

//? std::string <-> QString conveniences used by collectors
inline QString qstr(const std::string& s) { return QString::fromStdString(s); }
inline std::string sstr(const QString& s) { return s.toStdString(); }

//? Container contains-value check (btop Tools::v_contains)
template <typename Container, typename Value>
inline bool v_contains(const Container& c, const Value& v) {
	return std::find(c.begin(), c.end(), v) != c.end();
}

//? Match value against any of the candidates (btop Tools::is_in)
template <typename First, typename... Args>
inline bool is_in(const First& first, const Args&... args) {
	return ((first == args) or ...);
}

//? Index of value in container, size() when absent (btop Tools::v_index)
template <typename Container, typename Value>
inline size_t v_index(const Container& c, const Value& v) {
	auto it = std::find(c.begin(), c.end(), v);
	return (it != c.end()) ? static_cast<size_t>(it - c.begin()) : c.size();
}

std::string str_to_lower(std::string str);
std::string capitalize(std::string str);
std::string trim(const std::string& str, const std::string& whitespace = " \t\r\n");
std::string s_replace(std::string str, const std::string& from, const std::string& to);

//? Case-insensitive substring search (btop Tools::s_contains_ic)
inline bool s_contains_ic(const std::string_view str, const std::string_view find_val) {
	auto it = std::search(
		str.begin(), str.end(),
		find_val.begin(), find_val.end(),
		[](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
	);
	return it != str.end();
}

//? "X days HH:MM:SS" (btop Tools::sec_to_dhms)
std::string sec_to_dhms(size_t seconds, bool no_days = false, bool no_seconds = false);

//? Base-2 human-readable byte size, e.g. "928 MiB" (btop Tools::floating_humanizer, byte/base-2 path)
std::string floating_humanizer(uint64_t value, bool per_second = false);

//? Monotonic time in microseconds (btop get_monotonicTimeUSec)
uint64_t get_monotonicTimeUSec();

} // namespace tools