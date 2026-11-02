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

#include "template-edit-dialog.hpp"
#include "layout-editor-widget.hpp"
#include "widget-type-style.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QFrame *makeRule()
{
	auto *line = new QFrame();
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

} // namespace

TemplateEditDialog::TemplateEditDialog(const TemplateConfig &tmpl, QWidget *parent) : QDialog(parent), tmpl_(tmpl)
{
	setWindowTitle(QString(LG_TEXT("ManageTemplatesDialog.EditTitle")).arg(tmpl.name));
	setSizeGripEnabled(true);
	setMinimumSize(900, 560);
	resize(1180, 720);

	// Header, editor and footer sit edge to edge, divided by rules, so the
	// canvas gets as much of the window as possible
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// Header: name and inline error on one line
	auto *header = new QHBoxLayout();
	header->setContentsMargins(10, 6, 10, 6);
	header->setSpacing(8);

	nameEdit_ = new QLineEdit(tmpl_.name);
	nameEdit_->setMinimumWidth(200);
	nameEdit_->setMaximumWidth(420);
	nameEdit_->setPlaceholderText(LG_TEXT("TemplateEditDialog.NamePlaceholder"));

	auto *caption = new QLabel(LG_TEXT("EditDialog.Name"));
	caption->setBuddy(nameEdit_);
	header->addWidget(caption);
	header->addWidget(nameEdit_, 1);

	errorLabel_ = new QLabel();
	QPalette errorPalette = errorLabel_->palette();
	errorPalette.setColor(QPalette::WindowText, WidgetTypeColor(WidgetType::Program));
	errorLabel_->setPalette(errorPalette);
	header->addWidget(errorLabel_);
	header->addStretch(1);

	mainLayout->addLayout(header);
	mainLayout->addWidget(makeRule());

	// Body
	layoutEditor_ = new LayoutEditorWidget();
	layoutEditor_->setLineStyleEditable(false);
	LayoutState state;
	state.rows = tmpl_.gridRows;
	state.cols = tmpl_.gridCols;
	state.cells = tmpl_.cells;
	layoutEditor_->setLayoutState(state);
	mainLayout->addWidget(layoutEditor_, 1);
	mainLayout->addWidget(makeRule());

	// Footer
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(10, 6, 10, 6);
	footer->setSpacing(8);
	infoLabel_ = new QLabel();
	infoLabel_->setForegroundRole(QPalette::PlaceholderText);
	footer->addWidget(infoLabel_, 1);

	auto *cancelButton = new QPushButton(LG_TEXT("EditDialog.Cancel"));
	cancelButton->setAutoDefault(false);
	footer->addWidget(cancelButton);
	primaryButton_ = new QPushButton(LG_TEXT("TemplateEditDialog.Save"));
	// Enter must not save from arbitrary fields (the inspector has text boxes);
	// only the name field handles it, below
	primaryButton_->setAutoDefault(false);
	primaryButton_->setDefault(false);
	footer->addWidget(primaryButton_);
	mainLayout->addLayout(footer);

	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
	connect(primaryButton_, &QPushButton::clicked, this, &TemplateEditDialog::onConfirm);
	connect(nameEdit_, &QLineEdit::textChanged, this, &TemplateEditDialog::onNameChanged);
	connect(nameEdit_, &QLineEdit::returnPressed, this, [this]() {
		if (primaryButton_->isEnabled())
			onConfirm();
	});
	connect(layoutEditor_, &LayoutEditorWidget::modifiedChanged, this, &TemplateEditDialog::updateInfo);

	onNameChanged();
}

void TemplateEditDialog::onNameChanged()
{
	QString name = nameEdit_->text().trimmed();
	QString error;
	if (name.isEmpty()) {
		error = LG_TEXT("TemplateEditDialog.NameEmpty");
	} else {
		// hasTemplate() also covers the built-in template, whose name is refused
		if (name != tmpl_.name && GetConfigManager()->hasTemplate(name))
			error = QString(LG_TEXT("TemplateEditDialog.NameTaken")).arg(name);
	}

	errorLabel_->setText(error);
	primaryButton_->setEnabled(error.isEmpty());

	setWindowTitle(QString(LG_TEXT("ManageTemplatesDialog.EditTitle")).arg(name.isEmpty() ? tmpl_.name : name));
	updateInfo();
}

bool TemplateEditDialog::hasUnsavedChanges() const
{
	return layoutEditor_->isModified() || nameEdit_->text().trimmed() != tmpl_.name;
}

void TemplateEditDialog::updateInfo()
{
	infoLabel_->setText(hasUnsavedChanges() ? LG_TEXT("EditDialog.UnsavedChanges")
						: LG_TEXT("EditDialog.NoChanges"));
}

void TemplateEditDialog::reject()
{
	if (!skipDiscardPrompt_ && hasUnsavedChanges()) {
		QMessageBox box(QMessageBox::Question, windowTitle(), LG_TEXT("TemplateEditDialog.DiscardMsg"),
				QMessageBox::NoButton, this);
		QPushButton *discard = box.addButton(LG_TEXT("EditDialog.Discard"), QMessageBox::DestructiveRole);
		QPushButton *keep = box.addButton(LG_TEXT("EditDialog.KeepEditing"), QMessageBox::RejectRole);
		box.setDefaultButton(keep);
		box.setEscapeButton(keep);
		box.exec();
		if (box.clickedButton() != discard)
			return;
	}
	QDialog::reject();
}

void TemplateEditDialog::onConfirm()
{
	// Validate before closing so a rejected name never loses the edits
	onNameChanged();
	if (!primaryButton_->isEnabled())
		return;

	if (!GetConfigManager()->hasTemplate(tmpl_.name)) {
		// Deleted while this dialog was open
		skipDiscardPrompt_ = true;
		reject();
		return;
	}

	accept();
}

TemplateConfig TemplateEditDialog::result() const
{
	LayoutState state = layoutEditor_->layoutState();
	TemplateConfig t = tmpl_;
	t.name = nameEdit_->text().trimmed();
	t.gridRows = state.rows;
	t.gridCols = state.cols;
	t.cells = state.cells;
	return t;
}
