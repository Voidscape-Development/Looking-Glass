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

#include "multiview-renderer.hpp"
#include "live-state.hpp"
#include "../core/grid-model.hpp"
#include "../plugin.hpp"

#include <obs-module.h>
#include <obs-audio-controls.h>
#include <graphics/matrix4.h>
#include <util/platform.h>

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// --- Frame: everything the graphics thread needs to draw one layout ---

// Live levels of an audio source, written by the volmeter on the audio thread
struct MultiviewRenderer::Meter {
	obs_volmeter_t *volmeter = nullptr;
	std::mutex mutex;
	float magnitude[MAX_AUDIO_CHANNELS] = {};
	float peak[MAX_AUDIO_CHANNELS] = {};
	uint64_t updated = 0; // os_gettime_ns() of the last update

	explicit Meter(obs_source_t *source) : volmeter(obs_volmeter_create(OBS_FADER_LOG))
	{
		obs_volmeter_add_callback(volmeter, Update, this);
		obs_volmeter_attach_source(volmeter, source);
	}
	~Meter() { obs_volmeter_destroy(volmeter); } // Stops the callbacks

	static void Update(void *data, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			   const float[MAX_AUDIO_CHANNELS])
	{
		auto *self = static_cast<Meter *>(data);
		std::lock_guard<std::mutex> lock(self->mutex);
		std::copy(magnitude, magnitude + MAX_AUDIO_CHANNELS, self->magnitude);
		std::copy(peak, peak + MAX_AUDIO_CHANNELS, self->peak);
		self->updated = os_gettime_ns();
	}
};

struct MultiviewRenderer::Cell {
	CellConfig config;

	// Scene or source this cell displays. A weak reference lets the source be
	// destroyed while the cell points at it; the matching
	// obs_source_inc_showing() is undone when the Frame is destroyed.
	obs_weak_source_t *shown = nullptr;

	obs_source_t *label = nullptr; // Private text source
	obs_source_t *text = nullptr;  // Clock and Text cells: private text source for the content
	QString clockText;             // UI thread: what text currently shows
	std::unique_ptr<Meter> meter;  // Audio meter cells
	std::string canvasName;

	// Graphics thread cache, recreated when the cell's pixel size changes
	gs_texture_t *labelBg = nullptr;
	int labelBgW = 0;
	int labelBgH = 0;
	gs_texture_t *placeholder = nullptr;
	int placeholderSize = 0;
};

struct MultiviewRenderer::Frame {
	int rows = 1;
	int cols = 1;
	int borderWidth = 1;
	uint32_t lineColor = 0xFFFFFFFF;
	QVector<int> owners; // GridModel::Ownership() of the cells
	std::vector<Cell> cells;

	Frame() = default;
	Frame(const Frame &) = delete;
	Frame &operator=(const Frame &) = delete;

	// Runs on the UI thread, never while the Frame is being drawn
	~Frame()
	{
		obs_enter_graphics();
		for (Cell &cell : cells) {
			if (cell.labelBg)
				gs_texture_destroy(cell.labelBg);
			if (cell.placeholder)
				gs_texture_destroy(cell.placeholder);
		}
		obs_leave_graphics();

		for (Cell &cell : cells) {
			obs_source_release(cell.label);
			obs_source_release(cell.text);
			cell.meter.reset();

			// The source may already be gone; its showing count died with it.
			// hide callbacks run here, outside the graphics context.
			obs_source_t *source = obs_weak_source_get_source(cell.shown);
			if (source) {
				if (cell.config.widget.type != WidgetType::AudioMeter)
					obs_source_dec_showing(source);
				obs_source_release(source);
			}
			obs_weak_source_release(cell.shown);
		}
	}
};

struct MultiviewRenderer::CellRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;

	bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

namespace {

using CellRect = MultiviewRenderer::CellRect;

// OBS multiview-style status colors (ARGB)
constexpr uint32_t kPreviewColor = 0xFF00D000;
constexpr uint32_t kProgramColor = 0xFFD00000;

// Rec. ITU-R BT.1848-1 / EBU R 95 safe area constants
constexpr uint32_t kSafeAreaColor = 0xFFD0D0D0;
constexpr float kLineLength = 0.1f;
constexpr float kActionSafePercent = 0.035f;
constexpr float kGraphicsSafePercent = 0.05f;
constexpr float kFourByThreeSafePercent = 0.1625f;

// OBS Studio's multiview sizes label text at 1/9.81 of the cell height and
// scales it along with the cell; Looking Glass uses the same proportion for a
// label size of 100%
constexpr float kLabelFontToCellHeight = 1.0f / 9.81f;

// Labels are rasterized once at this font size and scaled when drawn. It sits
// above the size labels usually end up at, so they are mostly scaled down.
constexpr int kLabelRasterSize = 72;

// Audio meter scale and zones, in dBFS, as in OBS Studio's mixer
constexpr float kMeterMinDb = -60.0f;
constexpr float kMeterYellowDb = -20.0f;
constexpr float kMeterRedDb = -9.0f;
constexpr uint64_t kMeterStaleNs = 300000000; // No update for this long reads as silence
constexpr uint32_t kMeterColors[3] = {0xFF4CD94C, 0xFFE5D94C, 0xFFE54C4C};
constexpr uint32_t kMeterDimColors[3] = {0xFF1E3D1E, 0xFF3D3A1E, 0xFF3D1E1E};

// Where the grid sits in a target of totalW x totalH device pixels. Drawing and
// click hit testing both use it, so clicks land on what is drawn.
struct GridGeometry {
	int inset = 1;
	int offsetX = 0;
	int offsetY = 0;
	float cellW = 0.0f;
	float cellH = 0.0f;
	bool valid = false;

