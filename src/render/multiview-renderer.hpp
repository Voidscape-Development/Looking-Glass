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

#include "../core/multiview-config.hpp"

#include <obs.h>
#include <graphics/graphics.h>

#include <QByteArray>

#include <atomic>
#include <memory>
#include <mutex>

namespace LiveState {
struct Sources;
}

/**
 * Draws a whole multiview grid into a single obs_display_t.
 *
 * Threading model: the UI thread owns the configuration and builds an
 * immutable Frame from it (resolved sources held visible, label text sources,
 * the cell ownership map). The draw callback runs on the graphics thread and
 * only reads the current Frame, under frameMutex_. Swapping in a new Frame
 * takes the same mutex, so a Frame is never destroyed while it is being drawn;
 * the old Frame is then released on the UI thread, outside the lock.
 *
 * The only Frame state the graphics thread writes is its own texture cache
 * (label backgrounds, placeholder icons), which the UI thread never touches
 * until the Frame is destroyed.
 */
class MultiviewRenderer {
public:
	MultiviewRenderer();
	~MultiviewRenderer();

	MultiviewRenderer(const MultiviewRenderer &) = delete;
	MultiviewRenderer &operator=(const MultiviewRenderer &) = delete;

	// UI thread: show a new layout
	void setConfig(const MultiviewConfig &config);

	// UI thread: re-resolve the scenes and sources the cells display, after
	// sources were created, renamed, or removed. Rebuilds only if a cell now
	// resolves to a different source.
	void refreshSources();

	// Any thread: device pixels per logical pixel of the target window, used
	// to keep grid lines and labels the same visual size on HiDPI screens
	void setPixelRatio(float ratio);

	// obs_display draw callback; data is the MultiviewRenderer
	static void Draw(void *data, uint32_t cx, uint32_t cy);

private:
	struct Cell;
	struct Frame;
	struct CellRect;

	std::unique_ptr<Frame> buildFrame(const MultiviewConfig &config) const;
	void swapFrame(std::unique_ptr<Frame> next);

	// Graphics thread
	void render(uint32_t cx, uint32_t cy);
	void drawGridLines(const Frame &frame, int offsetX, int offsetY, float cellW, float cellH, int inset);
	void drawCell(Cell &cell, const CellRect &rect, const LiveState::Sources &live);
	void drawContent(obs_source_t *source, const CellRect &rect, bool safeAreas);
	void drawMainTexture(const CellRect &rect, bool safeAreas);
	void drawCanvas(const Cell &cell, const CellRect &rect);
	void drawLabel(Cell &cell, const CellRect &rect);
	void drawPlaceholder(Cell &cell, const CellRect &rect);
	void drawStatusBorder(const Cell &cell, obs_source_t *shown, const CellRect &rect,
			      const LiveState::Sources &live);
	void drawSafeAreas(uint32_t baseW, uint32_t baseH);
	void initSafeAreaGeometry();
	void destroySafeAreaGeometry();

	MultiviewConfig config_; // UI thread only
	QByteArray placeholderSvg_;
	std::atomic<float> pixelRatio_{1.0f};

	std::mutex frameMutex_;
	std::unique_ptr<Frame> frame_;

	// EBU R 95 safe area vertex buffers (normalized 0-1 coordinates), created
	// lazily on the graphics thread
	gs_vertbuffer_t *actionSafeVb_ = nullptr;
	gs_vertbuffer_t *graphicsSafeVb_ = nullptr;
	gs_vertbuffer_t *fourByThreeSafeVb_ = nullptr;
	gs_vertbuffer_t *leftLineVb_ = nullptr;
	gs_vertbuffer_t *topLineVb_ = nullptr;
	gs_vertbuffer_t *rightLineVb_ = nullptr;
};
