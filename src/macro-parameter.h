#pragma once

#include <QList>
#include <QString>
#include <optional>

enum class MacroControlKind { Action, Slider, Knob, Toggle };
enum class MacroParameterType { Integer, Float, Boolean };

struct MacroParameterBinding {
	QString sourceUuid;
	QString filterUuid;
	QString key;
	MacroParameterType type = MacroParameterType::Float;
};

namespace MacroParameter {
struct Target {
	QString sourceUuid;
	QString filterUuid;
	QString label;
};

struct Spec {
	QString key;
	QString label;
	QString suffix;
	MacroParameterType type;
	double min;
	double max;
	double step;
	int steps() const;
	double valueAt(int index) const;
	int indexOf(double value) const;
};

QList<Target> targets();
QList<Spec> properties(const MacroParameterBinding &binding);
std::optional<Spec> describe(const MacroParameterBinding &binding);
std::optional<double> read(const MacroParameterBinding &binding);
bool write(const MacroParameterBinding &binding, double value);
} // namespace MacroParameter
