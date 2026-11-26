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

#include "multiview-window.hpp"
#include "multiview-edit-dialog.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../render/multiview-renderer.hpp"

#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

QMap<QString, MultiviewWindow *> MultiviewWindow::openWindows_;
bool MultiviewWindow::preserveOpenStateOnClose_ = false;
bool MultiviewWindow::shuttingDown_ = false;

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

// Index of the monitor a fullscreen window should return to: by name, since
// indices shift when monitors are added or removed, then by index
int FindScreen(const WindowState &state)
{
	QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < (int)screens.size(); i++) {
		if (!state.screenName.isEmpty() && screens[i]->name() == state.screenName)
			return i;
	}
	if (state.screenIndex >= 0 && state.screenIndex < (int)screens.size())
		return state.screenIndex;
	return -1;
}

QRect CenteredOnPrimaryScreen(int width, int height)
{
	QScreen *primary = QGuiApplication::primaryScreen();
	if (!primary)
		return QRect(100, 100, width, height);

	QRect area = primary->availableGeometry();
	return QRect(area.x() + (area.width() - width) / 2, area.y() + (area.height() - height) / 2, width, height);
}

} // namespace

MultiviewWindow::MultiviewWindow(const QString &id)
	: MultiviewDisplay(nullptr, Qt::Window),
	  id_(id),
	  renderer_(std::make_unique<MultiviewRenderer>())
{
	setAttribute(Qt::WA_DeleteOnClose);
	setMinimumSize(160, 90);

	ConfigManager *cm = GetConfigManager();
	renderer_->setPixelRatio((float)devicePixelRatioF());
	renderer_->setConfig(cm->getMultiview(id_));
	setDrawCallback(&MultiviewRenderer::Draw, renderer_.get());

	// Keeps clock cells current
	auto *clockTimer = new QTimer(this);
	connect(clockTimer, &QTimer::timeout, this, [this]() { renderer_->tick(); });
	clockTimer->start(250);

	updateTitle();
	restoreWindowState(cm->windowState(id_));
	updateCursor();

	openWindows_[id_] = this;

	connect(cm, &ConfigManager::multiviewUpdated, this, [this](const QString &updatedId) {
		if (updatedId == id_)
			reloadConfig();
	});
	connect(cm, &ConfigManager::multiviewRenamed, this, [this](const QString &renamedId) {
		if (renamedId == id_)
			updateTitle();
	});
	connect(cm, &ConfigManager::multiviewRemoved, this, [this](const QString &removedId) {
		if (removedId == id_)
			close();
	});
}

MultiviewWindow::~MultiviewWindow()
{
	// The draw callback points at renderer_, so the display must go first
	destroyDisplay();

	if (openWindows_.value(id_) == this)
		openWindows_.remove(id_);
}

void MultiviewWindow::reloadConfig()
{
	renderer_->setConfig(GetConfigManager()->getMultiview(id_));
	updateTitle();
}

// --- Static window management ---

void MultiviewWindow::openOrFocus(const QString &id)
{
	if (MultiviewWindow *w = findById(id)) {
		w->raise();
		w->activateWindow();
		return;
	}

	if (shuttingDown_ || !GetConfigManager() || !GetConfigManager()->hasMultiview(id))
		return;

	auto *w = new MultiviewWindow(id);
	w->show();
}

void MultiviewWindow::closeById(const QString &id)
{
	if (MultiviewWindow *w = findById(id))
		w->close();
}

MultiviewWindow *MultiviewWindow::findById(const QString &id)
{
	return openWindows_.value(id, nullptr);
}

void MultiviewWindow::closeAll(bool preserveOpenState)
{
	preserveOpenStateOnClose_ = preserveOpenState;

	// Delete outright rather than waiting for deferred deletion: this runs on
	// scene collection switches and at exit, when the windows' sources must be
	// released before OBS tears the collection (or itself) down
	const QList<MultiviewWindow *> windows = openWindows_.values();
	for (MultiviewWindow *w : windows) {
		w->close();
		delete w;
	}

	preserveOpenStateOnClose_ = false;
}

void MultiviewWindow::shutdown()
{
	shuttingDown_ = true;
	closeAll(true);
}

void MultiviewWindow::reopenPreviouslyOpen()
{
	for (const MultiviewConfig &mv : GetConfigManager()->multiviews()) {
		if (mv.window.open)
			openOrFocus(mv.id);
	}
}

void MultiviewWindow::refreshSources()
{
	for (MultiviewWindow *w : std::as_const(openWindows_))
		w->renderer_->refreshSources();
}

// --- Window state ---

