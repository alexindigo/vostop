/*
 * vostop — palette singleton (implementation).
 */
#include "Theme.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QStyleHints>

Theme::Theme(QObject* parent)
	: QObject(parent) {
	//? Palette follows the OS color scheme (xdg-desktop-portal appearance);
	//? repaints when the portal flips it
	connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &Theme::themeChanged);
}

Theme& Theme::instance() {
	static Theme inst;
	return inst;
}

Theme* Theme::create(QQmlEngine* engine, QJSEngine* jsEngine) {
	Q_UNUSED(engine);
	Q_UNUSED(jsEngine);
	Theme* obj = &instance();
	QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
	return obj;
}

bool Theme::dark() const {
	//? No portal answer (Unknown) keeps the btop-style dark default
	return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

QColor Theme::windowBg() const { return dark() ? QColor(0x1a, 0x1a, 0x1a) : QColor(0xf2, 0xf2, 0xf4); }
QColor Theme::cardBg() const { return dark() ? QColor(0x22, 0x22, 0x22) : QColor(0xff, 0xff, 0xff); }
QColor Theme::cardBgAlt() const { return dark() ? QColor(0x26, 0x26, 0x26) : QColor(0xf7, 0xf7, 0xf9); }
QColor Theme::cardBorder() const { return dark() ? QColor(0x44, 0x44, 0x44) : QColor(0xc9, 0xc9, 0xcf); }
QColor Theme::innerBg() const { return dark() ? QColor(0x33, 0x33, 0x33) : QColor(0xe4, 0xe4, 0xe8); }
QColor Theme::detailBg() const { return dark() ? QColor(0x1a, 0x1a, 0x1a) : QColor(0xec, 0xec, 0xef); }
QColor Theme::text() const { return dark() ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x1c, 0x1c, 0x1e); }
QColor Theme::textBright() const { return dark() ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x11, 0x11, 0x11); }
QColor Theme::textDim() const { return dark() ? QColor(0xaa, 0xaa, 0xaa) : QColor(0x55, 0x55, 0x55); }
QColor Theme::textFaint() const { return dark() ? QColor(0x88, 0x88, 0x88) : QColor(0x6b, 0x6b, 0x70); }
QColor Theme::textGhost() const { return dark() ? QColor(0x66, 0x66, 0x66) : QColor(0x9a, 0x9a, 0xa0); }
QColor Theme::textDead() const { return dark() ? QColor(0x55, 0x55, 0x55) : QColor(0xb0, 0xb0, 0xb5); }
QColor Theme::accentCpu() const { return QColor(0x4f, 0xc3, 0xf7); }
QColor Theme::accentMem() const { return QColor(0x81, 0xc7, 0x84); }
QColor Theme::accentGpu() const { return QColor(0xce, 0x93, 0xd8); }
QColor Theme::accentWarn() const { return QColor(0xc9, 0xa2, 0x27); }
QColor Theme::accentHot() const { return QColor(0xff, 0xb7, 0x4d); }
QColor Theme::accentDanger() const { return QColor(0xe5, 0x73, 0x73); }
QColor Theme::selection() const { return dark() ? QColor(0x33, 0x40, 0x4d) : QColor(0xd6, 0xe7, 0xf5); }

double Theme::heatAlpha(double v) const {
	const double c = v < 0.0 ? 0.0 : (v > 100.0 ? 100.0 : v);
	return 0.15 + 0.75 * (c / 100.0);
}