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

#include <QObject>
#include <QMap>
#include <QString>
#include <QTimer>
#include <QVector>

#include <obs-data.h>

#include "multiview-config.hpp"

/**
 * Owns the multiview layouts of the current scene collection and the global
 * reusable templates.
 *
 * Multiviews are stored inside the scene collection itself through the
 * frontend save callback, so they follow the collection through renames,
 * duplication, and export/import. Templates live in a global JSON file in the
 * plugin's config directory.
 *
 * Multiviews are addressed by a stable id; names are user-facing labels that
 * are kept unique but can change at any time.
 */
class ConfigManager : public QObject {
	Q_OBJECT

public:
	explicit ConfigManager(QObject *parent = nullptr);
	~ConfigManager();

	// Registers the frontend save callback and loads templates. Call from
	// obs_module_load(); undo with disconnectFromFrontend() at unload.
	void connectToFrontend();
	void disconnectFromFrontend();

	// --- Multiviews in the current scene collection ---

	// Every multiview, sorted case-insensitively by name
	QVector<MultiviewConfig> multiviews() const;
	bool hasMultiview(const QString &id) const;
	MultiviewConfig getMultiview(const QString &id) const;
	QString findIdByName(const QString &name) const;
	bool isNameTaken(const QString &name, const QString &exceptId = QString()) const;

	// Stores a new multiview under a freshly generated id and returns that id
	QString addMultiview(MultiviewConfig mv);
	// Replaces the name and layout of an existing multiview. Its window state
	// is left alone, so an editor holding an older copy cannot roll it back.
	void updateLayout(const MultiviewConfig &mv);
	void removeMultiview(const QString &id);
	bool renameMultiview(const QString &id, const QString &newName);
	QString duplicateMultiview(const QString &id, const QString &newName);

	// Window placement. Updating it emits nothing: it changes on every move
	// and resize and must not cause layout reloads.
	WindowState windowState(const QString &id) const;
	void setWindowState(const QString &id, const WindowState &state);

	// --- Global layout templates ---

	QStringList templateNames() const;
	bool hasTemplate(const QString &name) const;
	TemplateConfig getTemplate(const QString &name) const;
	void addTemplate(const TemplateConfig &t);
	// Replaces the template stored under oldName, renaming it if t.name differs
	void replaceTemplate(const QString &oldName, const TemplateConfig &t);
	void removeTemplate(const QString &name);
	void renameTemplate(const QString &oldName, const QString &newName);
	// The built-in default is generated on demand from the current scenes and
	// cannot be modified, renamed, or deleted
	TemplateConfig defaultTemplate() const;
	bool isBuiltInTemplate(const QString &name) const;

	// --- Frontend lifecycle (called from the frontend event callback) ---

	void onSceneCollectionChanging();
	void onSceneCollectionChanged();
	void onFinishedLoading();
	void onExit();

	// --- Source tracking (UI thread) ---

	// Keeps cells pointing at a renamed scene or source, and their names and
	// auto-derived labels in sync with it
	void onSourceRenamed(const QString &uuid, const QString &prevName, const QString &newName);

signals:
	void multiviewAdded(const QString &id);
	void multiviewRemoved(const QString &id);
	void multiviewRenamed(const QString &id);
	void multiviewUpdated(const QString &id);
	void multiviewsReloaded();
	void templatesChanged();

private:
	static void SaveCallback(obs_data_t *saveData, bool saving, void *param);
	void saveToCollection(obs_data_t *saveData) const;
	void loadFromCollection(obs_data_t *saveData);
	void finishLoad(const QVector<MultiviewConfig> &multiviews);
	QVector<MultiviewConfig> loadLegacyCollectionFile() const;
	void resolveSourceUuids(QVector<MultiviewConfig> &multiviews) const;

	// Asks OBS to save the scene collection soon. Saves are coalesced, so
	// bursts of changes cost a single write.
	void requestSave();

	void loadTemplates();
	void saveTemplates() const;
	QString templatesConfigPath() const;

	QMap<QString, MultiviewConfig> multiviews_; // By id
	QMap<QString, TemplateConfig> templates_;   // User templates by name
	QTimer saveTimer_;

	// Set while a scene collection is being switched or loaded; cleared once
	// its data arrives. A brand-new collection never delivers any, which is
	// how stale multiviews from the previous collection are detected.
	bool awaitingLoad_ = true;
	bool connected_ = false;
};
