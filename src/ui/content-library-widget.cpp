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

#include "content-library-widget.hpp"
#include "widget-type-style.hpp"
#include "../plugin.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int kRefRole = Qt::UserRole;            // QByteArray, ContentRefs::Encode
constexpr int kGroupRole = Qt::UserRole + 1;      // QString group key (group items only)
constexpr int kBadgeRole = Qt::UserRole + 2;      // QString badge text
constexpr int kBadgeColorRole = Qt::UserRole + 3; // QColor

bool isContentItem(const QTreeWidgetItem *item)
{
	return item && item->parent() && item->data(0, kRefRole).isValid();
}

ContentRef refOf(const QTreeWidgetItem *item)
{
	ContentRef ref;
	ContentRefs::Decode(item->data(0, kRefRole).toByteArray(), &ref);
	return ref;
}

bool sameRef(const ContentRef &a, const ContentRef &b)
{
	return a.type == b.type && a.name == b.name && a.uuid == b.uuid;
}

QIcon swatchIcon(const QColor &color)
{
	QPixmap pm(12, 12);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(QRectF(1, 1, 10, 10), 2, 2);
	p.end();
	return QIcon(pm);
}

// Draws the PGM / PVW badge right-aligned over the row
class BadgeDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QStyledItemDelegate::paint(painter, option, index);
		const QString badge = index.data(kBadgeRole).toString();
		if (badge.isEmpty())
			return;

		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		QFont f = option.font;
		f.setBold(true);
		f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.8));
		painter->setFont(f);
		const QFontMetrics fm(f);
		const int w = fm.horizontalAdvance(badge) + 10;
		const int h = fm.height() + 2;
		QRect r(option.rect.right() - w - 4, option.rect.center().y() - h / 2, w, h);
		painter->setPen(Qt::NoPen);
		painter->setBrush(index.data(kBadgeColorRole).value<QColor>());
		painter->drawRoundedRect(r, 3, 3);
		painter->setPen(Qt::white);
		painter->drawText(r, Qt::AlignCenter, badge);
		painter->restore();
	}
};

// Tree whose content items drag out as a ContentRef
class LibraryTree : public QTreeWidget {
public:
	std::function<void(QTreeWidgetItem *)> onClicked;
	std::function<void(QTreeWidgetItem *)> onEnter;

	explicit LibraryTree(QWidget *parent) : QTreeWidget(parent) {}

	QStringList mimeTypes() const override { return {ContentRefs::MimeType()}; }

protected:
	QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
	{
		for (QTreeWidgetItem *item : items) {
			if (!isContentItem(item))
				continue;
			auto *mime = new QMimeData();
			mime->setData(ContentRefs::MimeType(), ContentRefs::Encode(refOf(item)));
			return mime;
		}
		return nullptr;
	}

	void mouseReleaseEvent(QMouseEvent *e) override
	{
		const bool left = e->button() == Qt::LeftButton;
		QTreeWidgetItem *item = left ? itemAt(e->position().toPoint()) : nullptr;
		QTreeWidget::mouseReleaseEvent(e);
		if (item && isContentItem(item) && onClicked)
			onClicked(item);
	}

	void keyPressEvent(QKeyEvent *e) override
	{
		if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !e->isAutoRepeat()) {
			QTreeWidgetItem *item = currentItem();
			if (isContentItem(item) && onEnter)
				onEnter(item);
			e->accept();
			return;
		}
		QTreeWidget::keyPressEvent(e);
	}
};

struct Entry {
	QString name;
	QString uuid;
};

void callbackSourceChange(void *param, calldata_t *)
{
	// May run on any thread: no widget access and the source is not kept
	auto *self = static_cast<ContentLibraryWidget *>(param);
	QMetaObject::invokeMethod(self, "scheduleRefresh", Qt::QueuedConnection);
}

void callbackFrontendEvent(enum obs_frontend_event event, void *param)
{
	auto *self = static_cast<ContentLibraryWidget *>(param);
	QMetaObject::invokeMethod(self, "handleFrontendEvent", Qt::QueuedConnection, Q_ARG(int, (int)event));
}

} // namespace

