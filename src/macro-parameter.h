#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <optional>

enum class MacroControlKind { Action, Slider, Knob, Toggle, Text, XY };
enum class MacroParameterType { Integer, Float, Boolean, Text };

struct MacroParameterBinding {
	QString sourceUuid;
	QString filterUuid;
	QString key;
	MacroParameterType type = MacroParameterType::Float;
	QString domain; // Empty: OBS setting; mixer: native audio control.
};

namespace MacroParameter {
struct DefinedValue {
	MacroParameterBinding binding;
	QVariant value;
};
struct ToggleValues {
	QVariant a, b;
};
bool validToggleValues(MacroParameterType type, const ToggleValues &values);
struct Ramp {
	DefinedValue start;
	double end;
	QString easing;
};
QString encodeRamp(const Ramp &ramp);
std::optional<Ramp> decodeRamp(const QString &payload);
double rampValue(const Ramp &ramp, double progress);
QString bindingIdentity(const MacroParameterBinding &binding);
QString encodeValue(const DefinedValue &value);
std::optional<DefinedValue> decodeValue(const QString &payload);
QString displayValue(const DefinedValue &value);
QString mixerVolumeLabel(double position);

struct Target {
	QString sourceUuid;
	QString filterUuid;
	QString label;
	bool operator==(const Target &other) const
	{
		return sourceUuid == other.sourceUuid && filterUuid == other.filterUuid && label == other.label;
	}
};

struct Spec {
	QString key;
	QString label;
	QString suffix;
	MacroParameterType type;
	double min;
	double max;
	double step;
	QString domain;
	bool multiline = false;
	bool operator==(const Spec &other) const
	{
		return key == other.key && label == other.label && suffix == other.suffix && type == other.type &&
		       min == other.min && max == other.max && step == other.step && domain == other.domain &&
		       multiline == other.multiline;
	}
	int steps() const;
	double valueAt(int index) const;
	int indexOf(double value) const;
};

bool toggleValuesFit(const Spec &spec, const ToggleValues &values);
std::optional<int> toggleState(const MacroParameterBinding &binding, const Spec &spec, const ToggleValues &values);
std::optional<int> toggleStateForValue(const Spec &spec, const ToggleValues &values, const QVariant &current);
QList<Target> targets();
QList<Spec> properties(const MacroParameterBinding &binding);
std::optional<Spec> describe(const MacroParameterBinding &binding);
std::optional<double> read(const MacroParameterBinding &binding);
bool write(const MacroParameterBinding &binding, double value, bool skipUnchanged = false);
bool numericPair(const MacroParameterBinding &x, const MacroParameterBinding &y);
bool writePair(const MacroParameterBinding &x, double xValue, const MacroParameterBinding &y, double yValue);
std::optional<QString> readText(const MacroParameterBinding &binding);
bool writeText(const MacroParameterBinding &binding, const QString &value);
} // namespace MacroParameter