	CellRect cellRect(const CellConfig &c) const
	{
		int x0 = (int)std::lround(c.col * cellW);
		int x1 = (int)std::lround((c.col + c.colSpan) * cellW);
		int y0 = (int)std::lround(c.row * cellH);
		int y1 = (int)std::lround((c.row + c.rowSpan) * cellH);
		return {offsetX + x0 + inset, offsetY + y0 + inset, (x1 - x0) - 2 * inset, (y1 - y0) - 2 * inset};
	}
};

GridGeometry ComputeGeometry(int rows, int cols, int borderWidth, float ratio, int totalW, int totalH)
{
	GridGeometry g;
	rows = std::max(1, rows);
	cols = std::max(1, cols);
	const int border = std::max(1, (int)std::lround(borderWidth * ratio));

	// Grid lines are centered on cell boundaries, so the outer ones bleed
	// half their width outward. Reserve room for that on every side.
	const int outerHalf = (border + 1) / 2;
	const int availableW = totalW - 2 * outerHalf;
	const int availableH = totalH - 2 * outerHalf;
	if (availableW <= 0 || availableH <= 0)
		return g;

	// Every cell keeps a 16:9 shape
	float gridAspect = (float)(cols * 16) / (float)(rows * 9);
	int gridW, gridH;
	if ((float)availableW / (float)availableH > gridAspect) {
		gridH = availableH;
		gridW = (int)(gridH * gridAspect);
	} else {
		gridW = availableW;
		gridH = (int)(gridW / gridAspect);
	}

	// Keep the leftover margin even so the grid centers exactly
	if ((totalW - gridW) % 2 != 0)
		gridW--;
	if ((totalH - gridH) % 2 != 0)
		gridH--;

	// Cell boundaries are rounded individually from float sizes, spreading
	// sub-pixel remainders across cells instead of into the margins
	g.cellW = (float)gridW / cols;
	g.cellH = (float)gridH / rows;
	g.offsetX = (totalW - gridW) / 2;
	g.offsetY = (totalH - gridH) / 2;

	// Cells sit inside the grid lines. For a line of width W, floor(W/2) is
	// the ideal inset, but never less than 1 so a 1px line stays visible.
	g.inset = std::max(1, border / 2);
	g.valid = true;
	return g;
}

struct FitRect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

// Largest rectangle with the base aspect ratio that fits inside the cell,
// centered in it
FitRect FitInside(uint32_t baseW, uint32_t baseH, int x, int y, int w, int h)
{
	FitRect fit;
	double windowAspect = (double)w / (double)h;
	double baseAspect = (double)baseW / (double)baseH;

	if (windowAspect > baseAspect) {
		// Height-constrained: fill the height, center horizontally
		fit.h = h;
		fit.w = std::min(w, (int)std::lround((double)baseW * h / baseH));
	} else {
		// Width-constrained: fill the width, center vertically
		fit.w = w;
		fit.h = std::min(h, (int)std::lround((double)baseH * w / baseW));
	}

	fit.x = x + (w - fit.w) / 2;
	fit.y = y + (h - fit.h) / 2;
	return fit;
}

// Solid rectangle in the current (pixel) coordinate space
void FillRect(int x, int y, int w, int h, uint32_t color)
{
	if (w <= 0 || h <= 0)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), color);

	gs_matrix_push();
	gs_matrix_translate3f((float)x, (float)y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, (uint32_t)w, (uint32_t)h);
	gs_matrix_pop();
}

// Draws a straight-alpha texture at (x, y) in the current coordinate space
void DrawTexture(gs_texture_t *texture, int x, int y, int w, int h)
{
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);

	gs_matrix_push();
	gs_matrix_translate3f((float)x, (float)y, 0.0f);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(texture, 0, (uint32_t)w, (uint32_t)h);
	gs_matrix_pop();

	gs_blend_state_pop();
}

// Uploads a QImage (any format) as a straight-alpha RGBA texture. Must be
// called inside the graphics context.
gs_texture_t *TextureFromImage(const QImage &image)
{
	QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
	const uint8_t *bits = rgba.constBits();
	return gs_texture_create((uint32_t)rgba.width(), (uint32_t)rgba.height(), GS_RGBA, 1, &bits, 0);
}