void MultiviewWindow::restoreWindowState(const WindowState &state)
{
	restoring_ = true;

	// A window whose monitor is gone comes back centered on the primary one
	QRect geometry = state.geometry.isValid() ? state.geometry : QRect(0, 0, kDefaultWidth, kDefaultHeight);
	if (!QGuiApplication::screenAt(geometry.center()))
		geometry = CenteredOnPrimaryScreen(geometry.width(), geometry.height());
	setGeometry(geometry);

	if (state.fullscreen) {
		int screenIndex = FindScreen(state);
		// Deferred until after show(): fullscreen requested on a window that has
		// not been shown yet lands on the wrong monitor, framed
		if (screenIndex >= 0)
			QTimer::singleShot(0, this, [this, screenIndex]() { setFullscreenOnMonitor(screenIndex); });
	}

	restoring_ = false;
}

void MultiviewWindow::saveWindowState()
{
	if (restoring_)
		return;

	ConfigManager *cm = GetConfigManager();
	WindowState state = cm->windowState(id_);
	if (!fullscreen_)
		state.geometry = geometry();
	state.fullscreen = fullscreen_;

	if (QScreen *current = screen()) {
		state.screenName = current->name();
		state.screenIndex = (int)QGuiApplication::screens().indexOf(current);
	}

	cm->setWindowState(id_, state);
}

void MultiviewWindow::setOpen(bool open)
{
	ConfigManager *cm = GetConfigManager();
	WindowState state = cm->windowState(id_);
	state.open = open;
	cm->setWindowState(id_, state);
}

void MultiviewWindow::showEvent(QShowEvent *event)
{
	MultiviewDisplay::showEvent(event);
	setOpen(true);
}

void MultiviewWindow::moveEvent(QMoveEvent *event)
{
	MultiviewDisplay::moveEvent(event);
	saveWindowState();
}

void MultiviewWindow::resizeEvent(QResizeEvent *event)
{
	MultiviewDisplay::resizeEvent(event);
	saveWindowState();
}

void MultiviewWindow::closeEvent(QCloseEvent *event)
{
	if (!preserveOpenStateOnClose_)
		setOpen(false);

	// Drop out of the registry now rather than at deferred deletion, so the
	// same multiview can be reopened straight away
	if (openWindows_.value(id_) == this)
		openWindows_.remove(id_);

	MultiviewDisplay::closeEvent(event);
}

void MultiviewWindow::changeEvent(QEvent *event)
{
	MultiviewDisplay::changeEvent(event);
	if (event->type() == QEvent::WindowStateChange) {
		bool nowFullscreen = windowState() & Qt::WindowFullScreen;
		if (nowFullscreen == fullscreen_)
			return;
		if (!nowFullscreen)
			return setWindowed(); // Left fullscreen by the window manager
		fullscreen_ = true;
		updateTitle();
		saveWindowState();
	}
}

void MultiviewWindow::onPixelSizeChanged()
{
	if (renderer_)
		renderer_->setPixelRatio((float)devicePixelRatioF());
}

void MultiviewWindow::mousePressEvent(QMouseEvent *event)
{
	MultiviewDisplay::mousePressEvent(event);
	if (event->button() == Qt::LeftButton)
		switchToSceneAt(event->position(), false);
}

void MultiviewWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
	MultiviewDisplay::mouseDoubleClickEvent(event);
	if (event->button() == Qt::LeftButton)
		switchToSceneAt(event->position(), true);
}

void MultiviewWindow::switchToSceneAt(const QPointF &pos, bool doubleClick)
{
	if (!GetConfigManager()->windowState(id_).clickToSwitch)
		return;

	const qreal ratio = devicePixelRatioF();
	const QSize size = this->size() * ratio;
	obs_source_t *scene =
		renderer_->sceneAt((int)(pos.x() * ratio), (int)(pos.y() * ratio), size.width(), size.height());
	if (!scene)
		return;

	// Like OBS Studio's multiview: in studio mode a click previews the scene
	// and a double click takes it to program; otherwise a click switches to it
	const bool studio = obs_frontend_preview_program_mode_active();
	if (studio && !doubleClick)
		obs_frontend_set_current_preview_scene(scene);
	else if (studio || !doubleClick)
		obs_frontend_set_current_scene(scene);
	obs_source_release(scene);
}

