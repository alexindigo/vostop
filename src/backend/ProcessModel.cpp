/*
 * vostop — ProcessModel (implementation). QAbstractTableModel: per-column
 * delegates in QML; role-based access kept for the proxy's filter core.
 */
#include "ProcessModel.h"

#include "CollectorWorker.h"
#include "../collect/proc.h"

#include <QLoggingCategory>
#include <QQmlEngine>
#include <algorithm>
#include <signal.h>
#include <cerrno>
#include <cstring>

Q_LOGGING_CATEGORY(vostopModel, "vostop.model")

ProcessModel::ProcessModel(QObject* parent)
	: QAbstractTableModel(parent) {}

ProcessModel& ProcessModel::instance() {
	static ProcessModel inst;
	return inst;
}

ProcessModel* ProcessModel::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	ProcessModel* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

int ProcessModel::rowCount(const QModelIndex& parent) const {
	return parent.isValid() ? 0 : m_rows.size();
}

int ProcessModel::columnCount(const QModelIndex& parent) const {
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant ProcessModel::data(const QModelIndex& index, int role) const {
	if (not index.isValid() or index.row() >= m_rows.size())
		return {};
	const auto& r = m_rows.at(index.row());

	//? Role-based access (proxy filter core); falls through to per-column for DisplayRole
	if (role != Qt::DisplayRole) {
		switch (role) {
		case PidRole: return static_cast<qulonglong>(r.pid);
		case NameRole: return r.name;
		case CmdRole: return r.cmd;
		case CpuPctRole: return r.cpuPct;
		case MemBytesRole: return static_cast<qulonglong>(r.memBytes);
		case UserRole: return r.user;
		case ThreadsRole: return static_cast<qulonglong>(r.threads);
		case StateRole: return QChar(r.state);
		case PpidRole: return static_cast<qulonglong>(r.ppid);
		case IoReadRateRole: return r.ioReadRate;
		case IoWriteRateRole: return r.ioWriteRate;
		case IoKnownRole: return r.ioKnown;
		case CategoryRole: return r.category;
		}
		return {};
	}

	//? Per-column display
	switch (index.column()) {
	case NameCol: return r.name;
	case CpuCol: return r.cpuPct;
	case MemCol: return static_cast<qulonglong>(r.memBytes);
	case UserCol: return r.user;
	case ThreadsCol: return static_cast<qulonglong>(r.threads);
	case StateCol: return QChar(r.state);
	case PpidCol: return static_cast<qulonglong>(r.ppid);
	}
	return {};
}

QVariant ProcessModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (orientation != Qt::Horizontal or role != Qt::DisplayRole)
		return {};
	switch (section) {
	case NameCol: return tr("name");
	case CpuCol: return tr("cpu%");
	case MemCol: return tr("memory");
	case UserCol: return tr("user");
	case ThreadsCol: return tr("thr");
	case StateCol: return tr("state");
	case PpidCol: return tr("ppid");
	}
	return {};
}

QHash<int, QByteArray> ProcessModel::roleNames() const {
	return {
		{Qt::DisplayRole, "display"},
		{PidRole, "pid"},
		{NameRole, "name"},
		{CmdRole, "cmd"},
		{CpuPctRole, "cpuPct"},
		{MemBytesRole, "memBytes"},
		{UserRole, "user"},
		{ThreadsRole, "threads"},
		{StateRole, "state"},
		{PpidRole, "ppid"},
		{IoReadRateRole, "ioReadRate"},
		{IoWriteRateRole, "ioWriteRate"},
		{IoKnownRole, "ioKnown"},
		{CategoryRole, "category"},
	};
}

void ProcessModel::setSelectedPid(quint64 pid) {
	if (m_selectedPid == pid)
		return;
	m_selectedPid = pid;
	//? Drive the stolen _collect_details target (atomic — worker thread reads it)
	Proc::detailed_pid = static_cast<size_t>(pid);
	emit selectedPidChanged();
	if (pid != 0)
		requestOpenFiles(pid);
}

