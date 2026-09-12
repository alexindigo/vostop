/*
 * vostop — ProcessModel: delta-driven flat table over the stolen proc scan.
 *
 * QAbstractTableModel (per-column delegates in QML; a list model would render
 * the whole row inside every cell). Rows are keyed by pid; per tick the
 * snapshot is diffed against the previous one → insert/remove only the
 * difference, dataChanged for changed roles only (never a full reset —
 * selection and scroll survive by construction).
 */
#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

#include "snapshots.h"

class ProcessModel : public QAbstractTableModel {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	//? Stolen-scan accounting: total − filtered (status line)
	Q_PROPERTY(int numpids READ numpids NOTIFY totalsChanged FINAL)
	Q_PROPERTY(int totalProcs READ totalProcs NOTIFY totalsChanged FINAL)
	Q_PROPERTY(quint64 threadsTotal READ threadsTotal NOTIFY totalsChanged FINAL)
	//? Selection tracked by pid (survives re-sorts); 0 = none
	Q_PROPERTY(quint64 selectedPid READ selectedPid WRITE setSelectedPid NOTIFY selectedPidChanged FINAL)
	//? Detail readout for the selected pid (stolen _collect_details)
	Q_PROPERTY(bool detailValid READ detailValid NOTIFY detailChanged FINAL)
	Q_PROPERTY(QString detailName READ detailName NOTIFY detailChanged FINAL)
	Q_PROPERTY(QString detailStatus READ detailStatus NOTIFY detailChanged FINAL)
	Q_PROPERTY(QString detailElapsed READ detailElapsed NOTIFY detailChanged FINAL)
	Q_PROPERTY(QString detailParent READ detailParent NOTIFY detailChanged FINAL)
	Q_PROPERTY(quint64 detailPpid READ detailPpid NOTIFY detailChanged FINAL)
	Q_PROPERTY(quint64 detailMemBytes READ detailMemBytes NOTIFY detailChanged FINAL)
	Q_PROPERTY(quint64 detailThreads READ detailThreads NOTIFY detailChanged FINAL)
	Q_PROPERTY(QString detailState READ detailState NOTIFY detailChanged FINAL)
	//? Open files of the selected pid (read on selection, no polling)
	Q_PROPERTY(QStringList openFiles READ openFiles NOTIFY openFilesChanged FINAL)
	//? Per-process GPU column appears only when a backend probe succeeded (phase 5)
	Q_PROPERTY(bool gpuColumnVisible READ gpuColumnVisible NOTIFY gpuColumnChanged FINAL)

public:
	//* Columns (header + per-column delegates); ppid is a plain column (tree cut)
	enum Columns {
		NameCol = 0,
		CpuCol,
		MemCol,
		UserCol,
		ThreadsCol,
		StateCol,
		PpidCol,
		GpuCol, //? present only while gpuColumnVisible (dynamic column insert)
		ColumnCount,
	};
	Q_ENUM(Columns)

	//* Role-based access for the proxy filter (btop matches_filter core)
	enum Roles {
		PidRole = Qt::UserRole + 1,
		NameRole,
		CmdRole,
		CpuPctRole,
		MemBytesRole,
		UserRole,
		ThreadsRole,
		StateRole,
		PpidRole,
		IoReadRateRole,
		IoWriteRateRole,
		IoKnownRole,
		CategoryRole,
		GpuPctRole, //? per-process GPU busy% (−1 → "—")
	};
	Q_ENUM(Roles)

private:
	explicit ProcessModel(QObject* parent = nullptr);

public:

	//* Process-wide singleton instance (worker connects to it, QML displays it)
	static ProcessModel& instance();
	//* QML singleton provider — returns the same instance the worker feeds
	static ProcessModel* create(QQmlEngine* engine, QJSEngine* jsEngine);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	int numpids() const { return m_numpids; }
	int totalProcs() const { return m_totalProcs; }
	quint64 threadsTotal() const { return m_threadsTotal; }
	quint64 selectedPid() const { return m_selectedPid; }
	void setSelectedPid(quint64 pid);

	bool detailValid() const { return m_detail.valid and m_detail.pid == m_selectedPid; }
	QString detailName() const { return m_detail.name; }
	QString detailStatus() const { return m_detail.status; }
	QString detailElapsed() const { return m_detail.elapsed; }
	QString detailParent() const { return m_detail.parent; }
	quint64 detailPpid() const { return m_detail.ppid; }
	quint64 detailMemBytes() const { return m_detail.memBytes; }
	quint64 detailThreads() const { return m_detail.threads; }
	QString detailState() const { return QChar(m_detail.state); }
	QStringList openFiles() const { return m_openFiles.files; }
	bool gpuColumnVisible() const { return m_gpuColumnVisible; }

public slots:
	void update(const ProcSnapshot& snapshot);
	void detailUpdated(const ProcDetailSnapshot& detail);
	void openFilesUpdated(const OpenFilesSnapshot& openFiles);

	//* Actions (own-user processes only; plan.md §9 Q1)
	Q_INVOKABLE bool killProcess(quint64 pid, int signal);
	Q_INVOKABLE bool renice(quint64 pid, int priority);
	//* Request open-files read for a pid (worker reads, emits openFilesUpdated)
	Q_INVOKABLE void requestOpenFiles(quint64 pid);

signals:
	void totalsChanged();
	void selectedPidChanged();
	void detailChanged();
	void openFilesChanged();
	void gpuColumnChanged();

private:
	QList<ProcSnapshot::Row> m_rows;
	QHash<quint64, int> m_rowByPid;
	int m_numpids = 0;
	int m_totalProcs = 0;
	quint64 m_threadsTotal = 0;
	quint64 m_selectedPid = 0;

	ProcDetailSnapshot m_detail;
	OpenFilesSnapshot m_openFiles;
	bool m_gpuColumnVisible = false;
	double m_gpuTopPct = -1.0;
};