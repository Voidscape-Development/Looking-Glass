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

#include "cell-config-dialog.hpp"
#include "../plugin.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QFontDialog>
#include <QFontDatabase>
#include <QColorDialog>
#include <QPair>

#include <algorithm>

CellConfigDialog::CellConfigDialog(const WidgetConfig &current, QWidget *parent) : QDialog(parent), config_(current)
{
	setWindowTitle(LG_TEXT("CellDialog.Title"));
	setMinimumSize(600, 400);

	// Initialize font from config or use default
	if (!config_.labelFont.isEmpty()) {
		selectedFont_.fromString(config_.labelFont);
	} else {
		selectedFont_ = QFont();
		selectedFont_.setPointSize(36);
	}

	selectedBgColor_ = config_.labelBgColor;

	auto *mainLayout = new QVBoxLayout(this);

	auto *contentLayout = new QHBoxLayout();

	// Left pane: widget type selection
	auto *leftPane = new QGroupBox(LG_TEXT("CellDialog.WidgetType"));
	auto *leftLayout = new QVBoxLayout(leftPane);

	auto *typeLayout = new QFormLayout();

	typeCombo_ = new QComboBox();
	typeCombo_->addItem(LG_TEXT("CellDialog.TypeNone"), (int)WidgetType::None);
	typeCombo_->addItem(LG_TEXT("CellDialog.TypePreview"), (int)WidgetType::Preview);
	typeCombo_->addItem(LG_TEXT("CellDialog.TypeProgram"), (int)WidgetType::Program);
	typeCombo_->addItem(LG_TEXT("CellDialog.TypeCanvas"), (int)WidgetType::Canvas);
	typeCombo_->addItem(LG_TEXT("CellDialog.TypeScene"), (int)WidgetType::Scene);
	typeCombo_->addItem(LG_TEXT("CellDialog.TypeSource"), (int)WidgetType::Source);
	typeCombo_->addItem(LG_TEXT("CellDialog.TypePlaceholder"), (int)WidgetType::Placeholder);
	typeLayout->addRow(LG_TEXT("CellDialog.Type"), typeCombo_);

	subtypeCombo_ = new QComboBox();
	subtypeLabel_ = new QLabel(LG_TEXT("CellDialog.Selection"));
	typeLayout->addRow(subtypeLabel_, subtypeCombo_);

	safeRegionCheck_ = new QCheckBox(LG_TEXT("CellDialog.SafeRegion"));
	safeRegionCheck_->setChecked(config_.safeRegion);
	safeRegionCheck_->setToolTip(LG_TEXT("CellDialog.SafeRegionTooltip"));
	typeLayout->addRow(safeRegionCheck_);

	showStatusCheck_ = new QCheckBox(LG_TEXT("CellDialog.ShowStatus"));
	showStatusCheck_->setChecked(config_.showStatus);
	showStatusCheck_->setToolTip(LG_TEXT("CellDialog.ShowStatusTooltip"));
	typeLayout->addRow(showStatusCheck_);

	leftLayout->addLayout(typeLayout);
	leftLayout->addStretch();

	// Right pane: label settings
	auto *rightPane = new QGroupBox(LG_TEXT("CellDialog.LabelSettings"));
	auto *rightLayout = new QVBoxLayout(rightPane);

	auto *labelLayout = new QFormLayout();

	labelVisibleCheck_ = new QCheckBox(LG_TEXT("CellDialog.ShowLabel"));
	labelVisibleCheck_->setChecked(config_.labelVisible);
	labelLayout->addRow(labelVisibleCheck_);

	labelTextEdit_ = new QLineEdit(config_.labelText);
	labelTextEdit_->setPlaceholderText(LG_TEXT("CellDialog.CustomTextPlaceholder"));
	labelLayout->addRow(LG_TEXT("CellDialog.CustomText"), labelTextEdit_);

	// Font selection with preview
	auto *fontLayout = new QHBoxLayout();
	fontBtn_ = new QPushButton(LG_TEXT("CellDialog.FontChoose"));
	fontPreviewLabel_ = new QLabel();
	fontPreviewLabel_->setFrameStyle(QFrame::Box | QFrame::Plain);
	fontPreviewLabel_->setMinimumWidth(150);
	fontLayout->addWidget(fontPreviewLabel_, 1);
	fontLayout->addWidget(fontBtn_);
	labelLayout->addRow(LG_TEXT("CellDialog.Font"), fontLayout);

	// Background color selection with preview
	auto *bgColorLayout = new QHBoxLayout();
	bgColorBtn_ = new QPushButton(LG_TEXT("CellDialog.BgColorChoose"));
	bgColorPreviewLabel_ = new QLabel();
	bgColorPreviewLabel_->setFrameStyle(QFrame::Box | QFrame::Plain);
	bgColorPreviewLabel_->setMinimumWidth(150);
	bgColorPreviewLabel_->setMinimumHeight(24);
	bgColorLayout->addWidget(bgColorPreviewLabel_, 1);
	bgColorLayout->addWidget(bgColorBtn_);
	labelLayout->addRow(LG_TEXT("CellDialog.Background"), bgColorLayout);

	// Alignment
	labelVAlignCombo_ = new QComboBox();
	labelVAlignCombo_->addItem(LG_TEXT("CellDialog.AlignTop"), (int)Qt::AlignTop);
	labelVAlignCombo_->addItem(LG_TEXT("CellDialog.AlignMiddle"), (int)Qt::AlignVCenter);
	labelVAlignCombo_->addItem(LG_TEXT("CellDialog.AlignBottom"), (int)Qt::AlignBottom);
	labelLayout->addRow(LG_TEXT("CellDialog.AlignVertical"), labelVAlignCombo_);

	labelHAlignCombo_ = new QComboBox();
	labelHAlignCombo_->addItem(LG_TEXT("CellDialog.AlignLeft"), (int)Qt::AlignLeft);
	labelHAlignCombo_->addItem(LG_TEXT("CellDialog.AlignCenter"), (int)Qt::AlignHCenter);
	labelHAlignCombo_->addItem(LG_TEXT("CellDialog.AlignRight"), (int)Qt::AlignRight);
	labelLayout->addRow(LG_TEXT("CellDialog.AlignHorizontal"), labelHAlignCombo_);

	rightLayout->addLayout(labelLayout);
	rightLayout->addStretch();

	// Add panes to content layout
	contentLayout->addWidget(leftPane, 1);
	contentLayout->addWidget(rightPane, 1);
	mainLayout->addLayout(contentLayout, 1);

	// Buttons
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	mainLayout->addWidget(buttons);

	// Set current values
	int typeIdx = typeCombo_->findData((int)config_.type);
	if (typeIdx >= 0)
		typeCombo_->setCurrentIndex(typeIdx);

	int hIdx = labelHAlignCombo_->findData(
		(int)(config_.labelHAlign & (Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight)));
	if (hIdx >= 0)
		labelHAlignCombo_->setCurrentIndex(hIdx);

	int vIdx = labelVAlignCombo_->findData(
		(int)(config_.labelVAlign & (Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom)));
	if (vIdx >= 0)
		labelVAlignCombo_->setCurrentIndex(vIdx);

	// Connect signals
	connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&CellConfigDialog::onTypeChanged);
	connect(fontBtn_, &QPushButton::clicked, this, &CellConfigDialog::onChooseFont);
	connect(bgColorBtn_, &QPushButton::clicked, this, &CellConfigDialog::onChooseBgColor);

	populateSubtypes();
	updateTypeVisibility();
	updateFontPreview();
	updateBgColorPreview();
}

