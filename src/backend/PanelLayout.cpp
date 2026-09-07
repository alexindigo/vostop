/*
 * vostop — PanelLayout singleton: loads/validates ~/.config/vostop/layout.json
 * and exposes the sanitized panel tree to the QML layout engine. File-level
 * failure (missing/corrupt) falls back to the built-in default tree.
 * QFileSystemWatcher live-reloads on save: directory + file watched, re-armed
 * on change, reload only on real content change.
 */
#include "PanelLayout.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(vostopPanels, "vostop.panels")

#ifndef VOSTOP_PANELS_DIR
#define VOSTOP_PANELS_DIR "/usr/share/vostop/panels"
#endif

PanelLayout* PanelLayout::instance() {
	static PanelLayout inst;
	return &inst;
}

PanelLayout* PanelLayout::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	PanelLayout* obj = instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

PanelLayout::PanelLayout(QObject* parent)
	: QObject(parent)
	, m_builtInDir(QStringLiteral(VOSTOP_PANELS_DIR)) {
	const QString configBase = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
	m_configDir = configBase + QStringLiteral("/vostop");
	m_configFile = m_configDir + QStringLiteral("/layout.json");
	m_userDir = m_configDir + QStringLiteral("/panels");

	//? User panels dir created on startup if absent (spec §3)
	QDir().mkpath(m_configDir);
	QDir().mkpath(m_userDir);

	load();
	setupWatcher();
}

//? One Loader path for user panels and built-ins: user dir shadows built-ins
//? by design; `..`/separators rejected before resolution; the resolved
//? canonical path must stay under one of the two roots (spec §3)
QString PanelLayout::resolveSource(const QString& name) const {
	static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9_-]+$"));
	if (!identifier.match(name).hasMatch()) {
		qCWarning(vostopPanels) << "panel source rejected (not identifier-only):" << name;
		return {};
	}
	const QString file = name + QStringLiteral(".qml");

	auto resolveUnder = [](const QString& root, const QString& rel) -> QString {
		const QFileInfo rootInfo(root);
		const QString rootCanon = rootInfo.canonicalFilePath();
		if (rootCanon.isEmpty())
			return {};
		const QFileInfo fi(rootCanon + QLatin1Char('/') + rel);
		if (!fi.isFile())
			return {};
		const QString canon = fi.canonicalFilePath();
		if (canon.isEmpty() || !canon.startsWith(rootCanon + QLatin1Char('/')))
			return {};
		return canon;
	};

	QString path = resolveUnder(m_userDir, file);
	if (path.isEmpty())
		path = resolveUnder(m_builtInDir, file);
	if (path.isEmpty())
		qCWarning(vostopPanels) << "panel not found in" << m_userDir << "or" << m_builtInDir << ":" << name;
	return path;
}

QVariantMap PanelLayout::errorNode(const QString& msg) const {
	QVariantMap m;
	m[QStringLiteral("_invalid")] = true;
	m[QStringLiteral("_error")] = msg;
	//? Same default field set a sanitized node carries, so the QML delegate
	//? treats an invalid node like a default panel slot — otherwise nested
	//? error tiles get fillWidth: false + preferredWidth: 0 and collapse to
	//? zero size (invisible)
	m[QStringLiteral("direction")] = QStringLiteral("rows");
	m[QStringLiteral("gap")] = 0;
	m[QStringLiteral("grow")] = 1;
	m[QStringLiteral("minWidth")] = 0;
	m[QStringLiteral("minHeight")] = 0;
	return m;
}

QVariantMap PanelLayout::sanitizeNode(const QJsonValue& val) const {
	if (!val.isObject())
		return errorNode(QStringLiteral("invalid node: not an object"));

	const QJsonObject obj = val.toObject();
	const bool hasChildren = obj.contains(QStringLiteral("children"));
	const bool hasSource = obj.contains(QStringLiteral("source"));

	//? A node with both is invalid → error tile (spec §2)
	if (hasChildren && hasSource)
		return errorNode(QStringLiteral("node has both children and source"));

	QVariantMap out;

	//? Unknown keys ignored; direction default "rows"
	QString dir = QStringLiteral("rows");
	if (obj.contains(QStringLiteral("direction")) && obj.value(QStringLiteral("direction")).isString()) {
		const QString d = obj.value(QStringLiteral("direction")).toString();
		if (d == QStringLiteral("rows") || d == QStringLiteral("cols"))
			dir = d;
	}
	out[QStringLiteral("direction")] = dir;

	int gap = 0;
	if (obj.contains(QStringLiteral("gap")) && obj.value(QStringLiteral("gap")).isDouble())
		gap = obj.value(QStringLiteral("gap")).toInt();
	out[QStringLiteral("gap")] = gap;

	//? grow default 1 (panels fill by default); negative clamps to 0
	int grow = 1;
	if (obj.contains(QStringLiteral("grow")) && obj.value(QStringLiteral("grow")).isDouble()) {
		grow = obj.value(QStringLiteral("grow")).toInt();
		if (grow < 0)
			grow = 0;
	}
	out[QStringLiteral("grow")] = grow;

	int minW = 0;
	int minH = 0;
	if (obj.contains(QStringLiteral("minWidth")) && obj.value(QStringLiteral("minWidth")).isDouble())
		minW = obj.value(QStringLiteral("minWidth")).toInt();
	if (obj.contains(QStringLiteral("minHeight")) && obj.value(QStringLiteral("minHeight")).isDouble())
		minH = obj.value(QStringLiteral("minHeight")).toInt();
	out[QStringLiteral("minWidth")] = minW;
	out[QStringLiteral("minHeight")] = minH;

	if (hasChildren) {
		const QJsonValue childrenVal = obj.value(QStringLiteral("children"));
		if (!childrenVal.isArray())
			return errorNode(QStringLiteral("children is not an array"));
		const QJsonArray arr = childrenVal.toArray();
		QVariantList childrenOut;
		childrenOut.reserve(arr.size());
		for (const QJsonValue& c : arr)
			childrenOut.append(sanitizeNode(c));
		out[QStringLiteral("children")] = childrenOut;
		return out;
	}
	if (hasSource) {
		const QJsonValue srcVal = obj.value(QStringLiteral("source"));
		if (!srcVal.isString() || srcVal.toString().isEmpty())
			return errorNode(QStringLiteral("source is not a string"));
		out[QStringLiteral("source")] = srcVal.toString();
		return out;
	}
	//? Neither: renders as an empty gapless box
	out[QStringLiteral("_empty")] = true;
	return out;
}

