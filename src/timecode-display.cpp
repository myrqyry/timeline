#include "timecode-display.h"
#include "theme-helpers.h"

#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPalette>
#include <QApplication>
#include <QSvgRenderer>

TimecodeDisplay::TimecodeDisplay(QWidget *parent) : QWidget(parent)
{
	setObjectName("OBSAudioMeter");
	setMinimumHeight(28);
	setMinimumWidth(100);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	// Prefer a real monospace; fall back to default
	QFont mono;
	const QStringList families = QFontDatabase::families();
	const QStringList prefs = {"Cascadia Mono PL", "Cascadia Mono",   "Cascadia Code", "JetBrains Mono",
				   "Fira Code",        "Source Code Pro", "Ubuntu Mono",   "DejaVu Sans Mono",
				   "Liberation Mono",  "Consolas",        "Courier New",   "monospace"};
	for (const QString &name : prefs) {
		if (families.contains(name)) {
			mono.setFamily(name);
			break;
		}
	}
	mono.setPointSize(22);
	mono.setStyleHint(QFont::Monospace);
	mono.setBold(true);
	setFont(mono);
}

void TimecodeDisplay::setCompactMode(bool compact)
{
	if (mCompact == compact)
		return;
	mCompact = compact;
	updateGeometry();
	update();
}

QSize TimecodeDisplay::sizeHint() const
{
	// Compute width based on the actual digit rendering logic so the box is wide enough
	// and doesn't clip the numbers.
	int h = mCompact ? 28 : 32;
	double dh = h * 0.80;
	double dw = dh;
	double da = dw * 0.82;
	double sw = dw * 0.28;
	int content = da * 7 + sw * 2;
	int w = content + 8; // ultra-tight, dot area removed
	return QSize(w, h);
}

QSize TimecodeDisplay::minimumSizeHint() const
{
	return sizeHint();
}

void TimecodeDisplay::setTimeMs(double timeMs)
{
	if (qFuzzyCompare(mTimeMs, timeMs))
		return;
	mTimeMs = timeMs;
	update();
}

void TimecodeDisplay::setPlaying(bool playing)
{
	if (mPlaying == playing)
		return;
	mPlaying = playing;
	update();
}

void TimecodeDisplay::setStatusColor(const QColor &color)
{
	if (mStatusColor == color)
		return;
	mStatusColor = color;
	update();
}

void TimecodeDisplay::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	QRectF r = rect();

	// Stronger recessed display look — bigger box
	QColor window = palette().color(QPalette::Window);
	QColor mid = palette().color(QPalette::Mid);

	QLinearGradient grad(r.topLeft(), r.bottomLeft());
	QColor top = window.darker(118);
	QColor bottom = window.darker(103);
	grad.setColorAt(0.0, top);
	grad.setColorAt(1.0, bottom);
	p.setPen(Qt::NoPen);
	p.setBrush(QBrush(grad));
	p.drawRoundedRect(r, 6.0, 6.0);

	// Indicate playing status via the thematic status color (e.g. meter green) or standard mid border
	QColor border = mPlaying ? mStatusColor : mid;
	if (mPlaying) {
		// Add a subtle glow for the status border
		QColor glow = border;
		glow.setAlpha(60);
		p.setPen(QPen(glow, 3.0));
		p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0), 6.0, 6.0);
	}

	p.setPen(QPen(border, 1.5));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);

	// Current color for the numbers - use the centralized OBS-theme-aware resolver
	QColor textColor = ThemeIcons::iconColorFor(this);
	QColor accent = palette().color(QPalette::Highlight);
	if (mPlaying)
		accent = accent.lighter(118);
	QString colorName = textColor.name();

	// Build digits
	int totalSeconds = static_cast<int>(mTimeMs / 1000.0);
	int msPart = static_cast<int>(mTimeMs) % 1000;
	int minutes = totalSeconds / 60;
	int seconds = totalSeconds % 60;

	int digits[7] = {minutes / 10, minutes % 10,       seconds / 10, seconds % 10,
			 msPart / 100, (msPart / 10) % 10, msPart % 10};

	// Make the numbers use up more of the box (taller digits)
	double digitH = r.height() * 0.80;
	double digitW = digitH; // keep square to match the icon design

	// Tighter horizontal packing so characters aren't spread apart
	double digitAdvance = digitW * 0.82;
	double sepW = digitW * 0.28;

	// Total width of the 7 digits + separators between groups
	// Groups: MM   :   SS   .   MMM
	double totalW = digitAdvance * 7 + sepW * 2;

	double startX = (r.width() - totalW) / 2.0;
	double y = r.center().y() - digitH / 2.0;

	// Draw the 7 digits using the moon bold number SVGs
	double x = startX;
	for (int i = 0; i < 7; ++i) {
		QByteArray svg = ThemeIcons::getMoonNumberSvg(digits[i], colorName);
		if (!svg.isEmpty()) {
			QSvgRenderer renderer(svg);
			renderer.render(&p, QRectF(x, y, digitW, digitH));
		}
		x += digitAdvance;

		// Draw separators after the 2nd and 4th digit
		if (i == 1 || i == 3) {
			// Draw a nice small vertical colon or dot separator in accent/dim color
			// If playing, we can also make the separators thematic for high visibility
			QColor sepColor = mPlaying ? mStatusColor : (i == 1 ? textColor : accent);
			p.setPen(QPen(sepColor, 2.0, Qt::SolidLine, Qt::RoundCap));
			double cy = r.center().y();
			if (i == 1) {
				// colon ":"
				double off = digitH * 0.22;
				p.drawPoint(QPointF(x + sepW * 0.5, cy - off));
				p.drawPoint(QPointF(x + sepW * 0.5, cy + off));
			} else {
				// dot "."
				p.setPen(QPen(sepColor, 2.5, Qt::SolidLine, Qt::RoundCap));
				p.drawPoint(QPointF(x + sepW * 0.5, cy));
			}
			x += sepW + 1;
		}
	}
}
