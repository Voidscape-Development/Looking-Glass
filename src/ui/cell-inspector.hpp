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

#include <QWidget>
#include <QColor>
#include <QString>
#include <QVector>

#include <functional>

#include "../core/multiview-config.hpp"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QStackedWidget;
class QToolButton;

/**
 * Docked inspector that edits whatever is selected in the grid, live. It never
 * mutates the model: every change is emitted as edited copies of the widgets
 * it was last shown, for the host to apply through its undo stack and then
 * show again. Showing is done in place, so it is safe to call on every edit.
 */
class CellInspector : public QWidget {
	Q_OBJECT

public:
	explicit CellInspector(QWidget *parent = nullptr);

	// Nothing selected: the layout page (grid line width and color)
	void showLayout(int lineWidth, const QColor &lineColor);
	// Templates have no line style; the layout page then shows only the help text
	void setLineStyleEditable(bool editable);
	// One or more cells selected, in selection order. title e.g. "Cell B3" or
	// "4 cells"; subtitle e.g. "2×1 · 960×540".
	void showCells(const QVector<WidgetConfig> &widgets, const QString &title, const QString &subtitle);
	// Arrange section of the multi-selection page
	void setMergeState(bool canMerge, const QString &reasonIfNot);

	QSize sizeHint() const override { return QSize(300, 560); }

signals:
	// Same length and order as the last showCells() call. mergeId -1 is a
	// standalone undo step; consecutive emissions with the same id from one
	// continuous interaction may be coalesced into one.
	void widgetsEdited(const QVector<WidgetConfig> &widgets, const QString &undoText, int mergeId);
	void lineStyleEdited(int width, const QColor &color, int mergeId);
	void mergeRequested();
	void clearRequested();

private:
	enum Page { PageLayout, PageSingle, PageMulti };
	enum MergeId { MergeNone = -1, MergeLabelText = 1, MergeLabelSize, MergeOpacity, MergeLineWidth };

	// Label controls shared by the single and multi pages
	struct LabelControls {
		QWidget *container = nullptr;
		QCheckBox *show = nullptr;
		QLineEdit *text = nullptr;   // single only
		QPushButton *font = nullptr; // single only
		QSlider *size = nullptr;
		QLabel *sizeValue = nullptr;
		QPushButton *bg = nullptr;
		QSlider *opacity = nullptr;
		QLabel *opacityValue = nullptr;
		QButtonGroup *pos = nullptr;
		QToolButton *posButtons[9] = {};
	};

	void buildLayoutPage();
	QWidget *buildSinglePage();
	QWidget *buildMultiPage();
	void buildLabelControls(LabelControls &c, bool single);
	QWidget *makeSwatchRow(QPushButton **main, const QVector<QColor> &presets,
			       std::function<void(const QColor &)> onPreset, std::function<void()> onPick);

	void setPage(Page page);
	void showSingle(const QString &title, const QString &subtitle);
	void showMulti(const QString &title, const QString &subtitle);
	void syncLabelControls(LabelControls &c, bool single);
	void populateTargets(WidgetType type);
	void syncTarget(const WidgetConfig &w);

	void applyEdit(const std::function<void(WidgetConfig &)> &fn, const QString &undoText, int mergeId);
	void bindLabelControls(LabelControls &c);
	void setLabelBgRgb(const QColor &rgb);
	void pickLabelBg();
	void pickFont();
	void setLineWidth(int width);
	void setLineColor(const QColor &color);
	void pickLineColor();

	QStackedWidget *stack_ = nullptr;
	QVector<WidgetConfig> widgets_;

	// Layout page
	QWidget *lineGroup_ = nullptr;
	QSlider *lineWidthSlider_ = nullptr;
	QLabel *lineWidthValue_ = nullptr;
	QPushButton *lineColorBtn_ = nullptr;
	int lineWidth_ = 6;
	QColor lineColor_ = QColor(255, 255, 255);

	// Single-cell page
	QLabel *titleLabel_ = nullptr;
	QLabel *subtitleLabel_ = nullptr;
	QButtonGroup *typeGroup_ = nullptr;
	QComboBox *targetCombo_ = nullptr;
	QLabel *previewNote_ = nullptr;
	QLabel *noneNote_ = nullptr;
	LabelControls single_;
	QCheckBox *tallyCheck_ = nullptr;
	QLabel *tallyCaption_ = nullptr;
	QWidget *safeRow_ = nullptr;
	QCheckBox *safeCheck_ = nullptr;
	QString lastTitle_;
	WidgetType populatedType_ = WidgetType::None;
	bool populated_ = false;

	// Multi-selection page
	QLabel *multiTitle_ = nullptr;
	QLabel *multiSubtitle_ = nullptr;
	QPushButton *mergeBtn_ = nullptr;
	QLabel *mergeReason_ = nullptr;
	LabelControls multi_;
	QCheckBox *multiSafeCheck_ = nullptr;
};
