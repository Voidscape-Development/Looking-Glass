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

#include "multiview-manage-dialog.hpp"
#include "multiview-edit-dialog.hpp"
#include "multiview-window.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFormLayout>

ManageMultiviewsDialog::ManageMultiviewsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(LG_TEXT("ManageDialog.Title"));
	setMinimumSize(400, 350);

	auto *mainLayout = new QHBoxLayout(this);

	// List
	listWidget_ = new QListWidget();
	mainLayout->addWidget(listWidget_, 1);

	// Buttons
	auto *btnLayout = new QVBoxLayout();
	showBtn_ = new QPushButton(LG_TEXT("ManageDialog.ShowMultiview"));
	editBtn_ = new QPushButton(LG_TEXT("ManageDialog.EditMultiview"));
	renameBtn_ = new QPushButton(LG_TEXT("ManageDialog.RenameMultiview"));
	deleteBtn_ = new QPushButton(LG_TEXT("ManageDialog.DeleteMultiview"));
	duplicateBtn_ = new QPushButton(LG_TEXT("ManageDialog.DuplicateMultiview"));
	createTemplateBtn_ = new QPushButton(LG_TEXT("ManageDialog.CreateTemplate"));

	btnLayout->addWidget(showBtn_);
	btnLayout->addWidget(editBtn_);
	btnLayout->addWidget(renameBtn_);
	btnLayout->addWidget(deleteBtn_);
	btnLayout->addWidget(duplicateBtn_);
	btnLayout->addSpacing(10);
	btnLayout->addWidget(createTemplateBtn_);
	btnLayout->addStretch();
	mainLayout->addLayout(btnLayout);

	connect(showBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onShow);
	connect(editBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onEdit);
	connect(renameBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onRename);
	connect(deleteBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onDelete);
	connect(duplicateBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onDuplicate);
	connect(createTemplateBtn_, &QPushButton::clicked, this, &ManageMultiviewsDialog::onCreateTemplate);
	connect(listWidget_, &QListWidget::currentItemChanged, this, &ManageMultiviewsDialog::onSelectionChanged);

	ConfigManager *cm = GetConfigManager();
	connect(cm, &ConfigManager::multiviewAdded, this, &ManageMultiviewsDialog::refreshList);
	connect(cm, &ConfigManager::multiviewRemoved, this, &ManageMultiviewsDialog::refreshList);
	connect(cm, &ConfigManager::multiviewRenamed, this, &ManageMultiviewsDialog::refreshList);
	connect(cm, &ConfigManager::multiviewsReloaded, this, &ManageMultiviewsDialog::refreshList);

	refreshList();
}

void ManageMultiviewsDialog::refreshList()
{
	QString currentId = selectedId();

	listWidget_->clear();
	for (const MultiviewConfig &mv : GetConfigManager()->multiviews()) {
		auto *item = new QListWidgetItem(mv.name, listWidget_);
		item->setData(Qt::UserRole, mv.id);
		if (mv.id == currentId)
			listWidget_->setCurrentItem(item);
	}
	onSelectionChanged();
}

