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
#include "widget-type-style.hpp"
#include "../plugin.hpp"
#include "../core/grid-model.hpp"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>

#include <algorithm>
#include <cmath>

namespace {

const QColor kAccent(0x4f, 0x74, 0xff);
const QColor kBackdrop(22, 23, 26);
const QColor kEmptyCell(26, 27, 31);

// Same proportion OBS Studio's multiview (and the renderer) uses for label text
constexpr double kLabelFontToCellHeight = 1.0 / 9.81;

// Safe area proportions, matching the renderer
constexpr double kActionSafe = 0.035;
constexpr double kTitleSafe = 0.05;
constexpr double kFourByThreeSafe = 0.1625;
constexpr double kTickLength = 0.1;

// Dark tint of a type color, for cell backgrounds
QColor CellFill(WidgetType type)
{
	if (type == WidgetType::None)
		return kEmptyCell;
	QColor base = WidgetTypeColor(type);
	const QColor dark(24, 25, 29);
	const double k = 0.26;
	return QColor(qRound(dark.red() + (base.red() - dark.red()) * k),
		      qRound(dark.green() + (base.green() - dark.green()) * k),
		      qRound(dark.blue() + (base.blue() - dark.blue()) * k));
}

QString DefaultLabel(const WidgetConfig &w)
{
	if (!w.labelText.isEmpty())
		return w.labelText;

	switch (w.type) {
	case WidgetType::Preview:
		return LG_TEXT("GridEditor.Preview");
	case WidgetType::Program:
		return LG_TEXT("GridEditor.Program");
	case WidgetType::Canvas:
		return LG_TEXT("GridEditor.Canvas");
	case WidgetType::Scene:
		return w.sceneName.isEmpty() ? QString(LG_TEXT("GridEditor.Scene")) : w.sceneName;
	case WidgetType::Source:
		return w.sourceName.isEmpty() ? QString(LG_TEXT("GridEditor.Source")) : w.sourceName;
	case WidgetType::Placeholder:
		return LG_TEXT("GridEditor.Placeholder");
	default:
		return QString();
	}
}

// Largest rectangle with the given aspect ratio centered in outer
QRectF FitAspect(const QRectF &outer, double aspect)
{
	double w = outer.width();
	double h = w / aspect;
	if (h > outer.height()) {
		h = outer.height();
		w = h * aspect;
	}
	return QRectF(outer.left() + (outer.width() - w) / 2, outer.top() + (outer.height() - h) / 2, w, h);
}

} // namespace

GridEditorWidget::GridEditorWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(400, 300);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAcceptDrops(true);
	rebuildOwnership();
}

void GridEditorWidget::setGrid(int rows, int cols, const QVector<CellConfig> &cells)
{
	rows_ = qMax(1, rows);
	cols_ = qMax(1, cols);
	cells_ = cells;
	selectedPositions_.clear();
	anchor_ = QPoint(-1, -1);
	dropCell_ = -1;
	rebuildOwnership();
	update();
	emit selectionChanged();
	emit cellsChanged();
}

void GridEditorWidget::setGridState(int rows, int cols, const QVector<CellConfig> &cells, bool keepSelection)
{
	rows_ = qMax(1, rows);
	cols_ = qMax(1, cols);
	cells_ = cells;
	dropCell_ = -1;
	dragging_ = false;
	rebuildOwnership();

	QSet<QPoint> selection;
	if (keepSelection) {
		for (const QPoint &p : selectedPositions_) {
			if (p.x() >= 0 && p.x() < cols_ && p.y() >= 0 && p.y() < rows_)
				selection.insert(p);
		}
	}
	if (anchor_.x() >= cols_ || anchor_.y() >= rows_ || !keepSelection)
		anchor_ = QPoint(-1, -1);

	bool selectionDiffers = selection != selectedPositions_;
	selectedPositions_ = selection;
	update();
	if (selectionDiffers)
		emit selectionChanged();
	emit cellsChanged();
}

void GridEditorWidget::setLineStyle(int width, const QColor &color)
{
	lineWidth_ = qMax(0, width);
	lineColor_ = color;
	update();
}

void GridEditorWidget::setOutputAspect(double aspect)
{
	if (aspect > 0.0)
		aspect_ = aspect;
	update();
}

