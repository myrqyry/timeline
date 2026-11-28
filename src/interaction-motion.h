#pragma once

#include <QApplication>
#include <QPointF>
#include <QStyle>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <functional>
#include <memory>
#include <utility>

// Paint-only motion. Qt's unified animation clock wakes only active animations.
// The target is never written back to an input control or OBS.
class InteractionMotion {
public:
	explicit InteractionMotion(std::function<void()> repaint, QPointF initial = {})
		: repaint(std::move(repaint)),
		  current(initial),
		  target(initial)
	{
	}

	QPointF value() const { return current; }
	void snap() { to(target, false); }

	void to(QPointF next, bool animate)
	{
		if (next == target && (animate || current == target))
			return;
		target = next;
		if (animation)
			animation->stop();
		if (!animate || current == target) {
			assign(target);
			return;
		}
		const int duration = QApplication::style()->styleHint(QStyle::SH_Widget_Animation_Duration);
		if (duration <= 0) {
			assign(target);
			return;
		}
		if (!animation) {
			animation = std::make_unique<QVariantAnimation>();
			animation->setEasingCurve(QEasingCurve::OutCubic);
			QObject::connect(animation.get(), &QVariantAnimation::valueChanged, animation.get(),
					 [this](const QVariant &v) { assign(v.toPointF()); });
		}
		{
			const QSignalBlocker block(animation.get());
			animation->setDuration(qMin(120, duration));
			animation->setStartValue(current);
			animation->setEndValue(target);
			animation->setCurrentTime(0);
		}
		animation->start();
	}

private:
	void assign(QPointF value)
	{
		if (current == value)
			return;
		current = value;
		repaint();
	}

	std::function<void()> repaint;
	QPointF current;
	QPointF target;
	std::unique_ptr<QVariantAnimation> animation;
};
