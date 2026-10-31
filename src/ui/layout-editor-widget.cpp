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

#include "layout-editor-widget.hpp"
#include "cell-inspector.hpp"
#include "content-library-widget.hpp"
#include "grid-editor-widget.hpp"
#include "widget-type-style.hpp"
#include "../plugin.hpp"
#include "../core/grid-model.hpp"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

// Output size used to describe cell sizes in the inspector
constexpr int kReferenceWidth = 1920;
constexpr int kReferenceHeight = 1080;

// Merge ids for grid size changes, so stepping a spin box is one undo step
constexpr int kMergeRows = 101;
constexpr int kMergeCols = 102;

constexpr int kToastMs = 4500;
constexpr int kStatusMessageMs = 3000;

bool WidgetsEqual(const WidgetConfig &a, const WidgetConfig &b)
{
	return a.type == b.type && a.sceneName == b.sceneName && a.sourceName == b.sourceName &&
	       a.sourceUuid == b.sourceUuid && a.placeholderPath == b.placeholderPath && a.canvasName == b.canvasName &&
	       a.labelVisible == b.labelVisible && a.labelHAlign == b.labelHAlign && a.labelVAlign == b.labelVAlign &&
	       a.labelText == b.labelText && a.labelFont == b.labelFont && a.labelSize == b.labelSize &&
	       a.labelBgColor == b.labelBgColor && a.safeRegion == b.safeRegion && a.showStatus == b.showStatus;
}

bool CellsEqual(const QVector<CellConfig> &a, const QVector<CellConfig> &b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); i++) {
		if (a[i].row != b[i].row || a[i].col != b[i].col || a[i].rowSpan != b[i].rowSpan ||
		    a[i].colSpan != b[i].colSpan || !WidgetsEqual(a[i].widget, b[i].widget))
			return false;
	}
	return true;
}

// One undo step: swaps the editor between two complete states
class LayoutCommand : public QUndoCommand {
public:
	using ApplyFn = std::function<void(const LayoutState &)>;

	LayoutCommand(ApplyFn apply, const LayoutState &before, const LayoutState &after, const QString &text,
		      int mergeId, const QVector<int> &affected)
		: QUndoCommand(text),
		  apply_(std::move(apply)),
		  before_(before),
		  after_(after),
		  mergeId_(mergeId),
		  affected_(affected)
	{
	}

	int id() const override { return mergeId_; }

	void undo() override { apply_(before_); }
	void redo() override { apply_(after_); }

	bool mergeWith(const QUndoCommand *other) override
	{
		if (mergeId_ == -1 || other->id() != mergeId_)
			return false;
		const LayoutCommand *next = static_cast<const LayoutCommand *>(other);
		// Edits to different cells stay separate steps
		if (next->affected_ != affected_)
			return false;

		after_ = next->after_;
		if (after_ == before_)
			setObsolete(true);
		return true;
	}

private:
	ApplyFn apply_;
	LayoutState before_;
	LayoutState after_;
	int mergeId_;
	QVector<int> affected_;
};

bool HasContentOrMerge(const CellConfig &cell)
{
	return cell.widget.type != WidgetType::None || cell.rowSpan > 1 || cell.colSpan > 1;
}

} // namespace

bool LayoutState::operator==(const LayoutState &other) const
{
	return rows == other.rows && cols == other.cols && lineWidth == other.lineWidth &&
	       lineColor == other.lineColor && CellsEqual(cells, other.cells);
}

