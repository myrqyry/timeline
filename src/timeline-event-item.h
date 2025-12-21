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
    void doubleClicked();
    void deleteRequested();

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    double pixelsPerSecond;
};
