/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_toolbar.h A construction toolbar of the mobile layout, shared by the landscape, water and airport
 * toolbars (round 5 design, batch 9). The rail and road toolbars predate it and keep their own copies.
 */

#ifndef MOBILE_TOOLBAR_H
#define MOBILE_TOOLBAR_H

#include "window_gui.h"
#include "mobile_build.h"
#include "widgets/mobile_widget.h"

/**
 * A construction toolbar of the mobile layout: in landscape one bar along the bottom of the map, in portrait two rows
 * replacing the dock. The status line, Turn, Cancel and Build/Undo/Show tools work as in the rail toolbar; the tools
 * are the derived toolbar's own buttons.
 */
struct MobileToolbarWindow : Window {
	const bool landscape; ///< One bar (landscape) rather than two rows (portrait).
	bool tools_hidden = false; ///< A sheet hides the tools and nothing is in progress.
	bool picker_was_open = false; ///< The picker was open at the last update (its hint changes with it).
	WidgetID last_tool = INVALID_WIDGET; ///< The tool selected last.

	MobileToolbarWindow(WindowDesc &desc, bool landscape) : Window(desc), landscape(landscape) {}

	void Close(int data = 0) override;
	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override;
	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, int window_number) override;
	std::string GetWidgetString(WidgetID widget, StringID stringid) const override;

	/** @return Whether a picker of this toolbar fills the sheet slot (the toolbar then shows only its status and Build). */
	virtual bool IsPickerOpen() const { return false; }

	/** @return The hint for the selected tool, shown while nothing is in review. */
	virtual StringID GetToolHint() const;

	/** @return The tool whose kind is Demolish, if any. */
	virtual WidgetID GetDemolishTool() const { return INVALID_WIDGET; }

	/** Open this toolbar again in the layout of the new orientation (closes this window). */
	virtual void Recreate() = 0;

	void UpdateMobileHint();
	void UpdateMobileWidgets();
	void DrawStatus(const Rect &r) const;
	bool OnMobileClick(WidgetID widget);
};

Window *GetMobileToolbar();

/** One tool button of a mobile toolbar. */
#define MOBILE_TOOLBAR_TOOL(widget, sprite, tip) NWidget(WWT_IMGBTN, Colours::Grey, widget), SetMinimalSize(40, 40), SetSpriteTip(sprite, tip)

/** The status line, Turn, Cancel and Build (or Undo, or Show tools) of a mobile toolbar. */
#define MOBILE_TOOLBAR_CONFIRM_PARTS \
	NWidget(WWT_INSET, Colours::Grey, WID_MTB_STATUS), SetMinimalSize(120, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MTB_SEL_ROTATE), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTB_ROTATE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_BUILD_ROTATE, STR_MOBILE_BUILD_ROTATE_TOOLTIP), \
	EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MTB_SEL_CANCEL), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTB_CANCEL), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_MOBILE_BUILD_CANCEL_TOOLTIP), \
	EndContainer(), \
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MTB_SEL_CONFIRM), \
		NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MTB_CONFIRM), SetMinimalSize(56, 40), SetStringTip(STR_MOBILE_BUILD_CONFIRM_BUILD, STR_MOBILE_BUILD_CONFIRM_TOOLTIP), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTB_UNDO), SetMinimalSize(100, 40), SetStringTip(STR_MOBILE_BUILD_UNDO, STR_MOBILE_BUILD_UNDO_TOOLTIP), \
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MTB_SHOW_TOOLS), SetMinimalSize(100, 40), SetStringTip(STR_MOBILE_BUILD_SHOW_TOOLS, STR_MOBILE_BUILD_SHOW_TOOLS_TOOLTIP), \
	EndContainer()

/** Start of a landscape mobile toolbar, up to its tools (the category button first). */
#define MOBILE_TOOLBAR_LANDSCAPE_BEGIN(category_sprite) \
	NWidget(WWT_PANEL, Colours::Grey), \
		NWidget(NWID_HORIZONTAL), SetPadding(3, 4, 3, 4), SetPIP(0, 4, 0), \
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MTB_SEL_TOOLS), \
				NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0), \
					MOBILE_TOOLBAR_TOOL(WID_MTB_CATEGORY, category_sprite, STR_MOBILE_TOOLBAR_CATEGORY_TOOLTIP), \
					NWidget(NWID_SPACER), SetMinimalSize(0, 0)

/** End of a landscape mobile toolbar, after its tools. */
#define MOBILE_TOOLBAR_LANDSCAPE_END \
				EndContainer(), \
			EndContainer(), \
			MOBILE_TOOLBAR_CONFIRM_PARTS, \
		EndContainer(), \
		NWidget(WWT_EMPTY, Colours::Invalid, WID_MTB_GAP_BOTTOM), SetFill(1, 0), SetResize(1, 0), \
	EndContainer()

/** Start of a portrait mobile toolbar: row A (Exit, status, Cancel, Build), then row B up to its tools. */
#define MOBILE_TOOLBAR_PORTRAIT_BEGIN(category_sprite) \
	NWidget(WWT_PANEL, Colours::Grey), \
		NWidget(NWID_VERTICAL), SetPadding(4), SetPIP(0, 4, 0), \
			NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0), \
				NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MTB_EXIT), SetMinimalSize(56, 40), SetStringTip(STR_MOBILE_BUILD_EXIT, STR_MOBILE_BUILD_EXIT_TOOLTIP), \
				MOBILE_TOOLBAR_CONFIRM_PARTS, \
			EndContainer(), \
			NWidget(NWID_SELECTION, Colours::Invalid, WID_MTB_SEL_TOOLS), \
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), \
					MOBILE_TOOLBAR_TOOL(WID_MTB_CATEGORY, category_sprite, STR_MOBILE_TOOLBAR_CATEGORY_TOOLTIP), SetFill(1, 0), SetResize(1, 0)

/** End of a portrait mobile toolbar, after its tools (each with SetFill(1, 0), SetResize(1, 0)). */
#define MOBILE_TOOLBAR_PORTRAIT_END \
				EndContainer(), \
			EndContainer(), \
		EndContainer(), \
		NWidget(WWT_EMPTY, Colours::Invalid, WID_MTB_GAP_BOTTOM), SetFill(1, 0), SetResize(1, 0), \
	EndContainer()

#endif /* MOBILE_TOOLBAR_H */
