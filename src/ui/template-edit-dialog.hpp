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
#include <QString>

#include "../core/multiview-config.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class LayoutEditorWidget;

/**
 * Window for editing a saved template: a name and the full layout
 * editor. The name is validated before the window closes,
 * so a clash never throws the edits away.
 */
class TemplateEditDialog : public QDialog {
	Q_OBJECT

public:
	explicit TemplateEditDialog(const TemplateConfig &tmpl, QWidget *parent = nullptr);
	TemplateConfig result() const;

public slots:
	void reject() override;

private slots:
	void onConfirm();

private:
	void onNameChanged();
	void updateInfo();
	bool hasUnsavedChanges() const;

	LayoutEditorWidget *layoutEditor_ = nullptr;
	QLineEdit *nameEdit_ = nullptr;
	QLabel *errorLabel_ = nullptr;
	QLabel *infoLabel_ = nullptr;
	QPushButton *primaryButton_ = nullptr;

	TemplateConfig tmpl_;
	bool skipDiscardPrompt_ = false;
};
