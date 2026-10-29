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

#include "config-manager.hpp"
#include "../plugin.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace {

// Key under which multiviews are stored in the scene collection's module data
constexpr const char *kSaveKey = "looking-glass";

// Coalesces bursts of changes (a window being dragged, several edits in a
// row) into a single scene collection save
constexpr int kSaveDelayMs = 1000;

QString NewId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ModuleConfigPath(const char *file)
{
	char *path = obs_module_config_path(file);
	QString result = path ? QString::fromUtf8(path) : QString();
	bfree(path);
	return result;
}

QJsonObject ReadJsonFile(const QString &path)
{
	QFile file(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
		return QJsonObject();

	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError) {
		obs_log(LOG_WARNING, "could not parse %s: %s", path.toUtf8().constData(),
			error.errorString().toUtf8().constData());
		return QJsonObject();
	}
	return doc.object();
}

bool WriteJsonFile(const QString &path, const QJsonObject &root)
{
	if (path.isEmpty())
		return false;

	QDir().mkpath(QFileInfo(path).absolutePath());

	// QSaveFile writes to a temporary file and renames it into place, so a
	// crash mid-write never leaves a truncated file behind
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "could not write %s", path.toUtf8().constData());
		return false;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	return file.commit();
}

QString BuiltInTemplateName()
{
	return QString::fromUtf8(LG_TEXT("DefaultTemplate.Name"));
}

bool LessByName(const MultiviewConfig &a, const MultiviewConfig &b)
{
	return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
}

} // namespace

ConfigManager::ConfigManager(QObject *parent) : QObject(parent)
{
	saveTimer_.setSingleShot(true);
	saveTimer_.setInterval(kSaveDelayMs);
	connect(&saveTimer_, &QTimer::timeout, this, []() { obs_frontend_save(); });
}

ConfigManager::~ConfigManager()
{
	disconnectFromFrontend();
}

void ConfigManager::connectToFrontend()
{
	if (connected_)
		return;

	obs_frontend_add_save_callback(SaveCallback, this);
	connected_ = true;
	loadTemplates();
}

void ConfigManager::disconnectFromFrontend()
{
	if (!connected_)
		return;

	saveTimer_.stop();
	obs_frontend_remove_save_callback(SaveCallback, this);
	connected_ = false;
}

// --- Per-collection multiviews ---

QVector<MultiviewConfig> ConfigManager::multiviews() const
{
	QVector<MultiviewConfig> list = multiviews_.values();
	std::sort(list.begin(), list.end(), LessByName);
	return list;
}

bool ConfigManager::hasMultiview(const QString &id) const
{
	return multiviews_.contains(id);
}

MultiviewConfig ConfigManager::getMultiview(const QString &id) const
{
	return multiviews_.value(id);
}

QString ConfigManager::findIdByName(const QString &name) const
{
	for (const MultiviewConfig &mv : multiviews_) {
		if (mv.name == name)
			return mv.id;
	}
	return QString();
}

bool ConfigManager::isNameTaken(const QString &name, const QString &exceptId) const
{
	QString id = findIdByName(name);
	return !id.isEmpty() && id != exceptId;
}

QString ConfigManager::addMultiview(MultiviewConfig mv)
{
	mv.name = mv.name.trimmed();
	if (mv.name.isEmpty() || isNameTaken(mv.name))
		return QString();

	mv.id = NewId();
	multiviews_.insert(mv.id, mv);
	requestSave();
	emit multiviewAdded(mv.id);
	return mv.id;
}

void ConfigManager::updateLayout(const MultiviewConfig &mv)
{
	auto it = multiviews_.find(mv.id);
	if (it == multiviews_.end())
		return;

	QString name = mv.name.trimmed();
	if (name.isEmpty() || isNameTaken(name, mv.id))
		name = it->name;
	bool renamed = name != it->name;

	MultiviewConfig updated = mv;
	updated.name = name;
	updated.window = it->window;
	*it = updated;

	requestSave();
	if (renamed)
		emit multiviewRenamed(mv.id);
	emit multiviewUpdated(mv.id);
}

void ConfigManager::removeMultiview(const QString &id)
{
	if (multiviews_.remove(id)) {
		requestSave();
		emit multiviewRemoved(id);
	}
}

bool ConfigManager::renameMultiview(const QString &id, const QString &newName)
{
	auto it = multiviews_.find(id);
	QString name = newName.trimmed();
	if (it == multiviews_.end() || name.isEmpty() || isNameTaken(name, id))
		return false;
	if (it->name == name)
		return true;

	it->name = name;
	requestSave();
	emit multiviewRenamed(id);
	return true;
}

QString ConfigManager::duplicateMultiview(const QString &id, const QString &newName)
{
	if (!multiviews_.contains(id))
		return QString();

	MultiviewConfig copy = multiviews_.value(id);
	copy.name = newName;
	copy.window.open = false;
	return addMultiview(copy);
}

WindowState ConfigManager::windowState(const QString &id) const
{
	return multiviews_.value(id).window;
}

