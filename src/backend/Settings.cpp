/*
 * vostop — Settings singleton (implementation).
 */
#include "Settings.h"

#include <topqml/TopConfig.h>

#include <QSettings>
#include <QQmlEngine>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(vostopSettings, "vostop.settings")

Settings* Settings::instance() {
	static Settings inst;
	return &inst;
}

Settings* Settings::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	Settings* obj = instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

Settings::Settings(QObject* parent)
	: QObject(parent) {
	loadCache();
}

void Settings::loadCache() {
	QSettings store;
	m_lock.lockForWrite();
	for (const QString& key : store.allKeys())
		m_cache.insert(key, store.value(key));
	//? Phase-2 defaults (btop defaults; sensor/battery/watts OFF until phase 5)
#define X(type, Name, key, def) \
	if (not m_cache.contains(QStringLiteral(key))) m_cache.insert(QStringLiteral(key), QVariant::fromValue<type>(def));
	SETTINGS_DSL
#undef X
	//? vostop-only defaults (no TopConfig counterpart — see Settings.h)
	if (not m_cache.contains(QStringLiteral("net_hide_docker")))
		m_cache.insert(QStringLiteral("net_hide_docker"), QVariant::fromValue<bool>(true));
	m_lock.unlock();
	//? Mirror every key into TopConfig (the library persists nothing — vostop
	//? owns persistence and forwards values collector-side; identical DSL
	//? shape on both sides, one macro line covers all 38 keys)
	auto* top = TopConfig::instance();
	QReadLocker lock(&m_lock);
#define X(type, Name, key, def) top->set_##Name(m_cache.value(QStringLiteral(key)).value<type>());
	SETTINGS_DSL
#undef X
}

void Settings::persist(const char* key, const QVariant& v) {
	QSettings store;
	store.setValue(QString::fromLatin1(key), v);
}

bool Settings::getB(const QString& key) {
	Settings* s = instance();
	QReadLocker lock(&s->m_lock);
	return s->m_cache.value(key).toBool();
}

QString Settings::getS(const QString& key) {
	Settings* s = instance();
	QReadLocker lock(&s->m_lock);
	return s->m_cache.value(key).toString();
}

#define X(type, Name, key, def) \
	void Settings::set_##Name(const type& v) { \
		{ \
			QWriteLocker lock(&m_lock); \
			if (m_cache.value(QStringLiteral(key)).value<type>() == v) return; \
			m_cache.insert(QStringLiteral(key), QVariant::fromValue<type>(v)); \
		} \
		persist(key, QVariant::fromValue<type>(v)); \
		TopConfig::instance()->set_##Name(v); \
		emit Name##Changed(); \
	}
	SETTINGS_DSL
#undef X

//? Standalone setter — same cache/persist/emit shape as the DSL setters,
//? but NO TopConfig forwarding (display-only pref, unknown to the library)
void Settings::set_netHideDocker(const bool& v) {
	{
		QWriteLocker lock(&m_lock);
		if (m_cache.value(QStringLiteral("net_hide_docker")).value<bool>() == v) return;
		m_cache.insert(QStringLiteral("net_hide_docker"), QVariant::fromValue<bool>(v));
	}
	persist("net_hide_docker", QVariant::fromValue<bool>(v));
	emit netHideDockerChanged();
}