QString ManageMultiviewsDialog::selectedId() const
{
	QListWidgetItem *item = listWidget_->currentItem();
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ManageMultiviewsDialog::onSelectionChanged()
{
	bool hasSelection = listWidget_->currentItem() != nullptr;
	showBtn_->setEnabled(hasSelection);
	editBtn_->setEnabled(hasSelection);
	renameBtn_->setEnabled(hasSelection);
	deleteBtn_->setEnabled(hasSelection);
	duplicateBtn_->setEnabled(hasSelection);
	createTemplateBtn_->setEnabled(hasSelection);
}

void ManageMultiviewsDialog::onShow()
{
	QString id = selectedId();
	if (!id.isEmpty())
		MultiviewWindow::openOrFocus(id);
}

void ManageMultiviewsDialog::onEdit()
{
	QString id = selectedId();
	if (id.isEmpty())
		return;

	// An open window reloads through ConfigManager::multiviewUpdated
	MultiviewEditDialog dlg(GetConfigManager()->getMultiview(id), false, this);
	dlg.exec();
}

void ManageMultiviewsDialog::onRename()
{
	QString id = selectedId();
	if (id.isEmpty())
		return;

	ConfigManager *cm = GetConfigManager();
	QString oldName = cm->getMultiview(id).name;
	bool ok;
	QString newName = QInputDialog::getText(this, LG_TEXT("ManageDialog.RenameMultiview"),
						LG_TEXT("ManageDialog.RenamePrompt"), QLineEdit::Normal, oldName, &ok);
	newName = newName.trimmed();
	if (!ok || newName.isEmpty() || newName == oldName)
		return;

	if (cm->isNameTaken(newName, id)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.MultiviewExists"));
		return;
	}

	// Open windows retitle themselves from the rename signal
	cm->renameMultiview(id, newName);
}

void ManageMultiviewsDialog::onDelete()
{
	QString id = selectedId();
	if (id.isEmpty())
		return;

	QString name = GetConfigManager()->getMultiview(id).name;
	auto reply = QMessageBox::question(this, LG_TEXT("ManageDialog.DeleteMultiview"),
					   QString(LG_TEXT("ManageDialog.DeleteConfirm")).arg(name),
					   QMessageBox::Yes | QMessageBox::No);
	if (reply == QMessageBox::Yes) {
		// An open window closes itself from the removal signal
		GetConfigManager()->removeMultiview(id);
	}
}

void ManageMultiviewsDialog::onDuplicate()
{
	QString id = selectedId();
	if (id.isEmpty())
		return;

	ConfigManager *cm = GetConfigManager();
	bool ok;
	QString newName = QInputDialog::getText(this, LG_TEXT("ManageDialog.DuplicateMultiview"),
						LG_TEXT("ManageDialog.DuplicatePrompt"), QLineEdit::Normal,
						cm->getMultiview(id).name + LG_TEXT("ManageDialog.DuplicateSuffix"),
						&ok);
	newName = newName.trimmed();
	if (!ok || newName.isEmpty())
		return;

	if (cm->isNameTaken(newName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.MultiviewExists"));
		return;
	}

	cm->duplicateMultiview(id, newName);
}

void ManageMultiviewsDialog::onCreateTemplate()
{
	QString id = selectedId();
	if (id.isEmpty())
		return;

	MultiviewConfig mv = GetConfigManager()->getMultiview(id);

	// Build a custom dialog with name field and preserve-sources checkbox
	QDialog dlg(this);
	dlg.setWindowTitle(LG_TEXT("ManageDialog.CreateTemplate"));
	auto *layout = new QFormLayout(&dlg);

	auto *nameEdit = new QLineEdit(mv.name + LG_TEXT("ManageDialog.TemplateSuffix"));
	layout->addRow(LG_TEXT("ManageDialog.TemplateNamePrompt"), nameEdit);

	auto *preserveCheck = new QCheckBox(LG_TEXT("ManageDialog.PreserveSources"));
	preserveCheck->setToolTip(LG_TEXT("ManageDialog.PreserveSourcesTooltip"));
	layout->addRow(preserveCheck);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	layout->addRow(buttons);

	if (dlg.exec() != QDialog::Accepted)
		return;

	QString templateName = nameEdit->text().trimmed();
	if (templateName.isEmpty())
		return;

	if (GetConfigManager()->hasTemplate(templateName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.TemplateExists"));
		return;
	}

	bool preserveSources = preserveCheck->isChecked();

	TemplateConfig tmpl;
	tmpl.name = templateName;
	tmpl.gridRows = mv.gridRows;
	tmpl.gridCols = mv.gridCols;
	tmpl.preserveSources = preserveSources;

	for (const CellConfig &cell : mv.cells) {
		CellConfig tc = cell;
		if (!preserveSources && tc.widget.type != WidgetType::None) {
			// Convert non-structural widgets to placeholders
			QString origLabel;
			switch (tc.widget.type) {
			case WidgetType::Preview:
				origLabel = LG_TEXT("GridEditor.Preview");
				break;
			case WidgetType::Program:
				origLabel = LG_TEXT("GridEditor.Program");
				break;
			case WidgetType::Canvas:
				origLabel = LG_TEXT("GridEditor.Canvas");
				break;
			case WidgetType::Scene:
				origLabel = tc.widget.sceneName;
				break;
			case WidgetType::Source:
				origLabel = tc.widget.sourceName;
				break;
			default:
				break;
			}
			tc.widget.type = WidgetType::Placeholder;
			tc.widget.sceneName.clear();
			tc.widget.sourceName.clear();
			tc.widget.sourceUuid.clear();
			if (!origLabel.isEmpty() && tc.widget.labelText.isEmpty())
				tc.widget.labelText = origLabel;
		}
		// When preserveSources is true, keep the cell exactly as-is
		tmpl.cells.append(tc);
	}

	GetConfigManager()->addTemplate(tmpl);
	QMessageBox::information(this, LG_TEXT("ManageDialog.TemplateCreated"),
				 QString(LG_TEXT("ManageDialog.TemplateCreatedMsg")).arg(templateName));
}