// QImage and QPainter may be used off the GUI thread as long as they paint
// onto an image, so these run directly on the graphics thread
gs_texture_t *CreateRoundedRectTexture(int w, int h, int radius, const QColor &color)
{
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);

	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setBrush(color);
	painter.setPen(Qt::NoPen);
	painter.drawRoundedRect(0, 0, w, h, radius, radius);
	painter.end();

	return TextureFromImage(img);
}

gs_texture_t *CreateSvgTexture(const QByteArray &svg, int size)
{
	QSvgRenderer renderer(svg);
	if (!renderer.isValid())
		return nullptr;

	QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);

	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	renderer.render(&painter);
	painter.end();

	return TextureFromImage(img);
}

const char *TextSourceId()
{
#ifdef _WIN32
	return "text_gdiplus";
#else
	return "text_ft2_source";
#endif
}

// Private text source used to draw a cell label, clock, or text. The font
// contributes its family and style; the size on screen is decided when
// drawing, relative to the cell. With a file, the text is read from it and
// follows its changes.
obs_source_t *CreateTextSource(const QString &text, const QString &fontString, const QString &file = QString())
{
	if (text.isEmpty() && file.isEmpty())
		return nullptr;

	QFont font;
	if (!fontString.isEmpty())
		font.fromString(fontString);

	// Font flags bitmask: bit 0 = bold, bit 1 = italic
	int fontFlags = 0;
	if (font.bold())
		fontFlags |= 1;
	if (font.italic())
		fontFlags |= 2;

	// Full style name (e.g. "Light Italic", "SemiBold") so sub-weights survive
	QString style = QFontDatabase::styleString(font);
	if (style.isEmpty())
		style = QStringLiteral("Regular");

	obs_data_t *fontData = obs_data_create();
	obs_data_set_string(fontData, "face", font.family().toUtf8().constData());
	obs_data_set_int(fontData, "size", kLabelRasterSize);
	obs_data_set_int(fontData, "flags", fontFlags);
	obs_data_set_string(fontData, "style", style.toUtf8().constData());

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.toUtf8().constData());
	obs_data_set_obj(settings, "font", fontData);
	// White text with full opacity
	obs_data_set_int(settings, "color1", 0xFFFFFFFF);
	obs_data_set_int(settings, "color2", 0xFFFFFFFF);
	if (!file.isEmpty()) {
#ifdef _WIN32
		obs_data_set_bool(settings, "read_from_file", true);
		obs_data_set_string(settings, "file", file.toUtf8().constData());
#else
		obs_data_set_bool(settings, "from_file", true);
		obs_data_set_string(settings, "text_file", file.toUtf8().constData());
#endif
	}
	obs_data_release(fontData);

	obs_source_t *source = obs_source_create_private(TextSourceId(), "lg_label", settings);
	obs_data_release(settings);
	return source;
}

// The scene or source a cell displays: by UUID first, then by name for
// layouts that predate UUIDs or came from another collection's template
obs_source_t *ResolveSource(const WidgetConfig &w)
{
	if (w.type != WidgetType::Scene && w.type != WidgetType::Source && w.type != WidgetType::AudioMeter)
		return nullptr;

	obs_source_t *source = nullptr;
	if (!w.sourceUuid.isEmpty())
		source = obs_get_source_by_uuid(w.sourceUuid.toUtf8().constData());

	QString name = w.targetName();
	if (!source && !name.isEmpty())
		source = obs_get_source_by_name(name.toUtf8().constData());
	return source;
}

QString FormatDuration(qint64 secs)
{
	return QString::asprintf("%02lld:%02lld:%02lld", secs / 3600, secs / 60 % 60, secs % 60);
}

// What a Clock cell shows right now. UI thread (uses the frontend API).
QString ClockText(const WidgetConfig &w)
{
	switch (w.clockMode) {
	case ClockMode::Countdown: {
		// Counts down to the target, then up past it
		qint64 left = QTime::currentTime().secsTo(w.clockTarget);
		return left >= 0 ? FormatDuration(left) : QStringLiteral("+") + FormatDuration(-left);
	}
	case ClockMode::StreamTime:
	case ClockMode::RecordTime: {
		// Frames output so far, which also leaves out recording pauses
		obs_output_t *output = w.clockMode == ClockMode::StreamTime ? obs_frontend_get_streaming_output()
									    : obs_frontend_get_recording_output();
		QString text = QStringLiteral("--:--:--");
		struct obs_video_info ovi;
		if (output && obs_output_active(output) && obs_get_video_info(&ovi) && ovi.fps_num > 0)
			text = FormatDuration((qint64)obs_output_get_total_frames(output) * ovi.fps_den / ovi.fps_num);
		obs_output_release(output);
		return text;
	}
	case ClockMode::TimeOfDay:
	default:
		return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
	}
}

