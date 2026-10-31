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

#include <QPoint>
#include <QString>
#include <QSet>
#include <QVector>

#include "multiview-config.hpp"

/**
 * Pure grid operations shared by the layout editor, the multiview renderer,
 * and config loading. Grid positions are QPoint(col, row), matching the
 * editor's selection model. Nothing here touches Qt widgets or libobs.
 */
namespace GridModel {

constexpr int kMinGridSize = 1;
constexpr int kMaxGridSize = 16;

// Row-major owner map: ownership[row * cols + col] is the index of the cell
// covering that position, or -1 when no cell covers it.
QVector<int> Ownership(int rows, int cols, const QVector<CellConfig> &cells);

// True when the selection is a filled rectangle and every cell it touches lies
// entirely inside it. This is the precondition for both merge and multi-cell
// reset.
bool IsWholeRectSelection(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// Index of the single cell covering every selected position, or -1
int SingleCellAt(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

bool CanMerge(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// Replaces every cell inside the selection with one empty cell spanning it.
// Returns the cells unchanged when CanMerge() is false.
QVector<CellConfig> Merge(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

bool CanReset(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// Clears a single 1x1 cell in place; otherwise splits every touched cell back
// into empty 1x1 cells. Returns the cells unchanged when CanReset() is false.
QVector<CellConfig> Reset(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// True when the selection touches at least one merged (multi-span) cell
bool CanSplit(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// Breaks every merged cell the selection touches into 1x1 cells. The content
// stays on the top-left piece (which keeps its index); the other pieces are new
// empty cells appended at the end. Cells that are not merged are untouched.
QVector<CellConfig> Split(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// Re-fits cells to a new grid size: cells that still start inside the grid are
// kept with their spans clamped, the rest are dropped, and any uncovered
// position gets an empty 1x1 cell.
QVector<CellConfig> Resize(const QVector<CellConfig> &cells, int newRows, int newCols);

// Number of cells holding content (type != None) that Resize() to the given
// size would drop entirely, because their origin falls outside the new bounds.
int ContentCellsLost(const QVector<CellConfig> &cells, int newRows, int newCols);

// Spreadsheet-style name: column letter (A..P) then 1-based row. (0,0) is "A1";
// a 2x2 span at the origin is "A1:B2".
QString CellName(int row, int col, int rowSpan = 1, int colSpan = 1);

// Indices of the cells covering any selected position, ascending, no duplicates
QVector<int> SelectedCellIndices(int rows, int cols, const QVector<CellConfig> &cells, const QSet<QPoint> &selection);

// Repairs a layout loaded from disk: clamps spans, drops cells that start out
// of bounds or overlap an earlier cell, and fills holes with empty cells. A
// valid layout passes through unchanged.
QVector<CellConfig> Normalize(int rows, int cols, const QVector<CellConfig> &cells);

} // namespace GridModel
