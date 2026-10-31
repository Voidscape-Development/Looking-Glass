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
#include <QSet>
#include <QPoint>
#include <QRect>
#include <QVector>

#include "../core/multiview-config.hpp"
#include "../core/content-ref.hpp"

/**
 * Interactive visual grid editor for multiview layouts.
 * Draws the layout the way the multiview window will look (letterboxed at the
 * output aspect ratio, with the configured grid lines and labels) and supports
 * drag-select, cell merging, keyboard navigation, and content drag & drop.
 * Tracks cell ownership to handle merged (multi-span) cells correctly.
 *
 * Keyboard and drop actions only emit signals; the owner decides how to apply
 * them (for example through an undo stack).
 */
class GridEditorWidget : public QWidget {
	Q_OBJECT

public:
	explicit GridEditorWidget(QWidget *parent = nullptr);

	void setGrid(int rows, int cols, const QVector<CellConfig> &cells);
	// Like setGrid(), but optionally keeps the selection (minus positions that
	// are now out of bounds). Meant for undo/redo.
	void setGridState(int rows, int cols, const QVector<CellConfig> &cells, bool keepSelection);
	int gridRows() const { return rows_; }
	int gridCols() const { return cols_; }
	QVector<CellConfig> cells() const { return cells_; }

	QSet<QPoint> selectedPositions() const { return selectedPositions_; }
	int selectedCellIndex() const;

	void setWidgetForSelected(const WidgetConfig &widget);
	bool canMergeSelected() const;
	void mergeSelected();
	bool canResetSelected() const;
	void resetSelected();
	void clearSelection();

	// Grid line style and output shape, as configured on the multiview
	void setLineStyle(int width, const QColor &color);
	void setOutputAspect(double aspect);
	// Scenes currently on program and preview, for the tally hint
	void setLiveScenes(const QString &programName, const QString &previewName);

	QSize minimumSizeHint() const override { return QSize(480, 270); }
	QSize sizeHint() const override { return QSize(720, 405); }

signals:
	void selectionChanged();
	void cellsChanged();
	void deleteRequested();
	void mergeRequested();
	void contentDropped(int cellIndex, const ContentRef &ref);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	QRect gridRect() const;
	int edgeX(int col) const;
	int edgeY(int row) const;
	int scaledLineWidth() const;
	QPoint gridPosFromPixel(const QPoint &pixel, bool clamp) const;
	QRect cellRect(int row, int col, int rowSpan, int colSpan) const;
	int ownerAt(int row, int col) const;
	int cellIndexAtPixel(const QPoint &pixel) const;
	void rebuildOwnership();
	void applyCells(const QVector<CellConfig> &cells);
	void setSelection(const QSet<QPoint> &selection);
	QSet<QPoint> positionsInRect(int minC, int minR, int maxC, int maxR) const;
	QSet<QPoint> cellPositions(int cellIndex) const;
	void selectCell(int cellIndex);
	void moveSelection(int dCol, int dRow);
	bool isCellSelected(const CellConfig &cell) const;
	QVector<CellConfig> allCellsForPaint() const;

	void paintCell(QPainter &painter, const CellConfig &cell, const QRect &rect, const QRect &content) const;
	void paintLabel(QPainter &painter, const WidgetConfig &widget, const QRect &content, int cellHeight) const;
	void paintSafeAreas(QPainter &painter, const QRect &content) const;

	int rows_ = 4;
	int cols_ = 4;
	QVector<CellConfig> cells_;
	QVector<int> ownership_; // GridModel::Ownership(): row-major cell index, -1 = empty

	int lineWidth_ = 6;
	QColor lineColor_ = QColor(255, 255, 255);
	double aspect_ = 16.0 / 9.0;
	QString programScene_;
	QString previewScene_;

	QSet<QPoint> selectedPositions_;
	bool dragging_ = false;
	bool dragCtrl_ = false;
	QSet<QPoint> dragBase_;
	QPoint dragStart_;
	QPoint dragCurrent_;
	QPoint anchor_ = QPoint(-1, -1); // Last clicked position, for Shift+click
	int dropCell_ = -1;              // Cell highlighted during a drag & drop

	static constexpr int kMargin = 8;
	static constexpr double kReferenceWidth = 1920.0;
};