LayoutEditorWidget::LayoutEditorWidget(QWidget *parent) : QWidget(parent)
{
	undoStack_ = new QUndoStack(this);
	connect(undoStack_, &QUndoStack::cleanChanged, this, [this](bool clean) { emit modifiedChanged(!clean); });

	statusTimer_.setSingleShot(true);
	connect(&statusTimer_, &QTimer::timeout, this, [this]() {
		statusMessage_.clear();
		refreshStatus();
	});
	toastTimer_.setSingleShot(true);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	buildToolbar();
	outer->addWidget(toolbar_);

	library_ = new ContentLibraryWidget();
	inspector_ = new CellInspector();
	center_ = buildCenter();

	auto *splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);
	splitter->addWidget(library_);
	splitter->addWidget(center_);
	splitter->addWidget(inspector_);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 0);
	splitter->setSizes({230, 640, 300});
	outer->addWidget(splitter, 1);

	// Toast floats over the bottom of the canvas
	toast_ = new QFrame(center_);
	toast_->setFrameShape(QFrame::StyledPanel);
	toast_->setAutoFillBackground(true);
	QPalette toastPalette = toast_->palette();
	toastPalette.setColor(QPalette::Window, toastPalette.color(QPalette::ToolTipBase));
	toastPalette.setColor(QPalette::WindowText, toastPalette.color(QPalette::ToolTipText));
	toastPalette.setColor(QPalette::ButtonText, toastPalette.color(QPalette::ToolTipText));
	toast_->setPalette(toastPalette);
	auto *toastLayout = new QHBoxLayout(toast_);
	toastLayout->setContentsMargins(12, 6, 8, 6);
	toastLabel_ = new QLabel();
	toastLabel_->setForegroundRole(QPalette::ToolTipText);
	toastLayout->addWidget(toastLabel_);
	auto *toastUndo = new QPushButton(LG_TEXT("LayoutEditor.Undo"));
	toastUndo->setFlat(true);
	toastUndo->setCursor(Qt::PointingHandCursor);
	toastLayout->addWidget(toastUndo);
	toast_->hide();
	connect(&toastTimer_, &QTimer::timeout, toast_, &QWidget::hide);
	connect(toastUndo, &QPushButton::clicked, this, [this]() {
		toast_->hide();
		toastTimer_.stop();
		undoStack_->undo();
	});
	grid_->installEventFilter(this);
	statusLabel_->installEventFilter(this);

	connect(grid_, &GridEditorWidget::selectionChanged, this, &LayoutEditorWidget::onSelectionChanged);
	connect(grid_, &GridEditorWidget::deleteRequested, this, &LayoutEditorWidget::onClear);
	connect(grid_, &GridEditorWidget::mergeRequested, this, &LayoutEditorWidget::onMerge);
	connect(grid_, &GridEditorWidget::contentDropped, this, &LayoutEditorWidget::onContentDropped);

	connect(inspector_, &CellInspector::widgetsEdited, this, &LayoutEditorWidget::onWidgetsEdited);
	connect(inspector_, &CellInspector::lineStyleEdited, this, &LayoutEditorWidget::onLineStyleEdited);
	connect(inspector_, &CellInspector::mergeRequested, this, &LayoutEditorWidget::onMerge);
	connect(inspector_, &CellInspector::clearRequested, this, &LayoutEditorWidget::onClear);

	connect(library_, &ContentLibraryWidget::contentActivated, this, &LayoutEditorWidget::onContentActivated);
	connect(library_, &ContentLibraryWidget::liveScenesChanged, grid_, &GridEditorWidget::setLiveScenes);
	grid_->setLiveScenes(library_->programSceneName(), library_->previewSceneName());

	connect(rowsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { onGridSizeEdited(true); });
	connect(colsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { onGridSizeEdited(false); });

	setLayoutState(LayoutState());
}

