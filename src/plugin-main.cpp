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

#include "plugin.hpp"
#include "api/websocket-api.hpp"
#include "core/config-manager.hpp"
#include "render/live-state.hpp"
#include "ui/tools-menu.hpp"
#include "ui/multiview-window.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QApplication>
#include <QMetaObject>

#include <atomic>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("ZombieHDGaming");
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("LookingGlass");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("LookingGlass");
}

// Global singleton instances for configuration and menu management
static ConfigManager *s_configManager = nullptr;
static ToolsMenuManager *s_toolsMenuManager = nullptr;

ConfigManager *GetConfigManager()
{
	return s_configManager;
}

ToolsMenuManager *GetToolsMenuManager()
{
	return s_toolsMenuManager;
}

// --- Source lifecycle ---
//
// Cells resolve their scene or source when a layout is shown and then hold it
// visible, so they have to follow the source list: a cell pointing at a source
// that does not exist yet picks it up when it is created, one whose source was
// removed lets go of it, and a rename is written back into the layouts.
//
// Core signals fire on OBS threads; everything is forwarded to the UI thread,
// posted to the application object so a window closing concurrently cannot
// be used after it is freed.

static std::atomic<bool> s_refreshQueued{false};

static void OnSourceListChanged(void *, calldata_t *)
{
	// Loading a scene collection creates sources by the hundred; one refresh
	// covers any number of changes that arrive before it runs
	if (s_refreshQueued.exchange(true))
		return;

	QMetaObject::invokeMethod(
		qApp,
		[]() {
			s_refreshQueued = false;
			MultiviewWindow::refreshSources();
		},
		Qt::QueuedConnection);
}

static void OnSourceRenamed(void *, calldata_t *cd)
{
	obs_source_t *source = (obs_source_t *)calldata_ptr(cd, "source");
	if (!source)
		return;

	QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	QString prevName = QString::fromUtf8(calldata_string(cd, "prev_name"));
	QString newName = QString::fromUtf8(calldata_string(cd, "new_name"));

	QMetaObject::invokeMethod(
		qApp,
		[uuid, prevName, newName]() {
			if (ConfigManager *cm = GetConfigManager())
				cm->onSourceRenamed(uuid, prevName, newName);
		},
		Qt::QueuedConnection);
}

static void ConnectSourceSignals(bool connect)
{
	signal_handler_t *sh = obs_get_signal_handler();
	if (!sh)
		return;

	auto apply = connect ? signal_handler_connect : signal_handler_disconnect;
	apply(sh, "source_create", OnSourceListChanged, nullptr);
	apply(sh, "source_destroy", OnSourceListChanged, nullptr);
	apply(sh, "source_rename", OnSourceRenamed, nullptr);
}

// --- Frontend events ---

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		// OBS saves the outgoing collection after this event. Close the
		// windows but keep them marked open, so they come back next time
		// this collection is loaded.
		s_configManager->onSceneCollectionChanging();
		MultiviewWindow::closeAll(true);
		LiveState::Clear();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		s_configManager->onSceneCollectionChanged();
		LiveState::Update();
		MultiviewWindow::reopenPreviouslyOpen();
		break;

	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// Initial startup: the collection has loaded; build the Tools menu
		// and restore the windows that were open at last exit
		s_configManager->onFinishedLoading();
		s_toolsMenuManager->initialize();
		LiveState::Update();
		MultiviewWindow::reopenPreviouslyOpen();
		break;

	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		LiveState::Update();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		LiveState::Clear();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		// Drop the vendor requests while obs-websocket is still loaded; module
		// unload order is not guaranteed, so it may be gone by obs_module_unload()
		LookingGlassWebSocket::Unregister();

		// The collection, including which windows are open, was saved just
		// before this event
		s_configManager->onExit();
		MultiviewWindow::closeAll(true);
		LiveState::Clear();
		break;

	default:
		break;
	}
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	s_configManager = new ConfigManager();
	s_toolsMenuManager = new ToolsMenuManager();

	// The save callback must be registered before the first scene collection
	// loads, which happens after all modules are loaded
	s_configManager->connectToFrontend();
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	ConnectSourceSignals(true);

	return true;
}

MODULE_EXPORT void obs_module_post_load(void)
{
	// obs-websocket's vendor API is only reachable once every module has been
	// loaded, so registration cannot happen in obs_module_load()
	LookingGlassWebSocket::Register();
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");

	ConnectSourceSignals(false);
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);

	delete s_toolsMenuManager;
	s_toolsMenuManager = nullptr;

	delete s_configManager;
	s_configManager = nullptr;
}
