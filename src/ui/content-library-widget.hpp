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

#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "../core/content-ref.hpp"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Left-hand library of everything a multiview cell can show. Items can be
// dragged onto the grid, or clicked to fill the selected cells.
class ContentLibraryWidget : public QWidget {
	Q_OBJECT

public:
	explicit ContentLibraryWidget(QWidget *parent = nullptr);
	~ContentLibraryWidget() override;

	// Re-enumerates scenes, sources and canvases, keeping the filter text,
	// expanded groups, current item and scroll position
	void refresh();

	QString programSceneName() const { return programScene_; }
	// Empty when studio mode is off
	QString previewSceneName() const { return previewScene_; }

	QSize sizeHint() const override { return QSize(230, 400); }

signals:
	// Single click or Enter on an item
	void contentActivated(const ContentRef &ref);
	void liveScenesChanged(const QString &program, const QString &preview);

private slots:
	void scheduleRefresh();
	void handleFrontendEvent(int event);

private:
	void updateLiveScenes();
	void applyFilter();
	void applyBadges();

	QLineEdit *filterEdit_ = nullptr;
	QTreeWidget *tree_ = nullptr;
	QTimer refreshTimer_;
	QString programScene_;
	QString previewScene_;
	bool liveEmitted_ = false;
};
