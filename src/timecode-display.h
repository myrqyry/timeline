#pragma once

#include <QWidget>

class TimecodeDisplay : public QWidget {
	Q_OBJECT

public:
	explicit TimecodeDisplay(QWidget *parent = nullptr);

	void setTimeMs(double timeMs);
	void setPlaying(bool playing);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	double mTimeMs = 0.0;
	bool mPlaying = false;
};
