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

#include "grid-model.hpp"

#include <QRect>

#include <algorithm>
#include <climits>

namespace GridModel {

namespace {

// Bounding rectangle of the selection, in grid positions
QRect SelectionBounds(const QSet<QPoint> &selection)
{
	int minR = INT_MAX, maxR = INT_MIN, minC = INT_MAX, maxC = INT_MIN;
	for (const QPoint &p : selection) {
		minR = std::min(minR, p.y());
		maxR = std::max(maxR, p.y());
		minC = std::min(minC, p.x());
		maxC = std::max(maxC, p.x());
	}
	return QRect(QPoint(minC, minR), QPoint(maxC, maxR));
}

// Indices of every cell that covers at least one selected position
QSet<int> TouchedCells(int rows, int cols, const QVector<int> &owners, const QSet<QPoint> &selection)
{
	QSet<int> touched;
	for (const QPoint &p : selection) {
		if (p.y() < 0 || p.y() >= rows || p.x() < 0 || p.x() >= cols)
			continue;
		int idx = owners[p.y() * cols + p.x()];
		if (idx >= 0)
			touched.insert(idx);
	}
	return touched;
}

// Removes the given cell indices, keeping the order of the rest
QVector<CellConfig> WithoutCells(const QVector<CellConfig> &cells, const QSet<int> &remove)
{
	QVector<CellConfig> kept;
	kept.reserve(cells.size());
	for (int i = 0; i < (int)cells.size(); i++) {
		if (!remove.contains(i))
			kept.append(cells[i]);
	}
	return kept;
}

CellConfig EmptyCell(int row, int col)
{
	CellConfig cell;
	cell.row = row;
	cell.col = col;
	return cell;
}

} // namespace

QVector<int> Ownership(int rows, int cols, const QVector<CellConfig> &cells)
{
	QVector<int> owners(std::max(0, rows) * std::max(0, cols), -1);
	for (int i = 0; i < (int)cells.size(); i++) {
		const CellConfig &cell = cells[i];
		for (int r = std::max(0, cell.row); r < cell.row + cell.rowSpan && r < rows; r++) {
			for (int c = std::max(0, cell.col); c < cell.col + cell.colSpan && c < cols; c++)
				owners[r * cols + c] = i;
		}
	}
	return owners;
}

bool IsWholeRectSelection(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	if (selection.isEmpty())
		return false;

	// Every position in the bounding rectangle must be selected and on the grid
	QRect bounds = SelectionBounds(selection);
	if (bounds.left() < 0 || bounds.top() < 0 || bounds.right() >= cols || bounds.bottom() >= rows)
		return false;
	if ((qsizetype)bounds.width() * bounds.height() != selection.size())
		return false;

	// Any cell the selection touches must lie entirely inside it
	QVector<int> owners = Ownership(rows, cols, cells);
	for (int idx : TouchedCells(rows, cols, owners, selection)) {
		const CellConfig &cell = cells[idx];
		QRect cellRect(cell.col, cell.row, cell.colSpan, cell.rowSpan);
		if (!bounds.contains(cellRect))
			return false;
	}
	return true;
}

int SingleCellAt(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	if (selection.isEmpty())
		return -1;

	QVector<int> owners = Ownership(rows, cols, cells);
	int found = -1;
	for (const QPoint &p : selection) {
		if (p.y() < 0 || p.y() >= rows || p.x() < 0 || p.x() >= cols)
			return -1;
		int idx = owners[p.y() * cols + p.x()];
		if (idx < 0 || (found >= 0 && idx != found))
			return -1;
		found = idx;
	}
	return found;
}

bool CanMerge(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	return selection.size() >= 2 && IsWholeRectSelection(rows, cols, cells, selection);
}

QVector<CellConfig> Merge(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	if (!CanMerge(rows, cols, cells, selection))
		return cells;

	QRect bounds = SelectionBounds(selection);
	QVector<int> owners = Ownership(rows, cols, cells);
	QVector<CellConfig> result = WithoutCells(cells, TouchedCells(rows, cols, owners, selection));

	CellConfig merged = EmptyCell(bounds.top(), bounds.left());
	merged.rowSpan = bounds.height();
	merged.colSpan = bounds.width();
	result.append(merged);
	return result;
}

bool CanReset(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	if (selection.isEmpty())
		return false;

	// Any selection inside a single cell can be reset
	if (SingleCellAt(rows, cols, cells, selection) >= 0)
		return true;

	return IsWholeRectSelection(rows, cols, cells, selection);
}

QVector<CellConfig> Reset(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	if (!CanReset(rows, cols, cells, selection))
		return cells;

	// A single 1x1 cell keeps its position and just loses its widget
	int single = SingleCellAt(rows, cols, cells, selection);
	if (single >= 0 && cells[single].rowSpan == 1 && cells[single].colSpan == 1) {
		QVector<CellConfig> result = cells;
		result[single].widget = WidgetConfig();
		return result;
	}

	// Otherwise split every touched cell into empty 1x1 cells covering the
	// whole area it occupied, not only the selected part of it
	QVector<int> owners = Ownership(rows, cols, cells);
	QSet<int> touched = TouchedCells(rows, cols, owners, selection);
	QVector<CellConfig> result = WithoutCells(cells, touched);

	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++) {
			if (touched.contains(owners[r * cols + c]))
				result.append(EmptyCell(r, c));
		}
	}
	return result;
}