void GridEditorWidget::setLiveScenes(const QString &programName, const QString &previewName)
{
	if (programScene_ == programName && previewScene_ == previewName)
		return;
	programScene_ = programName;
	previewScene_ = previewName;
	update();
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
	// Growing a selection over merged cells must not make a lone merged cell
	// look mergeable; merging needs at least two distinct cells
	return GridModel::CanMerge(rows_, cols_, cells_, selectedPositions_) &&
	       GridModel::SelectedCellIndices(rows_, cols_, cells_, selectedPositions_).size() >= 2;
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

void GridEditorWidget::setSelection(const QSet<QPoint> &selection)
{
	if (selection == selectedPositions_)
		return;
	selectedPositions_ = selection;
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

int GridEditorWidget::cellIndexAtPixel(const QPoint &pixel) const
{
	QPoint gp = gridPosFromPixel(pixel, false);
	if (gp.x() < 0)
		return -1;
	return ownerAt(gp.y(), gp.x());
}

// --- Geometry ---

QRect GridEditorWidget::gridRect() const
{
	QRect avail = rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
	if (avail.width() <= 0 || avail.height() <= 0)
		return QRect();

	QRectF fit = FitAspect(QRectF(avail), aspect_);
	QRect r(qRound(fit.left()), qRound(fit.top()), qMax(1, qRound(fit.width())), qMax(1, qRound(fit.height())));
	return r;
}

int GridEditorWidget::edgeX(int col) const
{
	QRect g = gridRect();
	return g.left() + qRound((double)col * g.width() / cols_);
}

int GridEditorWidget::edgeY(int row) const
{
	QRect g = gridRect();
	return g.top() + qRound((double)row * g.height() / rows_);
}

int GridEditorWidget::scaledLineWidth() const
{
	if (lineWidth_ <= 0)
		return 0;
	return qMax(1, qRound(lineWidth_ * gridRect().width() / kReferenceWidth));
}

QPoint GridEditorWidget::gridPosFromPixel(const QPoint &pixel, bool clamp) const
{
	QRect g = gridRect();
	if (g.isEmpty())
		return QPoint(-1, -1);
	if (!clamp && !g.contains(pixel))
		return QPoint(-1, -1);

	int c = cols_ - 1;
	for (int i = 0; i < cols_; i++) {
		if (pixel.x() < edgeX(i + 1)) {
			c = i;
			break;
		}
	}
	int r = rows_ - 1;
	for (int i = 0; i < rows_; i++) {
		if (pixel.y() < edgeY(i + 1)) {
			r = i;
			break;
		}
	}
	return QPoint(c, r);
}

QRect GridEditorWidget::cellRect(int row, int col, int rowSpan, int colSpan) const
{
	return QRect(QPoint(edgeX(col), edgeY(row)), QPoint(edgeX(col + colSpan) - 1, edgeY(row + rowSpan) - 1));
}

// --- Selection helpers ---

QSet<QPoint> GridEditorWidget::positionsInRect(int minC, int minR, int maxC, int maxR) const
{
	QRect bounds(QPoint(minC, minR), QPoint(maxC, maxR));

	// Grow the rectangle until it fully contains every merged cell it touches
	bool changed = true;
	while (changed) {
		changed = false;
		for (const CellConfig &cell : cells_) {
			QRect cr(cell.col, cell.row, cell.colSpan, cell.rowSpan);
			if (bounds.intersects(cr) && !bounds.contains(cr)) {
				bounds = bounds.united(cr);
				changed = true;
			}
		}
	}

	bounds = bounds.intersected(QRect(0, 0, cols_, rows_));
	QSet<QPoint> result;
	for (int r = bounds.top(); r <= bounds.bottom(); r++) {
		for (int c = bounds.left(); c <= bounds.right(); c++)
			result.insert(QPoint(c, r));
	}
	return result;
}

QSet<QPoint> GridEditorWidget::cellPositions(int cellIndex) const
{
	QSet<QPoint> result;
	if (cellIndex < 0 || cellIndex >= (int)cells_.size())
		return result;
	const CellConfig &cell = cells_[cellIndex];
	for (int r = cell.row; r < cell.row + cell.rowSpan; r++) {
		for (int c = cell.col; c < cell.col + cell.colSpan; c++)
			result.insert(QPoint(c, r));
	}
	return result;
}

void GridEditorWidget::selectCell(int cellIndex)
{
	if (cellIndex < 0 || cellIndex >= (int)cells_.size())
		return;
	anchor_ = QPoint(cells_[cellIndex].col, cells_[cellIndex].row);
	setSelection(cellPositions(cellIndex));
}

bool GridEditorWidget::isCellSelected(const CellConfig &cell) const
{
	for (int r = cell.row; r < cell.row + cell.rowSpan; r++) {
		for (int c = cell.col; c < cell.col + cell.colSpan; c++) {
			if (selectedPositions_.contains(QPoint(c, r)))
				return true;
		}
	}
	return false;
}

void GridEditorWidget::moveSelection(int dCol, int dRow)
{
	int idx = selectedCellIndex();
	if (idx < 0)
		return;

	const CellConfig &cell = cells_[idx];
	int col = cell.col;
	int row = cell.row;
	if (dCol > 0)
		col = cell.col + cell.colSpan;
	else if (dCol < 0)
		col = cell.col - 1;
	if (dRow > 0)
		row = cell.row + cell.rowSpan;
	else if (dRow < 0)
		row = cell.row - 1;

	if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
		return;

	int owner = ownerAt(row, col);
	if (owner >= 0) {
		selectCell(owner);
	} else {
		anchor_ = QPoint(col, row);
		setSelection({QPoint(col, row)});
	}
}

QVector<CellConfig> GridEditorWidget::allCellsForPaint() const
{
	// Positions no cell covers are drawn as empty 1x1 cells
	QVector<CellConfig> all = cells_;
	for (int r = 0; r < rows_; r++) {
		for (int c = 0; c < cols_; c++) {
			if (ownerAt(r, c) >= 0)
				continue;
			CellConfig empty;
			empty.row = r;
			empty.col = c;
			all.append(empty);
		}
	}
	return all;
}

// --- Painting ---

void GridEditorWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);

	// Backdrop with a faint dot pattern
	painter.fillRect(rect(), kBackdrop);
	painter.setPen(QColor(255, 255, 255, 14));
	for (int y = kMargin; y < height(); y += 16) {
		for (int x = kMargin; x < width(); x += 16)
			painter.drawPoint(x, y);
	}

	QRect grid = gridRect();
	if (grid.isEmpty())
		return;
	painter.fillRect(grid, QColor(10, 10, 12));

	const int lw = scaledLineWidth();
	const QVector<CellConfig> all = allCellsForPaint();

	// Cell contents
	for (const CellConfig &cell : all) {
		QRect cr = cellRect(cell.row, cell.col, cell.rowSpan, cell.colSpan);
		int left = cell.col == 0 ? lw : (lw + 1) / 2;
		int top = cell.row == 0 ? lw : (lw + 1) / 2;
		int right = cell.col + cell.colSpan >= cols_ ? lw : (lw + 1) / 2;
		int bottom = cell.row + cell.rowSpan >= rows_ ? lw : (lw + 1) / 2;
		QRect content = cr.adjusted(left, top, -right, -bottom);
		paintCell(painter, cell, cr, content);
	}

	// Grid lines: every cell's outline, merged into one region so translucent
	// colors do not double up where cells share an edge
	if (lw > 0) {
		QRegion lines;
		const int half = lw / 2;
		for (const CellConfig &cell : all) {
			QRect cr = cellRect(cell.row, cell.col, cell.rowSpan, cell.colSpan);
			int x0 = cell.col == 0 ? cr.left() : cr.left() - half;
			int x1 = cell.col + cell.colSpan >= cols_ ? cr.right() + 1 - lw : cr.right() + 1 - half;
			int y0 = cell.row == 0 ? cr.top() : cr.top() - half;
			int y1 = cell.row + cell.rowSpan >= rows_ ? cr.bottom() + 1 - lw : cr.bottom() + 1 - half;
			lines += QRect(x0, y0, x1 + lw - x0, lw); // top
			lines += QRect(x0, y1, x1 + lw - x0, lw); // bottom
			lines += QRect(x0, y0, lw, y1 + lw - y0); // left
			lines += QRect(x1, y0, lw, y1 + lw - y0); // right
		}
		painter.save();
		painter.setClipRect(grid);
		painter.setClipRegion(lines, Qt::IntersectClip);
		painter.fillRect(grid, lineColor_);
		painter.restore();
	}

	// Selection
	painter.setRenderHint(QPainter::Antialiasing, false);
	for (const CellConfig &cell : all) {
		if (!isCellSelected(cell))
			continue;
		QRect cr = cellRect(cell.row, cell.col, cell.rowSpan, cell.colSpan);
		painter.fillRect(cr, QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 56));
		painter.setPen(QPen(kAccent, 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(cr).adjusted(1, 1, -1, -1));
	}

	// Drop target
	if (dropCell_ >= 0 && dropCell_ < (int)cells_.size()) {
		const CellConfig &cell = cells_[dropCell_];
		QRect cr = cellRect(cell.row, cell.col, cell.rowSpan, cell.colSpan);
		painter.fillRect(cr, QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 90));
		painter.setPen(QPen(kAccent, 3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(cr).adjusted(1.5, 1.5, -1.5, -1.5));
	}

	// Drag rectangle
	if (dragging_) {
		int minC = qMin(dragStart_.x(), dragCurrent_.x());
		int maxC = qMax(dragStart_.x(), dragCurrent_.x());
		int minR = qMin(dragStart_.y(), dragCurrent_.y());
		int maxR = qMax(dragStart_.y(), dragCurrent_.y());
		QRect dragRect = cellRect(minR, minC, maxR - minR + 1, maxC - minC + 1);
		painter.setPen(QPen(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 200), 1, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(dragRect.adjusted(0, 0, -1, -1));
	}
}

void GridEditorWidget::paintCell(QPainter &painter, const CellConfig &cell, const QRect &rect,
				 const QRect &content) const
{
	const WidgetConfig &w = cell.widget;

	painter.save();
	painter.setClipRect(rect);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect, CellFill(w.type));
	if (w.type == WidgetType::None)
		painter.fillRect(rect, QBrush(QColor(255, 255, 255, 22), Qt::BDiagPattern));

	if (content.width() > 4 && content.height() > 4) {
		if (w.safeRegion)
			paintSafeAreas(painter, content);

		if (w.type != WidgetType::None && w.labelVisible)
			paintLabel(painter, w, content, rect.height());

		// Type tag (top-left) and cell name (top-right)
		QFont small = painter.font();
		small.setPixelSize(10);
		painter.setFont(small);
		QFontMetrics fm(small);
		const int pad = 4;
		const int square = 7;
		int x = content.left() + pad;
		int y = content.top() + pad;
		int textY = y + (fm.height() - square) / 2;
		int tagRight = x;

		QString name = GridModel::CellName(cell.row, cell.col, cell.rowSpan, cell.colSpan);
		int nameW = fm.horizontalAdvance(name);
		int nameX = content.right() + 1 - pad - nameW;
		bool showName = nameX > x + square + pad;

		if (w.type != WidgetType::None) {
			int limit = (showName ? nameX - pad : content.right() + 1 - pad) - x;
			if (limit >= square) {
				painter.fillRect(QRect(x, textY, square, square), WidgetTypeColor(w.type));
				tagRight = x + square;
				int textW = limit - square - 4;
				if (textW >= 24) {
					QString tag = fm.elidedText(WidgetTypeDisplayName(w.type).toUpper(),
								    Qt::ElideRight, textW);
					painter.setPen(QColor(255, 255, 255, 200));
					painter.drawText(QPoint(x + square + 4, y + fm.ascent()), tag);
					tagRight = x + square + 4 + fm.horizontalAdvance(tag);
				}
			}
		}

		if (showName && nameX > tagRight) {
			painter.setPen(QColor(255, 255, 255, 80));
			painter.drawText(QPoint(nameX, y + fm.ascent()), name);
		}

		// Tally hint, only for scenes that are live right now
		if (w.type == WidgetType::Scene && w.showStatus && !w.sceneName.isEmpty()) {
			QColor tally;
			if (w.sceneName == programScene_)
				tally = WidgetTypeColor(WidgetType::Program);
			else if (w.sceneName == previewScene_)
				tally = WidgetTypeColor(WidgetType::Preview);
			if (tally.isValid()) {
				int bw = qMax(2, (int)(qMin(content.width(), content.height()) * 0.015));
				painter.fillRect(content.left(), content.top(), content.width(), bw, tally);
				painter.fillRect(content.left(), content.bottom() + 1 - bw, content.width(), bw, tally);
				painter.fillRect(content.left(), content.top(), bw, content.height(), tally);
				painter.fillRect(content.right() + 1 - bw, content.top(), bw, content.height(), tally);
			}
		}
	}

	painter.restore();
}

