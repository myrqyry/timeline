#include "macro-button-model.h"

#include <algorithm>

MacroButtonModel::MacroButtonModel(QObject *parent) : QObject(parent) {}

int MacroButtonModel::indexOf(const QString &id) const
{
	for (int i = 0; i < m_buttons.size(); ++i) {
		if (m_buttons[i].id == id)
			return i;
	}
	return -1;
}

void MacroButtonModel::addButton(const MacroButton &button)
{
	m_buttons.append(button);
	emit modelChanged();
}

bool MacroButtonModel::removeButton(const QString &id)
{
	const int i = indexOf(id);
	if (i < 0)
		return false;
	m_buttons.removeAt(i);
	emit modelChanged();
	return true;
}

bool MacroButtonModel::moveButton(const QString &id, int newIndex)
{
	const int from = indexOf(id);
	if (from < 0)
		return false;
	if (m_buttons.isEmpty())
		return false;
	const int clamped = std::max(0, std::min(newIndex, static_cast<int>(m_buttons.size()) - 1));
	if (clamped == from)
		return true;
	MacroButton b = m_buttons.takeAt(from);
	m_buttons.insert(clamped, b);
	emit modelChanged();
	return true;
}

bool MacroButtonModel::replaceButtonById(const QString &id, const MacroButton &button)
{
	const int i = indexOf(id);
	if (i < 0)
		return false;
	MacroButton updated = button;
	updated.id = id;
	m_buttons[i] = updated;
	emit modelChanged();
	return true;
}

std::optional<MacroButton> MacroButtonModel::findById(const QString &id) const
{
	const int i = indexOf(id);
	if (i < 0)
		return std::nullopt;
	return m_buttons[i];
}

QList<MacroButton> MacroButtonModel::buttons() const
{
	return m_buttons;
}
int MacroButtonModel::count() const
{
	return m_buttons.size();
}
bool MacroButtonModel::isEmpty() const
{
	return m_buttons.isEmpty();
}
void MacroButtonModel::clear()
{
	m_buttons.clear();
}

void MacroButtonModel::setButtons(const QList<MacroButton> &buttons)
{
	m_buttons = buttons;
}
