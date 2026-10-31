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

// What a multiview cell can be filled with, independent of its label and
// overlay settings. Used for drag & drop and for retargeting a cell. Free of
// libobs so it can be unit tested on its own.

#include <QByteArray>
#include <QString>

#include "multiview-config.hpp"

struct ContentRef {
	WidgetType type = WidgetType::None;
	QString name; // Scene or source name; for Canvas the canvas name ("" = main canvas)
	QString uuid; // Scene or source UUID, may be empty
};

namespace ContentRefs {

QString MimeType();

// Compact JSON
QByteArray Encode(const ContentRef &ref);

// False on malformed data or an unknown type; *out is untouched then
bool Decode(const QByteArray &data, ContentRef *out);

// Points a widget at new content while keeping its label, overlay and style
// settings. Switching into Scene from another type enables the tally; the
// tally is always off for non-Scene types.
void Apply(WidgetConfig &w, const ContentRef &ref);

ContentRef FromWidget(const WidgetConfig &w);

} // namespace ContentRefs
