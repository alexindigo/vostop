/*
 * vostop — Settings singleton.
 *
 * Minimal QSettings wrapper replacing btop's Config::getS/getB for the
 * collector-side configuration. Thread-safe: collectors on the worker thread
 * read through a mutex-guarded cache; QML properties live on the GUI thread.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariantHash>
#include <QReadWriteLock>
#include <QtQmlIntegration/qqmlintegration.h>
QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

#define SETTINGS_DSL \
	X(bool, showSwap, "show_swap", true) \
	X(bool, swapDisk, "swap_disk", true) \
	X(bool, showCpuFreq, "show_cpu_freq", true) \
	X(QString, freqMode, "freq_mode", "first") \
	X(QString, cpuCoreMap, "cpu_core_map", "") \
	X(bool, zfsArcCached, "zfs_arc_cached", true) \
	X(bool, checkTemp, "check_temp", false) \
	X(bool, showCoretemp, "show_coretemp", true) \
	X(QString, cpuSensor, "cpu_sensor", "Auto") \
	X(bool, showBattery, "show_battery", false) \
	X(QString, selectedBattery, "selected_battery", "Auto") \
	X(bool, showCpuWatts, "show_cpu_watts", false) \
	X(bool, showDisks, "show_disks", true) \
	X(int, pollIntervalMs, "poll_interval_ms", 1000) \
	X(QString, procSorting, "proc_sorting", "cpu lazy") \
	X(bool, procReversed, "proc_reversed", false) \
	X(QString, procFilter, "proc_filter", "") \
	X(bool, procPerCore, "proc_per_core", false) \
	X(bool, procFilterKernel, "proc_filter_kernel", false) \
	X(bool, pauseProcList, "pause_proc_list", false) \
	X(bool, keepDeadProcUsage, "keep_dead_proc_usage", false) \
	X(bool, procInfoSmaps, "proc_info_smaps", false) \
	X(QString, disksFilter, "disks_filter", "") \
	X(bool, diskFreePriv, "disk_free_priv", false) \
	X(bool, useFstab, "use_fstab", true) \
	X(bool, onlyPhysical, "only_physical", true) \
	X(bool, zfsHideDatasets, "zfs_hide_datasets", false) \
	X(QString, netIface, "net_iface", "") \
	X(bool, netSync, "net_sync", true) \
	X(bool, netAuto, "net_auto", true) \
	X(QString, shownGpus, "shown_gpus", "Auto") \
	X(bool, nvmlMeasurePcieSpeeds, "nvml_measure_pcie_speeds", false) \
	X(bool, rsmiMeasurePcieSpeeds, "rsmi_measure_pcie_speeds", false) \
	X(QString, theme, "theme", "dark") \
	X(bool, groupCollapsedApps, "group_collapsed_apps", false) \
	X(bool, groupCollapsedBackground, "group_collapsed_background", false) \
	X(bool, groupCollapsedSystem, "group_collapsed_system", false) \
	X(bool, procIoReads, "proc_io_reads", true) \
	X(bool, gpuFdinfoWalk, "gpu_fdinfo_walk", true)

class Settings : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//? Per-key expansion: X(type, Name, "key", default)
#define X(type, Name, key, def) \
	Q_PROPERTY(type Name READ Name WRITE set_##Name NOTIFY Name##Changed FINAL)
	SETTINGS_DSL
#undef X

public:
	static Settings* instance();

	//* QML singleton provider — returns the same instance the C++ side uses
	static Settings* create(QQmlEngine* engine, QJSEngine* jsEngine);

	//? Thread-safe collector-side accessors (btop Config::getB/getS replacements)
	static bool getB(const QString& key);
	static QString getS(const QString& key);

#define X(type, Name, key, def) \
	type Name() const { return m_cache.value(QStringLiteral(key)).value<type>(); } \
	void set_##Name(const type& v);
	SETTINGS_DSL
#undef X

signals:
#define X(type, Name, key, def) void Name##Changed();
	SETTINGS_DSL
#undef X

private:
	explicit Settings(QObject* parent = nullptr);
	void loadCache();
	void persist(const char* key, const QVariant& v);

	QVariantHash m_cache;
	mutable QReadWriteLock m_lock;
};