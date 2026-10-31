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

#include "cell-inspector.hpp"
#include "widget-type-style.hpp"
#include "../core/content-ref.hpp"
#include "../plugin.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

namespace {

constexpr int kMarkerRole = Qt::UserRole + 1; // Marks the synthetic items at the top of the target list
constexpr int kNameRole = Qt::UserRole + 2;   // Scene, source or canvas name of a target item
constexpr int kMarkerMissing = 1;
constexpr int kMarkerPlaceholder = 2;

const Qt::Alignment kVAligns[3] = {Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom};
const Qt::Alignment kHAligns[3] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
const char *const kVNames[3] = {"Inspector.Top", "Inspector.Middle", "Inspector.Bottom"};
const char *const kHNames[3] = {"Inspector.Left", "Inspector.Center", "Inspector.Right"};

const WidgetType kTypeOrder[7] = {WidgetType::Program, WidgetType::Preview,     WidgetType::Scene, WidgetType::Source,
				  WidgetType::Canvas,  WidgetType::Placeholder, WidgetType::None};

QString T(const char *key)
{
	return QString::fromUtf8(LG_TEXT(key));
}

QLabel *makeHeading(const QString &text)
{
	auto *label = new QLabel(text.toUpper());
	QFont f = label->font();
	f.setBold(true);
	if (f.pointSizeF() > 0)
		f.setPointSizeF(f.pointSizeF() * 0.85);
	f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
	label->setFont(f);
	label->setContentsMargins(0, 6, 0, 0);
	return label;
}

QLabel *makeDim(const QString &text = QString())
{
	auto *label = new QLabel(text);
	label->setWordWrap(true);
	label->setForegroundRole(QPalette::PlaceholderText);
	return label;
}

QFrame *makeLine()
{
	auto *line = new QFrame();
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

void setSwatch(QPushButton *button, const QColor &color)
{
	if (button->property("lgColor").value<QColor>() == color && button->styleSheet().size())
		return;
	button->setProperty("lgColor", color);
	button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid palette(mid); "
					     "border-radius: 3px; }"
					     "QPushButton:hover { border-color: palette(highlight); }")
				      .arg(color.name(QColor::HexRgb)));
	button->setToolTip(color.name(QColor::HexRgb));
}

QPushButton *makeSwatch(const QColor &color, int w, int h)
{
	auto *button = new QPushButton();
	button->setFixedSize(w, h);
	button->setFocusPolicy(Qt::TabFocus);
	setSwatch(button, color);
	return button;
}

QIcon typeIcon(WidgetType type)
{
	QPixmap pm(14, 14);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(WidgetTypeColor(type));
	p.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
	p.end();
	return QIcon(pm);
}

QIcon positionIcon(int row, int col)
{
	QPixmap pm(16, 16);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setPen(Qt::NoPen);
	QColor c = QApplication::palette().color(QPalette::ButtonText);
	for (int r = 0; r < 3; r++) {
		for (int cc = 0; cc < 3; cc++) {
			bool on = r == row && cc == col;
			QColor dot = c;
			dot.setAlpha(on ? 255 : 70);
			p.setBrush(dot);
			int size = on ? 4 : 2;
			p.drawRect(1 + cc * 5 + (4 - size) / 2, 1 + r * 5 + (4 - size) / 2, size, size);
		}
	}
	p.end();
	return QIcon(pm);
}

int positionIndex(const WidgetConfig &w)
{
	int r = -1, c = -1;
	Qt::Alignment v = w.labelVAlign & (Qt::AlignTop | Qt::AlignVCenter | Qt::AlignBottom);
	Qt::Alignment h = w.labelHAlign & (Qt::AlignLeft | Qt::AlignHCenter | Qt::AlignRight);
	for (int i = 0; i < 3; i++) {
		if (v == kVAligns[i])
			r = i;
		if (h == kHAligns[i])
			c = i;
	}
	return (r < 0 || c < 0) ? -1 : r * 3 + c;
}

