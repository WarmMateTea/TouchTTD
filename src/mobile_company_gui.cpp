/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_company_gui.cpp The company's windows in the mobile layout: finances and the loan (round 5 design,
 * batch 6).
 */

#include "stdafx.h"
#include "mobile_company_gui.h"
#include "mobile_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_type.h"
#include "misc_cmd.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "zoom_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "settings_type.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
#include "timer/timer_game_economy.h"
#include "widgets/mobile_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static constexpr int GROUP_ROW = 32; ///< Height of a group row (and the profit row), in GUI pixels at 1x.
static constexpr int DETAIL_ROW = 24; ///< Height of a detail row, in GUI pixels at 1x.
static constexpr int VALUE_COLUMN = 76; ///< Width of a year's column, in GUI pixels at 1x.

/** A group of the finances table, and the desktop's categories under it. */
struct FinancesGroup {
	StringID name; ///< Its name.
	std::initializer_list<ExpensesType> items; ///< The categories added up.
};

/** The groups: Income, Running costs (with property maintenance and loan interest), Construction, New vehicles, Other. */
static const FinancesGroup _finances_groups[] = {
	{STR_MOBILE_FINANCES_INCOME, {ExpensesType::TrainRevenue, ExpensesType::RoadVehRevenue, ExpensesType::AircraftRevenue, ExpensesType::ShipRevenue}},
	{STR_MOBILE_FINANCES_RUNNING, {ExpensesType::TrainRun, ExpensesType::RoadVehRun, ExpensesType::AircraftRun, ExpensesType::ShipRun, ExpensesType::Property, ExpensesType::LoanInterest}},
	{STR_MOBILE_FINANCES_CONSTRUCTION, {ExpensesType::Construction}},
	{STR_MOBILE_FINANCES_NEW_VEHICLES, {ExpensesType::NewVehicles}},
	{STR_MOBILE_FINANCES_OTHER, {ExpensesType::Other}},
};

/** Entries of the Borrow and Repay dropdowns. */
enum LoanEntry : int { LOAN_ONE_STEP, LOAN_FIVE_STEPS, LOAN_ALL };

/** The company's finances: this year so far against another year, by group, and the loan. */
struct MobileFinancesWindow : Window {
	int compare = 1; ///< Year to compare with: 1 is last year, 2 the year before.
	std::array<bool, std::size(_finances_groups)> expanded{}; ///< Groups showing their categories.
	Scrollbar *vscroll = nullptr;

	/** One line of the table. */
	struct Row {
		int group; ///< The group.
		int item; ///< Category within the group; -1 for the group row, -2 for the profit row.
	};

