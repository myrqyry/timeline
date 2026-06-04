void TimelineDock::rebuildEventItems()
{
	blog(LOG_DEBUG, "[Timeline] rebuildEventItems() called (events: %d)", static_cast<int>(model->getEvents().size()));

	rebuildScheduled = false;
	if (isDragging) {
		rebuildScheduled = true;
		blog(LOG_DEBUG, "[Timeline] rebuildEventItems() skipped - currently dragging");
		return;
	}

	const auto &events = model->getEvents();

	// Build set of current IDs for stale-item removal
	QSet<QString> currentIds;
	currentIds.reserve(static_cast<int>(events.size()));

	// Track stacking - count events at same time (within 50ms tolerance)
	std::map<int, int> timeSlotCount;
	const double STACK_TOLERANCE_MS = 50.0;

	for (size_t i = 0; i < events.size(); i++) {
		const auto &evt = events[i];
		currentIds.insert(evt.id);

		int timeBucket = static_cast<int>(evt.timeMs / STACK_TOLERANCE_MS);
		int stackIndex = timeSlotCount[timeBucket]++;

		QColor borderColor = palette().color(QPalette::Text);
		QColor selectionColor = palette().color(QPalette::Highlight);
		QColor bgColor = palette().color(QPalette::Base);

		TimelineEventItem *item = eventItemMap.value(evt.id, nullptr);
		if (!item) {
			// Create new item
			item = new TimelineEventItem(evt.timeMs, pixelsPerSecond, evt.type, evt.color,
						     evt.customIconPath);
			item->setEventId(evt.id);
			item->setBorderColor(borderColor);
			item->setSelectionColor(selectionColor);
			item->setBackgroundColor(bgColor);
			scene->addItem(item);
			connectItemSignals(item, evt.id);
			eventItemMap[evt.id] = item;
		} else {
			// Update existing item properties
			item->setSuppressDragSignals(true);
			item->setPixelsPerSecond(pixelsPerSecond);
			item->setTimeMs(evt.timeMs);
			item->setStackOffset(stackIndex);
			item->setSuppressDragSignals(false);
			item->setEventType(evt.type);
			item->setEventColor(evt.color);
			item->setCustomIconPath(evt.customIconPath);
			item->setBorderColor(borderColor);
			item->setSelectionColor(selectionColor);
			item->setBackgroundColor(bgColor);
		}
		item->setStackOffset(stackIndex);

		item->setHasDuration(evt.hasDuration);
		item->setDuration(evt.durationMs);
		item->setSnapEnabled(snapEnabled);
		item->setSnapInterval(getSnapIntervalMs());

		// Tooltip and label
		QString typeStr;
		switch (evt.type) {
		case TimelineEventType::SceneSwitch:
			typeStr = "Scene Switch";
			break;
		case TimelineEventType::SourceShow:
			typeStr = "Source Show";
			break;
		case TimelineEventType::SourceHide:
			typeStr = "Source Hide";
			break;
		case TimelineEventType::MediaStart:
			typeStr = "Media Start";
			break;
		case TimelineEventType::MediaStop:
			typeStr = "Media Stop";
			break;
		case TimelineEventType::FilterEnable:
			typeStr = "Filter Enable";
			break;
		case TimelineEventType::FilterDisable:
			typeStr = "Filter Disable";
			break;
		case TimelineEventType::HotkeyTrigger:
			typeStr = "Hotkey Trigger";
			break;
		case TimelineEventType::TriggerTimeline:
			typeStr = "Trigger Timeline";
			break;
		default:
			typeStr = "Unknown";
			break;
		}
		double timeSec = evt.timeMs / 1000.0;
		int minutes = static_cast<int>(timeSec) / 60;
		int seconds = static_cast<int>(timeSec) % 60;
		int ms = static_cast<int>(evt.timeMs) % 1000;
		QString targetDisplay = evt.sourceName;
		if (!evt.filterName.isEmpty()) {
			targetDisplay += "::" + evt.filterName;
		}

		QString tooltip =
			QString("%1\nTarget: %2\nTime: %3:%4.%5\n\nDouble-click to edit | Right-click for options")
				.arg(typeStr)
				.arg(targetDisplay)
				.arg(minutes, 2, 10, QChar('0'))
				.arg(seconds, 2, 10, QChar('0'))
				.arg(ms / 10, 2, 10, QChar('0'));
		item->setToolTip(tooltip);
		item->setEventLabel(evt.label.isEmpty() ? evt.sourceName : evt.label);
	}

	// Remove items no longer in the model
	auto it = eventItemMap.begin();
	while (it != eventItemMap.end()) {
		if (!currentIds.contains(it.key())) {
			TimelineEventItem *item = it.value();
			if (item) {
				scene->removeItem(item);
				item->deleteLater();   // Safer Qt ownership - respects event loop
			}
			it = eventItemMap.erase(it);
		} else {
			++it;
		}
	}

	blog(LOG_DEBUG, "[Timeline] rebuildEventItems() complete (active items: %d)", eventItemMap.size());

	// Empty hint
	if (events.empty()) {
		if (!emptyHintItem) {
			emptyHintItem = scene->addSimpleText(
				"Double-click timeline or press Add Event to create your first event");
			QColor hintColor = palette().color(QPalette::Text);
			hintColor.setAlpha(128);
			emptyHintItem->setBrush(QBrush(hintColor));
			QFont hintFont = QApplication::font();
			hintFont.setPointSize(10);
			emptyHintItem->setFont(hintFont);
			emptyHintItem->setZValue(-5);
		}

		const double x = std::max(20.0, scene->sceneRect().width() * 0.1);
		emptyHintItem->setPos(x, 60);
		emptyHintItem->setVisible(true);
	} else if (emptyHintItem) {
		emptyHintItem->setVisible(false);
	}
}