//? Interim default = today's arrangement (design spec §2 example):
//? two card rows over the full-width process list with its 320px minimum.
QVariantMap PanelLayout::defaultTree() const {
	auto leaf = [](const QString& source) {
		QVariantMap m;
		m[QStringLiteral("source")] = source;
		m[QStringLiteral("gap")] = 0;
		m[QStringLiteral("grow")] = 1;
		m[QStringLiteral("minWidth")] = 0;
		m[QStringLiteral("minHeight")] = 0;
		return m;
	};
	auto row = [](const QVariantList& children) {
		QVariantMap m;
		m[QStringLiteral("direction")] = QStringLiteral("rows");
		m[QStringLiteral("gap")] = 8;
		m[QStringLiteral("grow")] = 1;
		m[QStringLiteral("minWidth")] = 0;
		m[QStringLiteral("minHeight")] = 0;
		m[QStringLiteral("children")] = children;
		return m;
	};

	QVariantMap top;
	top[QStringLiteral("direction")] = QStringLiteral("cols");
	top[QStringLiteral("gap")] = 8;
	top[QStringLiteral("grow")] = 1;
	top[QStringLiteral("minWidth")] = 0;
	top[QStringLiteral("minHeight")] = 0;

	QVariantMap proc = leaf(QStringLiteral("ProcessList"));
	proc[QStringLiteral("grow")] = 2;
	proc[QStringLiteral("minHeight")] = 320;

	top[QStringLiteral("children")] = QVariantList{
		row(QVariantList{leaf(QStringLiteral("CpuCard")), leaf(QStringLiteral("MemCard")), leaf(QStringLiteral("DiskCard"))}),
		row(QVariantList{leaf(QStringLiteral("NetCard")), leaf(QStringLiteral("GpuCard")), leaf(QStringLiteral("TempCard"))}),
		proc};
	return top;
}

void PanelLayout::load() {
	QString content;
	const bool fileExists = QFile::exists(m_configFile);
	if (fileExists) {
		QFile f(m_configFile);
		if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
			content = QString::fromUtf8(f.readAll());
			f.close();
		}
	}

	//? Reload only on real content change (atomic-save editors included)
	if (!m_lastContent.isNull() && content == m_lastContent)
		return;

	QVariantMap next;
	if (!fileExists || content.trimmed().isEmpty()) {
		qCDebug(vostopPanels) << "layout.json missing, using built-in default tree";
		next = defaultTree();
	} else {
		QJsonParseError err;
		QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
		if (err.error != QJsonParseError::NoError || !doc.isObject()) {
			//? Anything file-level invalid → built-in default tree (spec §2)
			qCWarning(vostopPanels) << "layout.json invalid:" << err.errorString() << "— using built-in default tree";
			next = defaultTree();
		} else {
			next = sanitizeNode(doc.object());
		}
	}

	m_lastContent = content;
	if (m_tree != next) {
		m_tree = next;
		emit treeChanged();
		logResolved();
	} else {
		logResolved();
	}
}

//? Verification aid, mirrors the worker tick-debug pattern: resolved panel
//* list per load, debug-gated under vostop.panels
void PanelLayout::logResolved() const {
	if (!vostopPanels().isDebugEnabled())
		return;
	QStringList sources;
	collectSources(m_tree, &sources);
	for (const QString& name : sources) {
		const QString path = resolveSource(name);
		qCDebug(vostopPanels) << "panel:" << name << "->" << (path.isEmpty() ? QStringLiteral("<unresolved>") : path);
	}
}

void PanelLayout::collectSources(const QVariantMap& node, QStringList* out) const {
	if (node.contains(QStringLiteral("source"))) {
		out->append(node.value(QStringLiteral("source")).toString());
		return;
	}
	const QVariantList children = node.value(QStringLiteral("children")).toList();
	for (const QVariant& child : children)
		collectSources(child.toMap(), out);
}

void PanelLayout::setupWatcher() {
	if (!m_watcher) {
		m_watcher = new QFileSystemWatcher(this);
		connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &PanelLayout::onFileChanged);
		connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &PanelLayout::onDirectoryChanged);
	}
	//? Re-arm: watch the directory (catches create/delete/atomic-rename of the
	//? file) and the file itself (catches in-place writes) — the standard pattern
	if (!m_watcher->directories().contains(m_configDir) && QDir(m_configDir).exists())
		m_watcher->addPath(m_configDir);
	if (QFile::exists(m_configFile)) {
		if (!m_watcher->files().contains(m_configFile))
			m_watcher->addPath(m_configFile);
	} else if (m_watcher->files().contains(m_configFile)) {
		m_watcher->removePath(m_configFile);
	}
}

void PanelLayout::onFileChanged(const QString& path) {
	Q_UNUSED(path);
	setupWatcher();
	load();
}

void PanelLayout::onDirectoryChanged(const QString& path) {
	Q_UNUSED(path);
	setupWatcher();
	load();
}
