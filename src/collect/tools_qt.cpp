/*
 * vostop — trimmed helper utilities, Qt-backed.
 * Re-implemented from btop's Tools (Apache-2.0); see THIRD-PARTY-NOTICES.
 */
#include "tools_qt.h"

#include <QFile>
#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <utility>

namespace tools {

std::string readfile(const std::string& path, const std::string& def) {
	QFile file(QString::fromStdString(path));
	if (not file.open(QIODevice::ReadOnly | QIODevice::Text))
		return def;
	QByteArray data = file.readAll();
	//? btop strips a single trailing newline
	while (not data.isEmpty() and (data.endsWith('\n') or data.endsWith('\r')))
		data.chop(1);
	return std::string(data.constData(), static_cast<size_t>(data.size()));
}
std::vector<std::string> ssplit(const std::string& str, char delim) {
	const QStringList parts = QString::fromStdString(str).split(QChar::fromLatin1(delim), Qt::SkipEmptyParts);
	std::vector<std::string> out;
	out.reserve(static_cast<size_t>(parts.size()));
	for (const QString& p : parts)
		out.emplace_back(p.toStdString());
	return out;
}

std::string str_to_lower(std::string str) {
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return str;
}

std::string capitalize(std::string str) {
	if (str.empty()) return str;
	str.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(str.front())));
	for (size_t i = 1; i < str.size(); ++i)
		str[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i])));
	return str;
}

std::string trim(const std::string& str, const std::string& whitespace) {
	const size_t strBegin = str.find_first_not_of(whitespace);
	if (strBegin == std::string::npos)
		return "";
	const size_t strEnd = str.find_last_not_of(whitespace);
	const size_t strRange = strEnd - strBegin + 1;
	return str.substr(strBegin, strRange);
}

std::string s_replace(std::string str, const std::string& from, const std::string& to) {
	if (from.empty()) return str;
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.size(), to);
		pos += to.size();
	}
	return str;
}

uint64_t get_monotonicTimeUSec() {
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string sec_to_dhms(size_t seconds, bool no_days, bool no_seconds) {
	size_t days = seconds / 86400; seconds %= 86400;
	size_t hours = seconds / 3600; seconds %= 3600;
	size_t minutes = seconds / 60; seconds %= 60;
	std::string out = (not no_days and days > 0 ? std::to_string(days) + "d " : "")
				+ (hours < 10 ? "0" : "") + std::to_string(hours) + ':'
				+ (minutes < 10 ? "0" : "") + std::to_string(minutes)
				+ (not no_seconds ? ":" + std::string(std::cmp_less(seconds, 10) ? "0" : "") + std::to_string(seconds) : "");
	return out;
}

std::string floating_humanizer(uint64_t value, bool per_second) {
	static const char* units[] = { "Byte", "KiB", "MiB", "GiB", "TiB", "PiB" };
	double v = static_cast<double>(value);
	size_t unit = 0;
	while (v >= 1024.0 and unit < 5) {
		v /= 1024.0;
		++unit;
	}
	QString out;
	if (unit == 0)
		out = QString::number(static_cast<qulonglong>(value));
	else
		out = QString::number(v, 'f', 1);
	out += u' ' + QString::fromLatin1(units[unit]);
	if (per_second)
		out += "/s";
	return sstr(out);
}

} // namespace tools