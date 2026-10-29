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

#include "multiview-display.hpp"
#include "../plugin.hpp"

#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

namespace {

// Fills in the native window handle for libobs, the same way OBS Studio does
bool QtToGsWindow(QWindow *window, gs_window &gswindow)
{
#ifdef _WIN32
	gswindow.hwnd = (HWND)window->winId();
	return true;
#elif defined(__APPLE__)
	gswindow.view = (id)window->winId();
	return true;
#else
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		gswindow.id = (uint32_t)window->winId();
		gswindow.display = obs_get_nix_platform_display();
		return true;
	case OBS_NIX_PLATFORM_WAYLAND:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
		// Since Qt 6.9 the window id of a Wayland window is its wl_surface
		gswindow.display = (void *)window->winId();
		return gswindow.display != nullptr;
#else
	{
		// Older Qt only exposes the wl_surface through private API
		static bool warned = false;
		if (!warned) {
			obs_log(LOG_WARNING, "multiview windows need Qt 6.9 or later under Wayland "
					     "(run OBS with QT_QPA_PLATFORM=xcb to use XWayland instead)");
			warned = true;
		}
		return false;
	}
#endif
	default:
		return false;
	}
#endif
}

} // namespace

MultiviewDisplay::MultiviewDisplay(QWidget *parent, Qt::WindowFlags flags) : QWidget(parent, flags)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	// Make sure the QWindow exists so its signals and surface events can be
	// observed from the start
	winId();
	QWindow *window = windowHandle();

	connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
		if (!visible) {
#if !defined(_WIN32) && !defined(__APPLE__)
			// The native surface may be torn down while hidden on Linux
			destroyDisplay();
#endif
			return;
		}
		createDisplay();
		resizeDisplay();
	});

	connect(window, &QWindow::screenChanged, this, [this](QScreen *) {
		createDisplay();
		resizeDisplay();
	});

	window->installEventFilter(this);
}

MultiviewDisplay::~MultiviewDisplay()
{
	destroyDisplay();
}

void MultiviewDisplay::setDrawCallback(DrawCallback callback, void *param)
{
	if (display_ && drawCallback_)
		obs_display_remove_draw_callback(display_, drawCallback_, drawParam_);

	drawCallback_ = callback;
	drawParam_ = param;

	if (display_ && drawCallback_)
		obs_display_add_draw_callback(display_, drawCallback_, drawParam_);
}

QSize MultiviewDisplay::pixelSize() const
{
	return size() * devicePixelRatioF();
}

void MultiviewDisplay::createDisplay()
{
	if (display_ || destroying_ || !windowHandle() || !windowHandle()->isExposed())
		return;

	QSize size = pixelSize();

	gs_init_data info = {};
	info.cx = (uint32_t)size.width();
	info.cy = (uint32_t)size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

	if (!QtToGsWindow(windowHandle(), info.window))
		return;

	display_ = obs_display_create(&info, 0xFF000000);
	if (display_ && drawCallback_)
		obs_display_add_draw_callback(display_, drawCallback_, drawParam_);
}

void MultiviewDisplay::destroyDisplay()
{
	if (!display_)
		return;

	// Removing the callback first guarantees it is not running, and will not
	// run again, once this returns
	if (drawCallback_)
		obs_display_remove_draw_callback(display_, drawCallback_, drawParam_);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void MultiviewDisplay::resizeDisplay()
{
	if (display_ && isVisible()) {
		QSize size = pixelSize();
		obs_display_resize(display_, (uint32_t)size.width(), (uint32_t)size.height());
	}
	onPixelSizeChanged();
}

void MultiviewDisplay::paintEvent(QPaintEvent *event)
{
	createDisplay();
	QWidget::paintEvent(event);
}

void MultiviewDisplay::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();
	resizeDisplay();
}

bool MultiviewDisplay::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == windowHandle() && event->type() == QEvent::PlatformSurface) {
		auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
		if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
			destroying_ = true;
			destroyDisplay();
		} else if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
			destroying_ = false;
		}
	}
	return QWidget::eventFilter(watched, event);
}