// Tri-state when the widgets disagree. Only tri-state while mixed, so a click
// on a mixed box goes straight to "checked" for all and a plain one just toggles.
void setTri(QCheckBox *cb, int on, int total)
{
	QSignalBlocker block(cb);
	if (on == 0 || on == total) {
		cb->setTristate(false);
		cb->setChecked(on == total);
	} else {
		cb->setTristate(true);
		cb->setCheckState(Qt::PartiallyChecked);
	}
}

void setTextIfChanged(QLabel *label, const QString &text)
{
	if (label->text() != text)
		label->setText(text);
}

// What a cell shows as its label when no custom text is set
QString autoLabel(const WidgetConfig &w)
{
	QString target = w.targetName();
	if (!target.isEmpty())
		return target;
	if (w.type == WidgetType::Canvas)
		return w.canvasName.isEmpty() ? T("CellDialog.MainCanvas") : w.canvasName;
	return WidgetTypeDisplayName(w.type);
}

} // namespace

CellInspector::CellInspector(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(260);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	outer->addWidget(scroll);

	auto *inner = new QWidget();
	auto *innerLayout = new QVBoxLayout(inner);
	innerLayout->setContentsMargins(14, 12, 14, 14);
	stack_ = new QStackedWidget();
	innerLayout->addWidget(stack_);
	innerLayout->addStretch(1);
	scroll->setWidget(inner);

	buildLayoutPage();
	stack_->addWidget(buildSinglePage());
	stack_->addWidget(buildMultiPage());
	setPage(PageLayout);
}

void CellInspector::setPage(Page page)
{
	// Hidden pages must not contribute to the stack's size hint
	for (int i = 0; i < stack_->count(); i++) {
		QWidget *w = stack_->widget(i);
		w->setSizePolicy(QSizePolicy::Preferred, i == page ? QSizePolicy::Preferred : QSizePolicy::Ignored);
	}
	if (stack_->currentIndex() != page)
		stack_->setCurrentIndex(page);
}

QWidget *CellInspector::makeSwatchRow(QPushButton **main, const QVector<QColor> &presets,
				      std::function<void(const QColor &)> onPreset, std::function<void()> onPick)
{
	auto *row = new QWidget();
	auto *h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(4);

	*main = makeSwatch(QColor(Qt::black), 34, 22);
	connect(*main, &QPushButton::clicked, this, [onPick]() { onPick(); });
	h->addWidget(*main);

	for (const QColor &c : presets) {
		QPushButton *preset = makeSwatch(c, 18, 18);
		connect(preset, &QPushButton::clicked, this, [onPreset, c]() { onPreset(c); });
		h->addWidget(preset);
	}
	h->addStretch(1);
	return row;
}

// ---------------------------------------------------------------- layout page

void CellInspector::buildLayoutPage()
{
	auto *page = new QWidget();
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(8);

	auto *title = new QLabel(T("Inspector.Layout"));
	QFont tf = title->font();
	tf.setBold(true);
	if (tf.pointSizeF() > 0)
		tf.setPointSizeF(tf.pointSizeF() * 1.15);
	title->setFont(tf);
	v->addWidget(title);

	lineGroup_ = new QWidget();
	auto *lg = new QVBoxLayout(lineGroup_);
	lg->setContentsMargins(0, 0, 0, 0);
	lg->setSpacing(8);
	lg->addWidget(makeLine());
	lg->addWidget(makeHeading(T("Inspector.GridLines")));

	auto *form = new QFormLayout();
	form->setContentsMargins(0, 0, 0, 0);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	auto *widthRow = new QHBoxLayout();
	lineWidthSlider_ = new QSlider(Qt::Horizontal);
	lineWidthSlider_->setRange(1, 10);
	lineWidthValue_ = new QLabel();
	lineWidthValue_->setMinimumWidth(40);
	lineWidthValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	widthRow->addWidget(lineWidthSlider_, 1);
	widthRow->addWidget(lineWidthValue_);
	form->addRow(T("Inspector.GridLineWidth"), widthRow);
	connect(lineWidthSlider_, &QSlider::valueChanged, this, [this](int value) { setLineWidth(value); });

	QWidget *colorRow = makeSwatchRow(
		&lineColorBtn_,
		{QColor(Qt::white), QColor(Qt::black), QColor(0x2b, 0x2f, 0x3b), QColor(0x4f, 0x74, 0xff),
		 QColor(0xe5, 0x48, 0x4d)},
		[this](const QColor &c) { setLineColor(c); }, [this]() { pickLineColor(); });
	form->addRow(T("Inspector.GridLineColor"), colorRow);
	lg->addLayout(form);
	v->addWidget(lineGroup_);

	v->addWidget(makeLine());
	v->addWidget(makeHeading(T("Inspector.Shortcuts")));
	static const char *const kHelp[] = {"Inspector.HelpClick", "Inspector.HelpDrag",  "Inspector.HelpCtrl",
					    "Inspector.HelpShift", "Inspector.HelpMerge", "Inspector.HelpDelete",
					    "Inspector.HelpUndo"};
	for (const char *key : kHelp)
		v->addWidget(makeDim(T(key)));
	v->addSpacing(4);
	v->addWidget(makeDim(T("Inspector.SelectHint")));
	v->addStretch(1);

	stack_->addWidget(page);
}

