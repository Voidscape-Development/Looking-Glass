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
#include <QVector>

#include "../core/multiview-config.hpp"

/**
 * Interactive visual grid editor for multiview layouts.
 * Supports drag-select, cell merging, and per-cell widget assignment.
 * Tracks cell ownership to handle merged (multi-span) cells correctly.
 */
class GridEditorWidget : public QWidget {
	Q_OBJECT

public:
	explicit GridEditorWidget(QWidget *parent = nullptr);

	void setGrid(int rows, int cols, const QVector<CellConfig> &cells);
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

signals:
	void selectionChanged();
	void cellsChanged();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	QPoint gridPosFromPixel(const QPoint &pixel) const;
	QRect cellRect(int row, int col, int rowSpan, int colSpan) const;
	int ownerAt(int row, int col) const;
	void rebuildOwnership();
	void applyCells(const QVector<CellConfig> &cells);

	int rows_ = 4;
	int cols_ = 4;
	QVector<CellConfig> cells_;
	QVector<int> ownership_; // GridModel::Ownership(): row-major cell index, -1 = empty

	QSet<QPoint> selectedPositions_;
	bool dragging_ = false;
	QPoint dragStart_;
	QPoint dragCurrent_;

	static constexpr int kPadding = 2;
};
