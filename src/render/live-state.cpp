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

#include "live-state.hpp"

#include <obs-frontend-api.h>

#include <mutex>

namespace LiveState {

namespace {

std::mutex s_mutex;
obs_weak_source_t *s_program = nullptr;
obs_weak_source_t *s_preview = nullptr;
bool s_studioMode = false;

obs_weak_source_t *WeakFrom(obs_source_t *source)
{
	obs_weak_source_t *weak = source ? obs_source_get_weak_source(source) : nullptr;
	obs_source_release(source);
	return weak;
}

void Store(obs_weak_source_t *program, obs_weak_source_t *preview, bool studioMode)
{
	obs_weak_source_t *oldProgram;
	obs_weak_source_t *oldPreview;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		oldProgram = s_program;
		oldPreview = s_preview;
		s_program = program;
		s_preview = preview;
		s_studioMode = studioMode;
	}
	obs_weak_source_release(oldProgram);
	obs_weak_source_release(oldPreview);
}

} // namespace

Sources::~Sources()
{
	obs_source_release(program);
	obs_source_release(preview);
}

void Update()
{
	bool studioMode = obs_frontend_preview_program_mode_active();
	obs_weak_source_t *program = WeakFrom(obs_frontend_get_current_scene());
	obs_weak_source_t *preview = studioMode ? WeakFrom(obs_frontend_get_current_preview_scene()) : nullptr;
	Store(program, preview, studioMode);
}

void Clear()
{
	Store(nullptr, nullptr, false);
}

void Acquire(Sources &out)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	out.program = obs_weak_source_get_source(s_program);
	out.preview = obs_weak_source_get_source(s_preview);
	out.studioMode = s_studioMode;
}

} // namespace LiveState