void CellInspector::showLayout(int lineWidth, const QColor &lineColor)
{
	setPage(PageLayout);
	widgets_.clear();
	lineWidth_ = lineWidth;
	lineColor_ = lineColor;
	{
		QSignalBlocker block(lineWidthSlider_);
		lineWidthSlider_->setValue(lineWidth);
	}
	setTextIfChanged(lineWidthValue_, T("Inspector.PixelsValue").arg(lineWidthSlider_->value()));
	setSwatch(lineColorBtn_, lineColor);
}

void CellInspector::setLineStyleEditable(bool editable)
{
	lineGroup_->setVisible(editable);
}

void CellInspector::setLineWidth(int width)
{
	lineWidth_ = width;
	setTextIfChanged(lineWidthValue_, T("Inspector.PixelsValue").arg(width));
	emit lineStyleEdited(width, lineColor_, MergeLineWidth);
}

void CellInspector::setLineColor(const QColor &color)
{
	if (color == lineColor_)
		return;
	lineColor_ = color;
	setSwatch(lineColorBtn_, color);
	emit lineStyleEdited(lineWidth_, color, MergeNone);
}

void CellInspector::pickLineColor()
{
	QColor color = QColorDialog::getColor(lineColor_, this, T("Inspector.PickLineColor"));
	if (color.isValid())
		setLineColor(color);
}

// ---------------------------------------------------------------- single page

