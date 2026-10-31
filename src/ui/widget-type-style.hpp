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

#include <QColor>
#include <QString>

#include "../core/multiview-config.hpp"
#include "../plugin.hpp"

// Colors and names shared by every place the editor shows a widget type

inline QColor WidgetTypeColor(WidgetType type)
{
	switch (type) {
	case WidgetType::Program:
		return QColor(0xe5, 0x48, 0x4d);
	case WidgetType::Preview:
		return QColor(0x30, 0xa4, 0x6c);
	case WidgetType::Scene:
		return QColor(0x6e, 0x7b, 0xd9);
	case WidgetType::Source:
		return QColor(0xd0, 0x88, 0x3a);
	case WidgetType::Canvas:
		return QColor(0x3a, 0xa7, 0xc9);
	case WidgetType::Placeholder:
	case WidgetType::None:
	default:
		return QColor(0x8a, 0x8f, 0xa0);
	}
}

inline QString WidgetTypeDisplayName(WidgetType type)
{
	switch (type) {
	case WidgetType::Preview:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypePreview"));
	case WidgetType::Program:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypeProgram"));
	case WidgetType::Canvas:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypeCanvas"));
	case WidgetType::Scene:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypeScene"));
	case WidgetType::Source:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypeSource"));
	case WidgetType::Placeholder:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypePlaceholder"));
	case WidgetType::None:
	default:
		return QString::fromUtf8(LG_TEXT("CellDialog.TypeNone"));
	}
}
