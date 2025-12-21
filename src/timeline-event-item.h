#pragma once

#include <QGraphicsRectItem>
#include <QObject>
#include <QBrush>
#include <QPen>
#include <QColor>
#include <QString>

class TimelineEventItem : public QObject, public QGraphicsRectItem {
    Q_OBJECT

public:
    TimelineEventItem(double timeMs, double pixelsPerSecond, QColor color = QColor(0, 200, 255), QGraphicsItem *parent = nullptr);
    
    // Limits
    void setMaxTime(double maxTimeMs);
    void setPixelsPerSecond(double pxPerSec);
    double getTimeMs() const;
    
    // Color
    void setEventColor(const QColor &color);
    QColor getEventColor() const { return eventColor; }
    
    // Label
    void setEventLabel(const QString &label);
    QString getEventLabel() const { return eventLabel; }

signals:
    void eventMoved(double newTimeMs);
    void doubleClicked();
    void deleteRequested();
    void colorChangeRequested();

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    double pixelsPerSecond;
    QColor eventColor;
    QString eventLabel;
};