QString LabelText(const WidgetConfig &w, obs_source_t *resolved)
{
	if (!w.labelVisible || w.type == WidgetType::None)
		return QString();

	if (!w.labelText.isEmpty())
		return w.labelText;

	switch (w.type) {
	case WidgetType::Preview:
		return QString::fromUtf8(LG_TEXT("Renderer.Preview"));
	case WidgetType::Program:
		return QString::fromUtf8(LG_TEXT("Renderer.Program"));
	case WidgetType::Canvas:
		return w.canvasName.isEmpty() ? QString::fromUtf8(LG_TEXT("Renderer.Canvas")) : w.canvasName;
	case WidgetType::Scene:
	case WidgetType::Source:
	case WidgetType::AudioMeter:
		// Follow the live name so the label tracks renames
		return resolved ? QString::fromUtf8(obs_source_get_name(resolved)) : w.targetName();
	case WidgetType::Placeholder:
		return QString::fromUtf8(LG_TEXT("Renderer.Placeholder"));
	case WidgetType::Clock:
		return QString::fromUtf8(obs_module_text(ClockModeTextKey(w.clockMode)));
	case WidgetType::Text:
		return QString::fromUtf8(LG_TEXT("Renderer.Text"));
	default:
		return QString();
	}
}

} // namespace

// --- UI thread ---

MultiviewRenderer::MultiviewRenderer()
{
	char *path = obs_module_file("looking-glass.svg");
	if (path) {
		QFile file(QString::fromUtf8(path));
		if (file.open(QIODevice::ReadOnly))
			placeholderSvg_ = file.readAll();
		bfree(path);
	}
}

MultiviewRenderer::~MultiviewRenderer()
{
	// The owning window detaches the draw callback before destroying this
	swapFrame(nullptr);
	destroySafeAreaGeometry();
}

void MultiviewRenderer::setConfig(const MultiviewConfig &config)
{
	config_ = config;
	swapFrame(buildFrame(config_));
}

void MultiviewRenderer::refreshSources()
{
	// Only the UI thread replaces frame_, so it can be read here unlocked
	if (!frame_)
		return;

	bool changed = false;
	for (const Cell &cell : frame_->cells) {
		obs_source_t *source = ResolveSource(cell.config.widget);
		bool same = source ? obs_weak_source_references_source(cell.shown, source) : !cell.shown;
		obs_source_release(source);
		if (!same) {
			changed = true;
			break;
		}
	}

	if (changed)
		setConfig(config_);
}

void MultiviewRenderer::tick()
{
	// Only the UI thread replaces frame_ or reads clockText
	if (!frame_)
		return;

	for (Cell &cell : frame_->cells) {
		if (cell.config.widget.type != WidgetType::Clock || !cell.text)
			continue;
		QString text = ClockText(cell.config.widget);
		if (text == cell.clockText)
			continue;
		cell.clockText = text;
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "text", text.toUtf8().constData());
		obs_source_update(cell.text, settings);
		obs_data_release(settings);
	}
}

obs_source_t *MultiviewRenderer::sceneAt(int x, int y, int cx, int cy) const
{
	GridGeometry g = ComputeGeometry(config_.gridRows, config_.gridCols, config_.gridBorderWidth,
					 pixelRatio_.load(), cx, cy);
	if (!g.valid)
		return nullptr;

	for (const CellConfig &c : config_.cells) {
		if (c.widget.type == WidgetType::Scene && g.cellRect(c).contains(x, y))
			return ResolveSource(c.widget);
	}
	return nullptr;
}

void MultiviewRenderer::setPixelRatio(float ratio)
{
	pixelRatio_.store(ratio > 0.0f ? ratio : 1.0f);
}

std::unique_ptr<MultiviewRenderer::Frame> MultiviewRenderer::buildFrame(const MultiviewConfig &config) const
{
	auto frame = std::make_unique<Frame>();
	frame->rows = std::max(1, config.gridRows);
	frame->cols = std::max(1, config.gridCols);
	frame->borderWidth = config.gridBorderWidth;
	frame->lineColor = config.gridLineColor.rgba();
	frame->owners = GridModel::Ownership(frame->rows, frame->cols, config.cells);
	frame->cells.resize(config.cells.size());

	for (int i = 0; i < (int)config.cells.size(); i++) {
		Cell &cell = frame->cells[i];
		cell.config = config.cells[i];
		const WidgetConfig &w = cell.config.widget;

		obs_source_t *source = ResolveSource(w);
		if (source && w.type == WidgetType::AudioMeter) {
			cell.meter = std::make_unique<Meter>(source);
			cell.shown = obs_source_get_weak_source(source);
		} else if (source) {
			// OBS only renders and captures a source while something
			// declares it visible. Without this, a scene or source that is
			// in neither program nor preview would freeze in its cell, the
			// same reason OBS's own multiview and projectors hold it.
			obs_source_inc_showing(source);
			cell.shown = obs_source_get_weak_source(source);
		}

		if (w.type == WidgetType::Clock) {
			cell.clockText = ClockText(w);
			cell.text = CreateTextSource(cell.clockText, w.labelFont);
		} else if (w.type == WidgetType::Text) {
			cell.text = CreateTextSource(w.text, w.labelFont, w.textFile);
		}

		cell.label = CreateTextSource(LabelText(w, source), w.labelFont);
		cell.canvasName = w.canvasName.toStdString();
		obs_source_release(source);
	}

	return frame;
}