void CellConfigDialog::onTypeChanged(int)
{
	populateSubtypes();
	updateTypeVisibility();
}

void CellConfigDialog::updateTypeVisibility()
{
	WidgetType type = (WidgetType)typeCombo_->currentData().toInt();

	// Selection dropdown only needed for types that have a subtype list
	bool needsSelection = (type == WidgetType::Scene || type == WidgetType::Source || type == WidgetType::Canvas);
	subtypeLabel_->setVisible(needsSelection);
	subtypeCombo_->setVisible(needsSelection);

	// Status border only applies to types that can be in preview/program
	bool canShowStatus = type == WidgetType::Scene;
	showStatusCheck_->setVisible(canShowStatus);
}

void CellConfigDialog::onChooseFont()
{
	bool ok;
	QFont font = QFontDialog::getFont(&ok, selectedFont_, this, LG_TEXT("CellDialog.ChooseLabelFont"));
	if (ok) {
		selectedFont_ = font;
		updateFontPreview();
	}
}

void CellConfigDialog::onChooseBgColor()
{
	QColor color = QColorDialog::getColor(selectedBgColor_, this, LG_TEXT("CellDialog.ChooseBgColor"),
					      QColorDialog::ShowAlphaChannel);
	if (color.isValid()) {
		selectedBgColor_ = color;
		updateBgColorPreview();
	}
}

void CellConfigDialog::updateFontPreview()
{
	QString style = QFontDatabase::styleString(selectedFont_);

	QString description =
		QString("%1 %2 %3pt").arg(selectedFont_.family()).arg(style).arg(selectedFont_.pointSize());

	fontPreviewLabel_->setText(description);
	fontPreviewLabel_->setFont(selectedFont_);
}

