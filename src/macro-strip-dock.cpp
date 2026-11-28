#include "macro-strip-dock.h"

#include "macro-button-widget.h"
#include "macro-parameter-widget.h"
#include "macro-serialization.h"
#include "obs-executor.h"
#include "event-editor.h"
#include "theme-helpers.h"

#include <obs-module.h>

#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTabBar>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>
#include <QCursor>
#include <memory>
#include <algorithm>
#include <functional>

namespace {
// A handle represents spacing owned by a control, not a user-created spacer.
// Only the handle receives layout gestures; the live widget is disabled.
class StripLayoutGrip final : public QWidget {
public:
	StripLayoutGrip(QWidget *parent, bool vertical, int base, int gap)
		: QWidget(parent),
		  vertical(vertical),
		  base(base),
		  gap(qBound(0, gap, MacroButton::MaxGap))
	{
		setFocusPolicy(Qt::NoFocus);
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		setAccessibleName(tr("Move control or adjust spacing"));
		setToolTip(tr("Drag to move or space. Shift-drag preserves order. Escape cancels."));
		setCursor(vertical ? Qt::SizeVerCursor : Qt::SizeHorCursor);
		applySize();
	}

	std::function<void()> started;
	std::function<void(int, int, bool)> committed;

	void setEditing(bool value)
	{
		if (editing == value)
			return;
		cancel();
		editing = value;
		setAttribute(Qt::WA_TransparentForMouseEvents, !editing);
		setFocusPolicy(editing ? Qt::StrongFocus : Qt::NoFocus);
		applySize();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		if (!editing)
			return;
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		auto ink = palette().color(QPalette::Text);
		ink.setAlpha(dragging ? 235 : 130);
		p.setPen(Qt::NoPen);
		p.setBrush(ink);
		const QPointF center = vertical ? QPointF(width() / 2.0, height() - 7.0)
						: QPointF(width() - 7.0, height() / 2.0);
		for (int i = -1; i <= 1; ++i)
			for (int j = 0; j < 2; ++j)
				p.drawEllipse(center + (vertical ? QPointF(i * 4, j * 4 - 2)
								 : QPointF(j * 4 - 2, i * 4)),
					      1.4, 1.4);
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		if (!editing || event->button() != Qt::LeftButton) {
			event->ignore();
			return;
		}
		setFocus(Qt::MouseFocusReason);
		dragging = true;
		spacingOnly = event->modifiers().testFlag(Qt::ShiftModifier);
		origin = axis(event->globalPosition());
		originalGap = gap;
		delta = 0;
		if (started)
			started();
		update();
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (!dragging || !(event->buttons() & Qt::LeftButton)) {
			event->ignore();
			return;
		}
		preview(event->globalPosition());
		event->accept();
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (!dragging || event->button() != Qt::LeftButton) {
			event->ignore();
			return;
		}
		preview(event->globalPosition());
		dragging = false;
		update();
		event->accept();
		if (committed && delta)
			committed(delta, gap, spacingOnly);
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		if (dragging && event->key() == Qt::Key_Escape) {
			cancel();
			event->accept();
			return;
		}
		QWidget::keyPressEvent(event);
	}

	bool event(QEvent *event) override
	{
		if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)
			cancel();
		return QWidget::event(event);
	}

private:
	double axis(QPointF p) const { return vertical ? p.y() : p.x(); }
	void preview(QPointF p)
	{
		delta = qRound(axis(p) - origin);
		gap = qBound(0, originalGap + delta, MacroButton::MaxGap);
		applySize();
	}
	void cancel()
	{
		if (!dragging)
			return;
		dragging = false;
		gap = originalGap;
		applySize();
	}
	void applySize()
	{
		const int extent = base + gap + (editing ? 14 : 0);
		if (vertical) {
			setFixedHeight(extent);
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		} else {
			setFixedWidth(extent);
			setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
		}
		update();
	}
	bool vertical;
	int base, gap;
	bool editing = false;
	bool dragging = false;
	bool spacingOnly = false;
	double origin = 0;
	int originalGap = 0;
	int delta = 0;
};
} // namespace

MacroStripDock::MacroStripDock(QWidget *parent, const QString &storageFilePath, const QString &legacyFilePath)
	: QWidget(parent),
	  m_storageFilePath(storageFilePath),
	  m_legacyFilePath(legacyFilePath)
{
	m_saveTimer.setSingleShot(true);
	m_saveTimer.setInterval(300);
	connect(&m_saveTimer, &QTimer::timeout, this, [this]() { saveNow(); });
	connect(this, &MacroStripDock::saveRequested, this, [this]() {
		if (m_autoSaveReady) {
			m_dirty = true;
			m_saveTimer.start();
		}
	});
	setupUi();
	connectModel();
	rebuildFromModel();
}