void MultiviewRenderer::swapFrame(std::unique_ptr<Frame> next)
{
	// The new Frame is fully built (and its sources shown) before the old one
	// is released, so a source shown by both never drops to a showing count of
	// zero and is not needlessly deactivated in between
	std::unique_ptr<Frame> old;
	{
		std::lock_guard<std::mutex> lock(frameMutex_);
		old = std::move(frame_);
		frame_ = std::move(next);
	}
	old.reset();
}

// --- Graphics thread ---

void MultiviewRenderer::Draw(void *data, uint32_t cx, uint32_t cy)
{
	static_cast<MultiviewRenderer *>(data)->render(cx, cy);
}

void MultiviewRenderer::render(uint32_t cx, uint32_t cy)
{
	std::lock_guard<std::mutex> lock(frameMutex_);
	Frame *frame = frame_.get();
	if (!frame || cx == 0 || cy == 0)
		return;

	const GridGeometry g =
		ComputeGeometry(frame->rows, frame->cols, frame->borderWidth, pixelRatio_.load(), (int)cx, (int)cy);
	if (!g.valid)
		return;

	LiveState::Sources live;
	LiveState::Acquire(live);

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(0, 0, (int)cx, (int)cy);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	drawGridLines(*frame, g.offsetX, g.offsetY, g.cellW, g.cellH, g.inset);

	for (Cell &cell : frame->cells) {
		CellRect rect = g.cellRect(cell.config);
		if (rect.w < 1 || rect.h < 1)
			continue;

		drawCell(cell, rect, live);
	}

	gs_projection_pop();
	gs_viewport_pop();
}

void MultiviewRenderer::drawGridLines(const Frame &frame, int offsetX, int offsetY, float cellW, float cellH, int inset)
{
	const int rows = frame.rows;
	const int cols = frame.cols;
	const int thick = 2 * inset;
	const int gridW = (int)std::lround(cols * cellW);
	const int gridH = (int)std::lround(rows * cellH);
	auto owner = [&](int r, int c) {
		return frame.owners[r * cols + c];
	};

	// Vertical lines: outer edges in full, interior lines only where the
	// cells on either side differ (merged cells have no line through them)
	for (int col = 0; col <= cols; col++) {
		int lineX = offsetX + (int)std::lround(col * cellW) - inset;

		if (col == 0 || col == cols) {
			FillRect(lineX, offsetY - inset, thick, gridH + thick, frame.lineColor);
			continue;
		}

		int start = -1;
		for (int row = 0; row <= rows; row++) {
			bool needLine = row < rows && owner(row, col - 1) != owner(row, col);
			if (needLine && start < 0) {
				start = row;
			} else if (!needLine && start >= 0) {
				int y0 = (int)std::lround(start * cellH);
				int y1 = (int)std::lround(row * cellH);
				FillRect(lineX, offsetY + y0 - inset, thick, (y1 - y0) + thick, frame.lineColor);
				start = -1;
			}
		}
	}

	// Horizontal lines, same rules
	for (int row = 0; row <= rows; row++) {
		int lineY = offsetY + (int)std::lround(row * cellH) - inset;

		if (row == 0 || row == rows) {
			FillRect(offsetX - inset, lineY, gridW + thick, thick, frame.lineColor);
			continue;
		}

		int start = -1;
		for (int col = 0; col <= cols; col++) {
			bool needLine = col < cols && owner(row - 1, col) != owner(row, col);
			if (needLine && start < 0) {
				start = col;
			} else if (!needLine && start >= 0) {
				int x0 = (int)std::lround(start * cellW);
				int x1 = (int)std::lround(col * cellW);
				FillRect(offsetX + x0 - inset, lineY, (x1 - x0) + thick, thick, frame.lineColor);
				start = -1;
			}
		}
	}
}

void MultiviewRenderer::drawCell(Cell &cell, const CellRect &rect, const LiveState::Sources &live)
{
	const WidgetConfig &w = cell.config.widget;
	obs_source_t *shown = nullptr;

	switch (w.type) {
	case WidgetType::Preview:
		// The studio-mode preview scene, or the program output outside
		// studio mode
		if (live.studioMode && live.preview)
			drawContent(live.preview, rect, w.safeRegion);
		else
			drawMainTexture(rect, w.safeRegion);
		break;
	case WidgetType::Program:
		drawMainTexture(rect, w.safeRegion);
		break;
	case WidgetType::Canvas:
		drawCanvas(cell, rect);
		break;
	case WidgetType::Scene:
	case WidgetType::Source:
		shown = obs_weak_source_get_source(cell.shown);
		if (shown)
			drawContent(shown, rect, w.safeRegion);
		break;
	case WidgetType::Placeholder:
		drawPlaceholder(cell, rect);
		break;
	case WidgetType::AudioMeter:
		drawMeter(cell, rect);
		break;
	case WidgetType::Clock:
		drawText(cell.text, rect, 0.45f, true);
		break;
	case WidgetType::Text:
		drawText(cell.text, rect, 0.125f, false);
		break;
	case WidgetType::None:
	default:
		return;
	}

	drawLabel(cell, rect);
	drawStatusBorder(cell, shown, rect, live);
	obs_source_release(shown);
}