ContentLibraryWidget::ContentLibraryWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(200);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	auto *heading = new QLabel(QString::fromUtf8(LG_TEXT("Library.Heading")).toUpper(), this);
	QFont hf = heading->font();
	hf.setBold(true);
	hf.setPointSizeF(std::max(7.0, hf.pointSizeF() * 0.85));
	hf.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
	heading->setFont(hf);
	heading->setEnabled(false);
	layout->addWidget(heading);

	filterEdit_ = new QLineEdit(this);
	filterEdit_->setPlaceholderText(QString::fromUtf8(LG_TEXT("Library.FilterPlaceholder")));
	filterEdit_->setClearButtonEnabled(true);
	layout->addWidget(filterEdit_);

	auto *tree = new LibraryTree(this);
	tree_ = tree;
	tree_->setHeaderHidden(true);
	tree_->setIndentation(12);
	tree_->setUniformRowHeights(true);
	tree_->setRootIsDecorated(false);
	tree_->setSelectionMode(QAbstractItemView::SingleSelection);
	tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	tree_->setDragEnabled(true);
	tree_->setDragDropMode(QAbstractItemView::DragOnly);
	tree_->setDefaultDropAction(Qt::CopyAction);
	tree_->setItemDelegate(new BadgeDelegate(tree_));
	layout->addWidget(tree_, 1);

	auto *hint = new QLabel(QString::fromUtf8(LG_TEXT("Library.Hint")), this);
	hint->setWordWrap(true);
	hint->setEnabled(false);
	layout->addWidget(hint);

	tree->onClicked = [this](QTreeWidgetItem *item) {
		emit contentActivated(refOf(item));
	};
	tree->onEnter = tree->onClicked;
	connect(filterEdit_, &QLineEdit::textChanged, this, [this] { applyFilter(); });

	refreshTimer_.setSingleShot(true);
	refreshTimer_.setInterval(150);
	connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });

	obs_frontend_add_event_callback(callbackFrontendEvent, this);

	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", callbackSourceChange, this);
	signal_handler_connect(sh, "source_destroy", callbackSourceChange, this);
	signal_handler_connect(sh, "source_rename", callbackSourceChange, this);

	refresh();
}

ContentLibraryWidget::~ContentLibraryWidget()
{
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", callbackSourceChange, this);
	signal_handler_disconnect(sh, "source_destroy", callbackSourceChange, this);
	signal_handler_disconnect(sh, "source_rename", callbackSourceChange, this);
	obs_frontend_remove_event_callback(callbackFrontendEvent, this);
}

void ContentLibraryWidget::handleFrontendEvent(int event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		scheduleRefresh();
		updateLiveScenes();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		updateLiveScenes();
		break;
	default:
		// Canvas added/removed events only exist from OBS 31.1 and cannot be
		// guarded by the preprocessor; canvases are re-read on every refresh
		break;
	}
}

void ContentLibraryWidget::scheduleRefresh()
{
	refreshTimer_.start();
}

void ContentLibraryWidget::updateLiveScenes()
{
	QString program, preview;
	if (obs_source_t *src = obs_frontend_get_current_scene()) {
		program = QString::fromUtf8(obs_source_get_name(src));
		obs_source_release(src);
	}
	if (obs_frontend_preview_program_mode_active()) {
		if (obs_source_t *src = obs_frontend_get_current_preview_scene()) {
			preview = QString::fromUtf8(obs_source_get_name(src));
			obs_source_release(src);
		}
	}

	const bool changed = program != programScene_ || preview != previewScene_;
	programScene_ = program;
	previewScene_ = preview;
	applyBadges();
	if (changed || !liveEmitted_) {
		liveEmitted_ = true;
		emit liveScenesChanged(programScene_, previewScene_);
	}
}

void ContentLibraryWidget::applyBadges()
{
	for (int g = 0; g < tree_->topLevelItemCount(); g++) {
		QTreeWidgetItem *group = tree_->topLevelItem(g);
		for (int i = 0; i < group->childCount(); i++) {
			QTreeWidgetItem *item = group->child(i);
			QString badge;
			QColor color;
			if (refOf(item).type == WidgetType::Scene) {
				const QString name = item->text(0);
				if (!programScene_.isEmpty() && name == programScene_) {
					badge = QString::fromUtf8(LG_TEXT("Library.BadgeProgram"));
					color = WidgetTypeColor(WidgetType::Program);
				} else if (!previewScene_.isEmpty() && name == previewScene_) {
					badge = QString::fromUtf8(LG_TEXT("Library.BadgePreview"));
					color = WidgetTypeColor(WidgetType::Preview);
				}
			}
			item->setData(0, kBadgeRole, badge);
			item->setData(0, kBadgeColorRole, color);
		}
	}
	tree_->viewport()->update();
}