QWidget *CellInspector::buildSinglePage()
{
	auto *page = new QWidget();
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(8);

	titleLabel_ = new QLabel();
	QFont tf = titleLabel_->font();
	tf.setBold(true);
	if (tf.pointSizeF() > 0)
		tf.setPointSizeF(tf.pointSizeF() * 1.15);
	titleLabel_->setFont(tf);
	subtitleLabel_ = makeDim();
	v->addWidget(titleLabel_);
	v->addWidget(subtitleLabel_);
	v->addWidget(makeLine());

	// Content type
	v->addWidget(makeHeading(T("Inspector.Content")));
	auto *grid = new QGridLayout();
	grid->setSpacing(4);
	typeGroup_ = new QButtonGroup(this);
	typeGroup_->setExclusive(true);
	for (int i = 0; i < 7; i++) {
		WidgetType type = kTypeOrder[i];
		QString name = type == WidgetType::None ? T("Inspector.TypeEmpty") : WidgetTypeDisplayName(type);
		auto *btn = new QToolButton();
		btn->setCheckable(true);
		btn->setText(name);
		btn->setToolTip(name);
		btn->setIcon(typeIcon(type));
		btn->setIconSize(QSize(14, 14));
		btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		QFont f = btn->font();
		if (f.pointSizeF() > 0)
			f.setPointSizeF(f.pointSizeF() * 0.9);
		btn->setFont(f);
		typeGroup_->addButton(btn, (int)type);
		grid->addWidget(btn, i / 4, i % 4);
	}
	v->addLayout(grid);
	connect(typeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
		WidgetType type = (WidgetType)id;
		bool changed = false;
		for (const WidgetConfig &w : widgets_)
			changed = changed || w.type != type;
		if (!changed)
			return;
		ContentRef ref;
		ref.type = type;
		applyEdit([ref](WidgetConfig &w) { ContentRefs::Apply(w, ref); }, T("Inspector.Undo.Content"),
			  MergeNone);
	});

	targetCombo_ = new QComboBox();
	targetCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	targetCombo_->setMinimumContentsLength(8);
	v->addWidget(targetCombo_);
	// activated() fires for user choices only, never for programmatic updates
	connect(targetCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
		if (idx < 0 || widgets_.isEmpty())
			return;
		if (targetCombo_->itemData(idx, kMarkerRole).toInt() == kMarkerPlaceholder)
			return;
		ContentRef ref;
		ref.type = widgets_.first().type;
		ref.name = targetCombo_->itemData(idx, kNameRole).toString();
		if (ref.type != WidgetType::Canvas)
			ref.uuid = targetCombo_->itemData(idx, Qt::UserRole).toString();
		applyEdit([ref](WidgetConfig &w) { ContentRefs::Apply(w, ref); }, T("Inspector.Undo.Content"),
			  MergeNone);
	});

	previewNote_ = makeDim(T("Inspector.PreviewNote"));
	noneNote_ = makeDim(T("Inspector.NoneNote"));
	v->addWidget(previewNote_);
	v->addWidget(noneNote_);

	// Label
	buildLabelControls(single_, true);
	v->addWidget(single_.container);

	// Overlays
	auto *overlays = new QWidget();
	auto *ov = new QVBoxLayout(overlays);
	ov->setContentsMargins(0, 0, 0, 0);
	ov->setSpacing(2);
	ov->addWidget(makeLine());
	ov->addWidget(makeHeading(T("Inspector.Overlays")));
	tallyCheck_ = new QCheckBox(T("Inspector.TallyBorder"));
	tallyCaption_ = makeDim();
	tallyCaption_->setContentsMargins(22, 0, 0, 6);
	ov->addWidget(tallyCheck_);
	ov->addWidget(tallyCaption_);
	connect(tallyCheck_, &QCheckBox::clicked, this, [this](bool on) {
		applyEdit([on](WidgetConfig &w) { w.showStatus = on; }, T("Inspector.Undo.Tally"), MergeNone);
	});

	safeRow_ = new QWidget();
	auto *sv = new QVBoxLayout(safeRow_);
	sv->setContentsMargins(0, 0, 0, 0);
	sv->setSpacing(2);
	safeCheck_ = new QCheckBox(T("Inspector.SafeAreas"));
	auto *safeCaption = makeDim(T("Inspector.SafeAreasCaption"));
	safeCaption->setContentsMargins(22, 0, 0, 0);
	sv->addWidget(safeCheck_);
	sv->addWidget(safeCaption);
	ov->addWidget(safeRow_);
	connect(safeCheck_, &QCheckBox::clicked, this, [this](bool on) {
		applyEdit([on](WidgetConfig &w) { w.safeRegion = on; }, T("Inspector.Undo.SafeAreas"), MergeNone);
	});
	v->addWidget(overlays);
	v->addStretch(1);
	return page;
}