// Content is drawn in its own viewport, scaled to fit the cell with its aspect
// ratio preserved. The viewport also clips anything a scene draws outside its
// own bounds, so it cannot bleed into neighbouring cells.
void MultiviewRenderer::drawContent(obs_source_t *source, const CellRect &rect, bool safeAreas)
{
	uint32_t baseW = obs_source_get_width(source);
	uint32_t baseH = obs_source_get_height(source);
	if (baseW == 0 || baseH == 0)
		return;

	FitRect fit = FitInside(baseW, baseH, rect.x, rect.y, rect.w, rect.h);

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(fit.x, fit.y, fit.w, fit.h);
	gs_ortho(0.0f, (float)baseW, 0.0f, (float)baseH, -100.0f, 100.0f);

	obs_source_video_render(source);
	if (safeAreas)
		drawSafeAreas(baseW, baseH);

	gs_projection_pop();
	gs_viewport_pop();
}

void MultiviewRenderer::drawMainTexture(const CellRect &rect, bool safeAreas)
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return;

	FitRect fit = FitInside(ovi.base_width, ovi.base_height, rect.x, rect.y, rect.w, rect.h);

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(fit.x, fit.y, fit.w, fit.h);
	gs_ortho(0.0f, (float)ovi.base_width, 0.0f, (float)ovi.base_height, -100.0f, 100.0f);

	obs_render_main_texture();
	if (safeAreas)
		drawSafeAreas(ovi.base_width, ovi.base_height);

	gs_projection_pop();
	gs_viewport_pop();
}

