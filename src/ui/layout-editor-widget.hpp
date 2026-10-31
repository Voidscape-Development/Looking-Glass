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

#include <QColor>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "../core/content-ref.hpp"
#include "../core/multiview-config.hpp"

class QAction;
class QFrame;
class QLabel;
class QSpinBox;
class QToolBar;
class QUndoStack;
class CellInspector;
class ContentLibraryWidget;
class GridEditorWidget;

// Everything the layout editor edits, as one comparable value
struct LayoutState {
	int rows = 4, cols = 4;
	QVector<CellConfig> cells;
	int lineWidth = 6;
	QColor lineColor = QColor(255, 255, 255);

	bool operator==(const LayoutState &other) const;
	bool operator!=(const LayoutState &other) const { return !(*this == other); }
};

/**
 * The whole layout editing surface: toolbar, content library, grid canvas,
 * inspector, status row and an undo stack. Used by the multiview dialog (and
 * later the template editor). All changes go through the undo stack; the grid
 * editor and the inspector are only views of the current LayoutState.
 */
class LayoutEditorWidget : public QWidget {
	Q_OBJECT

public:
	explicit LayoutEditorWidget(QWidget *parent = nullptr);

	// Loads a layout, clearing the undo history and marking it clean
	void setLayoutState(const LayoutState &state);
	LayoutState layoutState() const { return state_; }
	// Templates have no line style
	void setLineStyleEditable(bool editable);

	// Replaces the whole layout as one undoable step and shows toastText with
	// an Undo button (no toast when toastText is empty)
	void applyLayout(const LayoutState &state, const QString &undoText, const QString &toastText);

	bool isModified() const;
	QUndoStack *undoStack() const { return undoStack_; }

signals:
	void modifiedChanged(bool modified);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void buildToolbar();
	QWidget *buildCenter();

	// Applies a state to every view. Used by the undo commands only.
	void setState(const LayoutState &state, bool keepSelection);
	// Pushes one undo step; false (and nothing pushed) when nothing changes
	bool pushState(const LayoutState &after, const QString &text, int mergeId, const QVector<int> &affected);

	QVector<int> selectedIndices() const;
	bool mergeAllowed(QString *reason) const;

	void refreshAll();
	void refreshInspector();
	void refreshStatus();
	void refreshToolbar();
	void updateUndoActions();
	void setStatusText(const QString &text);

	void showStatusMessage(const QString &text, int ms);
	void clearStatusMessage();
	void showToast(const QString &text);
	void positionToast();

	void onSelectionChanged();
	void onGridSizeEdited(bool rowsChanged);
	void onWidgetsEdited(const QVector<WidgetConfig> &widgets, const QString &text, int mergeId);
	void onLineStyleEdited(int width, const QColor &color, int mergeId);
	void onContentActivated(const ContentRef &ref);
	void onContentDropped(int cellIndex, const ContentRef &ref);
	void onMerge();
	void onSplit();
	void onClear();

	LayoutState state_;
	bool updating_ = false;
	bool lineStyleEditable_ = true;

	QUndoStack *undoStack_ = nullptr;
	QToolBar *toolbar_ = nullptr;
	QSpinBox *rowsSpin_ = nullptr;
	QSpinBox *colsSpin_ = nullptr;
	QAction *mergeAction_ = nullptr;
	QAction *splitAction_ = nullptr;
	QAction *clearAction_ = nullptr;
	QAction *undoAction_ = nullptr;
	QAction *redoAction_ = nullptr;

	ContentLibraryWidget *library_ = nullptr;
	GridEditorWidget *grid_ = nullptr;
	CellInspector *inspector_ = nullptr;
	QWidget *center_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QString statusMessage_;
	QString statusFull_;
	QTimer statusTimer_;

	QFrame *toast_ = nullptr;
	QLabel *toastLabel_ = nullptr;
	QTimer toastTimer_;
};
