/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_toolbar.cpp The construction toolbar of the mobile layout shared by the landscape, water and airport toolbars. */

#include "stdafx.h"
#include "mobile_toolbar.h"
#include "mobile_gui.h"
#include "window_func.h"
#include "tilehighlight_func.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "zoom_func.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

void MobileToolbarWindow::Close(int data)
{
	this->Window::Close(data);
	/* Give the dock its place back. */
	RelocateMobileWindows();
}

void MobileToolbarWindow::UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize)
{
	/* The toolbar reaches the bottom edge of the screen; this spacer fills the inset there. */
	if (widget == WID_MTB_GAP_BOTTOM) size = {0, static_cast<uint>(GetMobileEdgeGaps().bottom)};
}

Point MobileToolbarWindow::OnInitialPosition([[maybe_unused]] int16_t sm_width, int16_t sm_height, [[maybe_unused]] int window_number)
{
	/* The mobile layout pass docks it. */
	return {0, _screen.height - sm_height};
}

std::string MobileToolbarWindow::GetWidgetString(WidgetID widget, StringID stringid) const
{
	if (widget == WID_MTB_UNDO) {
		Money cost = GetMobileBuildUndoCost();
		return cost < 0 ? GetString(STR_MOBILE_BUILD_UNDO_REFUND, -cost) : GetString(STR_MOBILE_BUILD_UNDO, cost);
	}
	return this->Window::GetWidgetString(widget, stringid);
}

StringID MobileToolbarWindow::GetToolHint() const
{
	return STR_MOBILE_BUILD_HINT_PICK_TOOL;
}

/** Show the hint of the selected tool in the status line. */
void MobileToolbarWindow::UpdateMobileHint()
{
	MobileBuildSetToolHint(_thd.GetCallbackWnd() == this || this->IsPickerOpen() ? this->GetToolHint() : STR_MOBILE_BUILD_HINT_PICK_TOOL);
}

/** Update the status line, Cancel, Build/Undo and Turn to the state of the review. */
void MobileToolbarWindow::UpdateMobileWidgets()
{
	MobileBuildState state = GetMobileBuildState();
	bool picker_open = this->IsPickerOpen();
	if (picker_open != this->picker_was_open) {
		/* The picker opened or closed: its hint comes or goes. */
		this->picker_was_open = picker_open;
		this->UpdateMobileHint();
	}
	/* While a picker fills the sheet slot, the toolbar shows only its status line, Cancel and Build. */
	bool picking = picker_open;
	/* In landscape the tools don't fit beside an open sheet either. */
	if (this->landscape && IsMobileSheetOpen()) picking = true;
	/* With no tool and nothing in review, offer to bring the tools back rather than a dead Cancel and Build. */
	bool own_tool = _thd.GetCallbackWnd() == this;
	this->tools_hidden = picking && state == MobileBuildState::Idle && !own_tool && !picker_open;
	bool rotate = state == MobileBuildState::Pending && MobileBuildCanRotate();

	bool reinit = false;
	auto set_plane = [&](WidgetID widget, int plane) {
		NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(widget);
		if (sel->shown_plane == plane) return;
		sel->SetDisplayedPlane(plane);
		reinit = true;
	};
	/* Portrait stacks the tool row under row A: hidden, it must still resize horizontally (SZSP_HORIZONTAL). */
	set_plane(WID_MTB_SEL_TOOLS, picking ? (this->landscape ? SZSP_NONE : SZSP_HORIZONTAL) : 0);
	set_plane(WID_MTB_SEL_CONFIRM, this->tools_hidden ? 2 : (MobileBuildHasUndo() ? 1 : 0));
	set_plane(WID_MTB_SEL_CANCEL, this->tools_hidden ? SZSP_NONE : 0);
	set_plane(WID_MTB_SEL_ROTATE, rotate ? 0 : SZSP_NONE);

	MobileBuildKind kind = MobileBuildKind::Build;
	if (state == MobileBuildState::Pending) {
		kind = GetMobileBuildKind();
	} else if (own_tool && this->last_tool == this->GetDemolishTool() && this->last_tool != INVALID_WIDGET) {
		kind = MobileBuildKind::Demolish;
	}
	NWidgetCore *confirm = this->GetWidget<NWidgetCore>(WID_MTB_CONFIRM);
	confirm->colour = !MobileBuildCanConfirm() ? Colours::Grey : ((kind == MobileBuildKind::Build || kind == MobileBuildKind::Change) ? Colours::Green : Colours::Red);
	static const StringID labels[] = {STR_MOBILE_BUILD_CONFIRM_BUILD, STR_MOBILE_BUILD_CONFIRM_REMOVE, STR_MOBILE_BUILD_CONFIRM_DEMOLISH, STR_MOBILE_BUILD_CONFIRM_CONVERT, STR_MOBILE_BUILD_CONFIRM_CHANGE};
	confirm->SetString(labels[to_underlying(kind)]);
	this->SetWidgetDisabledState(WID_MTB_CONFIRM, !MobileBuildCanConfirm());
	/* Cancel drops the selection in review, or else the selected tool. */
	this->SetWidgetDisabledState(WID_MTB_CANCEL, !MobileBuildCanCancel() && !own_tool && !picker_open);

	if (reinit) {
		this->ReInit();
		RelocateMobileWindows();
	}
	this->SetDirty();
}