void ConfigManager::setWindowState(const QString &id, const WindowState &state)
{
	auto it = multiviews_.find(id);
	if (it == multiviews_.end())
		return;

	WindowState &current = it->window;
	if (current.geometry == state.geometry && current.screenName == state.screenName &&
	    current.screenIndex == state.screenIndex && current.fullscreen == state.fullscreen &&
	    current.open == state.open)
		return;

	current = state;
	requestSave();
}

// --- Global layout templates ---

QStringList ConfigManager::templateNames() const
{
	QStringList names = templates_.keys();
	names.prepend(BuiltInTemplateName());
	return names;
}

bool ConfigManager::hasTemplate(const QString &name) const
{
	return isBuiltInTemplate(name) || templates_.contains(name);
}

TemplateConfig ConfigManager::getTemplate(const QString &name) const
{
	if (isBuiltInTemplate(name))
		return defaultTemplate();
	return templates_.value(name);
}

void ConfigManager::addTemplate(const TemplateConfig &t)
{
	if (t.name.isEmpty() || isBuiltInTemplate(t.name))
		return;

	templates_[t.name] = t;
	saveTemplates();
	emit templatesChanged();
}

void ConfigManager::replaceTemplate(const QString &oldName, const TemplateConfig &t)
{
	if (t.name.isEmpty() || isBuiltInTemplate(oldName) || isBuiltInTemplate(t.name))
		return;
	if (t.name != oldName && templates_.contains(t.name))
		return;

	templates_.remove(oldName);
	templates_[t.name] = t;
	saveTemplates();
	emit templatesChanged();
}

void ConfigManager::removeTemplate(const QString &name)
{
	if (templates_.remove(name)) {
		saveTemplates();
		emit templatesChanged();
	}
}

void ConfigManager::renameTemplate(const QString &oldName, const QString &newName)
{
	if (!templates_.contains(oldName) || hasTemplate(newName))
		return;

	TemplateConfig t = templates_.take(oldName);
	t.name = newName;
	templates_[newName] = t;
	saveTemplates();
	emit templatesChanged();
}

bool ConfigManager::isBuiltInTemplate(const QString &name) const
{
	return name == BuiltInTemplateName();
}

TemplateConfig ConfigManager::defaultTemplate() const
{
	TemplateConfig t;
	t.name = BuiltInTemplateName();
	t.gridRows = 4;
	t.gridCols = 4;

	// Top half: 2x2 Preview on the left, 2x2 Program on the right
	auto bigCell = [&](int col, WidgetType type, const char *labelKey) {
		CellConfig cell;
		cell.row = 0;
		cell.col = col;
		cell.rowSpan = 2;
		cell.colSpan = 2;
		cell.widget.type = type;
		cell.widget.labelText = QString::fromUtf8(LG_TEXT(labelKey));
		return cell;
	};
	t.cells.append(bigCell(0, WidgetType::Preview, "DefaultTemplate.Preview"));
	t.cells.append(bigCell(2, WidgetType::Program, "DefaultTemplate.Program"));

	// Bottom two rows: the collection's scenes in order, then placeholders.
	// Scene labels are left automatic so they follow scene renames.
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	size_t sceneIdx = 0;
	for (int r = 2; r <= 3; r++) {
		for (int c = 0; c <= 3; c++) {
			CellConfig cell;
			cell.row = r;
			cell.col = c;

			if (sceneIdx < scenes.sources.num) {
				obs_source_t *scene = scenes.sources.array[sceneIdx++];
				cell.widget.type = WidgetType::Scene;
				cell.widget.sceneName = QString::fromUtf8(obs_source_get_name(scene));
				cell.widget.sourceUuid = QString::fromUtf8(obs_source_get_uuid(scene));
			} else {
				cell.widget.type = WidgetType::Placeholder;
			}
			t.cells.append(cell);
		}
	}

	obs_frontend_source_list_free(&scenes);
	return t;
}

QString ConfigManager::templatesConfigPath() const
{
	return ModuleConfigPath("templates.json");
}

void ConfigManager::loadTemplates()
{
	templates_.clear();

	for (TemplateConfig t : MultiviewSerializer::TemplatesFromJson(ReadJsonFile(templatesConfigPath()))) {
		// Older versions allowed a user template to shadow the built-in name
		if (isBuiltInTemplate(t.name)) {
			QString base = t.name;
			for (int n = 2; hasTemplate(t.name); n++)
				t.name = QStringLiteral("%1 (%2)").arg(base).arg(n);
		}
		templates_[t.name] = t;
	}

	emit templatesChanged();
}

void ConfigManager::saveTemplates() const
{
	WriteJsonFile(templatesConfigPath(), MultiviewSerializer::TemplatesToJson(templates_.values()));
}

// --- Scene collection persistence ---

void ConfigManager::SaveCallback(obs_data_t *saveData, bool saving, void *param)
{
	auto *self = static_cast<ConfigManager *>(param);
	if (saving)
		self->saveToCollection(saveData);
	else
		self->loadFromCollection(saveData);
}

