/*
 * vostop — CollectorWorker.
 *
 * Naturalized btop Runner loop: owns ALL stolen collector state on a worker
 * QThread; a poll QTimer (Settings-driven interval, default 1000 ms) drives
 * the stolen collectors every tick and emits queued snapshot signals to the
 * GUI thread. No collector code ever runs on the GUI thread.
 */
#pragma once

#include <QObject>
#include <QTimer>

#include "snapshots.h"
#include "../collect/psi.h"

class CollectorWorker : public QObject {
	Q_OBJECT

public:
	explicit CollectorWorker(QObject* parent = nullptr);

	//* Process-wide instance (models invoke open-files requests on it)
	static CollectorWorker* instance();

public slots:
	void start();   //? invoked via queued connection on the worker thread
	void stop();
	void gatherOpenFiles(quint64 pid);

signals:
	void cpuUpdated(const CpuSnapshot& snapshot);
	void memUpdated(const MemSnapshot& snapshot);
	void procUpdated(const ProcSnapshot& snapshot);
	void procDetailUpdated(const ProcDetailSnapshot& detail);
	void openFilesUpdated(const OpenFilesSnapshot& openFiles);
	void diskUpdated(const DiskSnapshot& snapshot);
	void netUpdated(const NetSnapshot& snapshot);

private slots:
	void tick();

private:
	QTimer m_timer;
	bool m_psiAvailable = false;
};