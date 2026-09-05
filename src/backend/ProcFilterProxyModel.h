/*
 * vostop — ProcFilterProxyModel: Qt-side sort/filter over ProcessModel.
 *
 * Filtering core is btop's matches_filter (substring on pid/name/cmd/user,
 * or !extended-regex); sorting follows Proc::sort_vector semantics per column
 * — "cpu lazy" maps to plain cpu-descending (proxy owns sort per the plan's
 * steal mapping).
 */
#pragma once

#include <QSortFilterProxyModel>
#include <QtQmlIntegration/qqmlintegration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class ProcFilterProxyModel : public QSortFilterProxyModel {
	Q_OBJECT
	QML_ELEMENT
	//? filterAcceptsRow core: btop matches_filter on pid/name/cmd/user
	Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)

public:
	explicit ProcFilterProxyModel(QObject* parent = nullptr);

	QString filter() const { return m_filter; }
	void setFilter(const QString& filter);

	//* Qt-side sort owner: proxy sorts by column in one call
	Q_INVOKABLE void sortBy(int column, bool descending);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
	bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

signals:
	void filterChanged();

private:
	QString m_filter;
};