void CellInspector::buildLabelControls(LabelControls &c, bool single)
{
	c.container = new QWidget();
	auto *outer = new QVBoxLayout(c.container);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(8);
	outer->addWidget(makeLine());
	outer->addWidget(makeHeading(T("Inspector.Label")));

	auto *form = new QFormLayout();
	form->setContentsMargins(0, 0, 0, 0);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	outer->addLayout(form);

	c.show = new QCheckBox(T("Inspector.ShowLabel"));
	form->addRow(c.show);

	if (single) {
		c.text = new QLineEdit();
		c.text->setClearButtonEnabled(true);
		form->addRow(T("Inspector.Text"), c.text);

		c.font = new QPushButton();
		c.font->setStyleSheet(QStringLiteral("text-align: left; padding: 3px 8px;"));
		form->addRow(T("Inspector.Font"), c.font);
	}

	auto *sizeRow = new QHBoxLayout();
	c.size = new QSlider(Qt::Horizontal);
	c.size->setRange(kLabelSizeMin, kLabelSizeMax);
	c.size->setSingleStep(5);
	c.size->setPageStep(25);
	c.sizeValue = new QLabel();
	c.sizeValue->setMinimumWidth(40);
	c.sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	sizeRow->addWidget(c.size, 1);
	sizeRow->addWidget(c.sizeValue);
	form->addRow(T("Inspector.Size"), sizeRow);

	QWidget *bgRow = makeSwatchRow(
		&c.bg,
		{QColor(0x00, 0x00, 0x00), QColor(0x1f, 0x21, 0x29), QColor(0x2b, 0x4b, 0xd1), QColor(0xb3, 0x26, 0x1e),
		 QColor(0x1d, 0x7a, 0x46), QColor(0xff, 0xff, 0xff)},
		[this](const QColor &col) { setLabelBgRgb(col); }, [this]() { pickLabelBg(); });
	form->addRow(T("Inspector.Background"), bgRow);

	auto *opacityRow = new QHBoxLayout();
	c.opacity = new QSlider(Qt::Horizontal);
	c.opacity->setRange(0, 100);
	c.opacity->setPageStep(10);
	c.opacityValue = new QLabel();
	c.opacityValue->setMinimumWidth(40);
	c.opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	opacityRow->addWidget(c.opacity, 1);
	opacityRow->addWidget(c.opacityValue);
	form->addRow(T("Inspector.Opacity"), opacityRow);

	auto *posGrid = new QGridLayout();
	posGrid->setSpacing(2);
	c.pos = new QButtonGroup(this);
	c.pos->setExclusive(true);
	for (int r = 0; r < 3; r++) {
		for (int col = 0; col < 3; col++) {
			auto *btn = new QToolButton();
			btn->setCheckable(true);
			btn->setIcon(positionIcon(r, col));
			btn->setIconSize(QSize(16, 16));
			btn->setFixedSize(26, 22);
			QString h = T(kHNames[col]);
			btn->setToolTip(T("Inspector.PositionTip").arg(T(kVNames[r]), h));
			c.pos->addButton(btn, r * 3 + col);
			c.posButtons[r * 3 + col] = btn;
			posGrid->addWidget(btn, r, col);
		}
	}
	posGrid->setColumnStretch(3, 1);
	form->addRow(T("Inspector.Position"), posGrid);

	bindLabelControls(c);
}

void CellInspector::bindLabelControls(LabelControls &c)
{
	LabelControls *cp = &c;

	connect(c.show, &QCheckBox::clicked, this, [this, cp](bool) {
		bool on = cp->show->checkState() == Qt::Checked;
		applyEdit([on](WidgetConfig &w) { w.labelVisible = on; },
			  on ? T("Inspector.Undo.ShowLabel") : T("Inspector.Undo.HideLabel"), MergeNone);
	});
	if (c.text) {
		connect(c.text, &QLineEdit::textEdited, this, [this](const QString &text) {
			applyEdit([text](WidgetConfig &w) { w.labelText = text; }, T("Inspector.Undo.LabelText"),
				  MergeLabelText);
		});
		connect(c.font, &QPushButton::clicked, this, [this]() { pickFont(); });
	}
	connect(c.size, &QSlider::valueChanged, this, [this, cp](int value) {
		int size = qBound(kLabelSizeMin, qRound(value / 5.0) * 5, kLabelSizeMax);
		setTextIfChanged(cp->sizeValue, QStringLiteral("%1%").arg(size));
		applyEdit([size](WidgetConfig &w) { w.labelSize = size; }, T("Inspector.Undo.LabelSize"),
			  MergeLabelSize);
	});
	connect(c.opacity, &QSlider::valueChanged, this, [this, cp](int value) {
		setTextIfChanged(cp->opacityValue, QStringLiteral("%1%").arg(value));
		int alpha = qRound(value * 255.0 / 100.0);
		applyEdit([alpha](WidgetConfig &w) { w.labelBgColor.setAlpha(alpha); },
			  T("Inspector.Undo.LabelOpacity"), MergeOpacity);
	});
	connect(c.pos, &QButtonGroup::idClicked, this, [this](int id) {
		int r = id / 3, col = id % 3;
		bool changed = false;
		for (const WidgetConfig &w : widgets_)
			changed = changed || positionIndex(w) != id;
		if (!changed)
			return;
		applyEdit(
			[r, col](WidgetConfig &w) {
				w.labelVAlign = kVAligns[r];
				w.labelHAlign = kHAligns[col];
			},
			T("Inspector.Undo.LabelPosition"), MergeNone);
	});
}

