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

#include "websocket-api.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../ui/multiview-window.hpp"

#include <cassert>
#include <cstring>
#include <functional>

#include <obs.h>
#include <obs-websocket-api.h>

#include <QString>

namespace {

// Vendor name external clients address their CallVendorRequest calls to
constexpr const char *VENDOR_NAME = "looking-glass";

constexpr const char *REQUEST_OPEN = "OpenMultiview";
constexpr const char *REQUEST_CLOSE = "CloseMultiview";
constexpr const char *REQUEST_SEND_TO_MAIN_DISPLAY = "SendMultiviewToMainDisplay";

// Name of the multiview the request applies to
constexpr const char *FIELD_MULTIVIEW_NAME = "multiviewName";

obs_websocket_vendor s_vendor = nullptr;

void SetError(obs_data_t *response_data, const QString &message)
{
	obs_data_set_bool(response_data, "success", false);
	obs_data_set_string(response_data, "error", message.toUtf8().constData());
}

// Carries a callable across the obs_queue_task() C boundary
struct UiTask {
	const std::function<void()> *fn;
	bool ran;
};

// Vendor request callbacks are invoked on an obs-websocket thread, but every
// window operation below touches Qt widgets and must run on the UI thread.
// Returns false when OBS has no UI task handler (e.g. a headless host), in
// which case the task never ran.
bool RunOnUiThread(const std::function<void()> &fn)
{
	UiTask task = {&fn, false};

	obs_queue_task(
		OBS_TASK_UI,
		[](void *param) {
			auto *t = static_cast<UiTask *>(param);
			t->ran = true;
			(*t->fn)();
		},
		&task, true);

	return task.ran;
}

// Shared body for every request: pull the multiview name out of the payload,
// resolve it to the multiview's id on the UI thread, run `action` with that id,
// and report the outcome.
void HandleMultiviewRequest(obs_data_t *request_data, obs_data_t *response_data,
			    const std::function<void(const QString &id)> &action)
{
	const QString name = QString::fromUtf8(obs_data_get_string(request_data, FIELD_MULTIVIEW_NAME));
	if (name.isEmpty()) {
		SetError(response_data,
			 QStringLiteral("The '%1' field is required.").arg(QString::fromUtf8(FIELD_MULTIVIEW_NAME)));
		return;
	}

	bool exists = false;
	const bool ran = RunOnUiThread([&]() {
		ConfigManager *cm = GetConfigManager();
		QString id = cm ? cm->findIdByName(name) : QString();
		if (id.isEmpty())
			return;

		exists = true;
		action(id);
	});

	if (!ran) {
		SetError(response_data, QStringLiteral("Looking Glass could not reach the OBS UI thread."));
		return;
	}

	if (!exists) {
		SetError(response_data,
			 QStringLiteral("No multiview named '%1' exists in the current scene collection.").arg(name));
		return;
	}

	obs_data_set_bool(response_data, "success", true);
}

// Opens the multiview window, or focuses it when it is already open.
void OnOpenMultiview(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	HandleMultiviewRequest(request_data, response_data,
			       [](const QString &id) { MultiviewWindow::openOrFocus(id); });
}

// Closes the multiview window. Succeeds as a no-op when it is not open.
void OnCloseMultiview(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	HandleMultiviewRequest(request_data, response_data, [](const QString &id) { MultiviewWindow::closeById(id); });
}

// Opens the multiview if needed, then moves it to the main display.
void OnSendMultiviewToMainDisplay(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	HandleMultiviewRequest(request_data, response_data, [](const QString &id) {
		MultiviewWindow::openOrFocus(id);
		if (MultiviewWindow *win = MultiviewWindow::findById(id))
			win->sendToMainDisplay();
	});
}

} // namespace

void LookingGlassWebSocket::Register()
{
	if (s_vendor)
		return;

	s_vendor = obs_websocket_register_vendor(VENDOR_NAME);
	if (!s_vendor) {
		obs_log(LOG_INFO, "obs-websocket is unavailable, vendor requests are disabled");
		return;
	}

	bool ok = obs_websocket_vendor_register_request(s_vendor, REQUEST_OPEN, OnOpenMultiview, nullptr);
	ok &= obs_websocket_vendor_register_request(s_vendor, REQUEST_CLOSE, OnCloseMultiview, nullptr);
	ok &= obs_websocket_vendor_register_request(s_vendor, REQUEST_SEND_TO_MAIN_DISPLAY,
						    OnSendMultiviewToMainDisplay, nullptr);

	if (ok)
		obs_log(LOG_INFO, "registered obs-websocket vendor requests under \"%s\"", VENDOR_NAME);
	else
		obs_log(LOG_WARNING, "failed to register one or more obs-websocket vendor requests");
}

void LookingGlassWebSocket::Unregister()
{
	if (!s_vendor)
		return;

	obs_websocket_vendor_unregister_request(s_vendor, REQUEST_OPEN);
	obs_websocket_vendor_unregister_request(s_vendor, REQUEST_CLOSE);
	obs_websocket_vendor_unregister_request(s_vendor, REQUEST_SEND_TO_MAIN_DISPLAY);

	s_vendor = nullptr;
}
