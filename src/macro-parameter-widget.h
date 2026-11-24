#pragma once

#include "macro-button.h"
#include <functional>
#include <QSize>
class QWidget;

using MacroUserWrite = std::function<void(const MacroParameterBinding &, double)>;
QWidget *createMacroParameterWidget(const MacroButton &button, QWidget *parent, MacroUserWrite onCommitted = {});
QSize macroParameterRequiredRows(QWidget *widget);
void setMacroParameterRows(QWidget *widget, int labelHeight, int controlHeight, int bottomHeight = 0);
std::optional<MacroButton> editMacroParameter(QWidget *parent, const MacroButton &initial);

std::optional<MacroButton> editDefinedParameter(QWidget *parent, const MacroButton &initial,
						std::optional<QVariant> &value);
