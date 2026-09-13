/*
 * vostop — palette singleton: follows the OS color scheme via the
 * xdg-desktop-portal appearance setting (QStyleHints::colorScheme);
 * Unknown keeps the btop-style dark default. All cards read these;
 * HistoryGraph grid/labels follow the palette.
 */
#pragma once

#include <QObject>
#include <QColor>
#include <QtQmlIntegration/qqmlintegration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class Theme : public QObject {
	Q_OBJECT
	QML_SINGLETON
	QML_ELEMENT
	Q_PROPERTY(bool dark READ dark NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor windowBg READ windowBg NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor cardBg READ cardBg NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor cardBgAlt READ cardBgAlt NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor cardBorder READ cardBorder NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor innerBg READ innerBg NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor detailBg READ detailBg NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor text READ text NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor textBright READ textBright NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor textDim READ textDim NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor textFaint READ textFaint NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor textGhost READ textGhost NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor textDead READ textDead NOTIFY themeChanged FINAL)
	Q_PROPERTY(QColor accentCpu READ accentCpu CONSTANT FINAL)
	Q_PROPERTY(QColor accentMem READ accentMem CONSTANT FINAL)
	Q_PROPERTY(QColor accentGpu READ accentGpu CONSTANT FINAL)
	Q_PROPERTY(QColor accentWarn READ accentWarn CONSTANT FINAL)
	Q_PROPERTY(QColor accentHot READ accentHot CONSTANT FINAL)
	Q_PROPERTY(QColor accentDanger READ accentDanger CONSTANT FINAL)
	Q_PROPERTY(QColor selection READ selection NOTIFY themeChanged FINAL)

public:
	static Theme& instance();
	static Theme* create(QQmlEngine* engine, QJSEngine* jsEngine);

	bool dark() const;
	QColor windowBg() const;
	QColor cardBg() const;
	QColor cardBgAlt() const;
	QColor cardBorder() const;
	QColor innerBg() const;
	QColor detailBg() const;
	QColor text() const;
	QColor textBright() const;
	QColor textDim() const;
	QColor textFaint() const;
	QColor textGhost() const;
	QColor textDead() const;
	QColor accentCpu() const;
	QColor accentMem() const;
	QColor accentGpu() const;
	QColor accentWarn() const;
	QColor accentHot() const;
	QColor accentDanger() const;
	QColor selection() const;

	//* Heat-map ramp for cpu% cells (alpha follows value)
	Q_INVOKABLE double heatAlpha(double v) const;

signals:
	void themeChanged();

private:
	explicit Theme(QObject* parent = nullptr);
};