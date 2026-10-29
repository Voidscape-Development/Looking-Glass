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

#include "multiview-edit-dialog.hpp"
#include "grid-editor-widget.hpp"
#include "cell-config-dialog.hpp"
#include "multiview-window.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../core/grid-model.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QColorDialog>

MultiviewEditDialog::MultiviewEditDialog(const MultiviewConfig &config, bool isNew, QWidget *parent)
	: QDialog(parent),
	  config_(config),
	  isNew_(isNew)
{
	setWindowTitle(isNew ? LG_TEXT("EditDialog.CreateTitle")
			     : QString(LG_TEXT("EditDialog.EditTitle")).arg(config.name));

	// New multiviews start from the built-in default template
	if (config_.cells.isEmpty()) {
		TemplateConfig def = GetConfigManager()->defaultTemplate();
		config_.gridRows = def.gridRows;
		config_.gridCols = def.gridCols;
		config_.cells = def.cells;
	}
	setMinimumSize(800, 550);

	auto *mainLayout = new QVBoxLayout(this);

	// Name field
	auto *nameLayout = new QHBoxLayout();
	nameLayout->addWidget(new QLabel(LG_TEXT("EditDialog.Name")));
	nameEdit_ = new QLineEdit(config_.name);
	if (isNew_)
		nameEdit_->setPlaceholderText(LG_TEXT("EditDialog.NamePlaceholder"));
	nameLayout->addWidget(nameEdit_);
	mainLayout->addLayout(nameLayout);

	// Two-pane layout
	auto *paneLayout = new QHBoxLayout();

	// Left pane: grid editor
	auto *leftLayout = new QVBoxLayout();
	gridEditor_ = new GridEditorWidget();
	leftLayout->addWidget(gridEditor_, 1);

	// Template dropdown
	auto *templateLayout = new QHBoxLayout();
	templateLayout->addWidget(new QLabel(LG_TEXT("EditDialog.Template")));
	templateCombo_ = new QComboBox();
	templateCombo_->addItem(LG_TEXT("EditDialog.TemplateCurrent"));
	QStringList templateNames = GetConfigManager()->templateNames();
	templateNames.sort(Qt::CaseInsensitive);
	for (const QString &name : templateNames)
		templateCombo_->addItem(name);
	templateLayout->addWidget(templateCombo_, 1);
	leftLayout->addLayout(templateLayout);

	paneLayout->addLayout(leftLayout, 3);

	// Right pane: grid settings and buttons
	auto *rightLayout = new QVBoxLayout();

	// Grid size controls
	auto *gridSettingsForm = new QFormLayout();
	rowsSpin_ = new QSpinBox();
	rowsSpin_->setRange(GridModel::kMinGridSize, GridModel::kMaxGridSize);
	rowsSpin_->setValue(config_.gridRows);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.Rows"), rowsSpin_);

	colsSpin_ = new QSpinBox();
	colsSpin_->setRange(GridModel::kMinGridSize, GridModel::kMaxGridSize);
	colsSpin_->setValue(config_.gridCols);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.Columns"), colsSpin_);

	// Grid border width
	borderWidthSpin_ = new QSpinBox();
	borderWidthSpin_->setRange(1, 10);
	borderWidthSpin_->setValue(config_.gridBorderWidth);
	gridSettingsForm->addRow(LG_TEXT("EditDialog.BorderWidth"), borderWidthSpin_);

	// Grid line color
	gridLineColor_ = config_.gridLineColor;
	lineColorBtn_ = new QPushButton(LG_TEXT("EditDialog.LineColorChoose"));
	lineColorBtn_->setAutoFillBackground(true);
	auto updateColorBtnStyle = [this]() {
		lineColorBtn_->setStyleSheet(QString("background-color: %1; color: %2;")
						     .arg(gridLineColor_.name())
						     .arg(gridLineColor_.lightness() > 127 ? "black" : "white"));
	};
	updateColorBtnStyle();
	connect(lineColorBtn_, &QPushButton::clicked, this, [this, updateColorBtnStyle]() {
		QColor c = QColorDialog::getColor(gridLineColor_, this, LG_TEXT("EditDialog.ChooseLineColor"));
		if (c.isValid()) {
			gridLineColor_ = c;
			updateColorBtnStyle();
		}
	});
	gridSettingsForm->addRow(LG_TEXT("EditDialog.LineColor"), lineColorBtn_);

	rightLayout->addLayout(gridSettingsForm);
	rightLayout->addSpacing(10);

	// Widget action buttons
	setWidgetBtn_ = new QPushButton(LG_TEXT("EditDialog.SetWidget"));
	editWidgetBtn_ = new QPushButton(LG_TEXT("EditDialog.EditWidget"));
	mergeBtn_ = new QPushButton(LG_TEXT("EditDialog.MergeWidgets"));
	resetBtn_ = new QPushButton(LG_TEXT("EditDialog.ResetWidgets"));

	rightLayout->addWidget(setWidgetBtn_);
	rightLayout->addWidget(editWidgetBtn_);
	rightLayout->addSpacing(10);
	rightLayout->addWidget(mergeBtn_);
	rightLayout->addWidget(resetBtn_);
	rightLayout->addStretch();

	paneLayout->addLayout(rightLayout, 1);
	mainLayout->addLayout(paneLayout, 1);

	// Dialog buttons
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &MultiviewEditDialog::onConfirm);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	mainLayout->addWidget(buttons);

	gridEditor_->setGrid(config_.gridRows, config_.gridCols, config_.cells);

	// Connections
	connect(setWidgetBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onSetWidget);
	connect(editWidgetBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onEditWidget);
	connect(mergeBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onMergeWidgets);
	connect(resetBtn_, &QPushButton::clicked, this, &MultiviewEditDialog::onResetWidgets);
	connect(templateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MultiviewEditDialog::onTemplateChanged);
	connect(gridEditor_, &GridEditorWidget::selectionChanged, this, &MultiviewEditDialog::onSelectionChanged);
	connect(rowsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MultiviewEditDialog::onGridSizeChanged);
	connect(colsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MultiviewEditDialog::onGridSizeChanged);

	onSelectionChanged();

	// Lock the dialog to its laid-out size so it cannot be resized
	layout()->setSizeConstraint(QLayout::SetFixedSize);
}

