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

#include <obs.h>

#include <QWidget>

/**
 * A widget whose whole surface is an obs_display_t, modelled on OBS Studio's
 * own OBSQTDisplay. The display is created once the native window is exposed,
 * sized in device pixels (so it stays sharp on HiDPI screens), resized with
 * the widget, and destroyed before the native surface goes away.
 *
 * One draw callback can be registered; it is attached to every display this
 * widget creates and detached before the display is destroyed.
 */
class MultiviewDisplay : public QWidget {
	Q_OBJECT

public:
	explicit MultiviewDisplay(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
	~MultiviewDisplay() override;

	using DrawCallback = void (*)(void *param, uint32_t cx, uint32_t cy);
	void setDrawCallback(DrawCallback callback, void *param);

	// Destroys the display now. Subclasses whose draw callback state dies
	// with them must call this from their own destructor.
	void destroyDisplay();

	// Drawing is done entirely by OBS
	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

	// Called whenever the display's pixel size may have changed (resizes and
	// moves between screens with a different scale factor)
	virtual void onPixelSizeChanged() {}

private:
	void createDisplay();
	void resizeDisplay();
	QSize pixelSize() const;

	obs_display_t *display_ = nullptr;
	DrawCallback drawCallback_ = nullptr;
	void *drawParam_ = nullptr;
	bool destroying_ = false;
};
