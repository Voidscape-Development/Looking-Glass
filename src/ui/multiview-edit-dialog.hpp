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
#include <QMessageBox>
#include <QPointer>
#include <QString>

#include "../core/multiview-config.hpp"

class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class LayoutEditorWidget;

/**
 * Window for creating or editing a multiview layout: a name, template actions
 * and the full layout editor (content library, canvas and inspector). Every
 * change is undoable; closing with unsaved changes asks first.
 */
class MultiviewEditDialog : public QDialog {
	Q_OBJECT

public:
	explicit MultiviewEditDialog(const MultiviewConfig &config, bool isNew, QWidget *parent = nullptr);
	MultiviewConfig result() const;

	// Opens a dialog that deletes itself on close, without a nested event
	// loop, so whatever opened it (a window, a menu) may be deleted meanwhile
	static void launch(const MultiviewConfig &config, bool isNew, QWidget *parent);

public slots:
	void reject() override;

private slots:
	void onConfirm();

private:
	void populateTemplateMenu();
	void onSaveAsTemplate();
	void applyTemplate(const QString &name);
	void onNameChanged();
	void updateInfo();
	bool hasUnsavedChanges() const;
	void saveAndClose();

	LayoutEditorWidget *layoutEditor_ = nullptr;
	QLineEdit *nameEdit_ = nullptr;
	QLabel *errorLabel_ = nullptr;
	QMenu *templateMenu_ = nullptr;
	QLabel *infoLabel_ = nullptr;
	QPushButton *primaryButton_ = nullptr;

	MultiviewConfig config_;
	bool isNew_;
	bool skipDiscardPrompt_ = false;
	QPointer<QMessageBox> discardPrompt_;
};