MacroStripDock::~MacroStripDock() = default;

void MacroStripDock::setupUi()
{
	setObjectName("timeline-macro-strip-dock");
	// The labeled event bubbles are taller than the regular timeline toolbar.
	// Leave room for them even when a horizontal scrollbar is necessary.
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 2, 6, 2);
	root->setSpacing(2);
	m_tabs = new QTabBar(this);
	m_tabs->setObjectName("macro-strip-tabs");
	m_tabs->setExpanding(false);
	m_tabs->setUsesScrollButtons(true);
	connect(m_tabs, &QTabBar::currentChanged, this, &MacroStripDock::selectStrip);
	root->addWidget(m_tabs);
	auto *strip = new QWidget(this);
	m_stripLayout = new QHBoxLayout(strip);
	m_stripLayout->setContentsMargins(0, 0, 0, 0);
	m_stripLayout->setSpacing(6);
	root->addWidget(strip, 1);

	m_addButton = new QPushButton(this);
	m_addButton->setText("+");
	m_addButton->setFixedSize(28, 28);
	connect(m_addButton, &QPushButton::clicked, this, &MacroStripDock::addButtonAtEnd);
	m_addButton->setObjectName("macro-strip-add");
	m_stripLayout->addWidget(m_addButton, 0, Qt::AlignCenter);

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_scrollArea->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	m_buttonContainer = new QWidget(m_scrollArea);
	m_buttonLayout = new QHBoxLayout(m_buttonContainer);
	m_buttonLayout->setContentsMargins(0, 0, 0, 0);
	m_buttonLayout->setSpacing(0);
	m_buttonLayout->addStretch();

	m_scrollArea->setWidget(m_buttonContainer);
	m_stripLayout->addWidget(m_scrollArea, 1);
	refreshStripTabs();
}

void MacroStripDock::connectModel()
{
	connect(&m_model, &MacroButtonModel::modelChanged, this, [this] {
		m_strips[m_activeStrip].buttons = m_model.buttons();
		if (m_spacingEdit)
			emit saveRequested();
		else
			rebuildFromModel();
	});
}

void MacroStripDock::setObsExecutor(ObsExecutor *executor)
{
	m_executor = executor;
}

