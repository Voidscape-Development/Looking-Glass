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

#pragma once

// Plain data model for multiview layouts and their JSON form. Deliberately free
// of libobs so it can be unit tested on its own (see tests/).

#include <QString>
#include <QVector>
#include <QRect>
#include <QColor>
#include <QJsonObject>
#include <Qt>

// Content types that can be displayed in a multiview cell
enum class WidgetType {
	None,
	Preview,
	Program,
	Canvas,
	Scene,
	Source,
	Placeholder,
};

// Label size, as a percentage of the OBS multiview default. At 100% a label's
// text is 1/9.81 of its cell's height, the proportion OBS Studio's own
// multiview uses, and it scales with the cell like the content does.
constexpr int kLabelSizeDefault = 100;
constexpr int kLabelSizeMin = 25;
constexpr int kLabelSizeMax = 400;

// Per-cell display and label configuration
struct WidgetConfig {
	WidgetType type = WidgetType::None;
	QString sceneName;
	QString sourceName;
	// UUID of the scene (Scene type) or source (Source type). The name is kept
	// alongside it as a fallback for layouts that predate UUIDs and for
	// templates, which are shared across scene collections.
	QString sourceUuid;
	QString placeholderPath;
	QString canvasName; // Empty means main canvas
	bool labelVisible = true;
	Qt::Alignment labelHAlign = Qt::AlignHCenter;
	Qt::Alignment labelVAlign = Qt::AlignBottom;
	QString labelText;
	QString labelFont;                          // QFont::toString(); family and style only
	int labelSize = kLabelSizeDefault;          // Percent, kLabelSizeMin..kLabelSizeMax
	QColor labelBgColor = QColor(0, 0, 0, 128); // Label background color with alpha
	bool safeRegion = false;                    // Draw broadcast safe area overlays
	bool showStatus = false;                    // Show preview/program border indicator

	// Name of the scene or source this widget displays, if any
	QString targetName() const;
};

// Position and span of a single cell within the grid
struct CellConfig {
	int row = 0;
	int col = 0;
	int rowSpan = 1;
	int colSpan = 1;
	WidgetConfig widget;
};

// Where a multiview window sits on screen. This is runtime state rather than
// layout: it changes as the window is dragged around and never triggers a
// layout reload.
struct WindowState {
	QRect geometry = QRect(100, 100, 1280, 720); // Windowed geometry
	QString screenName;                          // Monitor used for fullscreen
	int screenIndex = -1;                        // Fallback when the name is unknown
	bool fullscreen = false;
	bool open = false; // Reopened when the scene collection loads
};

// Complete layout definition for a multiview window
struct MultiviewConfig {
	QString id; // Stable identity; names are user-facing and can change
	QString name;
	int gridRows = 4;
	int gridCols = 4;
	int gridBorderWidth = 6;
	QColor gridLineColor = QColor(255, 255, 255);
	QVector<CellConfig> cells;
	WindowState window;
};

// Reusable layout template (no window state)
// When preserveSources is true, the template retains exact widget types and
// source/scene names. When false, non-structural widgets are reset to placeholders.
struct TemplateConfig {
	QString name;
	int gridRows = 4;
	int gridCols = 4;
	QVector<CellConfig> cells;
	bool preserveSources = false;
};

namespace MultiviewSerializer {

// Version written by this build. Version 1 (implicit, no "version" key) is the
// original per-collection file format, which is still readable.
constexpr int kSchemaVersion = 2;

QJsonObject WidgetToJson(const WidgetConfig &w);
WidgetConfig WidgetFromJson(const QJsonObject &obj);

QJsonObject CellToJson(const CellConfig &c);
CellConfig CellFromJson(const QJsonObject &obj);

QJsonObject MultiviewToJson(const MultiviewConfig &mv);
MultiviewConfig MultiviewFromJson(const QJsonObject &obj);

QJsonObject TemplateToJson(const TemplateConfig &t);
TemplateConfig TemplateFromJson(const QJsonObject &obj);

// Whole documents. Loading validates every layout (grid bounds, overlapping
// cells), assigns missing ids, and drops entries with empty or duplicate
// names, so callers always get a consistent set back.
QJsonObject MultiviewsToJson(const QVector<MultiviewConfig> &multiviews);
QVector<MultiviewConfig> MultiviewsFromJson(const QJsonObject &root);

QJsonObject TemplatesToJson(const QVector<TemplateConfig> &templates);
QVector<TemplateConfig> TemplatesFromJson(const QJsonObject &root);

} // namespace MultiviewSerializer
