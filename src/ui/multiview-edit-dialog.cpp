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
#include "layout-editor-widget.hpp"
#include "multiview-window.hpp"
#include "widget-type-style.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../core/grid-model.hpp"

#include <QAction>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Small picture of a template's grid, filled with the widget type colors
QIcon TemplateThumbnail(const TemplateConfig &tmpl)
{
	const QSize size(64, 36);
	const qreal ratio = 2.0;
	QPixmap pixmap(size * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(QColor(0x1c, 0x1d, 0x22));

	QPainter painter(&pixmap);
	int rows = qMax(1, tmpl.gridRows);
	int cols = qMax(1, tmpl.gridCols);
	for (const CellConfig &cell : tmpl.cells) {
		int x0 = qRound((double)cell.col * size.width() / cols);
		int x1 = qRound((double)(cell.col + cell.colSpan) * size.width() / cols);
		int y0 = qRound((double)cell.row * size.height() / rows);
		int y1 = qRound((double)(cell.row + cell.rowSpan) * size.height() / rows);
		QColor fill = cell.widget.type == WidgetType::None ? QColor(0x3a, 0x3c, 0x46)
								   : WidgetTypeColor(cell.widget.type);
		painter.fillRect(QRect(x0, y0, x1 - x0, y1 - y0).adjusted(1, 1, 0, 0), fill);
	}
	painter.end();
	return QIcon(pixmap);
}

} // namespace

MultiviewEditDialog::MultiviewEditDialog(const MultiviewConfig &config, bool isNew, QWidget *parent)
	: QDialog(parent),
	  config_(config),
	  isNew_(isNew)
{
	setWindowTitle(isNew ? LG_TEXT("EditDialog.CreateTitle")
			     : QString(LG_TEXT("EditDialog.EditTitle")).arg(config.name));
	setSizeGripEnabled(true);
	setMinimumSize(900, 560);
	resize(1180, 720);

	// New multiviews start from the built-in default template
	if (config_.cells.isEmpty()) {
		TemplateConfig def = GetConfigManager()->defaultTemplate();
		config_.gridRows = def.gridRows;
		config_.gridCols = def.gridCols;
		config_.cells = def.cells;
	}

	auto *mainLayout = new QVBoxLayout(this);

	// Header: name and template menu
	auto *header = new QHBoxLayout();
	auto *nameColumn = new QVBoxLayout();
	nameColumn->setSpacing(2);
	auto *caption = new QLabel(LG_TEXT("EditDialog.Name"));
	caption->setForegroundRole(QPalette::PlaceholderText);
	nameColumn->addWidget(caption);

	// New multiviews get a unique suggested name, selected so typing replaces it
	if (isNew_ && config_.name.trimmed().isEmpty()) {
		QString base = LG_TEXT("EditDialog.DefaultName");
		QString candidate = base;
		for (int n = 2; GetConfigManager()->isNameTaken(candidate, QString()); n++)
			candidate = QString("%1 %2").arg(base).arg(n);
		config_.name = candidate;
	}
	nameEdit_ = new QLineEdit(config_.name);
	nameEdit_->setMinimumWidth(320);
	nameEdit_->setPlaceholderText(LG_TEXT("EditDialog.NamePlaceholder"));
	nameColumn->addWidget(nameEdit_);

	errorLabel_ = new QLabel();
	QPalette errorPalette = errorLabel_->palette();
	errorPalette.setColor(QPalette::WindowText, WidgetTypeColor(WidgetType::Program));
	errorLabel_->setPalette(errorPalette);
	errorLabel_->setMinimumHeight(errorLabel_->fontMetrics().height());
	nameColumn->addWidget(errorLabel_);
	header->addLayout(nameColumn);
	header->addStretch(1);

	auto *templateButton = new QToolButton();
	templateButton->setText(LG_TEXT("EditDialog.StartFromTemplate"));
	templateButton->setPopupMode(QToolButton::InstantPopup);
	templateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	templateMenu_ = new QMenu(templateButton);
	templateButton->setMenu(templateMenu_);
	connect(templateMenu_, &QMenu::aboutToShow, this, &MultiviewEditDialog::populateTemplateMenu);
	header->addWidget(templateButton, 0, Qt::AlignTop);
	mainLayout->addLayout(header);

	// Body
	layoutEditor_ = new LayoutEditorWidget();
	LayoutState state;
	state.rows = config_.gridRows;
	state.cols = config_.gridCols;
	state.cells = config_.cells;
	state.lineWidth = config_.gridBorderWidth;
	state.lineColor = config_.gridLineColor;
	layoutEditor_->setLayoutState(state);
	mainLayout->addWidget(layoutEditor_, 1);

	// Footer
	auto *footer = new QHBoxLayout();
	infoLabel_ = new QLabel();
	infoLabel_->setForegroundRole(QPalette::PlaceholderText);
	footer->addWidget(infoLabel_, 1);

	auto *cancelButton = new QPushButton(LG_TEXT("EditDialog.Cancel"));
	cancelButton->setAutoDefault(false);
	footer->addWidget(cancelButton);
	primaryButton_ =
		new QPushButton(isNew ? LG_TEXT("EditDialog.CreateAndOpen") : LG_TEXT("EditDialog.SaveChanges"));
	// Enter must not save from arbitrary fields (the inspector has text boxes);
	// only the name field handles it, below
	primaryButton_->setAutoDefault(false);
	primaryButton_->setDefault(false);
	footer->addWidget(primaryButton_);
	mainLayout->addLayout(footer);

	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
	connect(primaryButton_, &QPushButton::clicked, this, &MultiviewEditDialog::onConfirm);
	connect(nameEdit_, &QLineEdit::textChanged, this, &MultiviewEditDialog::onNameChanged);
	connect(nameEdit_, &QLineEdit::returnPressed, this, [this]() {
		if (primaryButton_->isEnabled())
			onConfirm();
	});
	connect(layoutEditor_, &LayoutEditorWidget::modifiedChanged, this, &MultiviewEditDialog::updateInfo);

	onNameChanged();
	updateInfo();
	if (isNew_) {
		nameEdit_->setFocus();
		nameEdit_->selectAll();
	}
}