void MultiviewRenderer::drawCanvas(const Cell &cell, const CellRect &rect)
{
	obs_canvas_t *canvas = cell.canvasName.empty() ? obs_get_main_canvas()
						       : obs_get_canvas_by_name(cell.canvasName.c_str());
	if (!canvas) {
		// A named canvas that no longer exists falls back to the main output
		drawMainTexture(rect, cell.config.widget.safeRegion);
		return;
	}

	struct obs_video_info ovi;
	if (obs_canvas_get_video_info(canvas, &ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		FitRect fit = FitInside(ovi.base_width, ovi.base_height, rect.x, rect.y, rect.w, rect.h);

		gs_viewport_push();
		gs_projection_push();
		gs_set_viewport(fit.x, fit.y, fit.w, fit.h);
		gs_ortho(0.0f, (float)ovi.base_width, 0.0f, (float)ovi.base_height, -100.0f, 100.0f);

		obs_render_canvas_texture(canvas);
		if (cell.config.widget.safeRegion)
			drawSafeAreas(ovi.base_width, ovi.base_height);

		gs_projection_pop();
		gs_viewport_pop();
	}

	obs_canvas_release(canvas);
}

void MultiviewRenderer::drawLabel(Cell &cell, const CellRect &rect)
{
	if (!cell.label)
		return;

	uint32_t labelW = obs_source_get_width(cell.label);
	uint32_t labelH = obs_source_get_height(cell.label);
	if (labelW == 0 || labelH == 0)
		return;

	const WidgetConfig &w = cell.config.widget;

	// Size the text relative to the cell, as OBS's multiview does, adjusted
	// by the cell's label size. Cell sizes are in device pixels, so this
	// also holds on HiDPI screens. A label too long for the cell is shrunk
	// to fit its width rather than spilling into the neighbouring cells.
	float fontPx = (float)rect.h * kLabelFontToCellHeight * (float)w.labelSize / 100.0f;
	float scale = fontPx / (float)kLabelRasterSize;
	scale = std::min(scale, (float)rect.w * 0.9f / (float)labelW);

	int scaledW = std::max(1, (int)(labelW * scale));
	int scaledH = std::max(1, (int)(labelH * scale));

	// Spacing follows the text size actually drawn
	float textPx = (float)kLabelRasterSize * scale;
	int padding = std::max(1, (int)std::lround(textPx * 0.25f));
	int labelX;
	if (w.labelHAlign & Qt::AlignHCenter)
		labelX = (rect.w - scaledW) / 2;
	else if (w.labelHAlign & Qt::AlignRight)
		labelX = rect.w - scaledW - padding;
	else
		labelX = padding;

	int labelY;
	if (w.labelVAlign & Qt::AlignVCenter)
		labelY = (rect.h - scaledH) / 2;
	else if (w.labelVAlign & Qt::AlignBottom)
		labelY = rect.h - scaledH - padding;
	else
		labelY = padding;

	labelX += rect.x;
	labelY += rect.y;

	// Rounded backing, cached at the size it is drawn at
	if (w.labelBgColor.alpha() > 0) {
		int bgPad = std::max(1, (int)std::lround(textPx * 0.15f));
		int bgRadius = std::max(1, (int)std::lround(textPx * 0.2f));
		int bgW = scaledW + 2 * bgPad;
		int bgH = scaledH + 2 * bgPad;

		if (!cell.labelBg || cell.labelBgW != bgW || cell.labelBgH != bgH) {
			if (cell.labelBg)
				gs_texture_destroy(cell.labelBg);
			cell.labelBg = CreateRoundedRectTexture(bgW, bgH, bgRadius, w.labelBgColor);
			cell.labelBgW = bgW;
			cell.labelBgH = bgH;
		}

		if (cell.labelBg)
			DrawTexture(cell.labelBg, labelX - bgPad, labelY - bgPad, bgW, bgH);
	}

	gs_matrix_push();
	gs_matrix_translate3f((float)labelX, (float)labelY, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);
	obs_source_video_render(cell.label);
	gs_matrix_pop();
}

// Text scaled to fit the cell, its font at most maxFontFraction of the cell's
// height. Centered, or from the top-left corner.
void MultiviewRenderer::drawText(obs_source_t *text, const CellRect &rect, float maxFontFraction, bool centered)
{
	uint32_t textW = text ? obs_source_get_width(text) : 0;
	uint32_t textH = text ? obs_source_get_height(text) : 0;
	if (textW == 0 || textH == 0)
		return;

	const int pad = std::max(1, rect.h / 20);
	float scale = std::min({(float)(rect.w - 2 * pad) / textW, (float)(rect.h - 2 * pad) / textH,
				rect.h * maxFontFraction / kLabelRasterSize});
	if (scale <= 0.0f)
		return;

	int x = centered ? rect.x + (rect.w - (int)(textW * scale)) / 2 : rect.x + pad;
	int y = centered ? rect.y + (rect.h - (int)(textH * scale)) / 2 : rect.y + pad;
	gs_matrix_push();
	gs_matrix_translate3f((float)x, (float)y, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);
	obs_source_video_render(text);
	gs_matrix_pop();
}

// One bar per audio channel, green, yellow and red zones lit up to the current
// level, with a tick at the peak. Horizontal in wide cells, vertical in tall ones.
void MultiviewRenderer::drawMeter(const Cell &cell, const CellRect &rect)
{
	if (!cell.meter)
		return;

	float magnitude[MAX_AUDIO_CHANNELS];
	float peak[MAX_AUDIO_CHANNELS];
	{
		std::lock_guard<std::mutex> lock(cell.meter->mutex);
		bool stale = os_gettime_ns() - cell.meter->updated > kMeterStaleNs;
		for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
			magnitude[i] = stale ? kMeterMinDb : cell.meter->magnitude[i];
			peak[i] = stale ? kMeterMinDb : cell.meter->peak[i];
		}
	}

	const int channels = std::clamp((int)audio_output_get_channels(obs_get_audio()), 1, MAX_AUDIO_CHANNELS);
	const bool horizontal = rect.w >= rect.h;
	const int length = (int)((horizontal ? rect.w : rect.h) * 0.9f);
	const int across = (int)((horizontal ? rect.h : rect.w) * 0.6f);
	const int gap = std::max(1, across / (channels * 8));
	const int bar = std::max(1, (across - gap * (channels - 1)) / channels);
	const int alongStart = horizontal ? rect.x + (rect.w - length) / 2 : rect.y + (rect.h + length) / 2;
	const int acrossStart = horizontal ? rect.y + (rect.h - across) / 2 : rect.x + (rect.w - across) / 2;

	auto position = [](float db) {
		return std::isfinite(db) ? std::clamp((db - kMeterMinDb) / -kMeterMinDb, 0.0f, 1.0f) : 0.0f;
	};
	// Fills the part of channel ch between two positions (0-1) along the bar
	auto fill = [&](int ch, float from, float to, uint32_t color) {
		int a = (int)std::lround(from * length);
		int b = (int)std::lround(to * length);
		int c = acrossStart + ch * (bar + gap);
		if (horizontal)
			FillRect(alongStart + a, c, b - a, bar, color);
		else
			FillRect(c, alongStart - b, bar, b - a, color);
	};

	const float zones[4] = {0.0f, position(kMeterYellowDb), position(kMeterRedDb), 1.0f};
	for (int ch = 0; ch < channels; ch++) {
		float level = position(magnitude[ch]);
		for (int z = 0; z < 3; z++) {
			fill(ch, zones[z], zones[z + 1], kMeterDimColors[z]);
			if (level > zones[z])
				fill(ch, zones[z], std::min(level, zones[z + 1]), kMeterColors[z]);
		}
		float p = position(peak[ch]);
		if (p > 0.0f)
			fill(ch, std::max(0.0f, p - 0.01f), p, 0xFFFFFFFF);
	}
}