void MacroStripDock::rebuildFromModel()
{
	QLayoutItem *child;
	while ((child = m_buttonLayout->takeAt(0)) != nullptr) {
		if (child->widget()) {
			child->widget()->hide();
			child->widget()->deleteLater();
		}
		delete child;
	}

	const bool vertical = m_strips[m_activeStrip].vertical;
	m_stripLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
	m_buttonLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
	m_scrollArea->setVerticalScrollBarPolicy(vertical ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
	m_scrollArea->setAlignment(vertical ? Qt::AlignHCenter | Qt::AlignTop : Qt::AlignLeft | Qt::AlignVCenter);
	const auto buttons = m_model.buttons();
	for (int index = 0; index < buttons.size(); ++index) {
		const auto &b = buttons[index];
		QWidget *w;
		if (b.kind == MacroControlKind::Action) {
			auto *action = new MacroButtonWidget(b, m_buttonContainer);
			connect(action, &MacroButtonWidget::fireRequested, this, &MacroStripDock::onButtonFired);
			w = action;
		} else if (b.kind == MacroControlKind::ButtonGrid) {
			w = createGrid(b);
		} else {
			auto effective = b;
			if (effective.orientation == MacroOrientation::FollowStrip)
				effective.orientation = vertical ? MacroOrientation::Vertical
								 : MacroOrientation::Horizontal;
			w = createMacroParameterWidget(effective, m_buttonContainer,
						       [this](const MacroParameterBinding &binding, double value) {
							       emit parameterCommitted(binding, value);
						       });
		}
		w->setProperty("macro-control-id", b.id);
		w->setEnabled(!m_editLayout);
		w->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(w, &QWidget::customContextMenuRequested, this, [this, w, id = b.id](const QPoint &pos) {
			QPointer<MacroStripDock> safeThis(this);
			QMenu menu;
			QAction *edit = menu.addAction(QObject::tr("Edit"));
			QAction *display = menu.addAction(QObject::tr("Labels and appearance…"));
			QAction *left =
				menu.addAction(m_strips[m_activeStrip].vertical ? tr("Move Up") : tr("Move Left"));
			QAction *right =
				menu.addAction(m_strips[m_activeStrip].vertical ? tr("Move Down") : tr("Move Right"));
			menu.addSeparator();
			QAction *del = menu.addAction(QObject::tr("Delete"));
			QAction *picked = menu.exec(w->mapToGlobal(pos));
			if (!safeThis || !picked)
				return;
			if (picked == edit)
				safeThis->editButton(id);
			else if (picked == display)
				safeThis->editDisplay(id);
			else if (picked == left)
				safeThis->moveLeft(id);
			else if (picked == right)
				safeThis->moveRight(id);
			else if (picked == del)
				safeThis->removeButton(id);
		});
		auto *grip = new StripLayoutGrip(m_buttonContainer, vertical, index == 0 ? 0 : 4, b.gapBefore);
		grip->setObjectName("macro-layout-grip-" + b.id);
		grip->setEditing(m_editLayout);
		// Capture the original control centers once, before the preview
		// changes layout. Crossing another center reorders the control.
		auto centers = std::make_shared<QList<int>>();
		grip->started = [this, centers, vertical] {
			centers->clear();
			for (int i = 0; i < m_buttonLayout->count(); ++i) {
				auto *control = m_buttonLayout->itemAt(i)->widget();
				if (control && control->property("macro-control-id").isValid()) {
					const auto point = control->mapToGlobal(control->rect().center());
					centers->append(vertical ? point.y() : point.x());
				}
			}
		};
		grip->committed = [this, centers, id = b.id, index](int delta, int gap, bool spacingOnly) {
			if (!m_editLayout || index >= centers->size())
				return;
			const int center = centers->at(index) + delta;
			int destination = index;
			if (!spacingOnly) {
				while (destination > 0 && center < centers->at(destination - 1))
					--destination;
				while (destination + 1 < centers->size() && center > centers->at(destination + 1))
					++destination;
			}
			commitLayoutDrag(id, destination, destination == index ? gap : 0);
		};
		m_buttonLayout->addWidget(grip);
		m_buttonLayout->addWidget(w, 0, vertical ? Qt::AlignHCenter : Qt::AlignVCenter);
	}
	m_buttonLayout->addStretch();
	updateStripHeight();
	emit saveRequested();
}

void MacroStripDock::updateStripHeight()
{
	const bool vertical = m_strips[m_activeStrip].vertical;
	m_buttonContainer->setMinimumHeight(0);
	m_buttonContainer->setMaximumHeight(QWIDGETSIZE_MAX);
	m_scrollArea->setMinimumHeight(0);
	m_scrollArea->setMaximumHeight(QWIDGETSIZE_MAX);
	const int tabHeight = m_strips.size() > 1 ? m_tabs->sizeHint().height() + layout()->spacing() : 0;
	const auto margins = layout()->contentsMargins();
	if (vertical) {
		setMinimumHeight(tabHeight + m_addButton->height() + 48 + margins.top() + margins.bottom());
		return;
	}
	QFont captionFont = font();
	captionFont.setPointSize(8);
	captionFont.setBold(true);
	int labelHeight = std::max(MacroButtonWidget::kLabelHeight, QFontMetrics(captionFont).height() + 2);
	int controlHeight = MacroButtonWidget::kControlHeight;
	QFont bottomFont = captionFont;
	bottomFont.setBold(false);
	int bottomHeight = 0;
	for (const auto &button : m_model.buttons())
		if (button.hasBottomCaption()) {
			bottomHeight =
				std::max(MacroButtonWidget::kBottomHeight, QFontMetrics(bottomFont).height() + 2);
			break;
		}
	for (int i = 0; i < m_buttonLayout->count(); ++i) {
		if (auto *widget = m_buttonLayout->itemAt(i)->widget()) {
			const QSize required = macroParameterRequiredRows(widget);
			labelHeight = std::max(labelHeight, required.width());
			controlHeight = std::max(controlHeight, required.height());
		}
	}
	for (int i = 0; i < m_buttonLayout->count(); ++i) {
		if (auto *widget = m_buttonLayout->itemAt(i)->widget()) {
			if (auto *action = qobject_cast<MacroButtonWidget *>(widget))
				action->setRowHeights(labelHeight, controlHeight, bottomHeight);
			else
				setMacroParameterRows(widget, labelHeight, controlHeight, bottomHeight);
		}
	}
	int contentHeight = labelHeight + controlHeight + bottomHeight;
	for (int i = 0; i < m_buttonLayout->count(); ++i)
		if (auto *widget = m_buttonLayout->itemAt(i)->widget();
		    widget && widget->objectName() == "macro-button-grid")
			contentHeight = std::max(contentHeight, widget->sizeHint().height());
	m_buttonContainer->setFixedHeight(contentHeight);
	// Keep the content strip compact, reserving its horizontal scrollbar
	// even when it is not currently visible. The outer dock stays resizable.
	const int scrollHeight = contentHeight + m_scrollArea->horizontalScrollBar()->sizeHint().height() +
				 2 * m_scrollArea->frameWidth();
	m_scrollArea->setFixedHeight(scrollHeight);
	setMinimumHeight(tabHeight + std::max(scrollHeight, m_addButton->height()) + margins.top() + margins.bottom());
}

void MacroStripDock::setEditLayout(bool editing)
{
	if (m_editLayout == editing)
		return;
	m_editLayout = editing;
	for (int i = 0; i < m_buttonLayout->count(); ++i) {
		auto *widget = m_buttonLayout->itemAt(i)->widget();
		if (auto *grip = dynamic_cast<StripLayoutGrip *>(widget))
			grip->setEditing(editing);
		else if (widget)
			widget->setEnabled(!editing);
	}
	m_addButton->setText(editing ? tr("✓") : QStringLiteral("+"));
	m_addButton->setToolTip(editing ? tr("Layout edit mode — open menu to finish")
					: tr("Add controls and manage strips"));
}

void MacroStripDock::commitLayoutDrag(const QString &id, int destination, int gap)
{
	auto buttons = m_model.buttons();
	int index = -1;
	for (int i = 0; i < buttons.size(); ++i)
		if (buttons[i].id == id)
			index = i;
	if (index < 0 || destination < 0 || destination >= buttons.size())
		return;
	gap = qBound(0, gap, MacroButton::MaxGap);
	if (index == destination) {
		if (buttons[index].gapBefore == gap)
			return;
		buttons[index].gapBefore = gap;
		// Commit the preview without rebuilding or discarding control state.
		const QScopedValueRollback<bool> guard(m_spacingEdit, true);
		m_model.replaceButtonById(id, buttons[index]);
		return;
	}
	auto moved = buttons.takeAt(index);
	moved.gapBefore = gap;
	buttons.insert(destination, moved);
	m_model.setButtons(buttons);
	emit m_model.modelChanged();
}

void MacroStripDock::refreshStripTabs()
{
	const QSignalBlocker block(m_tabs);
	while (m_tabs->count())
		m_tabs->removeTab(0);
	for (const auto &strip : m_strips)
		m_tabs->addTab(strip.name);
	m_tabs->setCurrentIndex(m_activeStrip);
	m_tabs->setVisible(m_strips.size() > 1);
}

void MacroStripDock::selectStrip(int index)
{
	if (index < 0 || index >= m_strips.size() || index == m_activeStrip)
		return;
	m_strips[m_activeStrip].buttons = m_model.buttons();
	m_activeStrip = index;
	m_model.setButtons(m_strips[index].buttons);
	refreshStripTabs();
	rebuildFromModel();
}

void MacroStripDock::addStrip()
{
	if (m_strips.size() >= 32)
		return;
	bool ok = false;
	const auto name = QInputDialog::getText(this, tr("New macro strip"), tr("Name"), QLineEdit::Normal,
						tr("Strip %1").arg(m_strips.size() + 1), &ok)
				  .trimmed();
	if (!ok || name.isEmpty())
		return;
	MacroStripDefinition strip;
	strip.name = name;
	strip.vertical = m_strips[m_activeStrip].vertical;
	m_strips.append(strip);
	selectStrip(m_strips.size() - 1);
}

void MacroStripDock::renameStrip()
{
	bool ok = false;
	const auto name = QInputDialog::getText(this, tr("Rename strip"), tr("Name"), QLineEdit::Normal,
						m_strips[m_activeStrip].name, &ok)
				  .trimmed();
	if (!ok || name.isEmpty() || name == m_strips[m_activeStrip].name)
		return;
	m_strips[m_activeStrip].name = name;
	refreshStripTabs();
	updateStripHeight();
	emit saveRequested();
}

void MacroStripDock::setStripVertical(bool vertical)
{
	if (m_strips[m_activeStrip].vertical == vertical)
		return;
	m_strips[m_activeStrip].vertical = vertical;
	rebuildFromModel();
}

void MacroStripDock::removeStrip()
{
	if (m_strips.size() <= 1)
		return;
	if (QMessageBox::question(this, tr("Remove strip"), tr("Delete this strip and all its controls?")) !=
	    QMessageBox::Yes)
		return;
	m_strips.removeAt(m_activeStrip);
	m_activeStrip = std::min(m_activeStrip, int(m_strips.size()) - 1);
	m_model.setButtons(m_strips[m_activeStrip].buttons);
	refreshStripTabs();
	rebuildFromModel();
}

QWidget *MacroStripDock::createGrid(const MacroButton &button)
{
	auto *widget = new QWidget(m_buttonContainer);
	widget->setObjectName("macro-button-grid");
	auto *outer = new QVBoxLayout(widget);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	if (!button.label.isEmpty()) {
		auto *label = new QLabel(button.label, widget);
		label->setAlignment(Qt::AlignCenter);
		QFont font = label->font();
		font.setPointSize(8);
		font.setBold(true);
		label->setFont(font);
		outer->addWidget(label);
	}
	auto *grid = new QGridLayout;
	grid->setSpacing(0);
	for (int i = 0; i < 4; ++i) {
		MacroButton action;
		action.id.clear();
		if (size_t(i) < button.gridActions.size())
			action = button.gridActions[i];
		const bool empty = action.id.isEmpty();
		auto *cell = new MacroButtonWidget(action, widget);
		cell->setGridCell(empty);
		cell->setObjectName(QStringLiteral("macro-grid-cell-%1").arg(i));
		grid->addWidget(cell, i / 2, i % 2);
		connect(cell, &MacroButtonWidget::fireRequested, this,
			[this, bank = m_strips[m_activeStrip].id, id = button.id, i, empty](const MacroButton &action) {
				if (m_editLayout || bank != m_strips[m_activeStrip].id)
					return;
				if (empty)
					QMetaObject::invokeMethod(
						this,
						[this, bank, id, i] {
							if (bank == m_strips[m_activeStrip].id)
								editGridCell(id, i);
						},
						Qt::QueuedConnection);
				else
					onButtonFired(action);
			});
		cell->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(cell, &QWidget::customContextMenuRequested, this,
			[this, cell, id = button.id, i, empty](QPoint pos) {
				QMenu menu;
				auto *edit = menu.addAction(tr("Assign / edit action…"));
				auto *clear = menu.addAction(tr("Clear action"));
				clear->setEnabled(!empty);
				menu.addSeparator();
				auto *appearance = menu.addAction(tr("Grid labels and appearance…"));
				auto *deleteGrid = menu.addAction(tr("Delete grid…"));
				auto *picked = menu.exec(cell->mapToGlobal(pos));
				if (picked == edit)
					editGridCell(id, i);
				else if (picked == appearance)
					editDisplay(id);
				else if (picked == deleteGrid) {
					if (QMessageBox::question(this, tr("Delete grid"),
								  tr("Delete all four action slots?")) ==
					    QMessageBox::Yes)
						removeButton(id);
				} else if (picked == clear) {
					auto current = m_model.findById(id);
					if (current && current->kind == MacroControlKind::ButtonGrid &&
					    size_t(i) < current->gridActions.size()) {
						current->gridActions[i] = MacroButton{};
						current->gridActions[i].id.clear();
						m_model.replaceButtonById(id, *current);
					}
				}
			});
	}
	outer->addLayout(grid);
	if (!button.bottomLabel.isEmpty()) {
		auto *label = new QLabel(button.bottomLabel, widget);
		label->setAlignment(Qt::AlignCenter);
		outer->addWidget(label);
	}
	widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	return widget;
}

void MacroStripDock::editGridCell(const QString &id, int index)
{
	auto current = m_model.findById(id);
	if (m_editLayout || !current || current->kind != MacroControlKind::ButtonGrid || index < 0 || index >= 4)
		return;
	const auto bank = m_strips[m_activeStrip].id;
	while (current->gridActions.size() < 4) {
		MacroButton empty;
		empty.id.clear();
		current->gridActions.push_back(empty);
	}
	auto action = current->gridActions[index];
	TimelineEvent event;
	event.id = action.id;
	event.type = action.type;
	event.sourceName = action.sourceName;
	event.filterName = action.filterName;
	event.data = action.data;
	event.color = action.color;
	event.label = action.label;
	std::unique_ptr<EventEditor> editor;
	if (action.id.isEmpty())
		editor = std::make_unique<EventEditor>(0.0, this, false);
	else
		editor = std::make_unique<EventEditor>(event, this, false);
	if (editor->exec() != QDialog::Accepted || bank != m_strips[m_activeStrip].id || m_editLayout)
		return;
	editor->getUpdatedEvent(event);
	auto latest = m_model.findById(id);
	if (!latest || latest->kind != MacroControlKind::ButtonGrid)
		return;
	if (action.id.isEmpty())
		action.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	action.type = event.type;
	action.sourceName = event.sourceName;
	action.filterName = event.filterName;
	action.data = event.data.toString();
	action.color = event.color;
	action.label = event.label.isEmpty() ? event.sourceName : event.label;
	while (latest->gridActions.size() < 4) {
		MacroButton empty;
		empty.id.clear();
		latest->gridActions.push_back(empty);
	}
	latest->gridActions[index] = action;
	m_model.replaceButtonById(id, *latest);
}

void MacroStripDock::editGrid(const QString &id)
{
	auto button = m_model.findById(id);
	if (!button || button->kind != MacroControlKind::ButtonGrid)
		return;
	QMenu menu(this);
	for (int i = 0; i < 4; ++i) {
		auto *action = menu.addAction(tr("Edit button %1…").arg(i + 1));
		action->setData(i);
	}
	if (auto *selected = menu.exec(QCursor::pos()))
		editGridCell(id, selected->data().toInt());
}

void MacroStripDock::onButtonFired(const MacroButton &button)
{
	if (m_editLayout || !m_executor || button.kind != MacroControlKind::Action)
		return;
	TimelineEvent evt;
	evt.id = button.id;
	evt.type = button.type;
	evt.sourceName = button.sourceName;
	evt.filterName = button.filterName;
	evt.data = button.data;
	evt.color = button.color;
	evt.label = button.label;
	m_executor->executeEventStart(evt);
}

void MacroStripDock::addButtonAtEnd()
{
	QMenu menu(this);
	auto *action = menu.addAction(tr("Action button"));
	auto *parameter = menu.addAction(tr("Parameter control…"));
	auto *xy = menu.addAction(tr("XY controller…"));
	auto *grid = menu.addAction(tr("2 × 2 action buttons"));
	grid->setObjectName("macro-add-grid");
	menu.addSeparator();
	auto *editLayout = menu.addAction(m_editLayout ? tr("Finish editing layout") : tr("Edit layout"));
	editLayout->setObjectName("macro-strip-edit-layout");
	editLayout->setCheckable(true);
	editLayout->setChecked(m_editLayout);
	auto *newStrip = menu.addAction(tr("New strip…"));
	newStrip->setEnabled(m_strips.size() < 32);
	newStrip->setObjectName("macro-add-strip");
	auto *rename = menu.addAction(tr("Rename strip…"));
	auto *orientation = menu.addAction(tr("Vertical strip"));
	orientation->setCheckable(true);
	orientation->setChecked(m_strips[m_activeStrip].vertical);
	orientation->setObjectName("macro-strip-vertical");
	auto *remove = menu.addAction(tr("Remove strip…"));
	remove->setEnabled(m_strips.size() > 1);
	auto *chosen = menu.exec(m_addButton->mapToGlobal(QPoint(0, m_addButton->height())));
	if (chosen == action)
		showAddEditor();
	else if (chosen == grid) {
		MacroButton b;
		b.kind = MacroControlKind::ButtonGrid;
		b.gridActions.resize(4);
		for (auto &action : b.gridActions)
			action.id.clear();
		m_model.addButton(b);
	} else if (chosen == editLayout) {
		setEditLayout(editLayout->isChecked());
	} else if (chosen == newStrip) {
		addStrip();
	} else if (chosen == rename) {
		renameStrip();
	} else if (chosen == orientation) {
		setStripVertical(orientation->isChecked());
	} else if (chosen == remove) {
		removeStrip();
	} else if (chosen == parameter || chosen == xy) {
		MacroButton initial;
		initial.kind = chosen == xy ? MacroControlKind::XY : MacroControlKind::Slider;
		if (auto edited = editMacroParameter(this, initial))
			m_model.addButton(*edited);
	}
}

void MacroStripDock::showAddEditor()
{
	EventEditor editor(0.0, this, false);
	if (editor.exec() != QDialog::Accepted)
		return;

	TimelineEvent evt;
	editor.getUpdatedEvent(evt);

	MacroButton b;
	b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	b.type = evt.type;
	b.sourceName = evt.sourceName;
	b.filterName = evt.filterName;
	b.color = evt.color;
	b.label = evt.label;
	if (evt.data.canConvert<QString>())
		b.data = evt.data.toString();
	if (b.label.isEmpty())
		b.label = b.sourceName;
	m_model.addButton(b);
}

void MacroStripDock::editButton(const QString &id)
{
	showEditEditor(id);
}

void MacroStripDock::editDisplay(const QString &id)
{
	auto existing = m_model.findById(id);
	if (!existing)
		return;
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Labels and appearance"));
	auto *form = new QFormLayout(&dialog);
	auto *top = new QLineEdit(existing->label, &dialog);
	top->setObjectName("macro-display-top");
	top->setPlaceholderText(tr("Automatic label"));
	form->addRow(tr("Top label"), top);
	auto *mode = new QComboBox(&dialog);
	mode->setObjectName("macro-display-bottom-mode");
	mode->addItem(tr("None"), 0);
	mode->addItem(tr("Custom text"), 1);
	if (existing->supportsLiveCaption())
		mode->addItem(tr("Live value"), 2);
	mode->setCurrentIndex(mode->findData(existing->showsLiveCaption()      ? 2
					     : existing->bottomLabel.isEmpty() ? 0
									       : 1));
	form->addRow(tr("Bottom row"), mode);
	auto *bottom = new QLineEdit(existing->bottomLabel, &dialog);
	bottom->setObjectName("macro-display-bottom");
	form->addRow(tr("Bottom text"), bottom);
	auto updateMode = [&] {
		bottom->setEnabled(mode->currentData().toInt() == 1);
	};
	connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { updateMode(); });
	updateMode();
	auto *size = new QComboBox(&dialog);
	size->setObjectName("macro-display-xy-size");
	if (existing->kind == MacroControlKind::XY) {
		size->addItem(tr("Compact (40 px)"), 40);
		size->addItem(tr("Medium (48 px)"), 48);
		size->addItem(tr("Large (64 px)"), 64);
		size->setCurrentIndex(size->findData(existing->padSize()));
		form->addRow(tr("Pad size"), size);
	} else {
		size->hide();
	}
	auto *direction = new QComboBox(&dialog);
	direction->setObjectName("macro-display-orientation");
	if (existing->kind == MacroControlKind::Slider) {
		direction->addItem(tr("Follow strip"), int(MacroOrientation::FollowStrip));
		direction->addItem(tr("Horizontal"), int(MacroOrientation::Horizontal));
		direction->addItem(tr("Vertical"), int(MacroOrientation::Vertical));
		direction->setCurrentIndex(direction->findData(int(existing->orientation)));
		form->addRow(tr("Slider orientation"), direction);
	} else {
		direction->hide();
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted)
		return;
	// Update presentation only; keep the original action or exact bindings.
	auto current = m_model.findById(id);
	if (!current || current->kind != existing->kind)
		return;
	current->label = top->text().trimmed();
	current->showValue = mode->currentData().toInt() == 2;
	current->bottomLabel = mode->currentData().toInt() == 1 ? bottom->text().trimmed() : QString();
	if (current->kind == MacroControlKind::XY)
		current->xySize = size->currentData().toInt();
	if (current->kind == MacroControlKind::Slider)
		current->orientation = MacroOrientation(direction->currentData().toInt());
	m_model.replaceButtonById(id, *current);
}

