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

/**
 * Program/preview state for the graphics thread.
 *
 * The frontend API that reports the program and preview scenes belongs to the
 * UI thread, so it cannot be queried from a draw callback. Instead the UI
 * thread refreshes this snapshot from frontend events and draw callbacks read
 * it under a lock.
 */
namespace LiveState {

// Strong references to the current program and preview scenes, released when
// the object goes out of scope. preview is null unless studio mode is active.
struct Sources {
	obs_source_t *program = nullptr;
	obs_source_t *preview = nullptr;
	bool studioMode = false;

	Sources() = default;
	Sources(const Sources &) = delete;
	Sources &operator=(const Sources &) = delete;
	~Sources();
};

// UI thread: re-read the program scene, preview scene, and studio mode
void Update();

// UI thread: drop all references (scene collection teardown and exit)
void Clear();

// Any thread: fill out with the latest state
void Acquire(Sources &out);

} // namespace LiveState
