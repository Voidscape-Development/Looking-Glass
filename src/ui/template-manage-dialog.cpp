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

#include "template-manage-dialog.hpp"
#include "template-edit-dialog.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>

ManageTemplatesDialog::ManageTemplatesDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(LG_TEXT("ManageTemplatesDialog.Title"));
	setMinimumSize(400, 300);

	auto *mainLayout = new QHBoxLayout(this);

	// List
	listWidget_ = new QListWidget();
	mainLayout->addWidget(listWidget_, 1);

	// Buttons
	auto *btnLayout = new QVBoxLayout();
	renameBtn_ = new QPushButton(LG_TEXT("ManageTemplatesDialog.RenameTemplate"));
	editBtn_ = new QPushButton(LG_TEXT("ManageTemplatesDialog.EditTemplate"));
	deleteBtn_ = new QPushButton(LG_TEXT("ManageTemplatesDialog.DeleteTemplate"));

	btnLayout->addWidget(renameBtn_);
	btnLayout->addWidget(editBtn_);
	btnLayout->addWidget(deleteBtn_);
	btnLayout->addStretch();
	mainLayout->addLayout(btnLayout);

	connect(renameBtn_, &QPushButton::clicked, this, &ManageTemplatesDialog::onRename);
	connect(editBtn_, &QPushButton::clicked, this, &ManageTemplatesDialog::onEdit);
	connect(deleteBtn_, &QPushButton::clicked, this, &ManageTemplatesDialog::onDelete);
	connect(listWidget_, &QListWidget::currentItemChanged, this, &ManageTemplatesDialog::onSelectionChanged);

	connect(GetConfigManager(), &ConfigManager::templatesChanged, this, &ManageTemplatesDialog::refreshList);

	refreshList();
}

bool ManageTemplatesDialog::isDefaultTemplate(const QString &name) const
{
	return GetConfigManager()->isBuiltInTemplate(name);
}

void ManageTemplatesDialog::refreshList()
{
	QString current;
	if (listWidget_->currentItem())
		current = listWidget_->currentItem()->text();

	listWidget_->clear();
	QStringList names = GetConfigManager()->templateNames();
	names.sort(Qt::CaseInsensitive);
	listWidget_->addItems(names);

	if (!current.isEmpty()) {
		auto items = listWidget_->findItems(current, Qt::MatchExactly);
		if (!items.isEmpty())
			listWidget_->setCurrentItem(items.first());
	}
	onSelectionChanged();
}

void ManageTemplatesDialog::onSelectionChanged()
{
	bool hasSelection = listWidget_->currentItem() != nullptr;
	bool isDefault = hasSelection && isDefaultTemplate(listWidget_->currentItem()->text());

	// The built-in default is generated from the current scenes and is
	// read-only
	renameBtn_->setEnabled(hasSelection && !isDefault);
	editBtn_->setEnabled(hasSelection && !isDefault);
	deleteBtn_->setEnabled(hasSelection && !isDefault);
}

void ManageTemplatesDialog::onRename()
{
	if (!listWidget_->currentItem())
		return;

	QString oldName = listWidget_->currentItem()->text();
	if (isDefaultTemplate(oldName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"),
				     LG_TEXT("ManageTemplatesDialog.CannotModifyDefault"));
		return;
	}

	bool ok;
	QString newName = QInputDialog::getText(this, LG_TEXT("ManageTemplatesDialog.RenameTemplate"),
						LG_TEXT("ManageTemplatesDialog.RenamePrompt"), QLineEdit::Normal,
						oldName, &ok);
	if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
		return;

	newName = newName.trimmed();
	if (GetConfigManager()->hasTemplate(newName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageTemplatesDialog.TemplateExists"));
		return;
	}

	GetConfigManager()->renameTemplate(oldName, newName);
}

void ManageTemplatesDialog::onEdit()
{
	if (!listWidget_->currentItem())
		return;

	QString templateName = listWidget_->currentItem()->text();
	if (isDefaultTemplate(templateName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"),
				     LG_TEXT("ManageTemplatesDialog.CannotModifyDefault"));
		return;
	}
	TemplateConfig tmpl = GetConfigManager()->getTemplate(templateName);

	TemplateEditDialog dlg(tmpl, this);
	if (dlg.exec() == QDialog::Accepted)
		GetConfigManager()->replaceTemplate(templateName, dlg.result());
}

void ManageTemplatesDialog::onDelete()
{
	if (!listWidget_->currentItem())
		return;

	QString name = listWidget_->currentItem()->text();
	if (isDefaultTemplate(name)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"),
				     LG_TEXT("ManageTemplatesDialog.CannotModifyDefault"));
		return;
	}

	auto reply = QMessageBox::question(this, LG_TEXT("ManageTemplatesDialog.DeleteTemplate"),
					   QString(LG_TEXT("ManageTemplatesDialog.DeleteConfirm")).arg(name),
					   QMessageBox::Yes | QMessageBox::No);
	if (reply == QMessageBox::Yes)
		GetConfigManager()->removeTemplate(name);
}
