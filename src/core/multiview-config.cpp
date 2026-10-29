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

#include "multiview-config.hpp"
#include "grid-model.hpp"

#include <QJsonArray>
#include <QSet>
#include <QUuid>

#include <algorithm>

QString WidgetConfig::targetName() const
{
	switch (type) {
	case WidgetType::Scene:
		return sceneName;
	case WidgetType::Source:
		return sourceName;
	default:
		return QString();
	}
}

namespace {

constexpr int kMinBorderWidth = 1;
constexpr int kMaxBorderWidth = 10;

// --- String conversion helpers for enum serialization ---

const char *WidgetTypeToString(WidgetType t)
{
	switch (t) {
	case WidgetType::Preview:
		return "preview";
	case WidgetType::Program:
		return "program";
	case WidgetType::Canvas:
		return "canvas";
	case WidgetType::Scene:
		return "scene";
	case WidgetType::Source:
		return "source";
	case WidgetType::Placeholder:
		return "placeholder";
	default:
		return "none";
	}
}

WidgetType StringToWidgetType(const QString &s)
{
	if (s == QLatin1String("preview"))
		return WidgetType::Preview;
	if (s == QLatin1String("program"))
		return WidgetType::Program;
	if (s == QLatin1String("canvas"))
		return WidgetType::Canvas;
	if (s == QLatin1String("scene"))
		return WidgetType::Scene;
	if (s == QLatin1String("source"))
		return WidgetType::Source;
	if (s == QLatin1String("placeholder"))
		return WidgetType::Placeholder;
	return WidgetType::None;
}

const char *AlignHToString(Qt::Alignment a)
{
	if (a & Qt::AlignLeft)
		return "left";
	if (a & Qt::AlignRight)
		return "right";
	return "center";
}

Qt::Alignment StringToAlignH(const QString &s)
{
	if (s == QLatin1String("left"))
		return Qt::AlignLeft;
	if (s == QLatin1String("right"))
		return Qt::AlignRight;
	return Qt::AlignHCenter;
}

const char *AlignVToString(Qt::Alignment a)
{
	if (a & Qt::AlignTop)
		return "top";
	if (a & Qt::AlignVCenter)
		return "middle";
	return "bottom";
}

Qt::Alignment StringToAlignV(const QString &s)
{
	if (s == QLatin1String("top"))
		return Qt::AlignTop;
	if (s == QLatin1String("middle"))
		return Qt::AlignVCenter;
	return Qt::AlignBottom;
}

QColor ColorFromJson(const QJsonValue &value, const QColor &fallback)
{
	QColor color(value.toString());
	return color.isValid() ? color : fallback;
}

int IntFromJson(const QJsonObject &obj, const char *key, int fallback)
{
	QJsonValue value = obj.value(QLatin1String(key));
	return value.isDouble() ? value.toInt(fallback) : fallback;
}

QString NewId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Clamps grid dimensions and repairs the cell list so the rest of the plugin
// can assume a well-formed layout
void SanitizeGrid(int &rows, int &cols, QVector<CellConfig> &cells)
{
	rows = std::clamp(rows, GridModel::kMinGridSize, GridModel::kMaxGridSize);
	cols = std::clamp(cols, GridModel::kMinGridSize, GridModel::kMaxGridSize);
	cells = GridModel::Normalize(rows, cols, cells);
}

QJsonArray CellsToJson(const QVector<CellConfig> &cells)
{
	QJsonArray array;
	for (const CellConfig &cell : cells)
		array.append(MultiviewSerializer::CellToJson(cell));
	return array;
}

QVector<CellConfig> CellsFromJson(const QJsonValue &value)
{
	QVector<CellConfig> cells;
	const QJsonArray array = value.toArray();
	for (const QJsonValue &item : array)
		cells.append(MultiviewSerializer::CellFromJson(item.toObject()));
	return cells;
}

QJsonObject WindowToJson(const WindowState &w)
{
	QJsonObject obj;
	obj["x"] = w.geometry.x();
	obj["y"] = w.geometry.y();
	obj["width"] = w.geometry.width();
	obj["height"] = w.geometry.height();
	obj["screen_name"] = w.screenName;
	obj["screen_index"] = w.screenIndex;
	obj["fullscreen"] = w.fullscreen;
	obj["open"] = w.open;
	return obj;
}

WindowState WindowFromJson(const QJsonObject &obj)
{
	WindowState w;
	int width = IntFromJson(obj, "width", 0);
	int height = IntFromJson(obj, "height", 0);
	if (width > 0 && height > 0)
		w.geometry = QRect(IntFromJson(obj, "x", 0), IntFromJson(obj, "y", 0), width, height);
	w.screenName = obj.value("screen_name").toString();
	w.screenIndex = IntFromJson(obj, "screen_index", -1);
	w.fullscreen = obj.value("fullscreen").toBool(false);
	w.open = obj.value("open").toBool(false);
	return w;
}

// Version 1 stored the window state as flat keys on the multiview itself
WindowState LegacyWindowFromJson(const QJsonObject &obj)
{
	WindowState w;
	int width = IntFromJson(obj, "geometry_w", 0);
	int height = IntFromJson(obj, "geometry_h", 0);
	if (width > 0 && height > 0)
		w.geometry = QRect(IntFromJson(obj, "geometry_x", 0), IntFromJson(obj, "geometry_y", 0), width, height);
	w.screenIndex = IntFromJson(obj, "monitor_id", -1);
	w.fullscreen = obj.value("fullscreen").toBool(false);
	w.open = obj.value("was_open").toBool(false);
	return w;
}

} // namespace

