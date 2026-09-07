#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QFileSystemWatcher>
#include <QtQmlIntegration/qqmlintegration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class PanelLayout : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_NAMED_ELEMENT(PanelLayoutBackend)
	Q_PROPERTY(QVariantMap tree READ tree NOTIFY treeChanged FINAL)
	Q_PROPERTY(QString builtInDir READ builtInDir CONSTANT FINAL)

public:
	static PanelLayout* instance();
	static PanelLayout* create(QQmlEngine* engine, QJSEngine* jsEngine);

	QVariantMap tree() const { return m_tree; }
	QString builtInDir() const { return m_builtInDir; }

	//* User dir first (shadows built-ins by design), then the CMake-baked
	//* dir; identifier-only names, canonical result must stay under a root
	Q_INVOKABLE QString resolveSource(const QString& name) const;

signals:
	void treeChanged();

private:
	explicit PanelLayout(QObject* parent = nullptr);
	void load();
	void setupWatcher();
	QVariantMap defaultTree() const;
	QVariantMap sanitizeNode(const QJsonValue& val) const;
	QVariantMap errorNode(const QString& msg) const;
	void logResolved() const;
	void collectSources(const QVariantMap& node, QStringList* out) const;

	void onFileChanged(const QString& path);
	void onDirectoryChanged(const QString& path);

	QFileSystemWatcher* m_watcher = nullptr;
	QString m_configFile;
	QString m_configDir;
	QString m_userDir;
	QString m_lastContent;
	QVariantMap m_tree;
	QString m_builtInDir;
};
