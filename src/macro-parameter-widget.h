#pragma once

#include "macro-button.h"
#include <QSize>
class QWidget;

QWidget *createMacroParameterWidget(const MacroButton &button, QWidget *parent);
QSize macroParameterRequiredRows(QWidget *widget);
void setMacroParameterRows(QWidget *widget, int labelHeight, int controlHeight, int bottomHeight = 0);
std::optional<MacroButton> editMacroParameter(QWidget *parent, const MacroButton &initial);

std::optional<MacroButton> editDefinedParameter(QWidget *parent, const MacroButton &initial,
						std::optional<QVariant> &value);