namespace MultiviewSerializer {

QJsonObject WidgetToJson(const WidgetConfig &w)
{
	QJsonObject obj;
	obj["type"] = WidgetTypeToString(w.type);
	obj["scene_name"] = w.sceneName;
	obj["source_name"] = w.sourceName;
	obj["source_uuid"] = w.sourceUuid;
	obj["placeholder_path"] = w.placeholderPath;
	obj["canvas_name"] = w.canvasName;
	obj["label_visible"] = w.labelVisible;
	obj["label_h_align"] = AlignHToString(w.labelHAlign);
	obj["label_v_align"] = AlignVToString(w.labelVAlign);
	obj["label_text"] = w.labelText;
	obj["label_font"] = w.labelFont;
	obj["label_size"] = w.labelSize;
	obj["label_bg_color"] = w.labelBgColor.name(QColor::HexArgb);
	obj["safe_region"] = w.safeRegion;
	obj["show_status"] = w.showStatus;
	return obj;
}

WidgetConfig WidgetFromJson(const QJsonObject &obj)
{
	// Every key falls back to the WidgetConfig default, so files written by
	// older versions (or edited by hand) load the way they were meant to
	const WidgetConfig defaults;
	WidgetConfig w;
	w.type = StringToWidgetType(obj.value("type").toString());
	w.sceneName = obj.value("scene_name").toString();
	w.sourceName = obj.value("source_name").toString();
	w.sourceUuid = obj.value("source_uuid").toString();
	w.placeholderPath = obj.value("placeholder_path").toString();
	w.canvasName = obj.value("canvas_name").toString();
	w.labelVisible = obj.value("label_visible").toBool(defaults.labelVisible);
	w.labelHAlign = StringToAlignH(obj.value("label_h_align").toString());
	w.labelVAlign = StringToAlignV(obj.value("label_v_align").toString());
	w.labelText = obj.value("label_text").toString();
	w.labelFont = obj.value("label_font").toString();
	w.labelSize = std::clamp(IntFromJson(obj, "label_size", defaults.labelSize), kLabelSizeMin, kLabelSizeMax);
	w.labelBgColor = ColorFromJson(obj.value("label_bg_color"), defaults.labelBgColor);
	w.safeRegion = obj.value("safe_region").toBool(defaults.safeRegion);
	w.showStatus = obj.value("show_status").toBool(defaults.showStatus);
	return w;
}

QJsonObject CellToJson(const CellConfig &c)
{
	QJsonObject obj;
	obj["row"] = c.row;
	obj["col"] = c.col;
	obj["row_span"] = c.rowSpan;
	obj["col_span"] = c.colSpan;
	obj["widget"] = WidgetToJson(c.widget);
	return obj;
}

CellConfig CellFromJson(const QJsonObject &obj)
{
	CellConfig c;
	c.row = IntFromJson(obj, "row", 0);
	c.col = IntFromJson(obj, "col", 0);
	c.rowSpan = std::max(1, IntFromJson(obj, "row_span", 1));
	c.colSpan = std::max(1, IntFromJson(obj, "col_span", 1));
	c.widget = WidgetFromJson(obj.value("widget").toObject());
	return c;
}

QJsonObject MultiviewToJson(const MultiviewConfig &mv)
{
	QJsonObject obj;
	obj["id"] = mv.id;
	obj["name"] = mv.name;
	obj["grid_rows"] = mv.gridRows;
	obj["grid_cols"] = mv.gridCols;
	obj["grid_border_width"] = mv.gridBorderWidth;
	obj["grid_line_color"] = mv.gridLineColor.name(QColor::HexArgb);
	obj["cells"] = CellsToJson(mv.cells);
	obj["window"] = WindowToJson(mv.window);
	return obj;
}

MultiviewConfig MultiviewFromJson(const QJsonObject &obj)
{
	const MultiviewConfig defaults;
	MultiviewConfig mv;
	mv.id = obj.value("id").toString();
	mv.name = obj.value("name").toString().trimmed();
	mv.gridRows = IntFromJson(obj, "grid_rows", defaults.gridRows);
	mv.gridCols = IntFromJson(obj, "grid_cols", defaults.gridCols);
	mv.gridBorderWidth = std::clamp(IntFromJson(obj, "grid_border_width", defaults.gridBorderWidth),
					kMinBorderWidth, kMaxBorderWidth);
	mv.gridLineColor = ColorFromJson(obj.value("grid_line_color"), defaults.gridLineColor);
	mv.cells = CellsFromJson(obj.value("cells"));
	mv.window = obj.value("window").isObject() ? WindowFromJson(obj.value("window").toObject())
						   : LegacyWindowFromJson(obj);
	SanitizeGrid(mv.gridRows, mv.gridCols, mv.cells);
	return mv;
}

QJsonObject TemplateToJson(const TemplateConfig &t)
{
	QJsonObject obj;
	obj["name"] = t.name;
	obj["grid_rows"] = t.gridRows;
	obj["grid_cols"] = t.gridCols;
	obj["preserve_sources"] = t.preserveSources;
	obj["cells"] = CellsToJson(t.cells);
	return obj;
}

TemplateConfig TemplateFromJson(const QJsonObject &obj)
{
	const TemplateConfig defaults;
	TemplateConfig t;
	t.name = obj.value("name").toString().trimmed();
	t.gridRows = IntFromJson(obj, "grid_rows", defaults.gridRows);
	t.gridCols = IntFromJson(obj, "grid_cols", defaults.gridCols);
	t.preserveSources = obj.value("preserve_sources").toBool(defaults.preserveSources);
	t.cells = CellsFromJson(obj.value("cells"));
	SanitizeGrid(t.gridRows, t.gridCols, t.cells);
	return t;
}

QJsonObject MultiviewsToJson(const QVector<MultiviewConfig> &multiviews)
{
	QJsonArray array;
	for (const MultiviewConfig &mv : multiviews)
		array.append(MultiviewToJson(mv));

	QJsonObject root;
	root["version"] = kSchemaVersion;
	root["multiviews"] = array;
	return root;
}

QVector<MultiviewConfig> MultiviewsFromJson(const QJsonObject &root)
{
	QVector<MultiviewConfig> result;
	QSet<QString> ids;
	QSet<QString> names;

	const QJsonArray array = root.value("multiviews").toArray();
	for (const QJsonValue &item : array) {
		MultiviewConfig mv = MultiviewFromJson(item.toObject());
		if (mv.name.isEmpty() || names.contains(mv.name))
			continue;
		if (mv.id.isEmpty() || ids.contains(mv.id))
			mv.id = NewId();

		names.insert(mv.name);
		ids.insert(mv.id);
		result.append(mv);
	}
	return result;
}

QJsonObject TemplatesToJson(const QVector<TemplateConfig> &templates)
{
	QJsonArray array;
	for (const TemplateConfig &t : templates)
		array.append(TemplateToJson(t));

	QJsonObject root;
	root["version"] = kSchemaVersion;
	root["templates"] = array;
	return root;
}

QVector<TemplateConfig> TemplatesFromJson(const QJsonObject &root)
{
	QVector<TemplateConfig> result;
	QSet<QString> names;

	const QJsonArray array = root.value("templates").toArray();
	for (const QJsonValue &item : array) {
		TemplateConfig t = TemplateFromJson(item.toObject());
		if (t.name.isEmpty() || names.contains(t.name))
			continue;
		names.insert(t.name);
		result.append(t);
	}
	return result;
}

} // namespace MultiviewSerializer