void GridEditorWidget::paintLabel(QPainter &painter, const WidgetConfig &w, const QRect &content, int cellHeight) const
{
	QString text = DefaultLabel(w);
	if (text.isEmpty())
		return;

	QFont font = painter.font();
	if (!w.labelFont.isEmpty()) {
		QFont custom;
		if (custom.fromString(w.labelFont))
			font = custom;
	}
	double px = cellHeight * kLabelFontToCellHeight * w.labelSize / 100.0;
	font.setPixelSize(qMax(6, qRound(px)));
	QFontMetrics fm(font);

	int textPx = font.pixelSize();
	int padding = qMax(1, qRound(textPx * 0.25));
	int bgPad = qMax(1, qRound(textPx * 0.15));
	int radius = qMax(1, qRound(textPx * 0.2));

	int maxW = int(content.width() * 0.9) - 2 * bgPad;
	if (maxW < 4)
		return;
	text = fm.elidedText(text, Qt::ElideRight, maxW);
	int tw = fm.horizontalAdvance(text);
	int th = fm.height();

	int x;
	if (w.labelHAlign & Qt::AlignHCenter)
		x = (content.width() - tw) / 2;
	else if (w.labelHAlign & Qt::AlignRight)
		x = content.width() - tw - padding;
	else
		x = padding;

	int y;
	if (w.labelVAlign & Qt::AlignVCenter)
		y = (content.height() - th) / 2;
	else if (w.labelVAlign & Qt::AlignBottom)
		y = content.height() - th - padding;
	else
		y = padding;

	x += content.left();
	y += content.top();

	if (w.labelBgColor.alpha() > 0) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(w.labelBgColor);
		painter.drawRoundedRect(QRect(x - bgPad, y - bgPad, tw + 2 * bgPad, th + 2 * bgPad), radius, radius);
	}

	painter.setFont(font);
	painter.setPen(Qt::white);
	painter.drawText(QPoint(x, y + fm.ascent()), text);
}

