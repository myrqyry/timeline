#include "timecode-display.h"
#include "theme-helpers.h"

#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPalette>
#include <QApplication>
#include <QSvgRenderer>

namespace {
constexpr int kHeight = 36;
constexpr int kPaddingX = 10;
constexpr int kPaddingY = 4;
}

TimecodeDisplay::TimecodeDisplay(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(kHeight);
	setMinimumWidth(160);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	// Prefer a real monospace; fall back to default
	QFont mono;
	const QStringList families = QFontDatabase::families();
	const QStringList prefs = {"Cascadia Mono PL", "Cascadia Mono", "Cascadia Code",
				   "JetBrains Mono", "Fira Code", "Source Code Pro",
				   "Ubuntu Mono", "DejaVu Sans Mono", "Liberation Mono",
				   "Consolas", "Courier New", "monospace"};
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

QSize TimecodeDisplay::sizeHint() const
{
	// Compute width based on the actual digit rendering logic so the box is wide enough
	// and doesn't clip the numbers.
	int h = kHeight;
	double dh = h * 0.80;
	double dw = dh;
	double da = dw * 0.82;
	double sw = dw * 0.28;
	int content = da * 7 + sw * 2;
	int w = content + 50; // room for status dot, margins, padding, and the box itself
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
	p.drawRoundedRect(r, 9.0, 9.0);

	// More prominent border
	QColor border = mid;
	border.setAlpha(200);
	p.setPen(QPen(border, 1.5));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 9.0, 9.0);

	// Current color for the numbers - use the centralized OBS-theme-aware resolver
	QColor textColor = ThemeIcons::iconColorFor(this);
	QColor accent = palette().color(QPalette::Highlight);
	if (mPlaying) accent = accent.lighter(118);
	QString colorName = textColor.name();

	// Build digits
	int totalSeconds = static_cast<int>(mTimeMs / 1000.0);
	int msPart = static_cast<int>(mTimeMs) % 1000;
	int minutes = totalSeconds / 60;
	int seconds = totalSeconds % 60;

	int digits[7] = {
		minutes / 10, minutes % 10,
		seconds / 10, seconds % 10,
		msPart / 100, (msPart / 10) % 10, msPart % 10
	};

	// Make the numbers use up more of the box (taller digits)
	double digitH = r.height() * 0.80;
	double digitW = digitH;   // keep square to match the icon design

	// Tighter horizontal packing so characters aren't spread apart
	double digitAdvance = digitW * 0.82;
	double sepW = digitW * 0.28;

	// Total width of the 7 digits + separators between groups
	// Groups: MM   :   SS   .   MMM
	double totalW = digitAdvance * 7 + sepW * 2;

	double startX = (r.width() - totalW) / 2.0;
	double y = r.center().y() - digitH / 2.0;

	// Status dot on the left (fixed, doesn't affect centering of the big numbers)
	const int dotR = 4;
	const int dotLeft = 11;
	const int dotX = dotLeft + dotR;
	const int dotY = r.center().y();

	QColor dotColor = mPlaying ? QColor(72, 210, 100) : mid;
	if (mPlaying) {
		QColor glow = dotColor;
		glow.setAlpha(85);
		p.setPen(Qt::NoPen);
		p.setBrush(QBrush(glow));
		p.drawEllipse(QPointF(dotX, dotY), dotR + 5, dotR + 5);
	}
	p.setPen(Qt::NoPen);
	p.setBrush(QBrush(dotColor));
	p.drawEllipse(QPointF(dotX, dotY), dotR, dotR);

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
			p.setPen(QPen( (i==1 ? textColor : accent) , 2.0, Qt::SolidLine, Qt::RoundCap));
			double cy = r.center().y();
			if (i == 1) {
				// colon ":"
				double off = digitH * 0.22;
				p.drawPoint(QPointF(x + sepW*0.5, cy - off));
				p.drawPoint(QPointF(x + sepW*0.5, cy + off));
			} else {
				// dot "."
				p.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::RoundCap));
				p.drawPoint(QPointF(x + sepW*0.5, cy));
			}
			x += sepW + 1;
		}
	}
}
