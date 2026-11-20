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

#include "hotkeys.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../ui/multiview-window.hpp"

#include <obs-hotkey.h>

#include <QApplication>
#include <QGuiApplication>
#include <QMap>
#include <QScreen>
#include <QSet>

#include <array>
#include <iterator>

namespace {

constexpr const char *kSaveKey = "looking-glass-hotkeys";

void Toggle(const QString &id)
{
	if (MultiviewWindow::findById(id))
		MultiviewWindow::closeById(id);
	else
		MultiviewWindow::openOrFocus(id);
}

// Fullscreen on the monitor the window is on, or back to windowed
void ToggleFullscreen(const QString &id)
{
	MultiviewWindow *w = MultiviewWindow::findById(id);
	if (w && w->isFullScreen()) {
		w->setWindowed();
		return;
	}
	MultiviewWindow::openOrFocus(id);
	if ((w = MultiviewWindow::findById(id)))
		w->setFullscreenOnMonitor((int)QGuiApplication::screens().indexOf(w->screen()));
}

struct Action {
	const char *key; // Part of the hotkey name and the saved bindings
	const char *textKey;
	void (*run)(const QString &id);
};

const Action kActions[] = {
	{"open", "Hotkeys.Open", MultiviewWindow::openOrFocus},
	{"close", "Hotkeys.Close", MultiviewWindow::closeById},
	{"toggle", "Hotkeys.Toggle", Toggle},
	{"fullscreen", "Hotkeys.ToggleFullscreen", ToggleFullscreen},
};
using HotkeyIds = std::array<obs_hotkey_id, std::size(kActions)>;

QMap<QString, HotkeyIds> s_registered; // By multiview id
obs_data_t *s_bindings = nullptr;      // As loaded from the collection: multiview id ▸ action ▸ bindings

QByteArray Description(const QString &name, const Action &action)
{
	return QString::fromUtf8(obs_module_text(action.textKey)).arg(name).toUtf8();
}

// Hotkey thread. Names are "looking_glass.<multiview id>.<action>".
void Pressed(void *data, obs_hotkey_id, obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;
	auto *action = static_cast<const Action *>(data);
	QString id = QString::fromUtf8(obs_hotkey_get_name(hotkey)).section('.', 1, 1);
	QMetaObject::invokeMethod(qApp, [action, id]() { action->run(id); }, Qt::QueuedConnection);
}

void Unregister(const HotkeyIds &ids)
{
	for (obs_hotkey_id id : ids)
		obs_hotkey_unregister(id);
}

// Registers hotkeys for new multiviews, renames those of renamed ones, and
// drops those of removed ones
void Sync()
{
	ConfigManager *cm = GetConfigManager();
	if (!cm)
		return;

	QSet<QString> current;
	for (const MultiviewConfig &mv : cm->multiviews()) {
		current.insert(mv.id);
		if (s_registered.contains(mv.id)) {
			const HotkeyIds &ids = s_registered[mv.id];
			for (size_t i = 0; i < ids.size(); i++)
				obs_hotkey_set_description(ids[i], Description(mv.name, kActions[i]).constData());
			continue;
		}

		HotkeyIds &ids = s_registered[mv.id];
		obs_data_t *saved = s_bindings ? obs_data_get_obj(s_bindings, mv.id.toUtf8().constData()) : nullptr;
		for (size_t i = 0; i < ids.size(); i++) {
			const Action &action = kActions[i];
			QByteArray name = QStringLiteral("looking_glass.%1.%2").arg(mv.id, action.key).toUtf8();
			ids[i] = obs_hotkey_register_frontend(
				name.constData(), Description(mv.name, action).constData(), Pressed, (void *)&action);
			obs_data_array_t *bindings = saved ? obs_data_get_array(saved, action.key) : nullptr;
			if (bindings)
				obs_hotkey_load(ids[i], bindings);
			obs_data_array_release(bindings);
		}
		obs_data_release(saved);
	}

	for (auto it = s_registered.begin(); it != s_registered.end();) {
		if (current.contains(it.key())) {
			++it;
			continue;
		}
		Unregister(it.value());
		it = s_registered.erase(it);
	}
}

void UnregisterAll()
{
	for (const HotkeyIds &ids : std::as_const(s_registered))
		Unregister(ids);
	s_registered.clear();
}

void SaveCallback(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		obs_data_t *all = obs_data_create();
		for (auto it = s_registered.cbegin(); it != s_registered.cend(); ++it) {
			obs_data_t *mv = obs_data_create();
			for (size_t i = 0; i < it.value().size(); i++) {
				obs_data_array_t *bindings = obs_hotkey_save(it.value()[i]);
				obs_data_set_array(mv, kActions[i].key, bindings);
				obs_data_array_release(bindings);
			}
			obs_data_set_obj(all, it.key().toUtf8().constData(), mv);
			obs_data_release(mv);
		}
		obs_data_set_obj(saveData, kSaveKey, all);
		obs_data_release(all);
		return;
	}

	// A collection loaded: its bindings replace the previous one's. This may
	// run before or after the multiviews load; either way Sync() runs once
	// both are in.
	obs_data_release(s_bindings);
	s_bindings = obs_data_get_obj(saveData, kSaveKey);
	UnregisterAll();
	Sync();
}

} // namespace

namespace LookingGlassHotkeys {

void Initialize()
{
	ConfigManager *cm = GetConfigManager();
	QObject::connect(cm, &ConfigManager::multiviewAdded, cm, [] { Sync(); });
	QObject::connect(cm, &ConfigManager::multiviewRemoved, cm, [] { Sync(); });
	QObject::connect(cm, &ConfigManager::multiviewRenamed, cm, [] { Sync(); });
	QObject::connect(cm, &ConfigManager::multiviewsReloaded, cm, [] { Sync(); });
	obs_frontend_add_save_callback(SaveCallback, nullptr);
}

void Shutdown()
{
	obs_frontend_remove_save_callback(SaveCallback, nullptr);
	UnregisterAll();
	obs_data_release(s_bindings);
	s_bindings = nullptr;
}

} // namespace LookingGlassHotkeys