void MultiviewRenderer::drawPlaceholder(Cell &cell, const CellRect &rect)
{
	if (placeholderSvg_.isEmpty())
		return;

	// Half the cell's shorter side, rasterized at exactly that size
	int size = std::max(16, std::min(rect.w, rect.h) / 2);
	if (!cell.placeholder || cell.placeholderSize != size) {
		if (cell.placeholder)
			gs_texture_destroy(cell.placeholder);
		cell.placeholder = CreateSvgTexture(placeholderSvg_, size);
		cell.placeholderSize = size;
	}

	if (cell.placeholder)
		DrawTexture(cell.placeholder, rect.x + (rect.w - size) / 2, rect.y + (rect.h - size) / 2, size, size);
}

void MultiviewRenderer::drawStatusBorder(const Cell &cell, obs_source_t *shown, const CellRect &rect,
					 const LiveState::Sources &live)
{
	const WidgetConfig &w = cell.config.widget;
	if (!w.showStatus || w.type != WidgetType::Scene || !shown)
		return;

	// Red when on air in program, green when in the studio-mode preview
	uint32_t color = 0;
	if (shown == live.program)
		color = kProgramColor;
	else if (live.studioMode && shown == live.preview)
		color = kPreviewColor;
	if (!color)
		return;

	const int bw = std::max(2, (int)(std::min(rect.w, rect.h) * 0.015f));
	FillRect(rect.x, rect.y, rect.w, bw, color);
	FillRect(rect.x, rect.y + rect.h - bw, rect.w, bw, color);
	FillRect(rect.x, rect.y + bw, bw, rect.h - 2 * bw, color);
	FillRect(rect.x + rect.w - bw, rect.y + bw, bw, rect.h - 2 * bw, color);
}

// --- Safe areas ---

void MultiviewRenderer::initSafeAreaGeometry()
{
	// Normalized 0-1 vertex buffers matching OBS Studio's InitSafeAreas()
	// from display-helpers.hpp. Called from the draw callback, so the
	// graphics context is already active.
	auto box = [](float left, float top) {
		gs_render_start(true);
		gs_vertex2f(left, top);
		gs_vertex2f(left, 1 - top);
		gs_vertex2f(1 - left, 1 - top);
		gs_vertex2f(1 - left, top);
		gs_vertex2f(left, top);
		return gs_render_save();
	};
	auto line = [](float x1, float y1, float x2, float y2) {
		gs_render_start(true);
		gs_vertex2f(x1, y1);
		gs_vertex2f(x2, y2);
		return gs_render_save();
	};

	actionSafeVb_ = box(kActionSafePercent, kActionSafePercent);
	graphicsSafeVb_ = box(kGraphicsSafePercent, kGraphicsSafePercent);
	fourByThreeSafeVb_ = box(kFourByThreeSafePercent, kGraphicsSafePercent);
	leftLineVb_ = line(0.0f, 0.5f, kLineLength, 0.5f);
	topLineVb_ = line(0.5f, 0.0f, 0.5f, kLineLength);
	rightLineVb_ = line(1.0f, 0.5f, 1 - kLineLength, 0.5f);
}

void MultiviewRenderer::destroySafeAreaGeometry()
{
	if (!actionSafeVb_)
		return;

	obs_enter_graphics();
	gs_vertexbuffer_destroy(actionSafeVb_);
	gs_vertexbuffer_destroy(graphicsSafeVb_);
	gs_vertexbuffer_destroy(fourByThreeSafeVb_);
	gs_vertexbuffer_destroy(leftLineVb_);
	gs_vertexbuffer_destroy(topLineVb_);
	gs_vertexbuffer_destroy(rightLineVb_);
	obs_leave_graphics();

	actionSafeVb_ = nullptr;
	graphicsSafeVb_ = nullptr;
	fourByThreeSafeVb_ = nullptr;
	leftLineVb_ = nullptr;
	topLineVb_ = nullptr;
	rightLineVb_ = nullptr;
}

void MultiviewRenderer::drawSafeAreas(uint32_t baseW, uint32_t baseH)
{
	if (!actionSafeVb_)
		initSafeAreaGeometry();

	// Scale the normalized geometry up to the content's base size
	matrix4 transform;
	matrix4_identity(&transform);
	transform.x.x = (float)baseW;
	transform.y.y = (float)baseH;

	gs_matrix_push();
	gs_matrix_mul(&transform);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_effect_set_color(gs_effect_get_param_by_name(solid, "color"), kSafeAreaColor);

	gs_vertbuffer_t *buffers[] = {actionSafeVb_, graphicsSafeVb_, fourByThreeSafeVb_,
				      leftLineVb_,   topLineVb_,      rightLineVb_};
	while (gs_effect_loop(solid, "Solid")) {
		for (gs_vertbuffer_t *vb : buffers) {
			gs_load_vertexbuffer(vb);
			gs_draw(GS_LINESTRIP, 0, 0);
		}
	}

	gs_matrix_pop();
}
