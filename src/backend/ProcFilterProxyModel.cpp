/*
 * vostop — ProcFilterProxyModel (implementation).
 */
#include "ProcFilterProxyModel.h"

#include "ProcessModel.h"
#include "../collect/proc.h"

ProcFilterProxyModel::ProcFilterProxyModel(QObject* parent)
	: QSortFilterProxyModel(parent) {
	sort(ProcessModel::CpuCol, Qt::DescendingOrder); //? "cpu lazy" → cpu-descending default
}

void ProcFilterProxyModel::setFilter(const QString& filter) {
	if (m_filter == filter)
		return;
	m_filter = filter;
	setFilterFixedString(filter); //? triggers re-filter; filterAcceptsRow does the matching
	emit filterChanged();
}

void ProcFilterProxyModel::sortBy(int column, bool descending) {
	setSortRole(Qt::DisplayRole);
	sort(column, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

bool ProcFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
	if (m_filter.isEmpty())
		return true;
	const auto model = qobject_cast<const ProcessModel*>(sourceModel());
	if (not model)
		return false;
	const QModelIndex idx = model->index(sourceRow, 0, sourceParent);
	const auto pid = model->data(idx, ProcessModel::PidRole).toULongLong();
	const auto name = model->data(idx, ProcessModel::NameRole).toString();
	const auto cmd = model->data(idx, ProcessModel::CmdRole).toString();
	const auto user = model->data(idx, ProcessModel::UserRole).toString();
	return Proc::matches_filter_row(pid, name.toStdString(), cmd.toStdString(),
		user.toStdString(), m_filter.toStdString());
}

bool ProcFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
	const auto model = qobject_cast<const ProcessModel*>(sourceModel());
	if (not model)
		return false;
	const QVariant lv = model->data(left, Qt::DisplayRole);
	const QVariant rv = model->data(right, Qt::DisplayRole);
	//? Numeric columns compare numerically; text columns case-insensitively
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