void CellInspector::setLabelBgRgb(const QColor &rgb)
{
	applyEdit(
		[rgb](WidgetConfig &w) {
			// Each cell keeps its own opacity
			w.labelBgColor = QColor(rgb.red(), rgb.green(), rgb.blue(), w.labelBgColor.alpha());
		},
		T("Inspector.Undo.LabelBackground"), MergeNone);
}

void CellInspector::pickLabelBg()
{
	if (widgets_.isEmpty())
		return;
	QColor initial = widgets_.first().labelBgColor;
	initial.setAlpha(255);
	QColor color = QColorDialog::getColor(initial, this, T("Inspector.PickBgColor"));
	if (color.isValid())
		setLabelBgRgb(color);
}

void CellInspector::pickFont()
{
	if (widgets_.isEmpty())
		return;
	QFont current;
	if (!widgets_.first().labelFont.isEmpty())
		current.fromString(widgets_.first().labelFont);
	bool ok = false;
	QFont font = QFontDialog::getFont(&ok, current, this, T("Inspector.PickFont"));
	if (!ok)
		return;
	QString spec = font.toString();
	applyEdit([spec](WidgetConfig &w) { w.labelFont = spec; }, T("Inspector.Undo.LabelFont"), MergeNone);
}

void CellInspector::applyEdit(const std::function<void(WidgetConfig &)> &fn, const QString &undoText, int mergeId)
{
	if (widgets_.isEmpty())
		return;
	for (WidgetConfig &w : widgets_)
		fn(w);
	emit widgetsEdited(widgets_, undoText, mergeId);
}

void CellInspector::syncLabelControls(LabelControls &c, bool single)
{
	const WidgetConfig &first = widgets_.first();
	const int n = widgets_.size();

	int shown = 0;
	bool samePos = true;
	for (const WidgetConfig &w : widgets_) {
		shown += w.labelVisible ? 1 : 0;
		samePos = samePos && positionIndex(w) == positionIndex(first);
	}
	setTri(c.show, shown, n);

	{
		QSignalBlocker block(c.size);
		c.size->setValue(first.labelSize);
	}
	setTextIfChanged(c.sizeValue, QStringLiteral("%1%").arg(c.size->value()));

	setSwatch(c.bg, QColor(first.labelBgColor.red(), first.labelBgColor.green(), first.labelBgColor.blue()));
	{
		QSignalBlocker block(c.opacity);
		c.opacity->setValue(qRound(first.labelBgColor.alpha() * 100.0 / 255.0));
	}
	setTextIfChanged(c.opacityValue, QStringLiteral("%1%").arg(c.opacity->value()));

	c.pos->setExclusive(false);
	for (QToolButton *btn : c.posButtons)
		btn->setChecked(false);
	int idx = samePos ? positionIndex(first) : -1;
	if (idx >= 0)
		c.posButtons[idx]->setChecked(true);
	c.pos->setExclusive(true);

	if (single) {
		// Leave the text alone when equal, so typing keeps its cursor and selection
		if (c.text->text() != first.labelText) {
			QSignalBlocker block(c.text);
			c.text->setText(first.labelText);
		}
		QString placeholder = T("Inspector.AutoLabel").arg(autoLabel(first));
		if (c.text->placeholderText() != placeholder)
			c.text->setPlaceholderText(placeholder);

		QFont font;
		if (!first.labelFont.isEmpty())
			font.fromString(first.labelFont);
		QString fontText = QStringLiteral("%1 %2").arg(font.family(), QFontDatabase::styleString(font));
		if (c.font->text() != fontText)
			c.font->setText(fontText);
	}
}

