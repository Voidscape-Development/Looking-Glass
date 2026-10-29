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

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>

#include "multiview-config.hpp"

using namespace MultiviewSerializer;

namespace {

QJsonObject Parse(const char *json)
{
	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &error);
	if (error.error != QJsonParseError::NoError)
		qFatal("bad test JSON: %s", qPrintable(error.errorString()));
	return doc.object();
}

MultiviewConfig SampleMultiview()
{
	MultiviewConfig mv;
	mv.id = "8a6f0a52-6f6b-4a5e-9d7c-1f2e3d4c5b6a";
	mv.name = "Main Wall";
	mv.gridRows = 2;
	mv.gridCols = 3;
	mv.gridBorderWidth = 4;
	mv.gridLineColor = QColor(10, 20, 30);
	mv.window.geometry = QRect(50, 60, 800, 450);
	mv.window.screenName = "DP-1";
	mv.window.screenIndex = 1;
	mv.window.fullscreen = true;
	mv.window.open = true;

	CellConfig scene;
	scene.row = 0;
	scene.col = 0;
	scene.rowSpan = 2;
	scene.colSpan = 2;
	scene.widget.type = WidgetType::Scene;
	scene.widget.sceneName = "Gameplay";
	scene.widget.sourceUuid = "0d9b1a3c-2e4f-4a6b-8c0d-1e2f3a4b5c6d";
	scene.widget.labelVisible = false;
	scene.widget.labelHAlign = Qt::AlignRight;
	scene.widget.labelVAlign = Qt::AlignTop;
	scene.widget.labelText = "Game";
	scene.widget.labelFont = "Arial,24,-1,5,700,0,0,0,0,0,0,0,0,0,0,1";
	scene.widget.labelBgColor = QColor(255, 0, 0, 64);
	scene.widget.safeRegion = true;
	scene.widget.showStatus = true;
	mv.cells.append(scene);

	CellConfig canvas;
	canvas.row = 0;
	canvas.col = 2;
	canvas.widget.type = WidgetType::Canvas;
	canvas.widget.canvasName = "Vertical";
	canvas.widget.labelVAlign = Qt::AlignVCenter;
	mv.cells.append(canvas);

	CellConfig program;
	program.row = 1;
	program.col = 2;
	program.widget.type = WidgetType::Program;
	mv.cells.append(program);

	return mv;
}

void CompareWidgets(const WidgetConfig &a, const WidgetConfig &b)
{
	QCOMPARE(a.type, b.type);
	QCOMPARE(a.sceneName, b.sceneName);
	QCOMPARE(a.sourceName, b.sourceName);
	QCOMPARE(a.sourceUuid, b.sourceUuid);
	QCOMPARE(a.canvasName, b.canvasName);
	QCOMPARE(a.labelVisible, b.labelVisible);
	QCOMPARE(a.labelHAlign, b.labelHAlign);
	QCOMPARE(a.labelVAlign, b.labelVAlign);
	QCOMPARE(a.labelText, b.labelText);
	QCOMPARE(a.labelFont, b.labelFont);
	QCOMPARE(a.labelBgColor, b.labelBgColor);
	QCOMPARE(a.safeRegion, b.safeRegion);
	QCOMPARE(a.showStatus, b.showStatus);
}

} // namespace

class TestSerializer : public QObject {
	Q_OBJECT

private slots:
	void multiviewRoundTrip()
	{
		MultiviewConfig original = SampleMultiview();
		QJsonObject json = MultiviewToJson(original);
		// Through text, as it is stored
		MultiviewConfig loaded =
			MultiviewFromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());

