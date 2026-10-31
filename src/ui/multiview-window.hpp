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

#include <QMap>
#include <QString>

#include <memory>

#include "multiview-display.hpp"
#include "../core/multiview-config.hpp"

class MultiviewRenderer;

/**
 * Top-level window showing one multiview. The whole window is a single OBS
 * display; MultiviewRenderer draws the grid, cell content, labels, and grid
 * lines into it. Supports windowed and per-monitor fullscreen modes, and keeps
 * its placement in the multiview's WindowState.
 */
class MultiviewWindow : public MultiviewDisplay {
	Q_OBJECT

public:
	explicit MultiviewWindow(const QString &id);
	~MultiviewWindow() override;

	QString multiviewId() const { return id_; }

	// Window mode controls
	void setFullscreenOnMonitor(int screenIndex);
	void setWindowed();
	void sendToMainDisplay();

	// Resizes the window to the grid's aspect ratio, removing letterbox bars
	void fitToContent();

	// Static window management. Windows are keyed by multiview id.
	static void openOrFocus(const QString &id);
	static void closeById(const QString &id);
	static MultiviewWindow *findById(const QString &id);

	// Closes every window. With preserveOpenState, each multiview stays marked
	// as open so it is restored the next time its scene collection loads.
	static void closeAll(bool preserveOpenState);
	static void reopenPreviouslyOpen();

	// Re-resolves the scenes and sources shown by every open window
	static void refreshSources();

protected:
	void showEvent(QShowEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void changeEvent(QEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void onPixelSizeChanged() override;

private:
	void reloadConfig();
	void restoreWindowState(const WindowState &state);
	void saveWindowState();
	void setOpen(bool open);
	void openEditDialog();
	void updateTitle();

	QString id_;
	std::unique_ptr<MultiviewRenderer> renderer_;
	bool fullscreen_ = false;
	bool restoring_ = false;

	static QMap<QString, MultiviewWindow *> openWindows_;
	static bool preserveOpenStateOnClose_;
};
