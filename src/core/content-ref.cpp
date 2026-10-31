/*
OBS Looking Glass - Custom Dynamic Multiview Plugin
Copyright (C) 2025

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "content-ref.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace ContentRefs {

namespace {

struct TypeName {
	WidgetType type;
	const char *name;
};

const TypeName kTypeNames[] = {
	{WidgetType::None, "none"},
	{WidgetType::Preview, "preview"},
	{WidgetType::Program, "program"},
	{WidgetType::Canvas, "canvas"},
	{WidgetType::Scene, "scene"},
	{WidgetType::Source, "source"},
	{WidgetType::Placeholder, "placeholder"},
};

} // namespace

QString MimeType()
{
	return QStringLiteral("application/x-looking-glass-content");
}

QByteArray Encode(const ContentRef &ref)
{
	QString typeName = QStringLiteral("none");
	for (const TypeName &t : kTypeNames) {
		if (t.type == ref.type)
			typeName = QString::fromLatin1(t.name);
	}

	QJsonObject obj;
	obj.insert(QStringLiteral("type"), typeName);
	obj.insert(QStringLiteral("name"), ref.name);
	obj.insert(QStringLiteral("uuid"), ref.uuid);
	return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

bool Decode(const QByteArray &data, ContentRef *out)
{
	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(data, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject())
		return false;

	QJsonObject obj = doc.object();
	QJsonValue typeValue = obj.value(QStringLiteral("type"));
	if (!typeValue.isString())
		return false;

	QString typeName = typeValue.toString();
	for (const TypeName &t : kTypeNames) {
		if (typeName != QLatin1String(t.name))
			continue;
		if (out) {
			out->type = t.type;
			out->name = obj.value(QStringLiteral("name")).toString();
			out->uuid = obj.value(QStringLiteral("uuid")).toString();
		}
		return true;
	}
	return false;
}

void Apply(WidgetConfig &w, const ContentRef &ref)
{
	bool enteringScene = ref.type == WidgetType::Scene && w.type != WidgetType::Scene;

	w.type = ref.type;
	w.sceneName.clear();
	w.sourceName.clear();
	w.sourceUuid.clear();
	w.canvasName.clear();

	switch (ref.type) {
	case WidgetType::Scene:
		w.sceneName = ref.name;
		w.sourceUuid = ref.uuid;
		break;
	case WidgetType::Source:
		w.sourceName = ref.name;
		w.sourceUuid = ref.uuid;
		break;
	case WidgetType::Canvas:
		w.canvasName = ref.name;
		break;
	default:
		break;
	}

	if (ref.type != WidgetType::Scene)
		w.showStatus = false;
	else if (enteringScene)
		w.showStatus = true;
}

ContentRef FromWidget(const WidgetConfig &w)
{
	ContentRef ref;
	ref.type = w.type;
	switch (w.type) {
	case WidgetType::Scene:
		ref.name = w.sceneName;
		ref.uuid = w.sourceUuid;
		break;
	case WidgetType::Source:
		ref.name = w.sourceName;
		ref.uuid = w.sourceUuid;
		break;
	case WidgetType::Canvas:
		ref.name = w.canvasName;
		break;
	default:
		break;
	}
	return ref;
}

} // namespace ContentRefs