void ContentLibraryWidget::applyFilter()
{
	const QString needle = filterEdit_->text().trimmed();
	for (int g = 0; g < tree_->topLevelItemCount(); g++) {
		QTreeWidgetItem *group = tree_->topLevelItem(g);
		int visible = 0;
		for (int i = 0; i < group->childCount(); i++) {
			QTreeWidgetItem *item = group->child(i);
			const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive);
			item->setHidden(!match);
			if (match)
				visible++;
		}
		group->setHidden(visible == 0);
	}
}

void ContentLibraryWidget::refresh()
{
	refreshTimer_.stop();

	// Remember state to restore
	QHash<QString, bool> expanded;
	for (int g = 0; g < tree_->topLevelItemCount(); g++) {
		QTreeWidgetItem *group = tree_->topLevelItem(g);
		expanded[group->data(0, kGroupRole).toString()] = group->isExpanded();
	}
	ContentRef current;
	const bool hadCurrent = isContentItem(tree_->currentItem());
	if (hadCurrent)
		current = refOf(tree_->currentItem());
	const int scroll = tree_->verticalScrollBar()->value();

	// Enumerate
	auto collect = [](void *param, obs_source_t *source) -> bool {
		auto *list = static_cast<QVector<Entry> *>(param);
		const char *name = obs_source_get_name(source);
		if (name && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
			list->append({QString::fromUtf8(name), QString::fromUtf8(obs_source_get_uuid(source))});
		return true;
	};
	QVector<Entry> scenes, sources;
	struct obs_frontend_source_list sceneList = {};
	obs_frontend_get_scenes(&sceneList);
	for (size_t i = 0; i < sceneList.sources.num; i++)
		collect(&scenes, sceneList.sources.array[i]);
	obs_frontend_source_list_free(&sceneList);
	obs_enum_sources(collect, &sources);
	std::sort(sources.begin(), sources.end(), [](const Entry &a, const Entry &b) {
		return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
	});

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

	// Rebuild
	tree_->setUpdatesEnabled(false);
	tree_->clear();
	QTreeWidgetItem *restore = nullptr;

	auto addGroup = [&](const char *key, const char *textKey) {
		auto *group = new QTreeWidgetItem(tree_);
		group->setText(0, QString::fromUtf8(LG_TEXT(textKey)));
		group->setData(0, kGroupRole, QString::fromUtf8(key));
		group->setFlags(Qt::ItemIsEnabled);
		QFont f = group->font(0);
		f.setBold(true);
		group->setFont(0, f);
		return group;
	};
	auto addItem = [&](QTreeWidgetItem *group, const QString &text, const ContentRef &ref) {
		auto *item = new QTreeWidgetItem(group);
		item->setText(0, text);
		item->setIcon(0, swatchIcon(WidgetTypeColor(ref.type)));
		item->setData(0, kRefRole, ContentRefs::Encode(ref));
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
		if (hadCurrent && sameRef(ref, current))
			restore = item;
	};

	QTreeWidgetItem *g = addGroup("outputs", "Library.GroupOutputs");
	addItem(g, WidgetTypeDisplayName(WidgetType::Program), {WidgetType::Program, QString(), QString()});
	addItem(g, WidgetTypeDisplayName(WidgetType::Preview), {WidgetType::Preview, QString(), QString()});

	g = addGroup("scenes", "Library.GroupScenes");
	for (const Entry &e : scenes)
		addItem(g, e.name, {WidgetType::Scene, e.name, e.uuid});

	g = addGroup("sources", "Library.GroupSources");
	for (const Entry &e : sources)
		addItem(g, e.name, {WidgetType::Source, e.name, e.uuid});

	g = addGroup("canvases", "Library.GroupCanvases");
	addItem(g, QString::fromUtf8(LG_TEXT("CellDialog.MainCanvas")), {WidgetType::Canvas, QString(), QString()});
	for (const QString &n : canvasNames)
		addItem(g, n, {WidgetType::Canvas, n, QString()});

	g = addGroup("other", "Library.GroupOther");
	addItem(g, WidgetTypeDisplayName(WidgetType::Placeholder), {WidgetType::Placeholder, QString(), QString()});

	for (int i = 0; i < tree_->topLevelItemCount(); i++) {
		QTreeWidgetItem *group = tree_->topLevelItem(i);
		group->setExpanded(expanded.value(group->data(0, kGroupRole).toString(), true));
	}
	if (restore)
		tree_->setCurrentItem(restore);

	applyFilter();
	applyBadges();
	tree_->setUpdatesEnabled(true);
	tree_->verticalScrollBar()->setValue(scroll);

	updateLiveScenes();
}