void LayoutEditorWidget::buildToolbar()
{
	toolbar_ = new QToolBar();
	toolbar_->setMovable(false);
	toolbar_->setFloatable(false);
	toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);

	rowsSpin_ = new QSpinBox();
	rowsSpin_->setRange(GridModel::kMinGridSize, GridModel::kMaxGridSize);
	rowsSpin_->setPrefix(LG_TEXT("LayoutEditor.RowsPrefix"));
	rowsSpin_->setToolTip(LG_TEXT("LayoutEditor.RowsTip"));
	toolbar_->addWidget(rowsSpin_);

	colsSpin_ = new QSpinBox();
	colsSpin_->setRange(GridModel::kMinGridSize, GridModel::kMaxGridSize);
	colsSpin_->setPrefix(LG_TEXT("LayoutEditor.ColumnsPrefix"));
	colsSpin_->setToolTip(LG_TEXT("LayoutEditor.ColumnsTip"));
	toolbar_->addWidget(colsSpin_);

	toolbar_->addSeparator();

	mergeAction_ = toolbar_->addAction(LG_TEXT("LayoutEditor.Merge"), this, &LayoutEditorWidget::onMerge);
	splitAction_ = toolbar_->addAction(LG_TEXT("LayoutEditor.Split"), this, &LayoutEditorWidget::onSplit);
	splitAction_->setToolTip(LG_TEXT("LayoutEditor.SplitTip"));
	clearAction_ = toolbar_->addAction(LG_TEXT("LayoutEditor.Clear"), this, &LayoutEditorWidget::onClear);
	clearAction_->setToolTip(LG_TEXT("LayoutEditor.ClearTip"));

	toolbar_->addSeparator();

	// Undo/redo shortcuts apply while focus is anywhere inside the editor. A
	// focused QLineEdit accepts the shortcut override for its own undo, so
	// typing in an inspector field never undoes the layout.
	undoAction_ = new QAction(LG_TEXT("LayoutEditor.Undo"), this);
	undoAction_->setShortcut(QKeySequence::Undo);
	undoAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(undoAction_, &QAction::triggered, undoStack_, &QUndoStack::undo);

	redoAction_ = new QAction(LG_TEXT("LayoutEditor.Redo"), this);
	QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
	QKeySequence ctrlY(QStringLiteral("Ctrl+Y"));
	if (!redoKeys.contains(ctrlY))
		redoKeys.append(ctrlY);
	redoAction_->setShortcuts(redoKeys);
	redoAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(redoAction_, &QAction::triggered, undoStack_, &QUndoStack::redo);

	toolbar_->addAction(undoAction_);
	toolbar_->addAction(redoAction_);
	addAction(undoAction_);
	addAction(redoAction_);

	connect(undoStack_, &QUndoStack::canUndoChanged, this, &LayoutEditorWidget::updateUndoActions);
	connect(undoStack_, &QUndoStack::canRedoChanged, this, &LayoutEditorWidget::updateUndoActions);
	connect(undoStack_, &QUndoStack::undoTextChanged, this, &LayoutEditorWidget::updateUndoActions);
	connect(undoStack_, &QUndoStack::redoTextChanged, this, &LayoutEditorWidget::updateUndoActions);
	updateUndoActions();
}