void MacroStripDock::showEditEditor(const QString &id)
{
	auto existing = m_model.findById(id);
	if (!existing)
		return;
	if (existing->kind == MacroControlKind::ButtonGrid) {
		editGrid(id);
		return;
	}
	if (existing->kind != MacroControlKind::Action) {
		if (auto edited = editMacroParameter(this, *existing))
			m_model.replaceButtonById(id, *edited);
		return;
	}

	TimelineEvent evt;
	evt.id = existing->id;
	evt.type = existing->type;
	evt.sourceName = existing->sourceName;
	evt.filterName = existing->filterName;
	evt.color = existing->color;
	evt.label = existing->label;
	evt.data = existing->data;
	EventEditor editor(evt, this, false);
	if (editor.exec() != QDialog::Accepted)
		return;

	editor.getUpdatedEvent(evt);

	MacroButton updated = *existing;
	updated.id = existing->id;
	updated.type = evt.type;
	updated.sourceName = evt.sourceName;
	updated.filterName = evt.filterName;
	updated.color = evt.color;
	updated.label = evt.label;
	if (evt.data.canConvert<QString>())
		updated.data = evt.data.toString();
	else
		updated.data = QString();
	m_model.replaceButtonById(id, updated);
}

void MacroStripDock::moveLeft(const QString &id)
{
	const auto buttons = m_model.buttons();
	for (int i = 0; i < buttons.size(); ++i) {
		if (buttons[i].id == id) {
			m_model.moveButton(id, std::max(0, i - 1));
			return;
		}
	}
}

