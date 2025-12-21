#pragma once

#include <QGraphicsRectItem>
#include <QObject>
#include <QBrush>
#include <QPen>

class TimelineEventItem : public QObject, public QGraphicsRectItem {
    Q_OBJECT

public:
    TimelineEventItem(double timeMs, double pixelsPerSecond, QGraphicsItem *parent = nullptr);
    
    // Limits
    void setMaxTime(double maxTimeMs);
    void setPixelsPerSecond(double pxPerSec);
    double getTimeMs() const;

signals:
    void eventMoved(double newTimeMs);

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    double pixelsPerSecond;
    double timeMs;
};
