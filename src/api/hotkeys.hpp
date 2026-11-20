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

// OBS hotkeys for every multiview in the current scene collection: open,
// close, toggle, and toggle fullscreen. They appear in Settings ▸ Hotkeys and
// follow multiviews as they are added, renamed, and removed. Bindings are
// saved inside the scene collection, alongside the multiviews themselves.
namespace LookingGlassHotkeys {

// From obs_module_load(), after the ConfigManager exists
void Initialize();
// On OBS_FRONTEND_EVENT_EXIT, after the collection has been saved
void Shutdown();

} // namespace LookingGlassHotkeys
