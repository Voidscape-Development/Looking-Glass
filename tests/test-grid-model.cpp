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

#include "grid-model.hpp"

namespace {

CellConfig Cell(int row, int col, int rowSpan = 1, int colSpan = 1, WidgetType type = WidgetType::None)
{
	CellConfig c;
	c.row = row;
	c.col = col;
	c.rowSpan = rowSpan;
	c.colSpan = colSpan;
	c.widget.type = type;
	return c;
}

// A rows x cols grid of 1x1 cells, all of the given type
QVector<CellConfig> Uniform(int rows, int cols, WidgetType type = WidgetType::None)
{
	QVector<CellConfig> cells;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++)
			cells.append(Cell(r, c, 1, 1, type));
	return cells;
}

QSet<QPoint> Rect(int row, int col, int rows, int cols)
{
	QSet<QPoint> selection;
	for (int r = row; r < row + rows; r++)
		for (int c = col; c < col + cols; c++)
			selection.insert(QPoint(c, r));
	return selection;
}

// Every grid position is covered by exactly one cell, and no cell leaves it
bool IsValidLayout(int rows, int cols, const QVector<CellConfig> &cells)
{
	QVector<int> coverage(rows * cols, 0);
	for (const CellConfig &cell : cells) {
		if (cell.row < 0 || cell.col < 0 || cell.rowSpan < 1 || cell.colSpan < 1 ||
		    cell.row + cell.rowSpan > rows || cell.col + cell.colSpan > cols)
			return false;
		for (int r = cell.row; r < cell.row + cell.rowSpan; r++)
			for (int c = cell.col; c < cell.col + cell.colSpan; c++)
				coverage[r * cols + c]++;
	}
	return std::all_of(coverage.begin(), coverage.end(), [](int n) { return n == 1; });
}

const CellConfig *CellAt(const QVector<CellConfig> &cells, int row, int col)
{
	for (const CellConfig &cell : cells) {
		if (row >= cell.row && row < cell.row + cell.rowSpan && col >= cell.col &&
		    col < cell.col + cell.colSpan)
			return &cell;
	}
	return nullptr;
}

} // namespace

class TestGridModel : public QObject {
	Q_OBJECT

private slots:
	void ownershipMapsMergedCells()
	{
		QVector<CellConfig> cells = {Cell(0, 0, 2, 2), Cell(0, 2), Cell(1, 2)};
		QVector<int> owners = GridModel::Ownership(2, 3, cells);
		QCOMPARE(owners, (QVector<int>{0, 0, 1, 0, 0, 2}));
	}

	void ownershipLeavesHolesEmpty()
	{
		QVector<int> owners = GridModel::Ownership(1, 2, {Cell(0, 0)});
		QCOMPARE(owners, (QVector<int>{0, -1}));
	}

