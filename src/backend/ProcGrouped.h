/*
 * vostop — grouped process presentation (plan.md §9 Q7/Q8, approved):
 * flat source model + one category proxy per section (Apps/Background/System),
 * each sorting and filtering independently; GroupedProcModel flattens the
 * three proxies into one list for a ListView with sticky section headers.
 */
#pragma once

#include <QSortFilterProxyModel>
#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class ProcessModel;

//* Per-category proxy: category filter + independent sort/filter per section
class ProcCategoryProxy : public QSortFilterProxyModel {
	Q_OBJECT
	//? 0 Apps, 1 Background, 2 System
	Q_PROPERTY(int category READ category WRITE setCategory NOTIFY categoryChanged FINAL)
	//? Section-level filter (btop matches_filter core on pid/name/cmd/user)
	Q_PROPERTY(QString sectionFilter READ sectionFilter WRITE setSectionFilter NOTIFY sectionFilterChanged FINAL)

public:
	explicit ProcCategoryProxy(QObject* parent = nullptr);

	int category() const { return m_category; }
	void setCategory(int category);
	QString sectionFilter() const { return m_sectionFilter; }
	void setSectionFilter(const QString& filter);

	Q_INVOKABLE void sortBy(int column, bool descending);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
	bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

signals:
	void categoryChanged();
	void sectionFilterChanged();

private:
	int m_category = 0;
	QString m_sectionFilter;
};

//* Flattens the three category proxies into one ListView-friendly model;
//* collapsible sections with translated (never reset) row updates per tick.
class GroupedProcModel : public QAbstractListModel {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//* Collapsed state per category (0/1/2); persisted via Settings (phase 6)
	Q_PROPERTY(bool appsCollapsed READ appsCollapsed WRITE setAppsCollapsed NOTIFY collapsedChanged FINAL)
	Q_PROPERTY(bool backgroundCollapsed READ backgroundCollapsed WRITE setBackgroundCollapsed NOTIFY collapsedChanged FINAL)
	Q_PROPERTY(bool systemCollapsed READ systemCollapsed WRITE setSystemCollapsed NOTIFY collapsedChanged FINAL)

public:
	enum { CategoryRole = Qt::UserRole + 100 }; //? section.property role (0/1/2)

	explicit GroupedProcModel(QObject* parent = nullptr);

	static GroupedProcModel& instance();
	static GroupedProcModel* create(QQmlEngine* engine, QJSEngine* jsEngine);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	bool appsCollapsed() const { return m_collapsed[0]; }
	void setAppsCollapsed(bool c) { setCollapsed(0, c); }
	bool backgroundCollapsed() const { return m_collapsed[1]; }
	void setBackgroundCollapsed(bool c) { setCollapsed(1, c); }
	bool systemCollapsed() const { return m_collapsed[2]; }
	void setSystemCollapsed(bool c) { setCollapsed(2, c); }

	//* Section header controls target these proxies
	Q_INVOKABLE ProcCategoryProxy* proxy(int category) const;

public slots:
	void rebuild(); //? full remap (sort/filter/collapse/source-structure changes)

signals:
	void collapsedChanged();

private:
	void setCollapsed(int category, bool c);

	ProcCategoryProxy* m_proxies[3];
	bool m_collapsed[3] = {false, false, false};
	//* display row → (category, proxy row)
	struct Entry {
		int category;
		int proxyRow;
	};
	QList<Entry> m_rows;
};