void LayoutEditorWidget::updateUndoActions()
{
	// Button text stays fixed so the toolbar does not change width
	auto tip = [](const char *verb, const QString &text, const QAction *action) {
		QString tip = QString::fromUtf8(verb);
		if (!text.isEmpty())
			tip += ' ' + text;
		if (!action->shortcut().isEmpty())
			tip += QString(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
		return tip;
	};
	undoAction_->setEnabled(undoStack_->canUndo());
	redoAction_->setEnabled(undoStack_->canRedo());
	undoAction_->setToolTip(tip(LG_TEXT("LayoutEditor.Undo"), undoStack_->undoText(), undoAction_));
	redoAction_->setToolTip(tip(LG_TEXT("LayoutEditor.Redo"), undoStack_->redoText(), redoAction_));
}

QWidget *LayoutEditorWidget::buildCenter()
{
	auto *center = new QWidget();
	auto *layout = new QVBoxLayout(center);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	grid_ = new GridEditorWidget();
	layout->addWidget(grid_, 1);

	auto *row = new QHBoxLayout();
	row->setSpacing(12);

	statusLabel_ = new QLabel();
	statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	row->addWidget(statusLabel_, 1);

	const WidgetType legend[] = {WidgetType::Program, WidgetType::Preview, WidgetType::Scene, WidgetType::Source,
				     WidgetType::Canvas};
	for (WidgetType type : legend) {
		auto *swatch = new QLabel();
		swatch->setFixedSize(10, 10);
		swatch->setStyleSheet(
			QString("background-color: %1; border-radius: 2px;").arg(WidgetTypeColor(type).name()));
		row->addWidget(swatch);
		auto *name = new QLabel(WidgetTypeDisplayName(type));
		name->setForegroundRole(QPalette::PlaceholderText);
		row->addWidget(name);
		row->addSpacing(4);
	}
	layout->addLayout(row);
	return center;
}

// --- Public API ---

void LayoutEditorWidget::setLayoutState(const LayoutState &state)
{
	LayoutState s = state;
	s.rows = std::clamp(s.rows, GridModel::kMinGridSize, GridModel::kMaxGridSize);
	s.cols = std::clamp(s.cols, GridModel::kMinGridSize, GridModel::kMaxGridSize);
	s.cells = GridModel::Normalize(s.rows, s.cols, s.cells);

	toast_->hide();
	toastTimer_.stop();
	undoStack_->clear();
	setState(s, false);
	undoStack_->setClean();
}

void LayoutEditorWidget::setLineStyleEditable(bool editable)
{
	lineStyleEditable_ = editable;
	inspector_->setLineStyleEditable(editable);
}

void LayoutEditorWidget::applyLayout(const LayoutState &state, const QString &undoText, const QString &toastText)
{
	LayoutState s = state;
	s.rows = std::clamp(s.rows, GridModel::kMinGridSize, GridModel::kMaxGridSize);
	s.cols = std::clamp(s.cols, GridModel::kMinGridSize, GridModel::kMaxGridSize);
	s.cells = GridModel::Normalize(s.rows, s.cols, s.cells);

	grid_->clearSelection();
	if (pushState(s, undoText, -1, {}) && !toastText.isEmpty())
		showToast(toastText);
}

bool LayoutEditorWidget::isModified() const
{
	return !undoStack_->isClean();
}

// --- State ---

void LayoutEditorWidget::setState(const LayoutState &state, bool keepSelection)
{
	// The grid emits selectionChanged while it is updated; refreshing once at
	// the end covers it
	updating_ = true;
	state_ = state;
	clearStatusMessage();

	grid_->setGridState(state_.rows, state_.cols, state_.cells, keepSelection);
	grid_->setLineStyle(state_.lineWidth, state_.lineColor);

	{
		QSignalBlocker rowsBlock(rowsSpin_);
		QSignalBlocker colsBlock(colsSpin_);
		rowsSpin_->setValue(state_.rows);
		colsSpin_->setValue(state_.cols);
	}

	refreshAll();
	updating_ = false;
}

bool LayoutEditorWidget::pushState(const LayoutState &after, const QString &text, int mergeId,
				   const QVector<int> &affected)
{
	if (after == state_)
		return false;

	toast_->hide();
	toastTimer_.stop();
	auto apply = [this](const LayoutState &s) {
		setState(s, true);
	};
	// redo() runs inside push() and applies the new state
	undoStack_->push(new LayoutCommand(apply, state_, after, text, mergeId, affected));
	return true;
}

QVector<int> LayoutEditorWidget::selectedIndices() const
{
	return GridModel::SelectedCellIndices(state_.rows, state_.cols, state_.cells, grid_->selectedPositions());
}

bool LayoutEditorWidget::mergeAllowed(QString *reason) const
{
	if (selectedIndices().size() < 2) {
		if (reason)
			*reason = LG_TEXT("LayoutEditor.SelectTwo");
		return false;
	}
	if (!GridModel::CanMerge(state_.rows, state_.cols, state_.cells, grid_->selectedPositions())) {
		if (reason)
			*reason = LG_TEXT("LayoutEditor.NeedRectangle");
		return false;
	}
	return true;
}

// --- Views ---

void LayoutEditorWidget::refreshAll()
{
	refreshInspector();
	refreshStatus();
	refreshToolbar();
}

void LayoutEditorWidget::refreshInspector()
{
	QVector<int> indices = selectedIndices();
	if (indices.isEmpty()) {
		inspector_->showLayout(state_.lineWidth, state_.lineColor);
		return;
	}

	if (indices.size() == 1) {
		const CellConfig &cell = state_.cells[indices[0]];
		auto edge = [](int i, int n, int total) {
			return qRound((double)i * total / n);
		};
		int w = edge(cell.col + cell.colSpan, state_.cols, kReferenceWidth) -
			edge(cell.col, state_.cols, kReferenceWidth);
		int h = edge(cell.row + cell.rowSpan, state_.rows, kReferenceHeight) -
			edge(cell.row, state_.rows, kReferenceHeight);
		QString title = QString(LG_TEXT("LayoutEditor.CellTitle"))
					.arg(GridModel::CellName(cell.row, cell.col, cell.rowSpan, cell.colSpan));
		QString subtitle = QString::fromUtf8("%1×%2 · %3×%4").arg(cell.colSpan).arg(cell.rowSpan).arg(w).arg(h);
		inspector_->showCells({cell.widget}, title, subtitle);
		return;
	}

	QVector<WidgetConfig> widgets;
	for (int idx : indices)
		widgets.append(state_.cells[idx].widget);
	inspector_->showCells(widgets, QString(LG_TEXT("Inspector.NCells")).arg(indices.size()), QString());
	QString reason;
	bool can = mergeAllowed(&reason);
	inspector_->setMergeState(can, reason);
}

void LayoutEditorWidget::refreshStatus()
{
	if (!statusMessage_.isEmpty()) {
		setStatusText(statusMessage_);
		return;
	}

	QVector<int> indices = selectedIndices();
	QString text;
	if (indices.isEmpty()) {
		int withContent = 0;
		for (const CellConfig &cell : state_.cells) {
			if (cell.widget.type != WidgetType::None)
				withContent++;
		}
		text = QString(LG_TEXT("LayoutEditor.StatusNone")).arg(withContent).arg(state_.cells.size());
	} else if (indices.size() == 1) {
		const CellConfig &cell = state_.cells[indices[0]];
		QStringList parts;
		parts << GridModel::CellName(cell.row, cell.col, cell.rowSpan, cell.colSpan);
		parts << WidgetTypeDisplayName(cell.widget.type);
		QString target = cell.widget.targetName();
		if (!target.isEmpty())
			parts << target;
		text = parts.join(QString::fromUtf8(" · "));
	} else {
		QString reason;
		if (mergeAllowed(&reason))
			text = QString(LG_TEXT("LayoutEditor.StatusManyMergeable")).arg(indices.size());
		else
			text = QString(LG_TEXT("LayoutEditor.StatusManyReason")).arg(indices.size()).arg(reason);
	}
	setStatusText(text);
}

void LayoutEditorWidget::setStatusText(const QString &text)
{
	statusFull_ = text;
	statusLabel_->setToolTip(text);
	statusLabel_->setText(statusLabel_->fontMetrics().elidedText(text, Qt::ElideRight, statusLabel_->width()));
}

void LayoutEditorWidget::refreshToolbar()
{
	QString reason;
	bool canMerge = mergeAllowed(&reason);
	QString mergeTip = LG_TEXT("LayoutEditor.MergeTip");
	if (!canMerge && !selectedIndices().isEmpty())
		mergeTip += QString::fromUtf8(" — ") + reason;
	mergeAction_->setEnabled(canMerge);
	mergeAction_->setToolTip(mergeTip);

	splitAction_->setEnabled(
		GridModel::CanSplit(state_.rows, state_.cols, state_.cells, grid_->selectedPositions()));

	bool canClear = false;
	for (int idx : selectedIndices()) {
		if (HasContentOrMerge(state_.cells[idx]))
			canClear = true;
	}
	clearAction_->setEnabled(canClear);
}

void LayoutEditorWidget::showStatusMessage(const QString &text, int ms)
{
	statusMessage_ = text;
	statusTimer_.start(ms);
	refreshStatus();
}

void LayoutEditorWidget::clearStatusMessage()
{
	statusMessage_.clear();
	statusTimer_.stop();
}

void LayoutEditorWidget::showToast(const QString &text)
{
	toastLabel_->setText(text);
	toast_->adjustSize();
	positionToast();
	toast_->show();
	toast_->raise();
	toastTimer_.start(kToastMs);
}

void LayoutEditorWidget::positionToast()
{
	toast_->adjustSize();
	QRect g = grid_->geometry();
	int x = g.left() + (g.width() - toast_->width()) / 2;
	int y = g.bottom() - toast_->height() - 16;
	toast_->move(qMax(g.left(), x), qMax(g.top(), y));
}

bool LayoutEditorWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == statusLabel_ && event->type() == QEvent::Resize)
		statusLabel_->setText(
			statusLabel_->fontMetrics().elidedText(statusFull_, Qt::ElideRight, statusLabel_->width()));
	if (watched == grid_ && toast_ && toast_->isVisible() &&
	    (event->type() == QEvent::Resize || event->type() == QEvent::Move))
		positionToast();
	return QWidget::eventFilter(watched, event);
}

