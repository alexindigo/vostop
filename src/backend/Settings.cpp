/*
 * vostop — Settings singleton (implementation).
 */
#include "Settings.h"

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
	m_lock.unlock();
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
		emit Name##Changed(); \
	}
SETTINGS_DSL
#undef X