	MobileFinancesWindow(WindowDesc &desc, CompanyID company) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_MFIN_SCROLL);
		this->FinishInitNested(company);
		this->owner = company;
		this->UpdateRows();
	}

	/** @return The company. */
	const Company *GetCompany() const
	{
		return Company::GetIfValid(static_cast<CompanyID>(this->window_number));
	}

	/**
	 * A category's value in a year.
	 * @param period 0 for this year, 1 last year, 2 the year before.
	 * @param type The category.
	 * @return The value: income positive, costs negative.
	 */
	Money GetValue(int period, ExpensesType type) const
	{
		const Company *c = this->GetCompany();
		/* The game keeps costs positive and income negative; the table shows income as gains. */
		return c == nullptr ? Money{0} : -c->yearly_expenses[period][type];
	}

	/**
	 * A group's value in a year.
	 * @param period The year.
	 * @param group The group.
	 * @return The sum of its categories.
	 */
	Money GetGroupValue(int period, int group) const
	{
		Money sum = 0;
		for (ExpensesType type : _finances_groups[group].items) sum += this->GetValue(period, type);
		return sum;
	}

	/** @return The lines shown: group rows, the categories of expanded groups that aren't zero, and the profit row. */
	std::vector<Row> GetRows() const
	{
		std::vector<Row> rows;
		for (int g = 0; g < static_cast<int>(std::size(_finances_groups)); g++) {
			rows.push_back({g, -1});
			if (!this->expanded[g]) continue;
			int i = 0;
			for (ExpensesType type : _finances_groups[g].items) {
				if (this->GetValue(0, type) != 0 || this->GetValue(this->compare, type) != 0) rows.push_back({g, i});
				i++;
			}
		}
		rows.push_back({0, -2});
		return rows;
	}

	/**
	 * Height of a line.
	 * @param row The line.
	 * @return Its height in pixels.
	 */
	static int GetRowHeight(const Row &row)
	{
		return ScaleGUITrad(row.item >= 0 ? DETAIL_ROW : GROUP_ROW);
	}

	/** The table scrolls by pixel: its content is as tall as its lines. */
	void UpdateRows()
	{
		int height = 0;
		for (const Row &row : this->GetRows()) height += GetRowHeight(row);
		this->vscroll->SetCount(height);
		this->vscroll->SetStepSize(ScaleGUITrad(DETAIL_ROW));
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_MFIN_TABLE:
				fill.height = resize.height = 1;
				size.height = 4 * ScaleGUITrad(GROUP_ROW);
				break;
			case WID_MFIN_LOAN:
				size.height = 2 * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.framerect.Vertical();
				break;
			case WID_MFIN_YEAR:
			case WID_MFIN_COMPARE:
				size.width = std::max<uint>(size.width, ScaleGUITrad(VALUE_COLUMN));
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_MFIN_TABLE);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_MFIN_YEAR: return GetString(STR_MOBILE_FINANCES_SO_FAR, TimerGameEconomy::year);
			case WID_MFIN_COMPARE: return GetString(STR_JUST_INT, TimerGameEconomy::year - this->compare);
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	/**
	 * Draw a value right-aligned in a column; negative values in red with a minus.
	 * @param left Left of the column.
	 * @param right Right of the column.
	 * @param y Top of the text.
	 * @param value The value.
	 * @param bold Draw the profit row's value.
	 */
	static void DrawValue(int left, int right, int y, Money value, bool bold)
	{
		if (value == 0) {
			DrawString(left, right, y, STR_MOBILE_FINANCES_DASH, TextColour::Black, AlignmentH::End);
			return;
		}
		std::string text = value < 0 ? GetString(STR_MOBILE_FINANCES_NEGATIVE, -value) : GetString(STR_JUST_CURRENCY_LONG, value);
		DrawString(left, right, y, text, value < 0 ? TextColour::Red : (bold ? TextColour::DarkGreen : TextColour::Black), AlignmentH::End);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Company *c = this->GetCompany();
		if (c == nullptr) return;
		int line = GetCharacterHeight(FontSize::Normal);
		bool rtl = _current_text_dir == TD_RTL;

		switch (widget) {
			case WID_MFIN_TABLE: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				/* Two value columns at the right, each as wide as the year labels above them. */
				int col = ScaleGUITrad(VALUE_COLUMN);
				Rect second = tr.WithWidth(col, !rtl);
				Rect first = tr.Indent(col + WidgetDimensions::scaled.hsep_wide, !rtl).WithWidth(col, !rtl);
				Rect names = tr.Indent(2 * (col + WidgetDimensions::scaled.hsep_wide), !rtl);

				int y = r.top + WidgetDimensions::scaled.framerect.top - this->vscroll->GetPosition();
				for (const Row &row : this->GetRows()) {
					int h = GetRowHeight(row);
					if (y + h <= r.top) {
						y += h;
						continue;
					}
					if (y >= r.bottom) break;
					int ty = CentreBounds(y, y + h, line);
					if (row.item == -2) {
						/* Profit: shaded, with the totals. */
						GfxFillRect(r.left, y, r.right, y + h - 1, GetColourGradient(Colours::Grey, Shade::Lighter));
						Money now = 0;
						Money then = 0;
						for (int g = 0; g < static_cast<int>(std::size(_finances_groups)); g++) {
							now += this->GetGroupValue(0, g);
							then += this->GetGroupValue(this->compare, g);
						}
						DrawString(names.left, names.right, ty, STR_MOBILE_FINANCES_PROFIT, TextColour::Black);
						DrawValue(first.left, first.right, ty, now, true);
						DrawValue(second.left, second.right, ty, then, true);
					} else if (row.item == -1) {
						const FinancesGroup &group = _finances_groups[row.group];
						bool can_expand = group.items.size() > 1;
						StringID arrow = !can_expand ? STR_EMPTY : (this->expanded[row.group] ? STR_MOBILE_FINANCES_OPEN : STR_MOBILE_FINANCES_CLOSED);
						Dimension ad = GetStringBoundingBox(STR_MOBILE_FINANCES_CLOSED);
						DrawString(names.left, names.right, ty, arrow, TextColour::Black);
						Rect name = names.Indent(ad.width + WidgetDimensions::scaled.hsep_normal, rtl);
						DrawString(name.left, name.right, ty, group.name, TextColour::Black);
						DrawValue(first.left, first.right, ty, this->GetGroupValue(0, row.group), false);
						DrawValue(second.left, second.right, ty, this->GetGroupValue(this->compare, row.group), false);
						GfxFillRect(r.left, y + h - 1, r.right, y + h - 1, GetColourGradient(Colours::Grey, Shade::Dark));
					} else {
						ExpensesType type = *(_finances_groups[row.group].items.begin() + row.item);
						Rect name = names.Indent(ScaleGUITrad(24), rtl);
						/* The desktop's category names are gold; here they are text like the rest. */
						DrawString(name.left, name.right, ty, STR_FINANCES_SECTION_CONSTRUCTION + to_underlying(type), ExtendedTextColour{TextColour::Black, {ExtendedTextColourFlag::Forced, ExtendedTextColourFlag::NoShade}});
						DrawValue(first.left, first.right, ty, this->GetValue(0, type), false);
						DrawValue(second.left, second.right, ty, this->GetValue(this->compare, type), false);
					}
					y += h;
				}
				break;
			}

			case WID_MFIN_LOAN: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Shrink(ScaleGUITrad(MOBILE_TEXT_INSET), 0);
				DrawString(tr.left, tr.right, tr.top, GetString(STR_MOBILE_FINANCES_LOAN, c->current_loan, c->GetMaxLoan(), _settings_game.difficulty.initial_interest), TextColour::Black);
				DrawString(tr.left, tr.right, tr.top + line, GetString(STR_MOBILE_FINANCES_BALANCE, c->money), c->money < 0 ? TextColour::Red : TextColour::Black);
				break;
			}
		}
	}

	/**
	 * The amount of a Borrow or Repay entry.
	 * @param entry The entry.
	 * @param repay Repaying rather than borrowing.
	 * @return The amount.
	 */
	Money GetLoanAmount(LoanEntry entry, bool repay) const
	{
		const Company *c = this->GetCompany();
		switch (entry) {
			case LOAN_ONE_STEP: return LOAN_INTERVAL;
			case LOAN_FIVE_STEPS: return 5 * LOAN_INTERVAL;
			default: return repay ? c->current_loan : c->GetMaxLoan() - c->current_loan;
		}
	}

	/**
	 * Open the Borrow or Repay dropdown. Entries that can't be done are disabled and say why.
	 * @param repay Repaying rather than borrowing.
	 */
	void ShowLoanMenu(bool repay)
	{
		const Company *c = this->GetCompany();
		if (c == nullptr) return;
		DropDownList list;
		for (LoanEntry entry : {LOAN_ONE_STEP, LOAN_FIVE_STEPS, LOAN_ALL}) {
			Money amount = this->GetLoanAmount(entry, repay);
			std::string text;
			bool possible;
			if (repay) {
				possible = amount > 0 && amount <= c->current_loan && amount <= c->money;
				if (c->current_loan == 0) {
					text = GetString(STR_MOBILE_FINANCES_NO_LOAN);
				} else if (amount > c->current_loan) {
					text = GetString(STR_MOBILE_FINANCES_AMOUNT_MORE_THAN_LOAN, amount);
				} else if (amount > c->money) {
					text = GetString(STR_MOBILE_FINANCES_AMOUNT_NOT_ENOUGH, amount);
				} else {
					text = entry == LOAN_ALL ? GetString(STR_MOBILE_FINANCES_REPAY_ALL, amount) : GetString(STR_JUST_CURRENCY_LONG, amount);
				}
			} else {
				possible = amount > 0 && c->current_loan + amount <= c->GetMaxLoan();
				if (c->current_loan >= c->GetMaxLoan()) {
					text = GetString(STR_MOBILE_FINANCES_MAXIMUM_REACHED);
				} else if (!possible) {
					text = GetString(STR_MOBILE_FINANCES_AMOUNT_OVER_MAXIMUM, amount);
				} else {
					text = entry == LOAN_ALL ? GetString(STR_MOBILE_FINANCES_BORROW_MAX, amount) : GetString(STR_JUST_CURRENCY_LONG, amount);
				}
			}
			list.push_back(MakeDropDownListStringItem(std::move(text), entry, !possible));
		}
		ShowDropDownList(this, std::move(list), -1, repay ? WID_MFIN_REPAY : WID_MFIN_BORROW);
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		switch (widget) {
			case WID_MFIN_COMPARE:
				this->compare = index;
				this->UpdateRows();
				this->SetDirty();
				break;

			case WID_MFIN_BORROW:
			case WID_MFIN_REPAY: {
				/* Reversible both ways, so no confirmation. */
				bool repay = widget == WID_MFIN_REPAY;
				LoanEntry entry = static_cast<LoanEntry>(index);
				LoanCommand cmd = entry == LOAN_ALL ? LoanCommand::Max : LoanCommand::Amount;
				Money amount = entry == LOAN_ALL ? Money{0} : this->GetLoanAmount(entry, repay);
				if (repay) {
					Command<Commands::DecreaseLoan>::Post(STR_ERROR_CAN_T_REPAY_LOAN, cmd, amount);
				} else {
					Command<Commands::IncreaseLoan>::Post(STR_ERROR_CAN_T_BORROW_ANY_MORE_MONEY, cmd, amount);
				}
				break;
			}
		}
	}

	void OnClick(Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MFIN_BORROW: this->ShowLoanMenu(false); break;
			case WID_MFIN_REPAY: this->ShowLoanMenu(true); break;
			case WID_MFIN_COMPARE: {
				DropDownList list;
				for (int period = 1; period <= 2; period++) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_JUST_INT, TimerGameEconomy::year - period), period));
				}
				ShowDropDownList(this, std::move(list), this->compare, widget);
				break;
			}
			case WID_MFIN_TABLE: {
				/* A tap on a group row opens or folds its categories. */
				const NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_MFIN_TABLE);
				int y = nwid->pos_y + WidgetDimensions::scaled.framerect.top - this->vscroll->GetPosition();
				for (const Row &row : this->GetRows()) {
					int h = GetRowHeight(row);
					if (pt.y >= y && pt.y < y + h) {
						if (row.item == -1 && _finances_groups[row.group].items.size() > 1) {
							this->expanded[row.group] = !this->expanded[row.group];
							this->UpdateRows();
							this->SetDirty();
						}
						break;
					}
					y += h;
				}
				break;
			}
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->UpdateRows();
		this->SetDirty();
	}

	const IntervalTimer<TimerWindow> refresh = {std::chrono::milliseconds(1000), [this](auto) {
		this->SetDirty();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_finances_widgets = {
	/* In the Company hub, whose tabs are the title. */
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL), SetPadding(4, 4 + MOBILE_TEXT_INSET, 4, 4), SetPIP(0, 4, 0),
			NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0),
			NWidget(WWT_TEXT, Colours::Invalid, WID_MFIN_YEAR), SetMinimalSize(VALUE_COLUMN, 28), SetAlignment({AlignmentH::End, AlignmentV::Middle}), SetTextStyle(TextColour::Black, FontSize::Normal),
			NWidget(WWT_DROPDOWN, Colours::Grey, WID_MFIN_COMPARE), SetMinimalSize(VALUE_COLUMN, 28), SetToolTip(STR_MOBILE_FINANCES_COMPARE_TOOLTIP),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Grey, WID_MFIN_TABLE), SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_MFIN_SCROLL), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::Grey, WID_MFIN_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_MFIN_LOAN), SetFill(1, 0), SetResize(1, 0), EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPadding(4), SetPIP(0, 4, 0),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MFIN_BORROW), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_FINANCES_BORROW, STR_FINANCES_BORROW_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MFIN_REPAY), SetMinimalSize(60, 40), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_MOBILE_FINANCES_REPAY, STR_FINANCES_REPAY_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _mobile_finances_desc(
	WindowPosition::Manual, {}, 300, 300,
	WindowClass::Finances, WindowClass::None,
	{},
	_nested_mobile_finances_widgets
);

/**
 * Show the company's finances in the sheet slot (the Company hub's Finances tab, or the status pill's money).
 * @param company The company.
 * @return The window.
 */
Window *ShowMobileFinances(CompanyID company)
{
	Window *w = BringWindowToFrontById(WindowClass::Finances, company);
	if (w == nullptr) w = new MobileFinancesWindow(_mobile_finances_desc, company);
	return w;
}
