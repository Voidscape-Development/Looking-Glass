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
#include "multiview-manage-dialog.hpp"
#include "multiview-window.hpp"
#include "widget-type-style.hpp"
#include "../plugin.hpp"
#include "../core/config-manager.hpp"
#include "../core/grid-model.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {

constexpr QSize kThumbSize(144, 81);

// Small picture of a template's grid, filled with the widget type colors
QPixmap TemplateThumbnail(const TemplateConfig &tmpl, const QSize &size, qreal ratio)
{
	QPixmap pixmap(size * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(QColor(0x55, 0x5b, 0x6e));

	QPainter painter(&pixmap);
	int rows = qMax(1, tmpl.gridRows);
	int cols = qMax(1, tmpl.gridCols);

	// Empty slots first, then the template's cells over them
	auto slot = [&](int row, int col, int rowSpan, int colSpan) {
		int x0 = qRound((double)col * size.width() / cols);
		int x1 = qRound((double)(col + colSpan) * size.width() / cols);
		int y0 = qRound((double)row * size.height() / rows);
		int y1 = qRound((double)(row + rowSpan) * size.height() / rows);
		return QRect(x0, y0, x1 - x0, y1 - y0).adjusted(1, 1, -1, -1);
	};
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++)
			painter.fillRect(slot(r, c, 1, 1), QColor(0x2f, 0x33, 0x41));
	}
	for (const CellConfig &cell : tmpl.cells) {
		QColor fill = cell.widget.type == WidgetType::None ? QColor(0x2f, 0x33, 0x41)
								   : WidgetTypeColor(cell.widget.type).darker(170);
		painter.fillRect(slot(cell.row, cell.col, cell.rowSpan, cell.colSpan), fill);
	}
	painter.end();
	return pixmap;
}

QString TemplateSummary(const TemplateConfig &tmpl)
{
	int filled = 0;
	for (const CellConfig &cell : tmpl.cells) {
		if (cell.widget.type != WidgetType::None)
			filled++;
	}
	return QString(LG_TEXT("EditDialog.TemplateSummary")).arg(tmpl.gridRows).arg(tmpl.gridCols).arg(filled);
}

// One card in the template picker: thumbnail, name and a one-line summary.
// The labels pass mouse events through so the whole card is one button.
QToolButton *MakeTemplateCard(const TemplateConfig &tmpl, qreal ratio)
{
	auto *card = new QToolButton();
	card->setCursor(Qt::PointingHandCursor);
	card->setToolTip(tmpl.name);

	auto *v = new QVBoxLayout(card);
	v->setContentsMargins(6, 6, 6, 6);
	v->setSpacing(3);

	auto *thumb = new QLabel();
	thumb->setPixmap(TemplateThumbnail(tmpl, kThumbSize, ratio));
	thumb->setFixedSize(kThumbSize);
	v->addWidget(thumb);

	auto *title = new QLabel();
	title->setText(title->fontMetrics().elidedText(tmpl.name, Qt::ElideRight, kThumbSize.width()));
	v->addWidget(title);

	auto *summary = new QLabel();
	QFont sf = summary->font();
	if (sf.pointSizeF() > 0)
		sf.setPointSizeF(sf.pointSizeF() * 0.85);
	summary->setFont(sf);
	summary->setForegroundRole(QPalette::PlaceholderText);
	summary->setText(summary->fontMetrics().elidedText(TemplateSummary(tmpl), Qt::ElideRight, kThumbSize.width()));
	v->addWidget(summary);

	for (QLabel *label : {thumb, title, summary})
		label->setAttribute(Qt::WA_TransparentForMouseEvents);

	card->setFixedSize(v->sizeHint());
	return card;
}

QFrame *MakeRule()
{
	auto *line = new QFrame();
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
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

	// Header, editor and footer sit edge to edge, divided by rules, so the
	// canvas gets as much of the window as possible
	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// Header: name, inline error and template actions on one line
	auto *header = new QHBoxLayout();
	header->setContentsMargins(10, 6, 10, 6);
	header->setSpacing(8);

	// New multiviews get a unique suggested name, selected so typing replaces it
	if (isNew_ && config_.name.trimmed().isEmpty()) {
		QString base = LG_TEXT("EditDialog.DefaultName");
		QString candidate = base;
		for (int n = 2; GetConfigManager()->isNameTaken(candidate, QString()); n++)
			candidate = QString("%1 %2").arg(base).arg(n);
		config_.name = candidate;
	}
	nameEdit_ = new QLineEdit(config_.name);
	nameEdit_->setMinimumWidth(200);
	nameEdit_->setMaximumWidth(420);
	nameEdit_->setPlaceholderText(LG_TEXT("EditDialog.NamePlaceholder"));

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

	auto *templateButton = new QToolButton();
	templateButton->setText(LG_TEXT("EditDialog.StartFromTemplate"));
	templateButton->setToolTip(LG_TEXT("EditDialog.StartFromTemplateTip"));
	templateButton->setPopupMode(QToolButton::InstantPopup);
	templateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	templateMenu_ = new QMenu(templateButton);
	templateButton->setMenu(templateMenu_);
	connect(templateMenu_, &QMenu::aboutToShow, this, &MultiviewEditDialog::populateTemplateMenu);
	header->addWidget(templateButton);

	auto *saveTemplateButton = new QPushButton(LG_TEXT("EditDialog.SaveAsTemplate"));
	saveTemplateButton->setFlat(true);
	saveTemplateButton->setAutoDefault(false);
	saveTemplateButton->setToolTip(LG_TEXT("EditDialog.SaveAsTemplateTip"));
	connect(saveTemplateButton, &QPushButton::clicked, this, &MultiviewEditDialog::onSaveAsTemplate);
	header->addWidget(saveTemplateButton);

	mainLayout->addLayout(header);
	mainLayout->addWidget(MakeRule());

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
	mainLayout->addWidget(MakeRule());

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
	// clear() deletes the previous picker along with its action
	templateMenu_->clear();
	ConfigManager *cm = GetConfigManager();

	QString builtIn = cm->defaultTemplate().name;
	QStringList names = cm->templateNames();
	names.removeAll(builtIn);
	names.sort(Qt::CaseInsensitive);
	names.prepend(builtIn);

	// A grid of thumbnail cards, two per row, inside the drop-down
	auto *picker = new QWidget();
	auto *grid = new QGridLayout(picker);
	grid->setContentsMargins(8, 8, 8, 8);
	grid->setSpacing(6);

	const qreal ratio = devicePixelRatioF();
	constexpr int kColumns = 2;
	int i = 0;
	for (const QString &name : names) {
		QToolButton *card = MakeTemplateCard(cm->getTemplate(name), ratio);
		connect(card, &QToolButton::clicked, this, [this, name]() {
			templateMenu_->close();
			applyTemplate(name);
		});
		grid->addWidget(card, i / kColumns, i % kColumns);
		i++;
	}

	auto *action = new QWidgetAction(templateMenu_);
	action->setDefaultWidget(picker);
	templateMenu_->addAction(action);
}

void MultiviewEditDialog::onSaveAsTemplate()
{
	MultiviewConfig mv = result();
	if (mv.name.isEmpty())
		mv.name = config_.name;
	QString name = PromptSaveAsTemplate(this, mv);
	if (!name.isEmpty())
		layoutEditor_->showMessage(QString(LG_TEXT("EditDialog.TemplateSaved")).arg(name));
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