void MultiviewEditDialog::populateTemplateMenu()
{
	templateMenu_->clear();
	ConfigManager *cm = GetConfigManager();

	QString builtIn = cm->defaultTemplate().name;
	QStringList names = cm->templateNames();
	names.removeAll(builtIn);
	names.sort(Qt::CaseInsensitive);
	names.prepend(builtIn);

	for (const QString &name : names) {
		QAction *action = templateMenu_->addAction(TemplateThumbnail(cm->getTemplate(name)), name);
		connect(action, &QAction::triggered, this, [this, name]() { applyTemplate(name); });
	}
}

void MultiviewEditDialog::applyTemplate(const QString &name)
{
	ConfigManager *cm = GetConfigManager();
	if (!cm->hasTemplate(name))
		return;
	TemplateConfig tmpl = cm->getTemplate(name);

	// Templates carry no line style, so the current one stays
	LayoutState state = layoutEditor_->layoutState();
	state.rows = tmpl.gridRows;
	state.cols = tmpl.gridCols;
	state.cells = tmpl.cells;
	layoutEditor_->applyLayout(state, QString(LG_TEXT("EditDialog.Undo.Template")).arg(name),
				   QString(LG_TEXT("EditDialog.TemplateApplied")).arg(name));
}

void MultiviewEditDialog::onNameChanged()
{
	QString name = nameEdit_->text().trimmed();
	QString error;
	if (name.isEmpty())
		error = LG_TEXT("EditDialog.NameEmpty");
	else if (GetConfigManager()->isNameTaken(name, config_.id))
		error = QString(LG_TEXT("EditDialog.NameTaken")).arg(name);

	errorLabel_->setText(error);
	primaryButton_->setEnabled(error.isEmpty());

	if (!isNew_)
		setWindowTitle(QString(LG_TEXT("EditDialog.EditTitle")).arg(name.isEmpty() ? config_.name : name));
	updateInfo();
}

bool MultiviewEditDialog::hasUnsavedChanges() const
{
	if (layoutEditor_->isModified())
		return true;
	return !isNew_ && nameEdit_->text().trimmed() != config_.name;
}

void MultiviewEditDialog::updateInfo()
{
	if (isNew_)
		infoLabel_->clear();
	else
		infoLabel_->setText(hasUnsavedChanges() ? LG_TEXT("EditDialog.UnsavedChanges")
							: LG_TEXT("EditDialog.NoChanges"));
}

void MultiviewEditDialog::reject()
{
	if (!skipDiscardPrompt_ && hasUnsavedChanges()) {
		QMessageBox box(QMessageBox::Question, windowTitle(), LG_TEXT("EditDialog.DiscardMsg"),
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

void MultiviewEditDialog::onConfirm()
{
	QString name = nameEdit_->text().trimmed();
	if (name.isEmpty()) {
		onNameChanged();
		return;
	}

	// The id identifies this multiview (empty while creating one), so keeping
	// its own name, or renaming it, is never mistaken for a clash
	ConfigManager *cm = GetConfigManager();
	if (cm->isNameTaken(name, config_.id)) {
		onNameChanged();
		return;
	}

	if (!isNew_ && !cm->hasMultiview(config_.id)) {
		// Deleted while this dialog was open
		skipDiscardPrompt_ = true;
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

MultiviewConfig MultiviewEditDialog::result() const
{
	LayoutState state = layoutEditor_->layoutState();
	MultiviewConfig mv = config_;
	mv.name = nameEdit_->text().trimmed();
	mv.gridRows = state.rows;
	mv.gridCols = state.cols;
	mv.gridBorderWidth = state.lineWidth;
	mv.gridLineColor = state.lineColor;
	mv.cells = state.cells;
	return mv;
}