void ProcessModel::update(const ProcSnapshot& snapshot) {
	//? pid-keyed diff: insert/remove only the difference, dataChanged for changed roles
	QHash<quint64, int> newRowByPid;
	newRowByPid.reserve(snapshot.rows.size());
	for (int i = 0; i < snapshot.rows.size(); ++i)
		newRowByPid.insert(snapshot.rows.at(i).pid, i);

	//? Removals (iterate descending so indices stay valid)
	for (int i = m_rows.size() - 1; i >= 0; --i) {
		if (not newRowByPid.contains(m_rows.at(i).pid)) {
			beginRemoveRows(QModelIndex(), i, i);
			const quint64 gone = m_rows.at(i).pid;
			m_rows.removeAt(i);
			m_rowByPid.remove(gone);
			//? Reindex rows after the removed one
			for (int j = i; j < m_rows.size(); ++j)
				m_rowByPid[m_rows.at(j).pid] = j;
			endRemoveRows();
		}
	}

	//? Insertions and updates
	for (int ni = 0; ni < snapshot.rows.size(); ++ni) {
		const auto& incoming = snapshot.rows.at(ni);
		const int oldRow = m_rowByPid.value(incoming.pid, -1);
		if (oldRow == -1) {
			//? Insert new pid in scan order (proxy owns presentation order)
			const int insertAt = m_rows.size();
			beginInsertRows(QModelIndex(), insertAt, insertAt);
			m_rows.append(incoming);
			m_rowByPid.insert(incoming.pid, insertAt);
			endInsertRows();
		} else {
			const auto& current = m_rows.at(oldRow);
			if (current.name != incoming.name or current.cmd != incoming.cmd
				or current.user != incoming.user or current.memBytes != incoming.memBytes
				or qFuzzyCompare(current.cpuPct, incoming.cpuPct) == false
				or current.threads != incoming.threads or current.state != incoming.state
				or current.ppid != incoming.ppid or current.nice != incoming.nice
				or qFuzzyCompare(current.ioReadRate, incoming.ioReadRate) == false
				or qFuzzyCompare(current.ioWriteRate, incoming.ioWriteRate) == false
				or current.ioKnown != incoming.ioKnown or current.category != incoming.category) {
				m_rows[oldRow] = incoming;
				const QModelIndex tl = index(oldRow, 0);
				const QModelIndex br = index(oldRow, ColumnCount - 1);
				emit dataChanged(tl, br);
			}
		}
	}

	if (m_numpids != snapshot.numpids or m_totalProcs != snapshot.totalProcs
		or m_threadsTotal != snapshot.threadsTotal) {
		m_numpids = snapshot.numpids;
		m_totalProcs = snapshot.totalProcs;
		m_threadsTotal = snapshot.threadsTotal;
		emit totalsChanged();
	}
}

void ProcessModel::detailUpdated(const ProcDetailSnapshot& detail) {
	m_detail = detail;
	emit detailChanged();
}

void ProcessModel::openFilesUpdated(const OpenFilesSnapshot& openFiles) {
	if (openFiles.pid != m_selectedPid)
		return;
	m_openFiles = openFiles;
	emit openFilesChanged();
}

bool ProcessModel::killProcess(quint64 pid, int signal) {
	//? Own-user processes only (plan.md §9 Q1): EPERM surfaces as false → UI keeps scope honest
	const int rc = ::kill(static_cast<pid_t>(pid), signal);
	if (rc != 0)
		qCWarning(vostopModel) << "kill(" << pid << "," << signal << ") failed:" << strerror(errno);
	return rc == 0;
}

bool ProcessModel::renice(quint64 pid, int priority) {
	return Proc::set_priority(static_cast<pid_t>(pid), priority);
}

void ProcessModel::requestOpenFiles(quint64 pid) {
	QMetaObject::invokeMethod(CollectorWorker::instance(), "gatherOpenFiles",
		Qt::QueuedConnection, Q_ARG(quint64, pid));
}