void ConfigManager::saveToCollection(obs_data_t *saveData) const
{
	QJsonObject root = MultiviewSerializer::MultiviewsToJson(multiviews());
	QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

	obs_data_t *data = obs_data_create_from_json(json.constData());
	if (!data) {
		obs_log(LOG_WARNING, "could not serialize multiviews for the scene collection");
		return;
	}
	obs_data_set_obj(saveData, kSaveKey, data);
	obs_data_release(data);
}

void ConfigManager::loadFromCollection(obs_data_t *saveData)
{
	obs_data_t *data = obs_data_get_obj(saveData, kSaveKey);
	if (!data) {
		// Collection saved before multiviews lived inside it
		QVector<MultiviewConfig> legacy = loadLegacyCollectionFile();
		finishLoad(legacy);
		if (!legacy.isEmpty())
			requestSave();
		return;
	}

	QByteArray json(obs_data_get_json(data));
	obs_data_release(data);

	finishLoad(MultiviewSerializer::MultiviewsFromJson(QJsonDocument::fromJson(json).object()));
}

void ConfigManager::finishLoad(const QVector<MultiviewConfig> &multiviews)
{
	QVector<MultiviewConfig> list = multiviews;
	resolveSourceUuids(list);

	multiviews_.clear();
	for (const MultiviewConfig &mv : list)
		multiviews_.insert(mv.id, mv);

	awaitingLoad_ = false;
	emit multiviewsReloaded();
}

// Version 1 kept one JSON file per scene collection, named after a sanitized
// form of the collection name. It is read once, when a collection without
// embedded multiview data loads, and the result is saved into the collection.
QVector<MultiviewConfig> ConfigManager::loadLegacyCollectionFile() const
{
	char *collection = obs_frontend_get_current_scene_collection();
	QString collectionName = collection ? QString::fromUtf8(collection) : QStringLiteral("default");
	bfree(collection);
	collectionName.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "_");

	QString dir = ModuleConfigPath("multiviews");
	if (dir.isEmpty())
		return {};

	QString path = dir + "/" + collectionName + ".json";
	if (!QFile::exists(path))
		return {};

	QVector<MultiviewConfig> list = MultiviewSerializer::MultiviewsFromJson(ReadJsonFile(path));
	obs_log(LOG_INFO, "migrated %d multiview(s) from %s into the scene collection", (int)list.size(),
		path.toUtf8().constData());
	return list;
}

// Layouts written before UUIDs were tracked only know scenes and sources by
// name. Look the UUIDs up once so later renames can be followed.
void ConfigManager::resolveSourceUuids(QVector<MultiviewConfig> &multiviews) const
{
	for (MultiviewConfig &mv : multiviews) {
		for (CellConfig &cell : mv.cells) {
			WidgetConfig &w = cell.widget;
			QString name = w.targetName();
			if (name.isEmpty() || !w.sourceUuid.isEmpty())
				continue;

			obs_source_t *source = obs_get_source_by_name(name.toUtf8().constData());
			if (source) {
				w.sourceUuid = QString::fromUtf8(obs_source_get_uuid(source));
				obs_source_release(source);
			}
		}
	}
}

void ConfigManager::requestSave()
{
	if (connected_)
		saveTimer_.start();
}

void ConfigManager::onSceneCollectionChanging()
{
	// The outgoing collection is saved after this event, from the data still
	// held here; the incoming one replaces it when it loads
	awaitingLoad_ = true;
}

void ConfigManager::onSceneCollectionChanged()
{
	// A brand-new or empty collection never calls the load callback
	if (awaitingLoad_) {
		QVector<MultiviewConfig> legacy = loadLegacyCollectionFile();
		finishLoad(legacy);
		if (!legacy.isEmpty())
			requestSave();
	}
}

void ConfigManager::onFinishedLoading()
{
	onSceneCollectionChanged();
}

void ConfigManager::onExit()
{
	// OBS has already saved the collection; anything after this point (such
	// as windows closing) must not schedule another save
	saveTimer_.stop();
	disconnectFromFrontend();
}

// --- Source tracking ---

void ConfigManager::onSourceRenamed(const QString &uuid, const QString &prevName, const QString &newName)
{
	for (MultiviewConfig &mv : multiviews_) {
		bool changed = false;

		for (CellConfig &cell : mv.cells) {
			WidgetConfig &w = cell.widget;
			if (w.type != WidgetType::Scene && w.type != WidgetType::Source)
				continue;
			if (w.sourceUuid != uuid && w.targetName() != prevName)
				continue;

			if (w.type == WidgetType::Scene)
				w.sceneName = newName;
			else
				w.sourceName = newName;
			w.sourceUuid = uuid;

			// A label that just repeated the old name follows the new one
			if (w.labelText == prevName)
				w.labelText = newName;
			changed = true;
		}

		if (changed) {
			requestSave();
			emit multiviewUpdated(mv.id);
		}
	}
}
