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

#include <QObject>
#include <QMenu>
#include <QAction>
#include <QPointer>

/**
 * Manages the "Looking Glass" submenu under OBS Tools.
 * Provides actions for creating, managing, and opening multiview windows,
 * and rebuilds the menu dynamically when multiviews are added/removed/renamed.
 */
class ToolsMenuManager : public QObject {
	Q_OBJECT

public:
	explicit ToolsMenuManager(QObject *parent = nullptr);
	~ToolsMenuManager();

	void initialize();
	void rebuildMenu();

private slots:
	void onCreateNew();
	void onManage();
	void onManageTemplates();
	void onOpenMultiview(const QString &id);
	void onCloseMultiview(const QString &id);
	void onEditMultiview(const QString &id);
	void onSendToMainDisplay(const QString &id);
	void onSetFullscreen(const QString &id, int screenIndex);
	void onSetWindowed(const QString &id);

private:
	QPointer<QMenu> submenu_;
	QList<QMenu *> dynamicSubmenus_;
};