void MacroStripDock::moveRight(const QString &id)
{
	const auto buttons = m_model.buttons();
	for (int i = 0; i < buttons.size(); ++i) {
		if (buttons[i].id == id) {
			m_model.moveButton(id, std::min(static_cast<int>(buttons.size()) - 1, i + 1));
			return;
		}
	}
}

void MacroStripDock::removeButton(const QString &id)
{
	m_model.removeButton(id);
}

QJsonObject MacroStripDock::toJson() const
{
	auto strips = m_strips;
	strips[m_activeStrip].buttons = m_model.buttons();
	return MacroSerialization::encodeStrips(strips, m_activeStrip);
}

bool MacroStripDock::fromJson(const QJsonObject &root, QString *errorMessage)
{
	QList<MacroStripDefinition> strips;
	int active = 0;
	if (!MacroSerialization::decodeStrips(root, strips, active, errorMessage))
		return false;
	setEditLayout(false);
	m_strips = std::move(strips);
	m_activeStrip = active;
	m_model.setButtons(m_strips[active].buttons);
	refreshStripTabs();
	emit m_model.modelChanged();
	return true;
}

QString MacroStripDock::dataFilePath() const
{
	if (!m_storageFilePath.isEmpty())
		return m_storageFilePath;
	const QString dir = QDir::homePath() + "/.config/obs-studio/plugins/timeline/data";
	QDir().mkpath(dir);
	return dir + "/macro_buttons.json";
}