	void canMergeFilledRectangle() { QVERIFY(GridModel::CanMerge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 2))); }

	void cannotMergeSingleCell() { QVERIFY(!GridModel::CanMerge(3, 3, Uniform(3, 3), Rect(1, 1, 1, 1))); }

	void cannotMergeLShape()
	{
		QSet<QPoint> selection = Rect(0, 0, 2, 2);
		selection.remove(QPoint(1, 1));
		QVERIFY(!GridModel::CanMerge(3, 3, Uniform(3, 3), selection));
	}

	void cannotMergePartialOverlap()
	{
		// A 2x2 merged cell at the top left; selecting only its right column
		// plus the column next to it would cut it in half
		QVector<CellConfig> cells = GridModel::Merge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 2));
		QVERIFY(!GridModel::CanMerge(3, 3, cells, Rect(0, 1, 2, 2)));
	}

	void canMergeAroundExistingMergedCell()
	{
		QVector<CellConfig> cells = GridModel::Merge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 2));
		QVERIFY(GridModel::CanMerge(3, 3, cells, Rect(0, 0, 3, 3)));
	}

	void mergeReplacesCellsWithOneSpanningCell()
	{
		QVector<CellConfig> cells =
			GridModel::Merge(3, 3, Uniform(3, 3, WidgetType::Program), Rect(1, 1, 2, 2));
		QVERIFY(IsValidLayout(3, 3, cells));
		QCOMPARE(cells.size(), 6);

		const CellConfig *merged = CellAt(cells, 2, 2);
		QVERIFY(merged);
		QCOMPARE(merged->row, 1);
		QCOMPARE(merged->col, 1);
		QCOMPARE(merged->rowSpan, 2);
		QCOMPARE(merged->colSpan, 2);
		QCOMPARE(merged->widget.type, WidgetType::None);

		// Cells outside the selection keep their content
		QCOMPARE(CellAt(cells, 0, 0)->widget.type, WidgetType::Program);
	}

	void mergeIgnoresInvalidSelection()
	{
		QVector<CellConfig> cells = Uniform(2, 2);
		QSet<QPoint> lShape = Rect(0, 0, 2, 2);
		lShape.remove(QPoint(0, 0));
		QCOMPARE(GridModel::Merge(2, 2, cells, lShape).size(), cells.size());
	}

	void singleCellAtFindsOwner()
	{
		QVector<CellConfig> cells = GridModel::Merge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 2));
		int idx = GridModel::SingleCellAt(3, 3, cells, Rect(0, 0, 2, 2));
		QVERIFY(idx >= 0);
		QCOMPARE(cells[idx].rowSpan, 2);
		QCOMPARE(GridModel::SingleCellAt(3, 3, cells, Rect(1, 1, 2, 2)), -1);
		QCOMPARE(GridModel::SingleCellAt(3, 3, cells, {}), -1);
	}

	void resetSingleCellClearsWidgetInPlace()
	{
		QVector<CellConfig> cells = Uniform(2, 2, WidgetType::Scene);
		cells[3].widget.sceneName = "Scene";
		QVector<CellConfig> reset = GridModel::Reset(2, 2, cells, Rect(1, 1, 1, 1));
		QCOMPARE(reset.size(), cells.size());
		QCOMPARE(reset[3].widget.type, WidgetType::None);
		QVERIFY(reset[3].widget.sceneName.isEmpty());
		QCOMPARE(reset[0].widget.type, WidgetType::Scene);
	}

	void resetMergedCellSplitsWholeArea()
	{
		QVector<CellConfig> cells = GridModel::Merge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 2));
		// Selecting one position inside the merged cell resets all of it
		QVERIFY(GridModel::CanReset(3, 3, cells, Rect(1, 1, 1, 1)));
		QVector<CellConfig> reset = GridModel::Reset(3, 3, cells, Rect(1, 1, 1, 1));
		QVERIFY(IsValidLayout(3, 3, reset));
		QCOMPARE(reset.size(), 9);
	}

	void cannotResetPartialOverlap()
	{
		QVector<CellConfig> cells = GridModel::Merge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 2));
		QVERIFY(!GridModel::CanReset(3, 3, cells, Rect(0, 1, 1, 2)));
		QVERIFY(!GridModel::CanReset(3, 3, cells, {}));
	}

	void resizeShrinkClampsSpansAndDropsOutside()
	{
		QVector<CellConfig> cells = GridModel::Merge(4, 4, Uniform(4, 4), Rect(1, 1, 3, 3));
		QVector<CellConfig> resized = GridModel::Resize(cells, 2, 2);
		QVERIFY(IsValidLayout(2, 2, resized));
		QCOMPARE(resized.size(), 4);
		const CellConfig *clamped = CellAt(resized, 1, 1);
		QCOMPARE(clamped->rowSpan, 1);
		QCOMPARE(clamped->colSpan, 1);
	}

	void resizeGrowFillsWithEmptyCells()
	{
		QVector<CellConfig> resized = GridModel::Resize(Uniform(2, 2, WidgetType::Program), 3, 4);
		QVERIFY(IsValidLayout(3, 4, resized));
		QCOMPARE(resized.size(), 12);
		QCOMPARE(CellAt(resized, 1, 1)->widget.type, WidgetType::Program);
		QCOMPARE(CellAt(resized, 2, 3)->widget.type, WidgetType::None);
	}

	void normalizeKeepsValidLayout()
	{
		QVector<CellConfig> cells = GridModel::Merge(3, 3, Uniform(3, 3), Rect(0, 0, 2, 3));
		QVector<CellConfig> normalized = GridModel::Normalize(3, 3, cells);
		QCOMPARE(normalized.size(), cells.size());
		for (int i = 0; i < (int)cells.size(); i++) {
			QCOMPARE(normalized[i].row, cells[i].row);
			QCOMPARE(normalized[i].col, cells[i].col);
			QCOMPARE(normalized[i].rowSpan, cells[i].rowSpan);
			QCOMPARE(normalized[i].colSpan, cells[i].colSpan);
		}
	}

	void normalizeRepairsBrokenLayout()
	{
		QVector<CellConfig> cells = {
			Cell(0, 0, 2, 2, WidgetType::Program), // valid
			Cell(1, 1, 1, 1, WidgetType::Scene),   // overlaps the first cell
			Cell(5, 5),                            // out of bounds
			Cell(-1, 0),                           // negative position
			Cell(2, 2, 9, 9, WidgetType::Source),  // span past the grid edge
		};
		QVector<CellConfig> normalized = GridModel::Normalize(3, 3, cells);
		QVERIFY(IsValidLayout(3, 3, normalized));
		QCOMPARE(CellAt(normalized, 1, 1)->widget.type, WidgetType::Program);
		QCOMPARE(CellAt(normalized, 2, 2)->widget.type, WidgetType::Source);
		QCOMPARE(CellAt(normalized, 2, 2)->rowSpan, 1);
	}

	void normalizeClampsGridSize()
	{
		QVector<CellConfig> normalized = GridModel::Normalize(40, 0, {});
		QVERIFY(IsValidLayout(GridModel::kMaxGridSize, GridModel::kMinGridSize, normalized));
	}

	void contentCellsLostCountsOnlyDroppedContent()
	{
		QVector<CellConfig> cells = Uniform(3, 3);
		cells[8].widget.type = WidgetType::Scene;   // (2,2)
		cells[5].widget.type = WidgetType::Source;  // (1,2)
		cells[0].widget.type = WidgetType::Program; // (0,0)
		QCOMPARE(GridModel::ContentCellsLost(cells, 3, 3), 0);
		QCOMPARE(GridModel::ContentCellsLost(cells, 3, 2), 2);
		QCOMPARE(GridModel::ContentCellsLost(cells, 2, 2), 2);
		QCOMPARE(GridModel::ContentCellsLost(cells, 1, 1), 2);
		QCOMPARE(GridModel::ContentCellsLost(cells, 5, 5), 0);
	}

	void contentCellsLostKeepsSpanningCellWithOriginInside()
	{
		QVector<CellConfig> cells = {Cell(0, 0, 3, 3, WidgetType::Program)};
		QCOMPARE(GridModel::ContentCellsLost(cells, 2, 2), 0);
	}

	void cellNames()
	{
		QCOMPARE(GridModel::CellName(0, 0), QString("A1"));
		QCOMPARE(GridModel::CellName(2, 3), QString("D3"));
		QCOMPARE(GridModel::CellName(15, 15), QString("P16"));
		QCOMPARE(GridModel::CellName(0, 0, 2, 2), QString("A1:B2"));
		QCOMPARE(GridModel::CellName(1, 1, 1, 3), QString("B2:D2"));
		QCOMPARE(GridModel::CellName(1, 1, 3, 1), QString("B2:B4"));
	}

	void selectedCellIndices()
	{
		// Merged 2x2 at origin, then 1x1 cells
		QVector<CellConfig> cells = {Cell(0, 0, 2, 2), Cell(0, 2), Cell(1, 2),
					     Cell(2, 0),       Cell(2, 1), Cell(2, 2)};
		QSet<QPoint> sel = {QPoint(0, 0), QPoint(1, 1), QPoint(2, 2), QPoint(0, 2)};
		QCOMPARE(GridModel::SelectedCellIndices(3, 3, cells, sel), (QVector<int>{0, 3, 5}));
		QVERIFY(GridModel::SelectedCellIndices(3, 3, cells, {}).isEmpty());
		// Out of bounds positions are ignored; (col,row) order: x=col
		QCOMPARE(GridModel::SelectedCellIndices(3, 3, cells, {QPoint(2, 0), QPoint(9, 9)}), (QVector<int>{1}));
	}
};

QTEST_GUILESS_MAIN(TestGridModel)
#include "test-grid-model.moc"