void CellInspector::populateTargets(WidgetType type)
{
	QSignalBlocker block(targetCombo_);
	targetCombo_->clear();
	populated_ = true;
	populatedType_ = type;

	auto addItem = [this](const QString &text, const QString &data, const QString &name) {
		targetCombo_->addItem(text, data);
		targetCombo_->setItemData(targetCombo_->count() - 1, name, kNameRole);
	};

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
			addItem(entry.first, entry.second, entry.first);
	} else if (type == WidgetType::Canvas) {
		addItem(T("CellDialog.MainCanvas"), QString(), QString());

		QStringList canvasNames;
		auto enumCanvasCb = [](void *param, obs_canvas_t *canvas) -> bool {
			auto *list = static_cast<QStringList *>(param);
			const char *name = obs_canvas_get_name(canvas);
			if (name && strlen(name) > 0)
				list->append(QString::fromUtf8(name));
			return true;
		};
		obs_enum_canvases(enumCanvasCb, &canvasNames);
		canvasNames.sort(Qt::CaseInsensitive);
		for (const QString &n : canvasNames)
			addItem(n, n, n);
	}
}

void CellInspector::syncTarget(const WidgetConfig &w)
{
	QSignalBlocker block(targetCombo_);

	// Drop last time's synthetic items; they are rebuilt below if still needed
	for (int i = targetCombo_->count() - 1; i >= 0; i--) {
		if (targetCombo_->itemData(i, kMarkerRole).isValid())
			targetCombo_->removeItem(i);
	}

	int idx = -1;
	if (w.type == WidgetType::Canvas) {
		idx = targetCombo_->findData(w.canvasName);
		if (idx < 0 && !w.canvasName.isEmpty()) {
			targetCombo_->insertItem(0, T("Inspector.Missing").arg(w.canvasName), w.canvasName);
			targetCombo_->setItemData(0, w.canvasName, kNameRole);
			targetCombo_->setItemData(0, kMarkerMissing, kMarkerRole);
			idx = 0;
		}
	} else {
		const QString name = w.type == WidgetType::Scene ? w.sceneName : w.sourceName;
		if (!w.sourceUuid.isEmpty())
			idx = targetCombo_->findData(w.sourceUuid);
		if (idx < 0 && !name.isEmpty()) {
			for (int i = 0; i < targetCombo_->count(); i++) {
				if (targetCombo_->itemData(i, kNameRole).toString() == name) {
					idx = i;
					break;
				}
			}
		}
		if (idx < 0 && !name.isEmpty()) {
			targetCombo_->insertItem(0, T("Inspector.Missing").arg(name), w.sourceUuid);
			targetCombo_->setItemData(0, name, kNameRole);
			targetCombo_->setItemData(0, kMarkerMissing, kMarkerRole);
			idx = 0;
		} else if (idx < 0) {
			targetCombo_->insertItem(0, w.type == WidgetType::Scene ? T("Inspector.ChooseScene")
										: T("Inspector.ChooseSource"));
			targetCombo_->setItemData(0, kMarkerPlaceholder, kMarkerRole);
			if (auto *model = qobject_cast<QStandardItemModel *>(targetCombo_->model())) {
				if (QStandardItem *item = model->item(0))
					item->setEnabled(false);
			}
			idx = 0;
		}
	}
	targetCombo_->setCurrentIndex(idx);
}

void CellInspector::showCells(const QVector<WidgetConfig> &widgets, const QString &title, const QString &subtitle)
{
	if (widgets.isEmpty()) {
		widgets_.clear();
		setPage(PageLayout);
		return;
	}
	widgets_ = widgets;
	if (widgets_.size() == 1)
		showSingle(title, subtitle);
	else
		showMulti(title, subtitle);
}

