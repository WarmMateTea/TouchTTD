/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_saveload_gui.cpp Saving and loading in the mobile layout: a list of saves with their company, date and
 * money, expandable rows with Load and Delete, and the save made when the player left the app pinned first.
 * See the round 4 design, section 9.
 */

#include "stdafx.h"
#include "mobile_saveload_gui.h"
#include "core/backup_type.hpp"
#include "mobile_gui.h"
#include "mobile_app.h"
#include "window_gui.h"
#include "window_func.h"
#include "querystring_gui.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "string_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "fios.h"
#include "fileio_func.h"
#include "error.h"
#include "openttd.h"
#include "company_base.h"
#include "company_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "settings_type.h"
#include "saveload/saveload_func.h"
#include "video/video_driver.hpp"
#include "widgets/mobile_widget.h"

#include <filesystem>

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

static constexpr int SAVE_ROW = 56; ///< Height of a save row, in GUI pixels at 1x.
static constexpr int SAVE_BUTTONS = 44; ///< Height of the Load and Delete row of an expanded save, in GUI pixels at 1x.

/**
 * Read a save's header: the player's company, the date and its money, without loading the game.
 * @param path Full path of the save.
 * @param[out] company Name of the first human company; empty if none.
 * @param[out] details "<date> · <money>".
 * @return \c false when the header couldn't be read.
 */
bool GetMobileSaveHeader(const std::string &path, std::string &company, std::string &details)
{
	company.clear();
	details.clear();
	_load_check_data.Clear();
	SaveOrLoad(path, SaveLoadOperation::Check, DetailedFileType::GameFile, Subdirectory::None, false);
	if (!_load_check_data.checkable || _load_check_data.HasErrors()) {
		_load_check_data.Clear();
		return false;
	}
	Money money = 0;
	bool has_money = false;
	for (auto &[id, c] : _load_check_data.companies) {
		if (c->is_ai) continue;
		if (c->name.empty()) {
			AutoRestoreBackup landscape_backup(_settings_game.game_creation.landscape, _load_check_data.landscape);
			company = GetString(c->name_1, c->name_2);
		} else {
			company = c->name;
		}
		money = c->money;
		has_money = true;
		break;
	}
	details = GetString(STR_JUST_DATE_LONG, _load_check_data.current_date);
	if (has_money) details += GetString(STR_MOBILE_ORDERS_SEPARATOR) + GetString(STR_JUST_CURRENCY_LONG, money);
	_load_check_data.Clear();
	return true;
}

/** A save file, with what its header says. */
struct MobileSaveFile {
	std::string path; ///< Full path.
	std::string name; ///< File name without extension.
	std::filesystem::file_time_type written{}; ///< When it was written.
	bool background = false; ///< The save made when the player left the app.
	bool autosave = false; ///< In the autosave folder.
	bool checked = false; ///< The header was read.
	std::string details; ///< "Company · date · money" from the header; empty when unknown.
};

/** Save or load, as a list of saves. */
struct MobileSaveLoadWindow : Window {
	SaveLoadOperation fop; ///< Saving or loading.
	bool autosaves = false; ///< Showing the autosave folder.
	bool by_name = false; ///< Sorted by name rather than newest first.
	std::vector<MobileSaveFile> files; ///< The list.
	int expanded = -1; ///< Row showing Load and Delete; -1 for none.
	QueryString name_editbox; ///< Name to save under.
	Scrollbar *vscroll = nullptr;