		QCOMPARE(loaded.id, original.id);
		QCOMPARE(loaded.name, original.name);
		QCOMPARE(loaded.gridRows, original.gridRows);
		QCOMPARE(loaded.gridCols, original.gridCols);
		QCOMPARE(loaded.gridBorderWidth, original.gridBorderWidth);
		QCOMPARE(loaded.gridLineColor, original.gridLineColor);
		QCOMPARE(loaded.window.geometry, original.window.geometry);
		QCOMPARE(loaded.window.screenName, original.window.screenName);
		QCOMPARE(loaded.window.screenIndex, original.window.screenIndex);
		QCOMPARE(loaded.window.fullscreen, original.window.fullscreen);
		QCOMPARE(loaded.window.open, original.window.open);
		QCOMPARE(loaded.cells.size(), original.cells.size());
		for (int i = 0; i < (int)original.cells.size(); i++) {
			QCOMPARE(loaded.cells[i].row, original.cells[i].row);
			QCOMPARE(loaded.cells[i].col, original.cells[i].col);
			QCOMPARE(loaded.cells[i].rowSpan, original.cells[i].rowSpan);
			QCOMPARE(loaded.cells[i].colSpan, original.cells[i].colSpan);
			CompareWidgets(loaded.cells[i].widget, original.cells[i].widget);
		}
	}

	void missingWidgetKeysUseDefaults()
	{
		// A widget with nothing but its type must load with the same
		// settings a newly created widget has
		WidgetConfig loaded = WidgetFromJson(Parse("{\"type\": \"scene\"}"));
		WidgetConfig defaults;
		QCOMPARE(loaded.type, WidgetType::Scene);
		QCOMPARE(loaded.labelVisible, defaults.labelVisible);
		QCOMPARE(loaded.labelVisible, true);
		QCOMPARE(loaded.labelHAlign, defaults.labelHAlign);
		QCOMPARE(loaded.labelVAlign, defaults.labelVAlign);
		QCOMPARE(loaded.labelVAlign, Qt::AlignBottom);
		QCOMPARE(loaded.labelBgColor, defaults.labelBgColor);
		QCOMPARE(loaded.safeRegion, false);
		QCOMPARE(loaded.showStatus, false);
	}

	void unknownValuesFallBack()
	{
		WidgetConfig loaded = WidgetFromJson(
			Parse("{\"type\": \"hologram\", \"label_bg_color\": \"not a color\", \"label_h_align\": 7}"));
		QCOMPARE(loaded.type, WidgetType::None);
		QCOMPARE(loaded.labelBgColor, WidgetConfig().labelBgColor);
		QCOMPARE(loaded.labelHAlign, Qt::AlignHCenter);
	}

	void legacyVersionOneFile()
	{
		// Written by the original per-collection file format
		QJsonObject root =
			Parse("{"
			      "\"multiviews\": [{"
			      "\"name\": \"Legacy\","
			      "\"grid_rows\": 2,"
			      "\"grid_cols\": 2,"
			      "\"grid_border_width\": 3,"
			      "\"grid_line_color\": \"#ff00ff00\","
			      "\"geometry_x\": 10,"
			      "\"geometry_y\": 20,"
			      "\"geometry_w\": 640,"
			      "\"geometry_h\": 360,"
			      "\"monitor_id\": 2,"
			      "\"fullscreen\": true,"
			      "\"was_open\": true,"
			      "\"cells\": ["
			      "{\"row\": 0, \"col\": 0, \"row_span\": 1, \"col_span\": 2,"
			      "\"widget\": {\"type\": \"scene\", \"scene_name\": \"Intro\", \"label_visible\": true,"
			      "\"label_h_align\": \"left\", \"label_v_align\": \"middle\","
			      "\"label_bg_color\": \"#80000000\"}},"
			      "{\"row\": 1, \"col\": 0, \"row_span\": 1, \"col_span\": 1,"
			      "\"widget\": {\"type\": \"program\", \"label_visible\": false,"
			      "\"label_h_align\": \"center\", \"label_v_align\": \"top\"}}"
			      "]"
			      "}]"
			      "}");

		QVector<MultiviewConfig> list = MultiviewsFromJson(root);
		QCOMPARE(list.size(), 1);
		const MultiviewConfig &mv = list[0];
		QVERIFY(!mv.id.isEmpty());
		QCOMPARE(mv.name, QString("Legacy"));
		QCOMPARE(mv.gridBorderWidth, 3);
		QCOMPARE(mv.gridLineColor, QColor(0, 255, 0));
		QCOMPARE(mv.window.geometry, QRect(10, 20, 640, 360));
		QCOMPARE(mv.window.screenIndex, 2);
		QVERIFY(mv.window.screenName.isEmpty());
		QCOMPARE(mv.window.fullscreen, true);
		QCOMPARE(mv.window.open, true);

		// The missing bottom-right cell is filled in
		QCOMPARE(mv.cells.size(), 3);
		QCOMPARE(mv.cells[0].colSpan, 2);
		QCOMPARE(mv.cells[0].widget.sceneName, QString("Intro"));
		QVERIFY(mv.cells[0].widget.sourceUuid.isEmpty());
		QCOMPARE(mv.cells[0].widget.labelHAlign, Qt::AlignLeft);
		QCOMPARE(mv.cells[0].widget.labelVAlign, Qt::AlignVCenter);
		QCOMPARE(mv.cells[1].widget.labelVisible, false);
		QCOMPARE(mv.cells[1].widget.labelVAlign, Qt::AlignTop);
		QCOMPARE(mv.cells[2].widget.type, WidgetType::None);
	}

	void gridValuesAreClamped()
	{
		MultiviewConfig mv = MultiviewFromJson(
			Parse("{\"name\": \"x\", \"grid_rows\": 99, \"grid_cols\": 0, \"grid_border_width\": 50}"));
		QCOMPARE(mv.gridRows, 16);
		QCOMPARE(mv.gridCols, 1);
		QCOMPARE(mv.gridBorderWidth, 10);
		QCOMPARE(mv.cells.size(), 16);
	}

	void missingGridValuesUseDefaults()
	{
		MultiviewConfig mv = MultiviewFromJson(Parse("{\"name\": \"x\"}"));
		MultiviewConfig defaults;
		QCOMPARE(mv.gridRows, defaults.gridRows);
		QCOMPARE(mv.gridCols, defaults.gridCols);
		QCOMPARE(mv.gridBorderWidth, defaults.gridBorderWidth);
		QCOMPARE(mv.gridLineColor, defaults.gridLineColor);
		QCOMPARE(mv.window.geometry, defaults.window.geometry);
		QCOMPARE(mv.window.open, false);
	}

	void documentDropsBadEntries()
	{
		QJsonObject root = Parse("{"
					 "\"version\": 2,"
					 "\"multiviews\": ["
					 "{\"id\": \"a\", \"name\": \"One\"},"
					 "{\"id\": \"a\", \"name\": \"Two\"},"
					 "{\"id\": \"b\", \"name\": \"One\"},"
					 "{\"id\": \"c\", \"name\": \"   \"},"
					 "{\"name\": \"Three\"}"
					 "]"
					 "}");

		QVector<MultiviewConfig> list = MultiviewsFromJson(root);
		QCOMPARE(list.size(), 3);
		QCOMPARE(list[0].name, QString("One"));
		QCOMPARE(list[0].id, QString("a"));
		// A duplicate id gets a fresh one rather than shadowing the first
		QCOMPARE(list[1].name, QString("Two"));
		QVERIFY(list[1].id != "a");
		QCOMPARE(list[2].name, QString("Three"));
		QVERIFY(!list[2].id.isEmpty());
	}

	void documentRoundTrip()
	{
		QVector<MultiviewConfig> original = {SampleMultiview()};
		QJsonObject root = MultiviewsToJson(original);
		QCOMPARE(root.value("version").toInt(), kSchemaVersion);

		QVector<MultiviewConfig> loaded = MultiviewsFromJson(root);
		QCOMPARE(loaded.size(), 1);
		QCOMPARE(loaded[0].id, original[0].id);
		QCOMPARE(loaded[0].cells.size(), original[0].cells.size());
	}

	void templateRoundTrip()
	{
		TemplateConfig t;
		t.name = "Four Up";
		t.gridRows = 2;
		t.gridCols = 2;
		t.preserveSources = true;
		for (int r = 0; r < 2; r++) {
			for (int c = 0; c < 2; c++) {
				CellConfig cell;
				cell.row = r;
				cell.col = c;
				cell.widget.type = WidgetType::Placeholder;
				t.cells.append(cell);
			}
		}

		QVector<TemplateConfig> loaded = TemplatesFromJson(TemplatesToJson({t, t}));
		QCOMPARE(loaded.size(), 1); // Duplicate name dropped
		QCOMPARE(loaded[0].name, t.name);
		QCOMPARE(loaded[0].preserveSources, true);
		QCOMPARE(loaded[0].cells.size(), 4);
		QCOMPARE(loaded[0].cells[3].widget.type, WidgetType::Placeholder);
	}

	void targetNameFollowsType()
	{
		WidgetConfig w;
		w.sceneName = "Scene";
		w.sourceName = "Source";
		w.type = WidgetType::Scene;
		QCOMPARE(w.targetName(), QString("Scene"));
		w.type = WidgetType::Source;
		QCOMPARE(w.targetName(), QString("Source"));
		w.type = WidgetType::Program;
		QVERIFY(w.targetName().isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestSerializer)
#include "test-serializer.moc"
