#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <optional>
#include "macro-button.h"

class MacroButtonModel : public QObject {
	Q_OBJECT
public:
	explicit MacroButtonModel(QObject *parent = nullptr);

	void addButton(const MacroButton &button);
	bool removeButton(const QString &id);
	bool moveButton(const QString &id, int newIndex);
	bool replaceButtonById(const QString &id, const MacroButton &button);
	std::optional<MacroButton> findById(const QString &id) const;
	QList<MacroButton> buttons() const;
	int count() const;
	bool isEmpty() const;
	void clear();

	// Bulk loading (no modelChanged emission from setButtons itself; callers emit once if needed)
	void setButtons(const QList<MacroButton> &buttons);

signals:
	void modelChanged();

private:
	int indexOf(const QString &id) const;
	QList<MacroButton> m_buttons;
};