void CellInspector::showSingle(const QString &title, const QString &subtitle)
{
	const WidgetConfig &w = widgets_.first();
	// A different cell than last time: the target list is rebuilt, not just resynced
	const bool fresh = stack_->currentIndex() != PageSingle || title != lastTitle_;
	lastTitle_ = title;
	setPage(PageSingle);

	setTextIfChanged(titleLabel_, title);
	setTextIfChanged(subtitleLabel_, subtitle);
	subtitleLabel_->setVisible(!subtitle.isEmpty());

	if (QAbstractButton *btn = typeGroup_->button((int)w.type))
		btn->setChecked(true);

	const bool needsTarget = w.type == WidgetType::Scene || w.type == WidgetType::Source ||
				 w.type == WidgetType::Canvas;
	targetCombo_->setVisible(needsTarget);
	if (needsTarget) {
		if (fresh || !populated_ || populatedType_ != w.type)
			populateTargets(w.type);
		syncTarget(w);
	} else {
		populated_ = false;
	}
	previewNote_->setVisible(w.type == WidgetType::Preview);
	noneNote_->setVisible(w.type == WidgetType::None);

	single_.container->setVisible(w.type != WidgetType::None);
	if (w.type != WidgetType::None)
		syncLabelControls(single_, true);

	const bool isScene = w.type == WidgetType::Scene;
	tallyCheck_->setEnabled(isScene);
	{
		QSignalBlocker block(tallyCheck_);
		tallyCheck_->setChecked(w.showStatus);
	}
	setTextIfChanged(tallyCaption_, isScene ? T("Inspector.TallyCaption") : T("Inspector.TallyUnavailable"));

	safeRow_->setVisible(w.type != WidgetType::None);
	setTri(safeCheck_, w.safeRegion ? 1 : 0, 1);
}

// ----------------------------------------------------------------- multi page

QWidget *CellInspector::buildMultiPage()
{
	auto *page = new QWidget();
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(8);

	multiTitle_ = new QLabel();
	QFont tf = multiTitle_->font();
	tf.setBold(true);
	if (tf.pointSizeF() > 0)
		tf.setPointSizeF(tf.pointSizeF() * 1.15);
	multiTitle_->setFont(tf);
	multiSubtitle_ = makeDim();
	v->addWidget(multiTitle_);
	v->addWidget(multiSubtitle_);
	v->addWidget(makeLine());

	v->addWidget(makeHeading(T("Inspector.Arrange")));
	auto *buttons = new QHBoxLayout();
	mergeBtn_ = new QPushButton(T("Inspector.Merge"));
	auto *clearBtn = new QPushButton(T("Inspector.Clear"));
	buttons->addWidget(mergeBtn_, 1);
	buttons->addWidget(clearBtn);
	v->addLayout(buttons);
	mergeReason_ = makeDim();
	mergeReason_->hide();
	v->addWidget(mergeReason_);
	connect(mergeBtn_, &QPushButton::clicked, this, &CellInspector::mergeRequested);
	connect(clearBtn, &QPushButton::clicked, this, &CellInspector::clearRequested);

	buildLabelControls(multi_, false);
	v->addWidget(multi_.container);

	v->addWidget(makeLine());
	v->addWidget(makeHeading(T("Inspector.Overlays")));
	multiSafeCheck_ = new QCheckBox(T("Inspector.SafeAreas"));
	v->addWidget(multiSafeCheck_);
	connect(multiSafeCheck_, &QCheckBox::clicked, this, [this](bool) {
		bool on = multiSafeCheck_->checkState() == Qt::Checked;
		applyEdit([on](WidgetConfig &w) { w.safeRegion = on; }, T("Inspector.Undo.SafeAreas"), MergeNone);
	});
	v->addStretch(1);
	return page;
}

void CellInspector::setMergeState(bool canMerge, const QString &reasonIfNot)
{
	mergeBtn_->setEnabled(canMerge);
	setTextIfChanged(mergeReason_, reasonIfNot);
	mergeReason_->setVisible(!canMerge && !reasonIfNot.isEmpty());
}

void CellInspector::showMulti(const QString &title, const QString &subtitle)
{
	setPage(PageMulti);
	lastTitle_.clear();
	populated_ = false;

	QStringList types;
	int safe = 0;
	for (const WidgetConfig &w : widgets_) {
		QString name = WidgetTypeDisplayName(w.type);
		if (!types.contains(name))
			types.append(name);
		safe += w.safeRegion ? 1 : 0;
	}
	setTextIfChanged(multiTitle_, title.isEmpty() ? T("Inspector.NCells").arg(widgets_.size()) : title);
	QString sub = types.join(QStringLiteral(", "));
	if (!subtitle.isEmpty())
		sub = subtitle + QStringLiteral(" · ") + sub;
	setTextIfChanged(multiSubtitle_, sub);

	syncLabelControls(multi_, false);
	setTri(multiSafeCheck_, safe, widgets_.size());
}
