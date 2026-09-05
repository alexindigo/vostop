/*
 * vostop — PSI (Pressure Stall Information) collector. Original vostop code
 * (GPLv3-or-later); btop has no equivalent. Parses /proc/pressure/{cpu,memory,io}.
 */
#pragma once

namespace Psi {

//* avg10/avg60/avg300 for the "some" and "full" lines
struct Pressure {
	double some[3] = {0.0, 0.0, 0.0};
	double full[3] = {0.0, 0.0, 0.0};
	bool hasFull = false;
};

//* /proc/pressure exists and is readable (kernel >= 4.20, PSI not disabled)
bool available();

//* Read pressure for one resource: "cpu", "memory" or "io". Returns false if absent/unreadable.
bool read(const char* resource, Pressure& out);

} // namespace Psi