/**
 * Draw the status line: the hint, the selection's cost, or an error after a warning sign.
 * @param r The status widget.
 */
void MobileToolbarWindow::DrawStatus(const Rect &r) const
{
	TextColour colour;
	std::string status = GetMobileBuildStatus(colour);
	if (this->tools_hidden) {
		colour = TextColour::Black;
		status = GetString(STR_MOBILE_BUILD_TOOLS_HIDDEN);
	}
	Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
	if (colour == TextColour::Red) {
		Dimension d = GetSpriteSize(SPR_WARNING_SIGN);
		bool rtl = _current_text_dir == TD_RTL;
		DrawSprite(SPR_WARNING_SIGN, PAL_NONE, rtl ? tr.right - d.width + 1 : tr.left, CentreBounds(r.top, r.bottom, d.height));
		tr = tr.Indent(d.width + WidgetDimensions::scaled.hsep_normal, rtl);
	}
	int lines_height = std::min<int>(tr.Height(), GetCharacterHeight(FontSize::Normal) * 2);
	tr.top = CentreBounds(r.top, r.bottom, std::min(lines_height, GetStringHeight(status, tr.Width())));
	DrawStringMultiLineWithClipping(tr.WithHeight(lines_height), status, colour);
}

/**
 * A click on one of the toolbar's own buttons.
 * @param widget The widget.
 * @return \c true when handled.
 */
bool MobileToolbarWindow::OnMobileClick(WidgetID widget)
{
	switch (widget) {
		case WID_MTB_CATEGORY: ShowMobileBuildFlyoutForToolbar(this, widget); return true;
		case WID_MTB_CANCEL:
			if (MobileBuildCanCancel()) {
				MobileBuildCancel();
			} else if (_thd.GetCallbackWnd() == this || this->IsPickerOpen()) {
				ResetObjectToPlace();
			}
			return true;
		case WID_MTB_CONFIRM: MobileBuildConfirm(); return true;
		case WID_MTB_UNDO: MobileBuildUndo(); return true;
		case WID_MTB_ROTATE: MobileBuildRotateDepot(); return true;
		case WID_MTB_SHOW_TOOLS: CloseMobileSheetPublic(); return true;
		case WID_MTB_EXIT: this->Close(); return true;
		case WID_MTB_STATUS: return true;
		default: return false;
	}
}

/**
 * The open construction toolbar built on #MobileToolbarWindow, if any.
 * @return The toolbar, or \c nullptr.
 */
Window *GetMobileToolbar()
{
	for (WindowClass cls : {WindowClass::BuildToolbar, WindowClass::ScenarioGenerateLandscape}) {
		for (Window *w : Window::Iterate()) {
			if (w->window_class == cls && dynamic_cast<MobileToolbarWindow *>(w) != nullptr) return w;
		}
	}
	return nullptr;
}