// --- Edits ---

void LayoutEditorWidget::onSelectionChanged()
{
	if (updating_)
		return;
	clearStatusMessage();
	refreshAll();
}

void LayoutEditorWidget::onGridSizeEdited(bool rowsChanged)
{
	if (updating_)
		return;

	LayoutState after = state_;
	after.rows = rowsSpin_->value();
	after.cols = colsSpin_->value();
	if (after.rows == state_.rows && after.cols == state_.cols)
		return;

	int lost = GridModel::ContentCellsLost(state_.cells, after.rows, after.cols);
	after.cells = GridModel::Resize(state_.cells, after.rows, after.cols);
	if (pushState(after, LG_TEXT("LayoutEditor.Undo.Resize"), rowsChanged ? kMergeRows : kMergeCols, {}) &&
	    lost > 0)
		showToast(QString(LG_TEXT("LayoutEditor.RemovedCells")).arg(lost));
}

void LayoutEditorWidget::onWidgetsEdited(const QVector<WidgetConfig> &widgets, const QString &text, int mergeId)
{
	if (updating_)
		return;
	QVector<int> indices = selectedIndices();
	if (indices.isEmpty() || indices.size() != widgets.size())
		return;

	LayoutState after = state_;
	for (int i = 0; i < indices.size(); i++)
		after.cells[indices[i]].widget = widgets[i];
	pushState(after, text, mergeId, indices);
}

