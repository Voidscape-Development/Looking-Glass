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

#include <QDialog>
#include <QListWidget>

#include "../core/multiview-config.hpp"

/**
 * Dialog for managing the list of multiviews in the current scene collection.
 * Provides show, edit, rename, delete, duplicate, and create-template actions.
 */
class ManageMultiviewsDialog : public QDialog {
	Q_OBJECT

public:
	explicit ManageMultiviewsDialog(QWidget *parent = nullptr);

private slots:
	void onShow();
	void onEdit();
	void onRename();
	void onDelete();
	void onDuplicate();
	void onCreateTemplate();
	void refreshList();
	void onSelectionChanged();

private:
	QString selectedId() const;

	QListWidget *listWidget_;
	QPushButton *showBtn_;
	QPushButton *editBtn_;
	QPushButton *renameBtn_;
	QPushButton *deleteBtn_;
	QPushButton *duplicateBtn_;
	QPushButton *createTemplateBtn_;
};

// Builds a template from a multiview's layout. Unless preserveSources is set,
// cells with content become placeholders labelled with what they showed.
TemplateConfig BuildTemplateFromMultiview(const MultiviewConfig &mv, const QString &name, bool preserveSources);

// Asks for a template name and whether to keep scene and source names, then
// saves mv's layout as a new template. Returns the new template's name, or an
// empty string when cancelled.
QString PromptSaveAsTemplate(QWidget *parent, const MultiviewConfig &mv);