bool CanSplit(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	QVector<int> owners = Ownership(rows, cols, cells);
	for (int idx : TouchedCells(rows, cols, owners, selection)) {
		if (cells[idx].rowSpan > 1 || cells[idx].colSpan > 1)
			return true;
	}
	return false;
}

QVector<CellConfig> Split(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	QVector<int> owners = Ownership(rows, cols, cells);
	QSet<int> touchedSet = TouchedCells(rows, cols, owners, selection);
	QVector<int> touched(touchedSet.begin(), touchedSet.end());
	std::sort(touched.begin(), touched.end());

	QVector<CellConfig> result = cells;
	QVector<CellConfig> pieces;
	for (int idx : touched) {
		const CellConfig &original = cells[idx];
		if (original.rowSpan <= 1 && original.colSpan <= 1)
			continue;

		result[idx].rowSpan = 1;
		result[idx].colSpan = 1;
		for (int r = original.row; r < original.row + original.rowSpan; r++) {
			for (int c = original.col; c < original.col + original.colSpan; c++) {
				if (r != original.row || c != original.col)
					pieces.append(EmptyCell(r, c));
			}
		}
	}
	result += pieces;
	return result;
}

QVector<CellConfig> Resize(const QVector<CellConfig> &cells, int newRows, int newCols)
{
	return Normalize(newRows, newCols, cells);
}

int ContentCellsLost(const QVector<CellConfig> &cells, int newRows, int newCols)
{
	newRows = std::clamp(newRows, kMinGridSize, kMaxGridSize);
	newCols = std::clamp(newCols, kMinGridSize, kMaxGridSize);

	int lost = 0;
	for (const CellConfig &cell : cells) {
		if (cell.widget.type == WidgetType::None)
			continue;
		if (cell.row < 0 || cell.col < 0 || cell.row >= newRows || cell.col >= newCols)
			lost++;
	}
	return lost;
}

QString CellName(int row, int col, int rowSpan, int colSpan)
{
	auto one = [](int r, int c) {
		return QString(QChar('A' + std::max(0, c))) + QString::number(r + 1);
	};
	QString name = one(row, col);
	if (rowSpan > 1 || colSpan > 1)
		name += ':' + one(row + std::max(1, rowSpan) - 1, col + std::max(1, colSpan) - 1);
	return name;
}

QVector<int> SelectedCellIndices(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection)
{
	QVector<int> owners = Ownership(rows, cols, cells);
	QSet<int> touched = TouchedCells(rows, cols, owners, selection);
	QVector<int> result(touched.begin(), touched.end());
	std::sort(result.begin(), result.end());
	return result;
}

QVector<CellConfig> Normalize(int rows, int cols, const QVector<CellConfig> &cells)
{
	rows = std::clamp(rows, kMinGridSize, kMaxGridSize);
	cols = std::clamp(cols, kMinGridSize, kMaxGridSize);

	QVector<bool> occupied(rows * cols, false);
	QVector<CellConfig> result;
	result.reserve(cells.size());

	for (const CellConfig &original : cells) {
		if (original.row < 0 || original.col < 0 || original.row >= rows || original.col >= cols)
			continue;

		CellConfig cell = original;
		cell.rowSpan = std::clamp(cell.rowSpan, 1, rows - cell.row);
		cell.colSpan = std::clamp(cell.colSpan, 1, cols - cell.col);

		bool overlaps = false;
		for (int r = cell.row; r < cell.row + cell.rowSpan && !overlaps; r++) {
			for (int c = cell.col; c < cell.col + cell.colSpan; c++) {
				if (occupied[r * cols + c]) {
					overlaps = true;
					break;
				}
			}
		}
		if (overlaps)
			continue;

		for (int r = cell.row; r < cell.row + cell.rowSpan; r++) {
			for (int c = cell.col; c < cell.col + cell.colSpan; c++)
				occupied[r * cols + c] = true;
		}
		result.append(cell);
	}

	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++) {
			if (!occupied[r * cols + c])
				result.append(EmptyCell(r, c));
		}
	}
	return result;
}

} // namespace GridModel