void CellConfigDialog::updateBgColorPreview()
{
	int alpha = selectedBgColor_.alpha();
	QString description =
		QString("%1 (%2% opacity)").arg(selectedBgColor_.name(QColor::HexRgb)).arg(qRound(alpha / 255.0 * 100));

	bgColorPreviewLabel_->setText(description);

	// Show a checkerboard pattern behind the color swatch to visualize transparency
	QString bgStyle = QString("QLabel { background-color: %1; color: %2; padding: 2px; }")
				  .arg(selectedBgColor_.name(QColor::HexArgb))
				  .arg(alpha > 127 ? "white" : "black");
	bgColorPreviewLabel_->setStyleSheet(bgStyle);
}

void CellConfigDialog::populateSubtypes()
{
	subtypeCombo_->clear();

	WidgetType type = (WidgetType)typeCombo_->currentData().toInt();

	if (type == WidgetType::Scene || type == WidgetType::Source) {
		// Each entry carries the source's UUID so the cell keeps pointing at
		// it through renames
		QVector<QPair<QString, QString>> entries; // name, uuid
		auto add = [](void *param, obs_source_t *source) -> bool {
			auto *list = static_cast<QVector<QPair<QString, QString>> *>(param);
			const char *name = obs_source_get_name(source);
			if (name && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				list->append({QString::fromUtf8(name), QString::fromUtf8(obs_source_get_uuid(source))});
			return true;
		};
		if (type == WidgetType::Scene) {
			struct obs_frontend_source_list scenes = {};
			obs_frontend_get_scenes(&scenes);
			for (size_t i = 0; i < scenes.sources.num; i++)
				add(&entries, scenes.sources.array[i]);
			obs_frontend_source_list_free(&scenes);
		} else {
			obs_enum_sources(add, &entries);
		}

		std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
			return QString::compare(a.first, b.first, Qt::CaseInsensitive) < 0;
		});
		for (const auto &entry : entries)
			subtypeCombo_->addItem(entry.first, entry.second);
		subtypeCombo_->setEnabled(true);

		// Select the current one, by UUID first and then by name
		int idx = config_.sourceUuid.isEmpty() ? -1 : subtypeCombo_->findData(config_.sourceUuid);
		if (idx < 0)
			idx = subtypeCombo_->findText(type == WidgetType::Scene ? config_.sceneName
										: config_.sourceName);
		if (idx >= 0)
			subtypeCombo_->setCurrentIndex(idx);

	} else if (type == WidgetType::Canvas) {
		subtypeCombo_->addItem(LG_TEXT("CellDialog.MainCanvas"), QString(""));

		QStringList canvasNames;
		auto enumCanvasCb = [](void *param, obs_canvas_t *canvas) -> bool {
			auto *list = (QStringList *)param;
			const char *name = obs_canvas_get_name(canvas);
			if (name && strlen(name) > 0)
				list->append(QString::fromUtf8(name));
			return true;
		};
		obs_enum_canvases(enumCanvasCb, &canvasNames);
		canvasNames.sort(Qt::CaseInsensitive);
		for (const QString &n : canvasNames)
			subtypeCombo_->addItem(n, n);
		subtypeCombo_->setEnabled(true);

		if (config_.canvasName.isEmpty()) {
			subtypeCombo_->setCurrentIndex(0);
		} else {
			int idx = subtypeCombo_->findData(config_.canvasName);
			if (idx >= 0)
				subtypeCombo_->setCurrentIndex(idx);
		}

	} else {
		subtypeCombo_->setEnabled(false);
	}
}

WidgetConfig CellConfigDialog::result() const
{
	WidgetConfig w;
	w.type = (WidgetType)typeCombo_->currentData().toInt();
	w.labelVisible = labelVisibleCheck_->isChecked();
	w.labelHAlign = (Qt::Alignment)labelHAlignCombo_->currentData().toInt();
	w.labelVAlign = (Qt::Alignment)labelVAlignCombo_->currentData().toInt();
	w.labelText = labelTextEdit_->text();
	w.labelFont = selectedFont_.toString();
	w.labelBgColor = selectedBgColor_;
	w.safeRegion = safeRegionCheck_->isChecked();
	w.showStatus = showStatusCheck_->isChecked();

	if (w.type == WidgetType::Scene) {
		w.sceneName = subtypeCombo_->currentText();
		w.sourceUuid = subtypeCombo_->currentData().toString();
	} else if (w.type == WidgetType::Source) {
		w.sourceName = subtypeCombo_->currentText();
		w.sourceUuid = subtypeCombo_->currentData().toString();
	} else if (w.type == WidgetType::Canvas) {
		w.canvasName = subtypeCombo_->currentData().toString();
	}

	return w;
}
