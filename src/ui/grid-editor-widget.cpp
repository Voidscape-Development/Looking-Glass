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

#include "grid-editor-widget.hpp"
#include "../plugin.hpp"
#include "../core/grid-model.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>

GridEditorWidget::GridEditorWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(400, 300);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setMouseTracking(true);
	rebuildOwnership();
}

void GridEditorWidget::setGrid(int rows, int cols, const QVector<CellConfig> &cells)
{
	rows_ = qMax(1, rows);
	cols_ = qMax(1, cols);
	cells_ = cells;
	selectedPositions_.clear();
	rebuildOwnership();
	update();
	emit selectionChanged();
	emit cellsChanged();
}

int GridEditorWidget::selectedCellIndex() const
{
	return GridModel::SingleCellAt(rows_, cols_, cells_, selectedPositions_);
}

void GridEditorWidget::setWidgetForSelected(const WidgetConfig &widget)
{
	int idx = selectedCellIndex();
	if (idx >= 0 && idx < (int)cells_.size()) {
		cells_[idx].widget = widget;
		update();
		emit cellsChanged();
	}
}

bool GridEditorWidget::canMergeSelected() const
{
	return GridModel::CanMerge(rows_, cols_, cells_, selectedPositions_);
}

void GridEditorWidget::mergeSelected()
{
	if (!canMergeSelected())
		return;
	applyCells(GridModel::Merge(rows_, cols_, cells_, selectedPositions_));
}

bool GridEditorWidget::canResetSelected() const
{
	return GridModel::CanReset(rows_, cols_, cells_, selectedPositions_);
}

void GridEditorWidget::resetSelected()
{
	if (!canResetSelected())
		return;

	// Clearing a single 1x1 cell keeps it selected for further edits
	int single = selectedCellIndex();
	bool keepSelection = single >= 0 && cells_[single].rowSpan == 1 && cells_[single].colSpan == 1;

	QSet<QPoint> selection = selectedPositions_;
	applyCells(GridModel::Reset(rows_, cols_, cells_, selectedPositions_));
	if (keepSelection) {
		selectedPositions_ = selection;
		update();
		emit selectionChanged();
	}
}

void GridEditorWidget::applyCells(const QVector<CellConfig> &cells)
{
	cells_ = cells;
	selectedPositions_.clear();
	rebuildOwnership();
	update();
	emit selectionChanged();
	emit cellsChanged();
}

void GridEditorWidget::clearSelection()
{
	selectedPositions_.clear();
	update();
	emit selectionChanged();
}

void GridEditorWidget::rebuildOwnership()
{
	ownership_ = GridModel::Ownership(rows_, cols_, cells_);
}

int GridEditorWidget::ownerAt(int row, int col) const
{
	if (row < 0 || row >= rows_ || col < 0 || col >= cols_)
		return -1;
	return ownership_[row * cols_ + col];
}

QPoint GridEditorWidget::gridPosFromPixel(const QPoint &pixel) const
{
	int cellW = width() / cols_;
	int cellH = height() / rows_;
	if (cellW <= 0 || cellH <= 0)
		return QPoint(-1, -1);
	int c = qBound(0, pixel.x() / cellW, cols_ - 1);
	int r = qBound(0, pixel.y() / cellH, rows_ - 1);
	return QPoint(c, r);
}

QRect GridEditorWidget::cellRect(int row, int col, int rowSpan, int colSpan) const
{
	int cellW = width() / cols_;
	int cellH = height() / rows_;
	return QRect(col * cellW + kPadding, row * cellH + kPadding, colSpan * cellW - 2 * kPadding,
		     rowSpan * cellH - 2 * kPadding);
}

void GridEditorWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// Background
	painter.fillRect(rect(), QColor(30, 30, 30));

	int cellW = width() / cols_;
	int cellH = height() / rows_;

	// Draw grid lines clipped to the actual grid area
	int gridW = cellW * cols_;
	int gridH = cellH * rows_;
	painter.setPen(QColor(60, 60, 60));
	for (int r = 0; r <= rows_; r++)
		painter.drawLine(0, r * cellH, gridW, r * cellH);
	for (int c = 0; c <= cols_; c++)
		painter.drawLine(c * cellW, 0, c * cellW, gridH);

	// Draw cells
	for (int i = 0; i < cells_.size(); i++) {
		const CellConfig &cell = cells_[i];
		QRect cr = cellRect(cell.row, cell.col, cell.rowSpan, cell.colSpan);

		// Background color based on widget type
		QColor bg(50, 50, 50);
		switch (cell.widget.type) {
		case WidgetType::Preview:
			bg = QColor(40, 80, 40);
			break;
		case WidgetType::Program:
			bg = QColor(120, 30, 30);
			break;
		case WidgetType::Canvas:
			bg = QColor(40, 40, 100);
			break;
		case WidgetType::Scene:
			bg = QColor(60, 60, 80);
			break;
		case WidgetType::Source:
			bg = QColor(80, 60, 40);
			break;
		case WidgetType::Placeholder:
			bg = QColor(60, 60, 60);
			break;
		default:
			break;
		}
		painter.fillRect(cr, bg);

		// Border
		bool selected = false;
		for (int r = cell.row; r < cell.row + cell.rowSpan; r++) {
			for (int c = cell.col; c < cell.col + cell.colSpan; c++) {
				if (selectedPositions_.contains(QPoint(c, r))) {
					selected = true;
					break;
				}
			}
			if (selected)
				break;
		}

		painter.setPen(QPen(selected ? QColor(0, 150, 255) : QColor(80, 80, 80), selected ? 2 : 1));
		painter.drawRect(cr);

		// Label text
		if (cell.widget.type != WidgetType::None) {
			QString label;
			if (!cell.widget.labelText.isEmpty()) {
				label = cell.widget.labelText;
			} else {
				switch (cell.widget.type) {
				case WidgetType::Preview:
					label = LG_TEXT("GridEditor.Preview");
					break;
				case WidgetType::Program:
					label = LG_TEXT("GridEditor.Program");
					break;
				case WidgetType::Canvas:
					label = LG_TEXT("GridEditor.Canvas");
					break;
				case WidgetType::Scene:
					label = cell.widget.sceneName.isEmpty() ? LG_TEXT("GridEditor.Scene")
										: cell.widget.sceneName;
					break;
				case WidgetType::Source:
					label = cell.widget.sourceName.isEmpty() ? LG_TEXT("GridEditor.Source")
										 : cell.widget.sourceName;
					break;
				case WidgetType::Placeholder:
					label = LG_TEXT("GridEditor.Placeholder");
					break;
				default:
					break;
				}
			}

			if (!label.isEmpty()) {
				QFont font = painter.font();
				font.setPointSize(10);
				painter.setFont(font);
				painter.setPen(Qt::white);

				int flags = (int)cell.widget.labelHAlign | (int)cell.widget.labelVAlign;
				painter.drawText(cr.adjusted(4, 4, -4, -4), flags, label);
			}
		}
	}

	// Draw empty cells (not owned by any CellConfig)
	for (int r = 0; r < rows_; r++) {
		for (int c = 0; c < cols_; c++) {
			if (ownerAt(r, c) >= 0)
				continue;
			QRect cr = cellRect(r, c, 1, 1);
			bool selected = selectedPositions_.contains(QPoint(c, r));
			painter.setPen(QPen(selected ? QColor(0, 150, 255) : QColor(80, 80, 80), selected ? 2 : 1));
			painter.drawRect(cr);
		}
	}

	// Drag rectangle overlay
	if (dragging_) {
		int minC = qMin(dragStart_.x(), dragCurrent_.x());
		int maxC = qMax(dragStart_.x(), dragCurrent_.x());
		int minR = qMin(dragStart_.y(), dragCurrent_.y());
		int maxR = qMax(dragStart_.y(), dragCurrent_.y());
		QRect dragRect = cellRect(minR, minC, maxR - minR + 1, maxC - minC + 1);
		painter.setPen(QPen(QColor(0, 150, 255, 180), 2, Qt::DashLine));
		painter.setBrush(QColor(0, 150, 255, 40));
		painter.drawRect(dragRect);
	}
}

void GridEditorWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	QPoint gp = gridPosFromPixel(event->pos());
	if (gp.x() < 0)
		return;

	dragStart_ = gp;
	dragCurrent_ = gp;
	dragging_ = true;

	bool ctrl = event->modifiers() & Qt::ControlModifier;
	if (!ctrl)
		selectedPositions_.clear();

	if (ctrl && selectedPositions_.contains(gp))
		selectedPositions_.remove(gp);
	else
		selectedPositions_.insert(gp);

	update();
	emit selectionChanged();
}

void GridEditorWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging_)
		return;

	QPoint gp = gridPosFromPixel(event->pos());
	if (gp.x() < 0)
		return;

	dragCurrent_ = gp;

	// Build selection from drag rect
	if (!(event->modifiers() & Qt::ControlModifier))
		selectedPositions_.clear();

	int minC = qMin(dragStart_.x(), dragCurrent_.x());
	int maxC = qMax(dragStart_.x(), dragCurrent_.x());
	int minR = qMin(dragStart_.y(), dragCurrent_.y());
	int maxR = qMax(dragStart_.y(), dragCurrent_.y());

	for (int r = minR; r <= maxR; r++)
		for (int c = minC; c <= maxC; c++)
			selectedPositions_.insert(QPoint(c, r));

	update();
	emit selectionChanged();
}

void GridEditorWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		dragging_ = false;
	update();
}
