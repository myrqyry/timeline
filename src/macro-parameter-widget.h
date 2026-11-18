#pragma once

#include "macro-button.h"
class QWidget;

QWidget *createMacroParameterWidget(const MacroButton &button, QWidget *parent);
std::optional<MacroButton> editMacroParameter(QWidget *parent, const MacroButton &initial);