void MultiviewWindow::contextMenuEvent(QContextMenuEvent *event)
{
	// popup(), not exec(): a nested event loop would return into a deleted
	// window if a hotkey or a collection switch closes it meanwhile
	auto *menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);

	QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < (int)screens.size(); i++) {
		QString label = QString(LG_TEXT("ToolsMenu.FullscreenOn")).arg(screens[i]->name());
		menu->addAction(label, this, [this, i]() { setFullscreenOnMonitor(i); });
	}

	if (fullscreen_) {
		menu->addSeparator();
		menu->addAction(LG_TEXT("ToolsMenu.Windowed"), this, &MultiviewWindow::setWindowed);
	}

	if (!fullscreen_) {
		menu->addSeparator();
		menu->addAction(LG_TEXT("WindowMenu.FitToContent"), this, &MultiviewWindow::fitToContent);
	}

	menu->addSeparator();
	auto addToggle = [this, menu](const char *key, bool WindowState::*field) {
		QAction *action = menu->addAction(LG_TEXT(key));
		action->setCheckable(true);
		action->setChecked(GetConfigManager()->windowState(id_).*field);
		connect(action, &QAction::toggled, this, [this, field](bool on) {
			WindowState state = GetConfigManager()->windowState(id_);
			state.*field = on;
			GetConfigManager()->setWindowState(id_, state);
			updateCursor();
		});
	};
	addToggle("WindowMenu.ClickToSwitch", &WindowState::clickToSwitch);
	addToggle("WindowMenu.HideCursor", &WindowState::hideCursor);
	menu->addAction(LG_TEXT("WindowMenu.EditMultiview"), this, &MultiviewWindow::openEditDialog);

	menu->addSeparator();
	menu->addAction(LG_TEXT("WindowMenu.CloseMultiview"), this, &QWidget::close);

	menu->popup(event->globalPos());
}

void MultiviewWindow::setFullscreenOnMonitor(int screenIndex)
{
	QList<QScreen *> screens = QGuiApplication::screens();
	if (screenIndex < 0 || screenIndex >= (int)screens.size())
		return;

	// Remember the windowed geometry to return to
	if (!fullscreen_)
		saveWindowState();

	// Set first, so the moves below do not save the monitor rect as the
	// windowed geometry
	fullscreen_ = true;
	QScreen *target = screens[screenIndex];
	setScreen(target);
	setGeometry(target->geometry());
	showFullScreen();

	updateTitle();
	saveWindowState();
}

void MultiviewWindow::setWindowed()
{
	// Read before showNormal(), and fullscreen_ cleared first, so the events it
	// sends cannot save the monitor rect over the windowed geometry
	const QRect geometry = GetConfigManager()->windowState(id_).geometry;
	fullscreen_ = false;
	showNormal();
	if (geometry.isValid())
		setGeometry(geometry);

	updateTitle();
	saveWindowState();
}

void MultiviewWindow::fitToContent()
{
	if (fullscreen_ || isMaximized())
		return;

	const MultiviewConfig config = GetConfigManager()->getMultiview(id_);
	const int rows = std::max(1, config.gridRows);
	const int cols = std::max(1, config.gridCols);

	// Mirror the renderer: grid lines bleed half their width outside the grid,
	// and every cell is 16:9
	const int border = std::max(1, config.gridBorderWidth);
	const int margin = 2 * ((border + 1) / 2);
	const double gridAspect = (double)(cols * 16) / (double)(rows * 9);

	// Keep the width and trim or grow the height to match the grid
	const int availableW = std::max(1, width() - margin);
	const int newHeight = (int)std::lround(availableW / gridAspect) + margin;

	QRect geometry = this->geometry();
	geometry.setHeight(std::max(newHeight, minimumHeight()));

	// Keep the window on screen
	if (QScreen *current = screen()) {
		const QRect area = current->availableGeometry();
		if (geometry.bottom() > area.bottom())
			geometry.moveBottom(area.bottom());
		if (geometry.top() < area.top())
			geometry.moveTop(area.top());
	}

	setGeometry(geometry);
}

void MultiviewWindow::sendToMainDisplay()
{
	// Leave fullscreen first so the window can be repositioned
	setWindowed();
	setGeometry(CenteredOnPrimaryScreen(kDefaultWidth, kDefaultHeight));

	raise();
	activateWindow();
}

void MultiviewWindow::openEditDialog()
{
	// The window reloads through ConfigManager::multiviewUpdated. Parented to
	// the main window: this one can be deleted while the dialog is open
	MultiviewEditDialog::launch(GetConfigManager()->getMultiview(id_), false,
				    (QWidget *)obs_frontend_get_main_window());
}

void MultiviewWindow::updateCursor()
{
	if (GetConfigManager()->windowState(id_).hideCursor)
		setCursor(Qt::BlankCursor);
	else
		unsetCursor();
}

void MultiviewWindow::updateTitle()
{
	QString title = GetConfigManager()->getMultiview(id_).name;
	if (fullscreen_)
		title += LG_TEXT("WindowMenu.FullscreenSuffix");
	setWindowTitle(title);
}