	MobileSaveLoadWindow(WindowDesc &desc, SaveLoadOperation fop) : Window(desc), fop(fop), name_editbox(64)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MSL_SCROLL);
		this->GetWidget<NWidgetStacked>(WID_MSL_SEL_SAVE)->SetDisplayedPlane(fop == SaveLoadOperation::Save ? 0 : SZSP_HORIZONTAL);
		this->querystrings[WID_MSL_NAME] = &this->name_editbox;
		this->name_editbox.ok_button = WID_MSL_SAVE;
		if (fop == SaveLoadOperation::Save) this->name_editbox.text.Assign(GetDefaultName());
		this->FinishInitNested(0);
		this->BuildList();
	}

	/** @return The company's name, the default name of a save. */
	static std::string GetDefaultName()
	{
		if (Company::IsValidID(_local_company)) return GetString(STR_COMPANY_NAME, _local_company);
		return GenerateDefaultSaveName();
	}

	void BuildList()
	{
		this->files.clear();
		std::error_code ec;
		auto add_dir = [&](Subdirectory dir, bool autosave) {
			for (const auto &entry : std::filesystem::directory_iterator(OTTD2FS(FioFindDirectory(dir)), ec)) {
				if (!entry.is_regular_file(ec) || entry.path().extension() != ".sav") continue;
				MobileSaveFile f;
				f.path = FS2OTTD(entry.path().native());
				f.name = FS2OTTD(entry.path().stem().native());
				f.written = entry.last_write_time(ec);
				f.autosave = autosave;
				f.background = autosave && f.name == "background";
				this->files.push_back(std::move(f));
			}
		};
		if (this->autosaves) {
			add_dir(Subdirectory::Autosave, true);
		} else {
			add_dir(Subdirectory::Save, false);
			/* Loading: the save made when the player left the app, pinned first. */
			if (this->fop == SaveLoadOperation::Load) {
				std::vector<MobileSaveFile> autos;
				std::swap(this->files, autos);
				add_dir(Subdirectory::Autosave, true);
				std::swap(this->files, autos);
				for (MobileSaveFile &f : autos) {
					if (f.background) this->files.insert(this->files.begin(), std::move(f));
				}
				this->autosave_count = autos.size();
			}
		}
		auto pinned_end = std::ranges::find_if(this->files, [](const MobileSaveFile &f) { return !f.background; });
		if (this->by_name) {
			std::sort(pinned_end, this->files.end(), [](const MobileSaveFile &a, const MobileSaveFile &b) { return StrNaturalCompare(a.name, b.name) < 0; });
		} else {
			std::sort(pinned_end, this->files.end(), [](const MobileSaveFile &a, const MobileSaveFile &b) { return a.written > b.written; });
		}
		/* A last row opens the autosaves (loading only). */
		this->vscroll->SetCount(this->files.size() + (this->HasAutosavesRow() ? 1 : 0));
		this->expanded = -1;
		/* FiosMakeSavegameName() saves into the folder the file list last browsed. */
		FileList list;
		list.BuildFileList(AbstractFileType::Savegame, SaveLoadOperation::Save, false);
	}

	size_t autosave_count = 0; ///< Number of autosaves, for the "Autosaves" row.

	/** @return Whether the list ends with a row opening the autosaves. */
	bool HasAutosavesRow() const
	{
		return this->fop == SaveLoadOperation::Load && !this->autosaves;
	}

	/**
	 * Read a save's header for its details, once.
	 * @param f The save.
	 */
	static void CheckFile(MobileSaveFile &f)
	{
		if (f.checked) return;
		f.checked = true;
		std::string company, details;
		if (!GetMobileSaveHeader(f.path, company, details)) return;
		f.details = company.empty() ? details : GetString(STR_MOBILE_SAVE_DETAILS, company, details);
	}

	/**
	 * "Saved 5 minutes ago".
	 * @param f The save.
	 * @return The text.
	 */
	static std::string GetAgeText(const MobileSaveFile &f)
	{
		auto minutes = std::chrono::duration_cast<std::chrono::minutes>(std::filesystem::file_time_type::clock::now() - f.written).count();
		minutes = std::max<int64_t>(minutes, 0);
		StringID str = minutes < 60 ? STR_MOBILE_SAVED_MINUTES_AGO : (minutes < 48 * 60 ? STR_MOBILE_SAVED_HOURS_AGO : STR_MOBILE_SAVED_DAYS_AGO);
		int64_t value = minutes < 60 ? minutes : (minutes < 48 * 60 ? minutes / 60 : minutes / (24 * 60));
		std::string text = GetString(str, value);
		if (f.autosave) text = GetString(STR_MOBILE_SAVE_AUTOMATICALLY, text);
		return text;
	}

	/**
	 * Height of a row.
	 * @param index The row.
	 * @return Its height.
	 */
	int GetRowHeight(int index) const
	{
		return ScaleGUITrad(SAVE_ROW) + (index == this->expanded ? ScaleGUITrad(SAVE_BUTTONS) : 0);
	}

	/**
	 * Find the row at a point of the list.
	 * @param y Window y.
	 * @param[out] row_top Top of the row (window coordinates).
	 * @return The row, or -1.
	 */
	int GetRowAt(int y, int &row_top) const
	{
		const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MSL_LIST);
		int top = nwid->pos_y + WidgetDimensions::scaled.framerect.top;
		int rows = static_cast<int>(this->files.size()) + (this->HasAutosavesRow() ? 1 : 0);
		for (int i = this->vscroll->GetPosition(); i < rows; i++) {
			int h = this->GetRowHeight(i);
			if (y >= top && y < top + h) {
				row_top = top;
				return i;
			}
			top += h;
		}
		return -1;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MSL_LIST:
				fill.height = resize.height = 1;
				size.height = 3 * ScaleGUITrad(SAVE_ROW);
				break;
			case WID_MSL_TITLE:
				size.height = std::max<uint>(size.height, GetCharacterHeight(FontSize::Normal));
				break;
		}
	}

	void OnResize() override
	{
		const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MSL_LIST);
		this->vscroll->SetCapacity(std::max(1, static_cast<int>(nwid->current_y) / ScaleGUITrad(SAVE_ROW)));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_MSL_SORT) return GetString(this->by_name ? STR_MOBILE_SAVE_SORT_NAME : STR_MOBILE_SAVE_SORT_NEWEST);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_MSL_TITLE) {
			StringID title = this->autosaves ? STR_MOBILE_SAVE_AUTOSAVES_TITLE : (this->fop == SaveLoadOperation::Save ? STR_MOBILE_SAVE_TITLE : STR_MOBILE_LOAD_TITLE);
			DrawMobileTitle(r, GetString(title));
			return;
		}
		if (widget != WID_MSL_LIST) return;

		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
		int line = GetCharacterHeight(FontSize::Normal);
		int rows = static_cast<int>(this->files.size()) + (this->HasAutosavesRow() ? 1 : 0);
		if (rows == 0) {
			DrawString(tr, STR_MOBILE_SAVE_EMPTY, TextColour::Black);
			return;
		}
		for (int i = this->vscroll->GetPosition(); i < rows && tr.top < tr.bottom; i++) {
			int h = this->GetRowHeight(i);
			Rect rr = tr.WithHeight(ScaleGUITrad(SAVE_ROW));
			if (i == static_cast<int>(this->files.size())) {
				/* The autosaves row. */
				DrawString(rr.left, rr.right, CentreBounds(rr.top, rr.bottom, line), GetString(STR_MOBILE_SAVE_AUTOSAVES_ROW, this->autosave_count), TextColour::Black);
				break;
			}
			MobileSaveFile &f = const_cast<MobileSaveFile &>(this->files[i]);
			CheckFile(f);
			int y = CentreBounds(rr.top, rr.bottom, 3 * line);
			int name_width = DrawString(rr.left, rr.right, y, f.background ? GetString(STR_MOBILE_SAVE_BACKGROUND_NAME) : f.name, TextColour::Black);
			if (f.background) DrawString(name_width + WidgetDimensions::scaled.hsep_wide, rr.right, y, STR_MOBILE_SAVE_LEFT_THE_APP, TextColour::DarkGreen);
			if (!f.details.empty()) DrawString(rr.left, rr.right, y + line, f.details, TextColour::Black);
			DrawString(rr.left, rr.right, y + 2 * line, GetAgeText(f), TextColour::DarkGreen);

			if (i == this->expanded) {
				Rect buttons = tr.WithY(rr.bottom + 1, rr.bottom + ScaleGUITrad(SAVE_BUTTONS)).Shrink(0, ScaleGUITrad(2));
				Rect del = buttons.WithWidth(ScaleGUITrad(80), _current_text_dir != TD_RTL);
				Rect load = buttons.Indent(del.Width() + ScaleGUITrad(4), _current_text_dir != TD_RTL);
				DrawFrameRect(load, Colours::Green, {});
				DrawString(load.left, load.right, CentreBounds(load.top, load.bottom, line), STR_MOBILE_SAVE_LOAD, TextColour::White, AlignmentH::Centre);
				DrawFrameRect(del, Colours::Red, {});
				DrawString(del.left, del.right, CentreBounds(del.top, del.bottom, line), STR_MOBILE_SAVE_DELETE, TextColour::White, AlignmentH::Centre);
			}
			GfxFillRect(tr.left, tr.top + h - 1, tr.right, tr.top + h - 1, GetColourGradient(Colours::Grey, Shade::Dark));
			tr.top += h;
		}
	}

	static inline std::string pending_path; ///< File the confirmation is about.

	/** Load the pending file. */
	static void LoadPending()
	{
		_file_to_saveload.name = pending_path;
		auto [ft, _] = FiosGetSavegameListCallback(SaveLoadOperation::Load, pending_path, ".sav");
		_file_to_saveload.SetMode(ft, SaveLoadOperation::Load);
		_switch_mode = SwitchMode::LoadGame;
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MSL_CLOSE:
				if (this->autosaves) {
					this->autosaves = false;
					this->BuildList();
					this->SetDirty();
				} else {
					this->Close();
				}
				break;

			case WID_MSL_SORT: {
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_SAVE_SORT_NEWEST, 0));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_SAVE_SORT_NAME, 1));
				ShowDropDownList(this, std::move(list), this->by_name ? 1 : 0, WID_MSL_SORT);
				break;
			}

			case WID_MSL_SAVE: {
				std::string name{this->name_editbox.text.GetText()};
				if (name.empty()) break;
				_file_to_saveload.name = FiosMakeSavegameName(name);
				if (FioCheckFileExists(_file_to_saveload.name, Subdirectory::Save)) {
					ShowQuery(GetEncodedString(STR_MOBILE_SAVE_OVERWRITE_TITLE, name), GetEncodedString(STR_MOBILE_SAVE_OVERWRITE_TEXT), this, [](Window *w, bool confirmed) {
						if (!confirmed) return;
						_switch_mode = SwitchMode::SaveGame;
						if (w != nullptr) w->Close();
					}, false, STR_MOBILE_SAVE_OVERWRITE_VERB);
				} else {
					_switch_mode = SwitchMode::SaveGame;
					this->Close();
				}
				break;
			}

			case WID_MSL_LIST: {
				int row_top = 0;
				int row = this->GetRowAt(pt.y, row_top);
				if (row < 0) break;
				if (row == static_cast<int>(this->files.size())) {
					this->autosaves = true;
					this->BuildList();
					this->SetDirty();
					break;
				}
				const MobileSaveFile &f = this->files[row];
				if (this->fop == SaveLoadOperation::Save) {
					/* Saving: the name goes into the field; Save then asks before overwriting. */
					this->name_editbox.text.Assign(f.name);
					this->SetWidgetDirty(WID_MSL_NAME);
					break;
				}
				if (row != this->expanded) {
					this->expanded = row;
					this->SetDirty();
					break;
				}
				/* A tap on the expanded row: on its buttons? */
				if (pt.y < row_top + ScaleGUITrad(SAVE_ROW)) {
					this->expanded = -1;
					this->SetDirty();
					break;
				}
				Rect list = this->GetWidget<NWidgetBase>(WID_MSL_LIST)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				bool on_delete = list.WithWidth(ScaleGUITrad(80), _current_text_dir != TD_RTL).Contains({pt.x, list.top});
				pending_path = f.path;
				if (on_delete) {
					ShowQuery(GetEncodedString(STR_MOBILE_SAVE_DELETE_TITLE, f.background ? GetString(STR_MOBILE_SAVE_BACKGROUND_NAME) : f.name), GetEncodedString(STR_MOBILE_SAVE_DELETE_TEXT), this, [](Window *w, bool confirmed) {
						if (!confirmed) return;
						if (!FioRemove(pending_path)) {
							ShowErrorMessage(GetEncodedString(STR_ERROR_UNABLE_TO_DELETE_FILE), {}, WarningLevel::Error);
						} else if (w != nullptr) {
							static_cast<MobileSaveLoadWindow *>(w)->BuildList();
							w->SetDirty();
						}
					}, false, STR_MOBILE_SAVE_DELETE_VERB);
				} else if (_game_mode == GameMode::Normal) {
					ShowQuery(GetEncodedString(STR_MOBILE_SAVE_LOAD_OVER_TITLE), GetEncodedString(STR_MOBILE_SAVE_LOAD_OVER_TEXT), this, [](Window *, bool confirmed) {
						if (confirmed) LoadPending();
					}, false, STR_MOBILE_SAVE_LOAD_VERB);
				} else {
					LoadPending();
				}
				break;
			}
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_MSL_SORT) return;
		this->by_name = index == 1;
		this->BuildList();
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_saveload_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MSL_TITLE), SetMinimalSize(0, 40), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MSL_CLOSE), SetMinimalSize(40, 40), SetStringTip(STR_MOBILE_CROSS, STR_TOOLTIP_CLOSE_WINDOW),
	EndContainer(),
	NWidget(NWID_SELECTION, Colours::Invalid, WID_MSL_SEL_SAVE),
		NWidget(WWT_PANEL, Colours::Grey),
			NWidget(NWID_HORIZONTAL), SetPadding(4), SetPIP(0, 4, 0),
				NWidget(WWT_EDITBOX, Colours::Grey, WID_MSL_NAME), SetMinimalSize(100, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_SAVELOAD_OSKTITLE, STR_SAVELOAD_EDITBOX_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, Colours::Green, WID_MSL_SAVE), SetMinimalSize(75, 40), SetStringTip(STR_MOBILE_SAVE_SAVE, STR_SAVELOAD_SAVE_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MSL_LIST), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MSL_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MSL_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_DROPDOWN, Colours::Grey, WID_MSL_SORT), SetMinimalSize(0, 32), SetFill(1, 0), SetResize(1, 0), SetPadding(4), SetToolTip(STR_TOOLTIP_SORT_CRITERIA),
	EndContainer(),
};

static WindowDesc _mobile_saveload_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::SaveLoad, WindowClass::None,
	{},
	_nested_mobile_saveload_widgets
);

/**
 * Show the save or load list: in the sheet slot in a game, full screen at the main menu.
 * @param fop Save or load.
 */
void ShowMobileSaveLoad(SaveLoadOperation fop)
{
	CloseWindowById(WindowClass::SaveLoad, 0);
	Window *w = new MobileSaveLoadWindow(_mobile_saveload_desc, fop);
	if (IsMobileLayout()) {
		MobileShowInSheet(w);
		return;
	}
	Rect safe = VideoDriver::GetInstance() != nullptr ? VideoDriver::GetInstance()->GetSafeArea() : Rect{0, 0, _screen.width - 1, _screen.height - 1};
	ResizeWindow(w, safe.Width() - w->width, safe.Height() - w->height, false);
	w->left = safe.left;
	w->top = safe.top;
	w->SetDirty();
}
