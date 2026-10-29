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

#include "tools-menu.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "multiview-edit-dialog.hpp"
#include "multiview-manage-dialog.hpp"
#include "template-manage-dialog.hpp"
#include "multiview-window.hpp"

#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QMenuBar>
#include <QScreen>
#include <QGuiApplication>

ToolsMenuManager::ToolsMenuManager(QObject *parent) : QObject(parent) {}

ToolsMenuManager::~ToolsMenuManager()
{
	if (submenu_)
		delete submenu_;
}

void ToolsMenuManager::initialize()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	if (!mainWindow)
		return;

	// OBS gives the Tools menu the stable Qt objectName "menuTools", so look it
	// up by that instead of the localized display text. Fall back to scanning
	// the menu bar's actions in case a future OBS release changes the name.
	QMenu *toolsMenu = mainWindow->findChild<QMenu *>("menuTools");
	if (!toolsMenu) {
		for (QAction *action : mainWindow->menuBar()->actions()) {
			if (action->menu() && action->objectName() == "menuTools") {
				toolsMenu = action->menu();
				break;
			}
		}
	}
	if (!toolsMenu)
		return;

	submenu_ = new QMenu(LG_TEXT("LookingGlass"), toolsMenu);
	toolsMenu->addMenu(submenu_);

	rebuildMenu();

	// Connect to ConfigManager signals
	ConfigManager *cm = GetConfigManager();
	connect(cm, &ConfigManager::multiviewAdded, this, &ToolsMenuManager::rebuildMenu);
	connect(cm, &ConfigManager::multiviewRemoved, this, &ToolsMenuManager::rebuildMenu);
	connect(cm, &ConfigManager::multiviewRenamed, this, &ToolsMenuManager::rebuildMenu);
	connect(cm, &ConfigManager::multiviewsReloaded, this, &ToolsMenuManager::rebuildMenu);
}

void ToolsMenuManager::rebuildMenu()
{
	if (!submenu_)
		return;

	submenu_->clear();
	for (QMenu *menu : dynamicSubmenus_)
		delete menu;
	dynamicSubmenus_.clear();

	QAction *createAction = submenu_->addAction(LG_TEXT("ToolsMenu.CreateNewMultiview"));
	connect(createAction, &QAction::triggered, this, &ToolsMenuManager::onCreateNew);

	QAction *manageAction = submenu_->addAction(LG_TEXT("ToolsMenu.ManageMultiviews"));
	connect(manageAction, &QAction::triggered, this, &ToolsMenuManager::onManage);

	QAction *manageTemplatesAction = submenu_->addAction(LG_TEXT("ToolsMenu.ManageTemplates"));
	connect(manageTemplatesAction, &QAction::triggered, this, &ToolsMenuManager::onManageTemplates);

	const QVector<MultiviewConfig> multiviews = GetConfigManager()->multiviews();
	if (!multiviews.isEmpty()) {
		submenu_->addSeparator();

		QList<QScreen *> screens = QGuiApplication::screens();

		for (const MultiviewConfig &mv : multiviews) {
			// Actions capture the id, which survives renames
			const QString id = mv.id;
			QMenu *mvMenu = new QMenu(mv.name, submenu_);
			dynamicSubmenus_.append(mvMenu);

			// Open action
			QAction *openAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Open"));
			connect(openAction, &QAction::triggered, this, [this, id]() { onOpenMultiview(id); });

			// Close action - only meaningful while a window is open, so its
			// enabled state is refreshed each time the submenu is shown
			QAction *closeAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Close"));
			connect(closeAction, &QAction::triggered, this, [this, id]() { onCloseMultiview(id); });
			connect(mvMenu, &QMenu::aboutToShow, this, [closeAction, id]() {
				closeAction->setEnabled(MultiviewWindow::findById(id) != nullptr);
			});

			// Edit action
			QAction *editAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Edit"));
			connect(editAction, &QAction::triggered, this, [this, id]() { onEditMultiview(id); });

			// Send to main display action
			QAction *sendToMainAction = mvMenu->addAction(LG_TEXT("ToolsMenu.SendToMainDisplay"));
			connect(sendToMainAction, &QAction::triggered, this, [this, id]() { onSendToMainDisplay(id); });

			mvMenu->addSeparator();

			// Fullscreen options for each monitor
			for (int i = 0; i < (int)screens.size(); i++) {
				QString label = QString(LG_TEXT("ToolsMenu.FullscreenOn")).arg(screens[i]->name());
				QAction *fsAction = mvMenu->addAction(label);
				connect(fsAction, &QAction::triggered, this,
					[this, id, i]() { onSetFullscreen(id, i); });
			}

			// Windowed option
			mvMenu->addSeparator();
			QAction *windowedAction = mvMenu->addAction(LG_TEXT("ToolsMenu.Windowed"));
			connect(windowedAction, &QAction::triggered, this, [this, id]() { onSetWindowed(id); });

			submenu_->addMenu(mvMenu);
		}
	}
}

void ToolsMenuManager::onCreateNew()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	MultiviewEditDialog dlg(MultiviewConfig(), true, mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onManage()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	ManageMultiviewsDialog dlg(mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onManageTemplates()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	ManageTemplatesDialog dlg(mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onOpenMultiview(const QString &id)
{
	MultiviewWindow::openOrFocus(id);
}

void ToolsMenuManager::onCloseMultiview(const QString &id)
{
	MultiviewWindow::closeById(id);
}

void ToolsMenuManager::onEditMultiview(const QString &id)
{
	// An open window reloads through ConfigManager::multiviewUpdated
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	MultiviewEditDialog dlg(GetConfigManager()->getMultiview(id), false, mainWindow);
	dlg.exec();
}

void ToolsMenuManager::onSendToMainDisplay(const QString &id)
{
	// Open the window if not already open, then move it to the main display
	MultiviewWindow::openOrFocus(id);
	if (MultiviewWindow *win = MultiviewWindow::findById(id))
		win->sendToMainDisplay();
}

void ToolsMenuManager::onSetFullscreen(const QString &id, int screenIndex)
{
	// Open the window if not already open, then set fullscreen
	MultiviewWindow::openOrFocus(id);
	if (MultiviewWindow *win = MultiviewWindow::findById(id))
		win->setFullscreenOnMonitor(screenIndex);
}

void ToolsMenuManager::onSetWindowed(const QString &id)
{
	// Open the window if not already open, then set windowed
	MultiviewWindow::openOrFocus(id);
	if (MultiviewWindow *win = MultiviewWindow::findById(id))
		win->setWindowed();
}