void GridEditorWidget::paintSafeAreas(QPainter &painter, const QRect &content) const
{
	QRectF f = FitAspect(QRectF(content), aspect_);
	const double w = f.width();
	const double h = f.height();

	painter.save();
	QPen dashed(QColor(0xd0, 0xd0, 0xd0, 190), 1, Qt::DashLine);
	dashed.setCosmetic(true);
	painter.setPen(dashed);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(f.adjusted(kActionSafe * w, kActionSafe * h, -kActionSafe * w, -kActionSafe * h));
	painter.drawRect(f.adjusted(kTitleSafe * w, kTitleSafe * h, -kTitleSafe * w, -kTitleSafe * h));
	painter.drawRect(f.adjusted(kFourByThreeSafe * w, kTitleSafe * h, -kFourByThreeSafe * w, -kTitleSafe * h));

	// Edge ticks as in the renderer, plus a small center crosshair
	QPen solid(QColor(0xd0, 0xd0, 0xd0, 190), 1);
	solid.setCosmetic(true);
	painter.setPen(solid);
	painter.drawLine(QPointF(f.left(), f.center().y()), QPointF(f.left() + kTickLength * w, f.center().y()));
	painter.drawLine(QPointF(f.center().x(), f.top()), QPointF(f.center().x(), f.top() + kTickLength * h));
	painter.drawLine(QPointF(f.right(), f.center().y()), QPointF(f.right() - kTickLength * w, f.center().y()));
	double arm = qMin(w, h) * 0.03;
	painter.drawLine(QPointF(f.center().x() - arm, f.center().y()), QPointF(f.center().x() + arm, f.center().y()));
	painter.drawLine(QPointF(f.center().x(), f.center().y() - arm), QPointF(f.center().x(), f.center().y() + arm));
	painter.restore();
}

