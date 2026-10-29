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
#include <QColor>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QSpinBox>

#include "../core/multiview-config.hpp"

/**
 * Dialog for configuring an individual cell's widget type, scene/source
 * selection, and label properties (text, font, alignment, background color, visibility).
 */
class CellConfigDialog : public QDialog {
	Q_OBJECT

public:
	explicit CellConfigDialog(const WidgetConfig &current, QWidget *parent = nullptr);
	WidgetConfig result() const;

private slots:
	void onTypeChanged(int index);
	void onChooseFont();
	void onChooseBgColor();

private:
	void populateSubtypes();
	void updateTypeVisibility();
	void updateFontPreview();
	void updateBgColorPreview();

	// Widget type controls (left pane)
	QComboBox *typeCombo_;
	QComboBox *subtypeCombo_;
	QLabel *subtypeLabel_;
	QCheckBox *safeRegionCheck_;
	QCheckBox *showStatusCheck_;

	// Label controls (right pane)
	QCheckBox *labelVisibleCheck_;
	QLineEdit *labelTextEdit_;
	QPushButton *fontBtn_;
	QLabel *fontPreviewLabel_;
	QSpinBox *labelSizeSpin_;
	QPushButton *bgColorBtn_;
	QLabel *bgColorPreviewLabel_;
	QComboBox *labelHAlignCombo_;
	QComboBox *labelVAlignCombo_;

	QFont selectedFont_;
	QColor selectedBgColor_;
	WidgetConfig config_;
};