bool MacroStripDock::saveNow()
{
	const QString path = dataFilePath();
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		blog(LOG_WARNING, "[MacroStrip] cannot create config directory for %s", path.toUtf8().constData());
		return false;
	}
	QString err;
	const bool ok = MacroSerialization::saveToFile(path, toJson(), &err);
	if (!ok)
		blog(LOG_WARNING, "[MacroStrip] save failed: %s", err.toUtf8().constData());
	else
		m_dirty = false; // Timer activity alone cannot tell us whether a write succeeded.
	return ok;
}

void MacroStripDock::activateAutoSave()
{
	// Delay autosaving until the OBS profile and on-disk macro state have loaded.
	m_autoSaveReady = true;
}

bool MacroStripDock::restoreFromObsJson(const QByteArray &json)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		blog(LOG_WARNING, "[MacroStrip] Invalid OBS profile macro JSON: %s",
		     parseError.errorString().toUtf8().constData());
		return false;
	}
	QString error;
	QList<MacroStripDefinition> backup;
	int active = 0;
	if (!document.isObject() || !MacroSerialization::decodeStrips(document.object(), backup, active, &error)) {
		blog(LOG_WARNING, "[MacroStrip] Ignoring invalid OBS profile macros: %s", error.toUtf8().constData());
		return false;
	}

	// The OBS scene-collection copy is a recovery fallback, not the canonical
	// Macro Strip state. Flush any pending edits before reading the on-disk file.
	if (m_dirty) {
		m_saveTimer.stop();
		if (!saveNow())
			return false; // Keep dirty in-memory edits; never load older backup/disk state.
	}
	if (loadNow()) {
		m_saveTimer.stop(); // Loading triggers modelChanged; no rewrite is needed.
		return true;
	}

	// No usable canonical (or migrated legacy) file: restore the validated backup.
	if (!fromJson(document.object(), &error))
		return false;
	m_saveTimer.stop();
	return saveNow(); // QSaveFile commits the fallback snapshot atomically.
}