void MultiviewEditDialog::onSetWidget()
{
	int idx = gridEditor_->selectedCellIndex();
	WidgetConfig current;
	if (idx >= 0)
		current = gridEditor_->cells()[idx].widget;

	CellConfigDialog dlg(current, this);
	if (dlg.exec() == QDialog::Accepted) {
		gridEditor_->setWidgetForSelected(dlg.result());
	}
}

void MultiviewEditDialog::onEditWidget()
{
	onSetWidget(); // Same behavior - opens the config dialog for the selected cell
}

void MultiviewEditDialog::onMergeWidgets()
{
	if (!gridEditor_->canMergeSelected()) {
		QMessageBox::warning(this, LG_TEXT("EditDialog.CannotMerge"), LG_TEXT("EditDialog.CannotMergeMsg"));
		return;
	}
	gridEditor_->mergeSelected();
}

void MultiviewEditDialog::onResetWidgets()
{
	gridEditor_->resetSelected();
}

void MultiviewEditDialog::onTemplateChanged(int index)
{
	if (index <= 0)
		return; // "(Current)" selected

	QString templateName = templateCombo_->currentText();
	if (!GetConfigManager()->hasTemplate(templateName))
		return;

	auto reply = QMessageBox::question(this, LG_TEXT("EditDialog.ApplyTemplate"),
					   LG_TEXT("EditDialog.ApplyTemplateMsg"), QMessageBox::Yes | QMessageBox::No);
	if (reply == QMessageBox::Yes) {
		loadTemplate(GetConfigManager()->getTemplate(templateName));
	}

	// Reset combo to "(Current)"
	templateCombo_->blockSignals(true);
	templateCombo_->setCurrentIndex(0);
	templateCombo_->blockSignals(false);
}

void MultiviewEditDialog::onSelectionChanged()
{
	int cellIdx = gridEditor_->selectedCellIndex();
	bool hasSingleCell = cellIdx >= 0;

	setWidgetBtn_->setEnabled(hasSingleCell);
	editWidgetBtn_->setEnabled(hasSingleCell && cellIdx < gridEditor_->cells().size() &&
				   gridEditor_->cells()[cellIdx].widget.type != WidgetType::None);
	mergeBtn_->setEnabled(gridEditor_->canMergeSelected());
	resetBtn_->setEnabled(gridEditor_->canResetSelected());
}

void MultiviewEditDialog::onConfirm()
{
	QString name = nameEdit_->text().trimmed();
	if (name.isEmpty()) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("EditDialog.EnterName"));
		return;
	}

	// The id identifies this multiview (empty while creating one), so keeping
	// its own name, or renaming it, is never mistaken for a clash
	ConfigManager *cm = GetConfigManager();
	if (cm->isNameTaken(name, config_.id)) {
		QMessageBox::warning(this, LG_TEXT("Common.Error"), LG_TEXT("ManageDialog.MultiviewExists"));
		return;
	}

	if (!isNew_ && !cm->hasMultiview(config_.id)) {
		// Deleted while this dialog was open
		reject();
		return;
	}

	config_ = result();

	if (isNew_) {
		// Auto-open the new multiview window
		QString id = cm->addMultiview(config_);
		MultiviewWindow::openOrFocus(id);
	} else {
		// Updates the name in place; open windows reload from the signal
		cm->updateLayout(config_);
	}

	accept();
}

void MultiviewEditDialog::onGridSizeChanged()
{
	int newRows = rowsSpin_->value();
	int newCols = colsSpin_->value();
	int oldRows = gridEditor_->gridRows();
	int oldCols = gridEditor_->gridCols();

	if (newRows == oldRows && newCols == oldCols)
		return;

	// Keep cells that still fit, discard ones outside the new bounds, and
	// fill uncovered positions with empty cells
	QVector<CellConfig> newCells = GridModel::Resize(gridEditor_->cells(), newRows, newCols);
	gridEditor_->setGrid(newRows, newCols, newCells);
}

void MultiviewEditDialog::loadTemplate(const TemplateConfig &tmpl)
{
	rowsSpin_->blockSignals(true);
	colsSpin_->blockSignals(true);
	rowsSpin_->setValue(tmpl.gridRows);
	colsSpin_->setValue(tmpl.gridCols);
	rowsSpin_->blockSignals(false);
	colsSpin_->blockSignals(false);

	// Auto-fill is handled by the default template itself (in ConfigManager::defaultTemplate).
	// User-created templates apply their cells as-is — placeholders stay as placeholders.
	gridEditor_->setGrid(tmpl.gridRows, tmpl.gridCols, tmpl.cells);
}

MultiviewConfig MultiviewEditDialog::result() const
{
	MultiviewConfig mv = config_;
	mv.name = nameEdit_->text().trimmed();
	mv.gridRows = gridEditor_->gridRows();
	mv.gridCols = gridEditor_->gridCols();
	mv.gridBorderWidth = borderWidthSpin_->value();
	mv.gridLineColor = gridLineColor_;
	mv.cells = gridEditor_->cells();
	return mv;
}
