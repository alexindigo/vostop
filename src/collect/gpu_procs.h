/*
 * vostop — per-process GPU collector. Original vostop code (GPLv3-or-later);
 * btop has no equivalent (the nvtop approach): DRM fdinfo walk, vendor-neutral
 * across i915/xe/amdgpu/msm, plus an NVML per-pid fallback for NVIDIA.
 */
#pragma once

#include <QString>
#include <QHash>

namespace GpuProcs {

//* Per-pid result of the fdinfo/NVML walk
struct ProcGpu {
	double busyPct = 0.0;      //? max across engines of Δ(engine ns)/Δwall
	quint64 memBytes = 0;      //? sum of drm-resident-* values (0 for NVML-compute-only)
	bool valid = false;
};

//* Device-level aggregate from fdinfo clients (Intel approximate device card:
//* misses other users' clients — the consumer must label it approximate)
struct DeviceApprox {
	QString pdev;
	double busyPct = 0.0;      //? sum of per-client busy, clamped to 100
	quint64 memBytes = 0;
};

//* Slow-cadence fdinfo walk over own-user pids. `newTick` toggles the 2-tick
//* cadence (call with alternating value). Returns pid → result (own-user only;
//* other users' pids unreadable → absent → UI shows "—").
QHash<quint64, ProcGpu> collect(bool newTick);

//* Device-level fdinfo aggregates (only populated when real backends found no card)
QHash<QString, DeviceApprox> deviceApprox();

//* NVML per-pid fallback: pid → busy% (empty when NVML absent; original vostop
//* code via the already-loaded QLibrary function pointers)
QHash<quint64, ProcGpu> nvmlPerPid();

//* Which DRM driver(s) own the clients seen so far ("i915", "xe", ...) — empty
//* when no fdinfo clients exist
QStringList drmDrivers();

} // namespace GpuProcs