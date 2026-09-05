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
	X(int, pollIntervalMs, "poll_interval_ms", 1000)

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