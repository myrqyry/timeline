#pragma once

#include <QGraphicsEllipseItem>
#include <QObject>
#include <QBrush>
#include <QPen>
#include <QColor>
#include <QString>
#include <QSvgRenderer>
#include "event-types.h"

class TimelineEventItem : public QObject, public QGraphicsEllipseItem {
    Q_OBJECT

public:
    TimelineEventItem(double timeMs, double pixelsPerSecond, TimelineEventType type,
                      QColor color = QColor(0, 200, 255), const QString &customIconPath = QString(), QGraphicsItem *parent = nullptr);
    
    // Limits
    void setMaxTime(double maxTimeMs);
    void setPixelsPerSecond(double pxPerSec);
    double getTimeMs() const;
    
    void setEventColor(const QColor &color);
    QColor getEventColor() const { return eventColor; }
    
    // Duration
    void setDuration(double durationMs);
    void setHasDuration(bool enabled);
    
    // Label
    void setEventLabel(const QString &label);
    QString getEventLabel() const { return eventLabel; }
    
    // Stacking offset
    void setStackOffset(int offset);
    int getStackOffset() const { return stackOffset; }
    
    // Snap to grid
    void setSnapEnabled(bool enabled);
    void setSnapInterval(double intervalMs);

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
    QString getTypeIcon() const;
    
    double pixelsPerSecond;
    QColor eventColor;
    QString customIconPath;
    QString eventLabel;
    TimelineEventType eventType;
    int stackOffset = 0;
    double baseY = 30.0;
    static constexpr double ITEM_SIZE = 32.0;
    
    // Snap settings
    bool snapEnabled = false;
    double snapIntervalMs = 1000.0; // Default 1 second
    
    // Duration display
    bool hasDuration = false;
    double durationMs = 0.0;
    
    QSvgRenderer *renderer = nullptr;
};