bool MacroStripDock::loadNow()
{
	QString path = dataFilePath();
	bool migrating = false;
	if (!QFile::exists(path)) {
		if (m_legacyFilePath.isEmpty() || !QFile::exists(m_legacyFilePath))
			return false;
		path = m_legacyFilePath;
		migrating = true;
	}
	QJsonDocument doc;
	QString err;
	if (!MacroSerialization::loadFromFile(path, doc, &err)) {
		const QString broken = path + ".broken-" + QString::number(QDateTime::currentSecsSinceEpoch());
		QFile::rename(path, broken);
		blog(LOG_WARNING, "[MacroStrip] %s; renamed to %s", err.toUtf8().constData(),
		     broken.toUtf8().constData());
		return false;
	}
	if (!fromJson(doc.object(), &err)) {
		blog(LOG_WARNING, "[MacroStrip] Invalid macro document: %s", err.toUtf8().constData());
		return false;
	}
	m_saveTimer.stop();
	m_dirty = false; // Loaded state is already persisted, not a user edit.
	if (migrating) {
		// Keep the original file untouched; a verified save at the new location
		// takes precedence over this fallback on subsequent launches.
		if (!saveNow())
			return false;
		blog(LOG_INFO, "[MacroStrip] Imported legacy buttons from %s", path.toUtf8().constData());
	}
	return true;
}

void MacroStripDock::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		refreshTheme();
		break;
	default:
		break;
	}
}

void MacroStripDock::refreshTheme()
{
	if (!m_buttonLayout || !m_scrollArea || !m_addButton)
		return;
	// Refresh geometry and painting in place. Rebuilding discards text
	// drafts and active gestures, and incorrectly schedules a save.
	updateStripHeight();
	for (int i = 0; i < m_buttonLayout->count(); ++i)
		if (auto *widget = m_buttonLayout->itemAt(i)->widget())
			widget->update();
	update();
}

void MacroStripDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
	    event->type() == QEvent::FontChange)
		QMetaObject::invokeMethod(this, [this] { refreshTheme(); }, Qt::QueuedConnection);
}
