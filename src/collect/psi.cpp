/*
 * vostop — PSI collector (implementation). Original vostop code, GPLv3-or-later.
 */
#include "psi.h"

#include <QFile>
#include <QStringList>

#include <cmath>

namespace Psi {

bool available() {
	QFile probe(QStringLiteral("/proc/pressure/cpu"));
	return probe.open(QIODevice::ReadOnly | QIODevice::Text);
}

//? Parse one "some|full avg10=x avg60=y avg300=z total=t" line
static bool parseLine(const QString& line, double out[3], bool& hasLine) {
	const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
	if (parts.isEmpty())
		return false;
	bool got[3] = {false, false, false};
	for (int i = 1; i < parts.size(); ++i) {
		const QString& kv = parts.at(i);
		const int eq = kv.indexOf(u'=');
		if (eq < 0) continue;
		bool ok = false;
		const double val = kv.mid(eq + 1).toDouble(&ok);
		if (not ok or std::isnan(val) or std::isinf(val)) continue;
		const QString key = kv.left(eq);
		if (key == "avg10") { out[0] = val; got[0] = true; }
		else if (key == "avg60") { out[1] = val; got[1] = true; }
		else if (key == "avg300") { out[2] = val; got[2] = true; }
	}
	hasLine = got[0] and got[1] and got[2];
	return hasLine;
}

bool read(const char* resource, Pressure& out) {
	QFile file(QStringLiteral("/proc/pressure/") + QString::fromLatin1(resource));
	if (not file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	bool gotSome = false, gotFull = false;
	while (not file.atEnd()) {
		const QString line = QString::fromLatin1(file.readLine());
		if (line.startsWith("some"))
			gotSome = parseLine(line, out.some, gotSome) or gotSome;
		else if (line.startsWith("full"))
			gotFull = parseLine(line, out.full, gotFull) or gotFull;
	}
	out.hasFull = gotFull;
	//? cpu pressure has no "full" line on Linux — some-only is valid
	return gotSome;
}

} // namespace Psi