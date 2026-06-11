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

    // Type and icon
    void setEventType(TimelineEventType type);
    void setCustomIconPath(const QString &path);
    void setTimeMs(double timeMs);

    // Label
    void setEventLabel(const QString &label);
    QString getEventLabel() const { return eventLabel; }

    // Stacking offset
    void setStackOffset(int offset);
    int getStackOffset() const { return stackOffset; }

    // Snap to grid
    void setSnapEnabled(bool enabled);
    void setSnapInterval(double intervalMs);
    void setEventId(const QString &id);
    QString getEventId() const;

    // Suppress drag signals during programmatic moves
    void setSuppressDragSignals(bool suppress) { suppressDragSignals = suppress; }

    // Compact mode: shrink the bubble to match toolbar button size
    void setCompactMode(bool enabled);
    bool isCompactMode() const { return compactMode; }

    // Theme colors
    void setBorderColor(const QColor &color);
    void setSelectionColor(const QColor &color);
    void setBackgroundColor(const QColor &color);

    // Label pill colors (pushed from dock so labels follow OBS theme text/base)
    void setLabelColors(const QColor &textColor, const QColor &bgColor);

private:
    void refreshRenderer();

    QRectF boundingRect() const override;

signals:
    void eventMoved(double newTimeMs);
    void dragStarted();
    void dragMoved(double timeMs);
    void doubleClicked();
    void deleteRequested();
    void colorChangeRequested();
    
protected:
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
	void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
	void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
	QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    double pixelsPerSecond;
    double maxTimeMs = -1.0; // -1 = unlimited
    QColor eventColor;
    QString customIconPath;
    QString eventId;
    QString eventLabel;
    TimelineEventType eventType;
    int stackOffset = 0;
    double baseY = 30.0;
    static constexpr double ITEM_SIZE = 32.0;
    static constexpr double COMPACT_SIZE = 22.0;
    double mItemSize = ITEM_SIZE;
    bool compactMode = false;
    
    // Snap settings
    bool snapEnabled = false;
    double snapIntervalMs = 1000.0; // Default 1 second

	// Drag tracking
	bool isBeingDragged = false;
	bool dragStartedEmitted = false;
	bool suppressDragSignals = false;

	// Hover tracking
	bool isHovered = false;

    // Duration display
    bool hasDuration = false;
    double durationMs = 0.0;

    // Theme colors
    QColor borderColor = Qt::black;
    QColor selectionColor = QColor(255, 200, 0);
    QColor backgroundColor = Qt::white;

    // Label pill theme colors (pushed from dock to follow OBS palette)
    QColor labelTextColor;
    QColor labelBgColor;

    QSvgRenderer *renderer = nullptr;
};
