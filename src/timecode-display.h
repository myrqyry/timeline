#pragma once

#include <QWidget>

class TimecodeDisplay : public QWidget {
	Q_OBJECT
	Q_PROPERTY(QColor greenColor READ greenColor WRITE setStatusColor)
	Q_PROPERTY(QColor yellowColor READ yellowColor WRITE setYellowColor)
	Q_PROPERTY(QColor redColor READ redColor WRITE setRedColor)

public:
	explicit TimecodeDisplay(QWidget *parent = nullptr);

	void setTimeMs(double timeMs);
	void setPlaying(bool playing);
	void setCompactMode(bool compact);
	void setStatusColor(const QColor &color);
	void setYellowColor(const QColor &color) { mYellowColor = color; update(); }
	void setRedColor(const QColor &color) { mRedColor = color; update(); }

	QColor greenColor() const { return mStatusColor; }
	QColor yellowColor() const { return mYellowColor; }
	QColor redColor() const { return mRedColor; }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	double mTimeMs = 0.0;
	bool mPlaying = false;
	bool mCompact = false;
	QColor mStatusColor = QColor(72, 210, 100);
	QColor mYellowColor = QColor(210, 210, 72);
	QColor mRedColor = QColor(210, 72, 72);
};
