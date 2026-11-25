#pragma once
#include "urlsource-snapshot-v1.h"
#include <obs.h>
#include <util/platform.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <optional>
#include <cmath>
#include <cstring>

namespace DataCondition {
struct Condition {
	QString uuid, op;
	double threshold = 0;
	int maxAgeMs = 5000;
};
inline bool valid(const Condition &c)
{
	const QUuid id(c.uuid);
	return !id.isNull() && id.toString(QUuid::WithoutBraces) == c.uuid &&
	       (c.op == ">=" || c.op == ">" || c.op == "<=" || c.op == "<" || c.op == "true" || c.op == "false") &&
	       std::isfinite(c.threshold) && std::abs(c.threshold) <= 1e12 && c.maxAgeMs >= 100 && c.maxAgeMs <= 60000;
}
inline QString encode(const Condition &c)
{
	return QString::fromUtf8(QJsonDocument(QJsonObject{{"uuid", c.uuid},
							   {"op", c.op},
							   {"threshold", c.threshold},
							   {"maxAgeMs", c.maxAgeMs}})
					 .toJson(QJsonDocument::Compact));
}
inline std::optional<Condition> decode(const QString &data)
{
	if (data.size() > 2048)
		return {};
	auto doc = QJsonDocument::fromJson(data.toUtf8());
	if (!doc.isObject())
		return {};
	const auto o = doc.object();
	if (!o["uuid"].isString() || !o["op"].isString() || !o["threshold"].isDouble() || !o["maxAgeMs"].isDouble())
		return {};
	const double age = o["maxAgeMs"].toDouble();
	if (!std::isfinite(age) || age < 100 || age > 60000 || std::floor(age) != age)
		return {};
	Condition c{o["uuid"].toString(), o["op"].toString(), o["threshold"].toDouble(), int(age)};
	return valid(c) ? std::optional<Condition>(c) : std::nullopt;
}
inline bool matches(const Condition &c, const QByteArray &scalar, uint64_t age)
{
	if (!valid(c) || age >= uint64_t(c.maxAgeMs) || scalar.isEmpty() || scalar.size() > 127 ||
	    scalar.contains('\0'))
		return false;
	// Qt's document parser requires an array or object. Wrapping a scalar
	// still validates the complete input; multiple elements are rejected.
	const auto doc = QJsonDocument::fromJson("[" + scalar + "]");
	if (!doc.isArray() || doc.array().size() != 1)
		return false;
	const auto value = doc.array().at(0);
	if (c.op == "true" || c.op == "false")
		return value.isBool() && value.toBool() == (c.op == "true");
	if (!value.isDouble() || !std::isfinite(value.toDouble()))
		return false;
	const double v = value.toDouble();
	if (c.op == ">=")
		return v >= c.threshold;
	if (c.op == ">")
		return v > c.threshold;
	if (c.op == "<=")
		return v <= c.threshold;
	if (c.op == "<")
		return v < c.threshold;
	return false;
}
inline urlsource_snapshot_v1_fn reader()
{
	auto *module = obs_get_module("obs-urlsource");
	return module ? reinterpret_cast<urlsource_snapshot_v1_fn>(
				os_dlsym(obs_get_module_lib(module), "urlsource_snapshot_v1"))
		      : nullptr;
}
inline bool evaluate(const Condition &c)
{
	if (!valid(c))
		return false;
	auto fn = reader();
	if (!fn)
		return false;
	char buffer[128] = {};
	uint64_t seq = 0, age = 0;
	const auto n = fn(c.uuid.toUtf8().constData(), buffer, sizeof buffer, &seq, &age);
	return seq && n > 1 && n <= sizeof buffer && buffer[n - 1] == '\0' &&
	       matches(c, QByteArray(buffer, int(n - 1)), age);
}
} // namespace DataCondition