void LayoutEditorWidget::onLineStyleEdited(int width, const QColor &color, int mergeId)
{
	if (updating_ || !lineStyleEditable_)
		return;
	LayoutState after = state_;
	after.lineWidth = width;
	after.lineColor = color;
	pushState(after, LG_TEXT("Inspector.Undo.GridLines"), mergeId, {});
}

void LayoutEditorWidget::onContentActivated(const ContentRef &ref)
{
	QVector<int> indices = selectedIndices();
	if (indices.isEmpty()) {
		showStatusMessage(LG_TEXT("LayoutEditor.SelectFirst"), kStatusMessageMs);
		return;
	}

	LayoutState after = state_;
	for (int idx : indices)
		ContentRefs::Apply(after.cells[idx].widget, ref);
	QString text = indices.size() == 1 ? QString(LG_TEXT("LayoutEditor.Undo.SetContent"))
					   : QString(LG_TEXT("LayoutEditor.Undo.FillCells")).arg(indices.size());
	pushState(after, text, -1, {});
}

void LayoutEditorWidget::onContentDropped(int cellIndex, const ContentRef &ref)
{
	if (cellIndex < 0 || cellIndex >= state_.cells.size())
		return;
	LayoutState after = state_;
	ContentRefs::Apply(after.cells[cellIndex].widget, ref);
	pushState(after, LG_TEXT("LayoutEditor.Undo.SetContent"), -1, {});
}

void LayoutEditorWidget::onMerge()
{
	QString reason;
	if (!mergeAllowed(&reason)) {
		showStatusMessage(reason, kStatusMessageMs);
		return;
	}

	// The selection is the merged rectangle afterwards, so it stays selected
	LayoutState after = state_;
	after.cells = GridModel::Merge(state_.rows, state_.cols, state_.cells, grid_->selectedPositions());
	pushState(after, LG_TEXT("LayoutEditor.Undo.Merge"), -1, {});
}

void LayoutEditorWidget::onSplit()
{
	QSet<QPoint> selection = grid_->selectedPositions();
	if (!GridModel::CanSplit(state_.rows, state_.cols, state_.cells, selection))
		return;

	LayoutState after = state_;
	after.cells = GridModel::Split(state_.rows, state_.cols, state_.cells, selection);
	pushState(after, LG_TEXT("LayoutEditor.Undo.Split"), -1, {});
}

void LayoutEditorWidget::onClear()
{
	QVector<int> indices = selectedIndices();
	if (indices.isEmpty())
		return;

	// Reset one cell at a time (by its origin) so the selection does not have
	// to be a rectangle, and merged cells are broken apart
	QVector<QPoint> origins;
	for (int idx : indices)
		origins.append(QPoint(state_.cells[idx].col, state_.cells[idx].row));

	LayoutState after = state_;
	for (const QPoint &origin : origins)
		after.cells = GridModel::Reset(after.rows, after.cols, after.cells, {origin});

	if (pushState(after, LG_TEXT("LayoutEditor.Undo.Clear"), -1, {})) {
		showToast(indices.size() == 1 ? QString(LG_TEXT("LayoutEditor.ClearedOne"))
					      : QString(LG_TEXT("LayoutEditor.ClearedMany")).arg(indices.size()));
	}
}
