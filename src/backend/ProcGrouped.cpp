/*
 * vostop — grouped process presentation (implementation).
 */
#include "ProcGrouped.h"

#include "ProcessModel.h"
#include "Settings.h"
#include "../collect/proc.h"

#include <QLoggingCategory>
#include <QQmlEngine>

Q_LOGGING_CATEGORY(vostopGrouped, "vostop.grouped")

ProcCategoryProxy::ProcCategoryProxy(QObject* parent)
	: QSortFilterProxyModel(parent) {}

void ProcCategoryProxy::setCategory(int category) {
	if (m_category == category)
		return;
	beginFilterChange();
	m_category = category;
	emit categoryChanged();
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProcCategoryProxy::setSectionFilter(const QString& filter) {
	if (m_sectionFilter == filter)
		return;
	m_sectionFilter = filter;
	setFilterFixedString(filter); //? filterAcceptsRow does the real matching
	emit sectionFilterChanged();
}

void ProcCategoryProxy::sortBy(int column, bool descending) {
	setSortRole(Qt::DisplayRole);
	sort(column, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

bool ProcCategoryProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
	const auto model = qobject_cast<const ProcessModel*>(sourceModel());
	if (not model)
		return false;
	const QModelIndex idx = model->index(sourceRow, 0, sourceParent);
	if (model->data(idx, ProcessModel::CategoryRole).toInt() != m_category)
		return false;
	if (m_sectionFilter.isEmpty())
		return true;
	const auto pid = model->data(idx, ProcessModel::PidRole).toULongLong();
	const auto name = model->data(idx, ProcessModel::NameRole).toString();
	const auto cmd = model->data(idx, ProcessModel::CmdRole).toString();
	const auto user = model->data(idx, ProcessModel::UserRole).toString();
	return Proc::matches_filter_row(pid, name.toStdString(), cmd.toStdString(),
		user.toStdString(), m_sectionFilter.toStdString());
}

bool ProcCategoryProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const {
	const auto model = qobject_cast<const ProcessModel*>(sourceModel());
	if (not model)
		return false;
	const QVariant lv = model->data(left, Qt::DisplayRole);
	const QVariant rv = model->data(right, Qt::DisplayRole);
	switch (left.column()) {
	case ProcessModel::CpuCol:
	case ProcessModel::MemCol:
	case ProcessModel::ThreadsCol:
	case ProcessModel::PpidCol:
		return lv.toDouble() < rv.toDouble();
	case ProcessModel::NameCol:
	case ProcessModel::UserCol:
		return lv.toString().compare(rv.toString(), Qt::CaseInsensitive) < 0;
	case ProcessModel::StateCol:
		return lv.toString() < rv.toString();
	}
	return QSortFilterProxyModel::lessThan(left, right);
}

GroupedProcModel::GroupedProcModel(QObject* parent)
	: QAbstractListModel(parent) {
	for (int i = 0; i < 3; ++i) {
		m_proxies[i] = new ProcCategoryProxy(this);
		m_proxies[i]->setCategory(i);
		m_proxies[i]->setSourceModel(&ProcessModel::instance());
	}
	//? Structure changes rebuild the mapping; per-tick dataChanged is translated
	//? (no full reset → no scroll jump)
	for (int i = 0; i < 3; ++i) {
		connect(m_proxies[i], &QAbstractItemModel::rowsInserted, this, &GroupedProcModel::rebuild);
		connect(m_proxies[i], &QAbstractItemModel::rowsRemoved, this, &GroupedProcModel::rebuild);
		connect(m_proxies[i], &QAbstractItemModel::dataChanged, this, [this, i](const QModelIndex& tl, const QModelIndex& br, const QVector<int>&) {
			Q_UNUSED(br);
			if (m_collapsed[i])
				return;
			//? tl/br are already proxy indexes (the proxy re-emits dataChanged mapped)
			const int d = tl.row();
			if (d < 0)
				return;
			int offset = 0;
			for (int c = 0; c < i; ++c)
				if (not m_collapsed[c])
					offset += m_proxies[c]->rowCount();
			emit dataChanged(index(offset + d), index(offset + d));
		});
		connect(m_proxies[i], &QAbstractItemModel::layoutChanged, this, &GroupedProcModel::rebuild);
		connect(m_proxies[i], &QAbstractItemModel::modelReset, this, &GroupedProcModel::rebuild);
	}
	//? Restore collapse persistence (phase 6)
	m_collapsed[0] = Settings::instance()->groupCollapsedApps();
	m_collapsed[1] = Settings::instance()->groupCollapsedBackground();
	m_collapsed[2] = Settings::instance()->groupCollapsedSystem();
	rebuild();
}

GroupedProcModel& GroupedProcModel::instance() {
	static GroupedProcModel inst;
	return inst;
}

GroupedProcModel* GroupedProcModel::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	GroupedProcModel* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

int GroupedProcModel::rowCount(const QModelIndex& parent) const {
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant GroupedProcModel::data(const QModelIndex& index, int role) const {
	if (not index.isValid() or index.row() >= m_rows.size())
		return {};
	if (role == CategoryRole)
		return m_rows.at(index.row()).category;
	const auto& e = m_rows.at(index.row());
	const QModelIndex src = m_proxies[e.category]->index(e.proxyRow, 0);
	return ProcessModel::instance().data(m_proxies[e.category]->mapToSource(src), role);
}

QHash<int, QByteArray> GroupedProcModel::roleNames() const {
	auto names = ProcessModel::instance().roleNames();
	names[CategoryRole] = "category";
	return names;
}

void GroupedProcModel::setCollapsed(int category, bool c) {
	if (m_collapsed[category] == c)
		return;
	m_collapsed[category] = c;
	auto* settings = Settings::instance();
	switch (category) {
	case 0: settings->set_groupCollapsedApps(c); break;
	case 1: settings->set_groupCollapsedBackground(c); break;
	case 2: settings->set_groupCollapsedSystem(c); break;
	}
	emit collapsedChanged();
	rebuild();
}

ProcCategoryProxy* GroupedProcModel::proxy(int category) const {
	return (category >= 0 and category < 3) ? m_proxies[category] : nullptr;
}

void GroupedProcModel::rebuild() {
	//? Full remap: only on structure changes (insert/remove/sort/filter/collapse),
	//? never on per-tick dataChanged → per-tick scrolling stays put.
	beginResetModel();
	m_rows.clear();
	for (int cat = 0; cat < 3; ++cat) {
		if (m_collapsed[cat])
			continue;
		const int n = m_proxies[cat]->rowCount();
		for (int r = 0; r < n; ++r)
			m_rows.append({cat, r});
	}
	endResetModel();
}