// --- Mouse ---

void GridEditorWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	setFocus(Qt::MouseFocusReason);

	QPoint gp = gridPosFromPixel(event->pos(), false);
	if (gp.x() < 0) {
		// Clicking the backdrop deselects
		dragging_ = false;
		setSelection({});
		return;
	}

	const bool ctrl = event->modifiers() & Qt::ControlModifier;
	const bool shift = event->modifiers() & Qt::ShiftModifier;

	if (shift && anchor_.x() >= 0 && anchor_.x() < cols_ && anchor_.y() < rows_) {
		QSet<QPoint> sel = positionsInRect(qMin(anchor_.x(), gp.x()), qMin(anchor_.y(), gp.y()),
						   qMax(anchor_.x(), gp.x()), qMax(anchor_.y(), gp.y()));
		if (ctrl)
			sel.unite(selectedPositions_);
		dragging_ = false;
		setSelection(sel);
		return;
	}

	// Clicking selects the whole cell under the cursor, merged or not
	int owner = ownerAt(gp.y(), gp.x());
	QSet<QPoint> clicked = owner >= 0 ? cellPositions(owner) : QSet<QPoint>{gp};
	anchor_ = owner >= 0 ? QPoint(cells_[owner].col, cells_[owner].row) : gp;

	dragStart_ = gp;
	dragCurrent_ = gp;
	dragCtrl_ = ctrl;
	dragBase_ = ctrl ? selectedPositions_ : QSet<QPoint>();

	if (ctrl && selectedPositions_.contains(gp)) {
		// Ctrl+click on a selected cell toggles it off
		dragBase_.subtract(clicked);
		dragging_ = false;
	} else {
		dragBase_.unite(clicked);
		dragging_ = true;
	}

	setSelection(dragBase_);
	update();
}

void GridEditorWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging_)
		return;

	QPoint gp = gridPosFromPixel(event->pos(), true);
	if (gp.x() < 0 || gp == dragCurrent_)
		return;
	dragCurrent_ = gp;

	QSet<QPoint> sel =
		positionsInRect(qMin(dragStart_.x(), dragCurrent_.x()), qMin(dragStart_.y(), dragCurrent_.y()),
				qMax(dragStart_.x(), dragCurrent_.x()), qMax(dragStart_.y(), dragCurrent_.y()));
	sel.unite(dragBase_);
	setSelection(sel);
	update();
}

void GridEditorWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		dragging_ = false;
	update();
}

// --- Keyboard ---

void GridEditorWidget::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		if (selectedPositions_.isEmpty()) {
			int first = ownerAt(0, 0);
			if (first >= 0)
				selectCell(first);
			else
				setSelection({QPoint(0, 0)});
			event->accept();
			return;
		}
		if (selectedCellIndex() < 0 && selectedPositions_.size() != 1)
			break;
		int dCol = event->key() == Qt::Key_Left ? -1 : (event->key() == Qt::Key_Right ? 1 : 0);
		int dRow = event->key() == Qt::Key_Up ? -1 : (event->key() == Qt::Key_Down ? 1 : 0);
		moveSelection(dCol, dRow);
		event->accept();
		return;
	}
	case Qt::Key_Escape:
		// Only consume Escape when it clears something, so it still closes dialogs
		if (!selectedPositions_.isEmpty()) {
			clearSelection();
			event->accept();
			return;
		}
		break;
	case Qt::Key_Delete:
	case Qt::Key_Backspace:
		if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
			emit deleteRequested();
			event->accept();
			return;
		}
		break;
	case Qt::Key_M:
		if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
			emit mergeRequested();
			event->accept();
			return;
		}
		break;
	default:
		break;
	}
	QWidget::keyPressEvent(event);
}

// --- Drag & drop ---

void GridEditorWidget::dragEnterEvent(QDragEnterEvent *event)
{
	ContentRef ref;
	if (event->mimeData()->hasFormat(ContentRefs::MimeType()) &&
	    ContentRefs::Decode(event->mimeData()->data(ContentRefs::MimeType()), &ref))
		event->acceptProposedAction();
	else
		event->ignore();
}

void GridEditorWidget::dragMoveEvent(QDragMoveEvent *event)
{
	int idx = cellIndexAtPixel(event->position().toPoint());
	if (idx != dropCell_) {
		dropCell_ = idx;
		update();
	}
	if (idx >= 0)
		event->acceptProposedAction();
	else
		event->ignore();
}

void GridEditorWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
	dropCell_ = -1;
	update();
	QWidget::dragLeaveEvent(event);
}

void GridEditorWidget::dropEvent(QDropEvent *event)
{
	int idx = cellIndexAtPixel(event->position().toPoint());
	dropCell_ = -1;
	update();

	ContentRef ref;
	if (idx < 0 || !event->mimeData()->hasFormat(ContentRefs::MimeType()) ||
	    !ContentRefs::Decode(event->mimeData()->data(ContentRefs::MimeType()), &ref)) {
		event->ignore();
		return;
	}

	event->acceptProposedAction();
	setFocus(Qt::MouseFocusReason);
	selectCell(idx);
	emit contentDropped(idx, ref);
}
