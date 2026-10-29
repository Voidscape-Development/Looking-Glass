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
#include "grid-editor-widget.hpp"
#include "cell-config-dialog.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../core/grid-model.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>

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

	// Build an edit dialog with grid editor, size controls, and template properties
	QDialog dlg(this);
	dlg.setWindowTitle(QString(LG_TEXT("ManageTemplatesDialog.EditTitle")).arg(templateName));
	dlg.setMinimumSize(800, 550);

	auto *mainLayout = new QVBoxLayout(&dlg);

	// Name field
	auto *nameLayout = new QHBoxLayout();
	nameLayout->addWidget(new QLabel(LG_TEXT("ManageTemplatesDialog.NameLabel")));
	auto *nameEdit = new QLineEdit(templateName);
	nameLayout->addWidget(nameEdit);
	mainLayout->addLayout(nameLayout);

	// Two-pane layout
	auto *paneLayout = new QHBoxLayout();

	// Left pane: grid editor
	auto *gridEditor = new GridEditorWidget();
	paneLayout->addWidget(gridEditor, 3);

	// Right pane: controls
	auto *rightLayout = new QVBoxLayout();

	auto *gridSettingsForm = new QFormLayout();
	auto *rowsSpin = new QSpinBox();
	rowsSpin->setRange(GridModel::kMinGridSize, GridModel::kMaxGridSize);
	rowsSpin->setValue(tmpl.gridRows);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.Rows"), rowsSpin);

	auto *colsSpin = new QSpinBox();
	colsSpin->setRange(GridModel::kMinGridSize, GridModel::kMaxGridSize);
	colsSpin->setValue(tmpl.gridCols);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.Columns"), colsSpin);

	rightLayout->addLayout(gridSettingsForm);
	rightLayout->addSpacing(10);

	// Preserve sources checkbox
	auto *preserveCheck = new QCheckBox(LG_TEXT("ManageDialog.PreserveSources"));
	preserveCheck->setToolTip(LG_TEXT("ManageDialog.PreserveSourcesTooltip"));
	preserveCheck->setChecked(tmpl.preserveSources);
	rightLayout->addWidget(preserveCheck);
	rightLayout->addSpacing(10);

	// Widget action buttons
	auto *setWidgetBtn = new QPushButton(LG_TEXT("EditDialog.SetWidget"));
	auto *editWidgetBtn = new QPushButton(LG_TEXT("EditDialog.EditWidget"));
	auto *mergeBtn = new QPushButton(LG_TEXT("EditDialog.MergeWidgets"));
	auto *resetBtn = new QPushButton(LG_TEXT("EditDialog.ResetWidgets"));

	rightLayout->addWidget(setWidgetBtn);
	rightLayout->addWidget(editWidgetBtn);
	rightLayout->addSpacing(10);
	rightLayout->addWidget(mergeBtn);
	rightLayout->addWidget(resetBtn);
	rightLayout->addStretch();

	paneLayout->addLayout(rightLayout, 1);
	mainLayout->addLayout(paneLayout, 1);

	// Dialog buttons
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	mainLayout->addWidget(buttons);

	// Load existing grid
	gridEditor->setGrid(tmpl.gridRows, tmpl.gridCols, tmpl.cells);

	// Update button states based on selection
	auto updateButtons = [&]() {
		int cellIdx = gridEditor->selectedCellIndex();
		bool hasSingleCell = cellIdx >= 0;
		setWidgetBtn->setEnabled(hasSingleCell);
		editWidgetBtn->setEnabled(hasSingleCell && cellIdx < gridEditor->cells().size() &&
					  gridEditor->cells()[cellIdx].widget.type != WidgetType::None);
		mergeBtn->setEnabled(gridEditor->canMergeSelected());
		resetBtn->setEnabled(gridEditor->canResetSelected());
	};

	QObject::connect(gridEditor, &GridEditorWidget::selectionChanged, &dlg, updateButtons);

	// Set/Edit widget
	auto onSetWidget = [&]() {
		int idx = gridEditor->selectedCellIndex();
		WidgetConfig current;
		if (idx >= 0)
			current = gridEditor->cells()[idx].widget;
		CellConfigDialog cellDlg(current, &dlg);
		if (cellDlg.exec() == QDialog::Accepted)
			gridEditor->setWidgetForSelected(cellDlg.result());
	};

	QObject::connect(setWidgetBtn, &QPushButton::clicked, &dlg, onSetWidget);
	QObject::connect(editWidgetBtn, &QPushButton::clicked, &dlg, onSetWidget);

	QObject::connect(mergeBtn, &QPushButton::clicked, &dlg, [&]() {
		if (!gridEditor->canMergeSelected()) {
			QMessageBox::warning(&dlg, LG_TEXT("EditDialog.CannotMerge"),
					     LG_TEXT("EditDialog.CannotMergeMsg"));
			return;
		}
		gridEditor->mergeSelected();
	});

	QObject::connect(resetBtn, &QPushButton::clicked, &dlg, [&]() { gridEditor->resetSelected(); });

	// Grid size changes
	auto onGridSizeChanged = [&]() {
		int newRows = rowsSpin->value();
		int newCols = colsSpin->value();
		int oldRows = gridEditor->gridRows();
		int oldCols = gridEditor->gridCols();
		if (newRows == oldRows && newCols == oldCols)
			return;

		QVector<CellConfig> newCells = GridModel::Resize(gridEditor->cells(), newRows, newCols);
		gridEditor->setGrid(newRows, newCols, newCells);
	};

	QObject::connect(rowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, onGridSizeChanged);
	QObject::connect(colsSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, onGridSizeChanged);

	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	updateButtons();

	if (dlg.exec() != QDialog::Accepted)
		return;

	QString newName = nameEdit->text().trimmed();
	if (newName.isEmpty())
		return;

	// Build updated template
	TemplateConfig updated;
	updated.name = newName;
	updated.gridRows = gridEditor->gridRows();
	updated.gridCols = gridEditor->gridCols();
	updated.cells = gridEditor->cells();
	updated.preserveSources = preserveCheck->isChecked();

	ConfigManager *cm = GetConfigManager();

	// Check for name conflicts (if name was changed)
	if (newName != templateName && cm->hasTemplate(newName)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageTemplatesDialog.TemplateExists"));
		return;
	}

	cm->replaceTemplate(templateName, updated);
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
