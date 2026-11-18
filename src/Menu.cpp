// Cooking Skill Overhaul's pages in SKSE Menu Framework's Mod Control Panel: Cooking (the skill screen, Shared Table) and Survival
// Mode (a switch for each Survival feature Cooking Skill Overhaul adds). Changes apply at once and are saved to FreeCooking.ini.
//
// SKSE Menu Framework 3 (Nexus 120352, by Thiago; 3.23 or newer: 3.23 fixed the game hanging when the panel closed
// while a number input was active). Its API header SKSEMenuFramework.h comes from
// github.com/QTR-Modding/SKSE-Menu-Framework-3-API (LGPL-2.1, licence beside it; the framework's README: "The header
// file has a license that does not require mods using it as a library to be open source"). It finds the framework's
// functions at run time (GetModuleHandle + GetProcAddress): nothing is linked, and without the framework the pages
// simply aren't there. Kept in its own file because the header brings in windows.h. (Same setup as NeedsHUD's Menu.cpp.)

// (the header's FontAwesome::UnicodeToUtf8 uses std::wstring_convert, deprecated since C++17 and an error under /sdl:
// CMakeLists.txt silences that deprecation for the whole project)
#include <algorithm>
#include <format>
#include <map>
#include <cfloat>   // FLT_MIN / FLT_MAX in the header
#include <cstdarg>  // va_list in the header
#include <string>
#include <vector>
#include "SKSEMenuFramework.h"

#include "Patcher/Dishes.h"  // FCPatch::SLOT_CATEGORIES
#include "PatcherPage.h"
#include "Settings.h"

namespace
{
	namespace UI = ImGuiMCP;
	using FC::Tr;

	// a slider's number format from the translation file, used only when it holds exactly one number slot of the form
	// %f / %.0f (and %% for a percent sign); anything else (a translator's %s or a second slot made the settings page
	// read memory it shouldn't: Codex review 2026-11-15) falls back to the English
	const char* SliderFormat(const char* a_en)
	{
		const char* tr = Tr(a_en);
		const std::string_view s(tr ? tr : "");
		int slots = 0;
		for (std::size_t i = 0; i < s.size(); ++i) {
			if (s[i] != '%') {
				continue;
			}
			if (i + 1 < s.size() && s[i + 1] == '%') {
				++i;  // %% shows a percent sign
				continue;
			}
			std::size_t j = i + 1;
			while (j < s.size() && (s[j] == '-' || s[j] == '+' || s[j] == ' ' || s[j] == '#' || s[j] == '0')) ++j;
			while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
			if (j < s.size() && s[j] == '.') {
				++j;
				while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
			}
			if (j >= s.size() || s[j] != 'f' || ++slots > 1) {
				return a_en;
			}
			i = j;
		}
		return slots == 1 ? tr : a_en;
	}

	FC::Options g_edit;  // the pages' working copy; every change is handed to FC::SetOptions
	bool        g_loaded = false;

	void LoadEdit()
	{
		if (!g_loaded) {
			g_edit = FC::GetOptions();
			g_loaded = true;
		}
	}

	// a tooltip for the item just drawn, wrapped to a fixed width (his rule 2026-11-12, all his mods: long text in a menu
	// breaks into lines by width; ImGui's SetItemTooltip never wraps, so a long tip ran off the screen). About 32 font
	// heights wide, roughly 60 characters a line, at any screen size or font scale
	void Tip(const char* a_text)
	{
		if (UI::BeginItemTooltip()) {
			UI::PushTextWrapPos(UI::GetFontSize() * 32.0f);
			UI::TextUnformatted(a_text);
			UI::PopTextWrapPos();
			UI::EndTooltip();
		}
	}

	// a checkbox with its tooltip; returns true when clicked
	bool Switch(const char* a_label, bool& a_value, const char* a_tip)
	{
		const bool clicked = UI::Checkbox(Tr(a_label), &a_value);
		Tip(Tr(a_tip));
		return clicked;
	}

	void Footer(bool& a_changed, bool& a_save, void (*a_reset)(FC::Options&))
	{
		UI::Spacing();
		UI::Separator();
		if (UI::Button(Tr("Reset this page to defaults"))) {
			a_reset(g_edit);
			a_changed = a_save = true;
		}
		UI::TextDisabled("%s", Tr("Changes take effect at once and are saved to FreeCooking.ini."));
	}

	void __stdcall RenderCooking()
	{
		LoadEdit();
		bool changed = false, save = false;

		UI::SeparatorText(Tr("Cooking skill"));
		// Cooking's place is fixed, right of Alchemy (his call, 2026-11-12); another mod's Skills menu is left as it is
		const auto state = FC::OtherSkillsMenu();
		const bool available = FC::SkydomeAvailable() && state != FC::OtherMenu::kOther;
		UI::BeginDisabled(!available);
		if (Switch("Show Cooking in the Skills menu", g_edit.skillsMenu,
				"Cooking gets a place in the vanilla Skills menu, right of Alchemy. Off: the Cooking Skills power opens Cooking's own skill screen.")) {
			changed = save = true;
		}
		UI::EndDisabled();
		if (state == FC::OtherMenu::kOther) {
			UI::TextWrapped("%s", Tr("Another mod replaces the Skills menu, so Cooking Skill Overhaul leaves it as it is. Use the Cooking Skills power to open Cooking's skill screen."));
		} else if (!FC::SkydomeAvailable()) {
			UI::TextWrapped("%s", Tr("Not available yet: the vanilla Skills menu has room for 18 skills, and Cooking would be the 19th. Until then, use the Cooking Skills power."));
		}

		UI::Spacing();
		UI::SeparatorText(Tr("Shared Table"));
		// the file keeps metres; with feet on, the slider shows and moves in feet
		const float k = g_edit.feet ? FC::FEET_PER_METRE : 1.0f;
		float shown = g_edit.shareRangeMetres * k;
		if (UI::SliderFloat(Tr("Sharing distance"), &shown, FC::SHARE_RANGE_MIN * k, FC::SHARE_RANGE_MAX * k,
				g_edit.feet ? SliderFormat("%.0f feet") : SliderFormat("%.0f metres"), UI::ImGuiSliderFlags_AlwaysClamp)) {
			g_edit.shareRangeMetres = shown / k;
			changed = true;
		}
		if (UI::IsItemDeactivatedAfterEdit()) {
			changed = save = true;  // the file is written once the slider is let go, not on every step
		}
		Tip(Tr("How far away a follower can be and still share your meal or drink. Needs the Shared Table perk."));

		UI::Spacing();
		UI::SeparatorText(Tr("Animation"));
		if (FC::EatingAnimationsInstalled()) {
			UI::TextDisabled("%s", Tr("Eating Animations and Sounds is installed: it animates your meals, Cooking Skill Overhaul's dishes included."));
		} else if (Switch("Play an eating or drinking animation", g_edit.playerEatIdle,
					   "When you eat a Cooking Skill Overhaul dish or have one of its drinks, you play the game's eating or drinking idle, like a tavern patron, in third person. Off by default: the idle always shows bread or a tankard, whatever you eat. Skipped in combat, with a weapon out, sneaking, swimming, sitting or riding.")) {
			changed = save = true;
		}

		UI::Spacing();
		UI::SeparatorText(Tr("Emergency fallback"));
		if (Switch("Offer the vanilla crafting menu", g_edit.vanillaCrafting,
				"Adds a Vanilla category at the bottom of the cooking screen at a cooking pot or an oven. It opens that station's own vanilla crafting menu, with every recipe as the game and your mods made them. Use it only if something can't be made in the cooking screen: what you make there skips Cooking Skill Overhaul's rules (no flavours, no stars, no discoveries).")) {
			changed = save = true;
		}

		UI::Spacing();
		UI::SeparatorText(Tr("Units"));
		if (Switch("Show distances in feet", g_edit.feet,
				"Distances on these pages are shown in feet instead of metres.")) {
			changed = save = true;
		}

		Footer(changed, save, [](FC::Options& o) {
			const FC::Options d;
			o.skillsMenu = d.skillsMenu;
			o.shareRangeMetres = d.shareRangeMetres;
			o.feet = d.feet;
			o.vanillaCrafting = d.vanillaCrafting;
			o.playerEatIdle = d.playerEatIdle;
		});
		if (changed) {
			FC::SetOptions(g_edit, save);
		}
	}

	void __stdcall RenderSurvival()
	{
		LoadEdit();
		bool changed = false, save = false;

		UI::SeparatorText(Tr("Survival Mode"));
		UI::TextWrapped("%s", Tr("Cooking Skill Overhaul's changes to Survival Mode. Turn one off and that part of Survival Mode works as it does without Cooking Skill Overhaul."));
		auto data = RE::TESDataHandler::GetSingleton();
		const auto& mods = FC::OtherSurvivalMods();
		if ((!data || !data->LookupLoadedLightModByName("ccQDRSSE001-SurvivalMode.esl")) && !mods.Any()) {
			UI::Spacing();
			UI::TextWrapped("%s", Tr("Survival Mode isn't installed, so these switches do nothing."));
		}
		if (mods.Any()) {
			// his words 2026-11-14: we recommend our survival experience, but players are free to use their mods; every switch
			// starts on and applies Cooking Skill Overhaul's version of that feature to their mod (his call, same evening)
			UI::Spacing();
			UI::TextWrapped("%s", FC::TrF("Found: {0}. All switches below start on, and each one applies Cooking Skill Overhaul's version of that feature to your survival mod as well (for example, our dishes lower its hunger and Spicy meals hold back its cold). To let your survival mod handle a feature on its own, turn that switch off.", mods.Names()).c_str());
		}
		UI::Spacing();

		struct Row
		{
			const char* label;
			bool FC::Options::*value;
			const char* tip;
			FC::Feature feature;
		};
		static const Row ROWS[] = {
			{ "Dishes keep you fed while they last", &FC::Options::hunger,
				"A cooked meal or baked dish keeps you fed for as long as its effect lasts, plus about 5 game hours. A Dragon Bone meal fully sates you. Off: dishes you cook from now on ease hunger like the vanilla food they are made from. Dishes you already have keep their effect.", FC::Feature::kHunger },
			{ "Spicy holds off the cold", &FC::Options::spicyCold,
				"While a Spicy meal or drink lasts, the cold can't get past a certain stage. The stage depends on the place and the Spicy level. Off: Spicy only gives its frost resistance and fire damage.", FC::Feature::kSpicyCold },
			{ "Spicy protects from freezing water", &FC::Options::spicyWater,
				"While a Spicy meal or drink lasts, freezing water doesn't hurt you or make you colder. Off: freezing water works as in Survival Mode.", FC::Feature::kSpicyWater },
			{ "Brewed drinks make up for sleeping outdoors", &FC::Options::campRest,
				"After a night outdoors, the next brewed drink gives back the rest the hard ground took, so you end up as rested as after a night in a bed. Off: sleeping outdoors leaves you Drained, as in Survival Mode.", FC::Feature::kCampRest },
			{ "Penalties in steps", &FC::Options::steppedDrains,
				"Hunger, cold and tiredness take a fixed share of your stamina, health and magicka at each stage (10%, 20%, 35%, 55%). Off: Survival Mode's own penalties, which grow a little at a time.", FC::Feature::kSteppedDrains },
			{ "Hearty heals over time", &FC::Options::heartyRegen,
				"Survival Mode stops health from regenerating on its own, so Hearty dishes add a small, steady health regeneration. Off: Hearty keeps only its other effects.", FC::Feature::kHeartyRegen },
			{ "Dragon drinks warm and refresh", &FC::Options::dragonWarmth,
				"A Dragon Scales drink (Dragonfeast perk) warms you fully and takes your tiredness down to Refreshed. Off: drinks you brew from now on have no Survival Mode effect. Drinks you already have keep their effect.", FC::Feature::kDragonWarmth },
			{ "Flavours clear afflictions", &FC::Options::afflictions,
				"A Mighty dish clears Weakened, a Brainy dish clears Addled and a Spicy dish clears Frostbitten: a meal, a baked dish or a brewed drink, at any level, while its effect lasts. While you are still starving, exhausted or freezing, Survival Mode can afflict you again. Off: afflictions wear off after 24 hours, as in Survival Mode.", FC::Feature::kAfflictions },
		};
		// Hearty's extra healing (his call 2026-11-16): only needed where natural health regeneration is stopped, i.e. the
		// player has Survival_abLowerRegenSpell (Survival Mode, SMI, Starfrost). Greyed only when a needs or cold mod is
		// found AND the player doesn't have that spell right now (certain in this game; it un-greys if Survival starts).
		// With no other mod and Survival not started yet it stays usable, so it can be set before turning Survival on.
		// the main-thread survival tick reads the player's spells; this page only reads its result (reviews 2026-11-16)
		const bool heartyUseless = FC::RegenStopped() == 0 && (mods.NeedsMod() || mods.frostfall);
		// a short grey line under a switch that works differently, or not at all, with the survival mods found
		const auto note = [&](FC::Feature a_feature) -> const char* {
			const bool smi = mods.smi || mods.starfrost;
			switch (a_feature) {
			case FC::Feature::kCampRest:
				if (smi) {
					return "Survival Mode Improved gives full rest anywhere, so this has no effect with it.";
				}
				if (mods.rnd && !mods.sunhelm && !mods.lastSeed && !mods.ineed) {
					return "Realistic Needs and Diseases rests you the same everywhere, so this has no effect with it.";
				}
				return nullptr;
			case FC::Feature::kSteppedDrains:
				if (smi) {
					return "Has no effect with Survival Mode Improved, which re-applies its own penalties all the time.";
				}
				if (mods.NeedsMod()) {
					return "Only changes Survival Mode's own penalties. Your needs mod already uses steps.";
				}
				return nullptr;
			case FC::Feature::kAfflictions:
				if (mods.starfrost) {
					return "Starfrost turns Survival Mode's afflictions off, so there is nothing to clear.";
				}
				if (mods.frostfall) {
					return "With Frostfall, a Spicy dish also clears frostbite.";
				}
				return nullptr;
			case FC::Feature::kHeartyRegen:
				if (heartyUseless) {
					return "Your survival mod doesn't stop health from regenerating, so this extra healing isn't needed. Hearty still speeds up your health regeneration.";
				}
				return nullptr;
			case FC::Feature::kSpicyWater:
				if (mods.frostfall) {
					return "With Frostfall, Spicy also keeps its cold water away, frigid water included.";
				}
				return nullptr;
			default:
				return nullptr;
			}
		};
		// a switch that can do nothing in this game is greyed out (his call 2026-11-14): only where that is certain, i.e.
		// Survival Mode Improved / Starfrost, which always take over Survival Mode once installed. A needs mod can be
		// installed but not started (Survival Mode then runs as usual), so those cases keep only the grey note
		const auto useless = [&](FC::Feature a_feature) {
			const bool smi = mods.smi || mods.starfrost;
			return (smi && (a_feature == FC::Feature::kCampRest || a_feature == FC::Feature::kSteppedDrains)) ||
			       (mods.starfrost && a_feature == FC::Feature::kAfflictions) ||
			       (heartyUseless && a_feature == FC::Feature::kHeartyRegen);
		};
		for (const auto& row : ROWS) {
			const bool off = useless(row.feature);
			UI::BeginDisabled(off);
			if (Switch(row.label, g_edit.*row.value, row.tip)) {
				g_edit.survivalChosen |= FC::FeatureBit(row.feature);
				changed = save = true;
			}
			UI::EndDisabled();
			if (const char* text = note(row.feature)) {
				UI::Indent();
				UI::PushTextWrapPos(0.0f);
				UI::TextDisabled("%s", Tr(text));
				UI::PopTextWrapPos();
				UI::Unindent();
			}
			if (row.value == &FC::Options::campRest) {
				// its own option (his call 2026-11-12), only meaningful while the camp rest itself is on
				UI::Indent();
				UI::BeginDisabled(!g_edit.campRest || useless(FC::Feature::kCampRest));
				if (Switch("The drink can also leave you Rested", g_edit.campRestBonus,
						"When the drink gives back enough rest to leave you fully rested, you also get the Rested bonus, as after a night in a bed. Off: the Rested bonus stays a reward for sleeping in a bed.")) {
					g_edit.survivalChosen |= FC::FeatureBit(FC::Feature::kCampRestBonus);
					changed = save = true;
				}
				UI::EndDisabled();
				UI::Unindent();
			}
		}

		// thirst (Cooking/Thirst.inl): Survival Mode has none, so this one adds a need rather than changing one
		UI::Spacing();
		UI::SeparatorText(Tr("Thirst"));
		if (Switch("Thirst", g_edit.thirst,
				"Cooking Skill Overhaul adds thirst to Survival Mode. Brewed drinks keep you quenched for as long as their effect lasts, plus about 5 game hours; vanilla drinks for about 4. While Quenched, magicka regenerates 10% faster. Thirsty, Parched, Dry and Dehydrated make spells cost 10 to 40% more magicka, and from Parched on shouts recharge more slowly. While SunHelm, Last Seed, Realistic Needs and Diseases or iNeed runs its own thirst, Cooking Skill Overhaul's thirst stays off and brewed drinks quench that mod's thirst instead.")) {
			g_edit.survivalChosen |= FC::FeatureBit(FC::Feature::kThirst);
			changed = save = true;
		}
		UI::Indent();
		UI::BeginDisabled(!g_edit.thirst);
		// the question on the left, the answer box after it (ImGui puts a combo's own label on the right, which read
		// backwards: his note 2026-11-12), so the line reads "Alcohol quenches thirst: [Half as much]"
		const char* alcohol[3] = { Tr("As much as any drink"), Tr("Half as much"), Tr("A fifth as much") };
		UI::AlignTextToFramePadding();
		UI::TextUnformatted(Tr("Alcohol quenches thirst:"));
		Tip(Tr("How much ale, mead, wine and other alcohol quenches thirst, brewed or bought. As much as any drink is the default."));
		UI::SameLine();
		UI::SetNextItemWidth(UI::GetFontSize() * 12.0f);
		if (UI::Combo("##FCAlcohol", &g_edit.alcohol, alcohol, 3)) {
			changed = save = true;
		}
		Tip(Tr("How much ale, mead, wine and other alcohol quenches thirst, brewed or bought. As much as any drink is the default."));
		UI::EndDisabled();
		UI::Unindent();

		Footer(changed, save, [](FC::Options& o) {
			const FC::Options d = FC::DefaultOptions();  // the defaults: every switch on, the Rested option off
			for (const auto& row : ROWS) {
				o.*row.value = d.*row.value;
			}
			o.campRestBonus = d.campRestBonus;
			o.thirst = d.thirst;
			o.alcohol = d.alcohol;
			o.survivalChosen = 0;  // back to following the defaults
		});
		if (changed) {
			const auto before = FC::GetOptions();
			FC::SetOptions(g_edit, save);
			if (before.hunger != g_edit.hunger || before.thirst != g_edit.thirst) {
				FC::RefreshNeedsLists();  // our dishes become food (or not) in the needs mods at once (Needs mods page)
			}
		}
	}
	// ---- the patcher pages: Patcher (what the patcher did, in one picture, and sharing) and Patched foods (every item, and
	// the player's changes). Ideas taken from Gourmet AutoPatch's pages (overview + foods list + an editor that stays in
	// view, a button to apply), made Cooking Skill Overhaul's own: our groups, flavours, stars, pot needs and recipe slots.
	// Changes are saved to FreeCooking_PatcherEdits.txt (My Games\Skyrim Special Edition\SKSE) and used at the next start.
	constexpr UI::ImU32 Col(int r, int g, int b, int a = 255) { return (static_cast<UI::ImU32>(a) << 24) | (static_cast<UI::ImU32>(b) << 16) | (static_cast<UI::ImU32>(g) << 8) | static_cast<UI::ImU32>(r); }
	UI::ImVec4          Vec(UI::ImU32 c) { return UI::ImVec4{ (c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f }; }

	// our eight flavours, coloured after what they do (Mighty stamina green, Brainy magicka blue, Hearty health red...)
	UI::ImU32 FlavourColor(const std::string& a_flavour)
	{
		static const std::pair<const char*, UI::ImU32> colors[] = { { "Mighty", Col(76, 150, 84) }, { "Brainy", Col(70, 110, 190) }, { "Hearty", Col(180, 64, 60) },
			{ "Sneaky", Col(120, 90, 160) }, { "Spicy", Col(214, 120, 40) }, { "Chilly", Col(90, 170, 200) }, { "Zesty", Col(190, 170, 50) }, { "Earthy", Col(140, 104, 64) } };
		for (const auto& [f, c] : colors) {
			if (a_flavour == f) return c;
		}
		return Col(110, 110, 110);  // neutral: vegetables, staples, cheese, drinks
	}

	// what the patcher did with an item, in six outcomes (the overview's bar and the list's first column)
	enum class Outcome { ToCheck, NewIngredient, NewDish, Covered, Misc, LeftOut };
	Outcome OutcomeOf(const std::string& a_kind)
	{
		if (a_kind == "Not recognised") return Outcome::ToCheck;
		if (a_kind == "Ingredient") return Outcome::NewIngredient;
		if (a_kind == "New dish") return Outcome::NewDish;
		if (a_kind == "Vanilla look" || a_kind == "Same look" || a_kind == "Variant") return Outcome::Covered;
		if (a_kind == "Misc") return Outcome::Misc;
		return Outcome::LeftOut;
	}
	struct OutcomeStyle
	{
		Outcome     outcome;
		const char* name;     // the badge
		const char* meaning;  // the overview's legend line
		UI::ImU32   color;
	};
	constexpr OutcomeStyle OUTCOMES[] = {
		{ Outcome::ToCheck, "To check", "the name wasn't recognised, so it isn't used until you set it", Col(196, 84, 70) },
		{ Outcome::NewIngredient, "Ingredient", "new ingredients: they go in the pot with their group's flavour", Col(76, 150, 84) },
		{ Outcome::NewDish, "New dish", "new dishes: their own recipe in the cooking screen", Col(214, 156, 62) },
		{ Outcome::Covered, "Covered", "dishes our rules already make (a vanilla look, a look already used, or a variant)", Col(70, 130, 170) },
		{ Outcome::Misc, "Misc", "special cases for the Misc section: quest items, preparation steps, poisoned food", Col(150, 110, 190) },
		{ Outcome::LeftOut, "Left out", "not used in cooking: meals you find or buy (eaten as they are), dishes made at another mod's station, no mesh, or left out by you", Col(150, 150, 150) },
	};
	const OutcomeStyle& StyleOf(Outcome a_o)
	{
		for (const auto& s : OUTCOMES) {
			if (s.outcome == a_o) return s;
		}
		return OUTCOMES[5];
	}

	constexpr UI::ImU32 YOURS = Col(232, 140, 200);  // the player's own changes (pink: no flavour uses it)

	// a word in a coloured pill
	void Pill(const char* a_text, UI::ImU32 a_color)
	{
		const auto pos = UI::GetCursorScreenPos();
		const auto size = UI::CalcTextSize(a_text, nullptr, false, -1.0f);
		auto*      dl = UI::GetWindowDrawList();
		UI::ImDrawListManager::AddRectFilled(dl, UI::ImVec2{ pos.x - 2.0f, pos.y - 1.0f }, UI::ImVec2{ pos.x + size.x + 6.0f, pos.y + size.y + 1.0f }, a_color, 4.0f, 0);
		UI::SetCursorPosX(UI::GetCursorPosX() + 2.0f);
		UI::TextColored(UI::ImVec4{ 1, 1, 1, 1 }, "%s", a_text);
	}

	FCPatch::EditSet      g_patchEdits;       // the player's edits file, as the pages last saved it
	FCPatch::EditSet      g_patchEditsStart;  // the same file when the game started (what this game's results use)
	bool                  g_patchEditsLoaded = false;
	std::string           g_patchSelected;  // key of the item in the editor
	char                  g_patchSearch[128] = {};
	int                   g_patchPage = 0;  // the page drawn last (1 Patcher, 2 Patched foods)
	int                   g_patchFilter = 0;  // starts on "To check"
	std::string           g_patchMessage;
	char                  g_slotSearch[3][64] = {};

	void LoadPatchEdits()
	{
		if (!g_patchEditsLoaded) {
			g_patchEdits = FC::PlayerEdits();
			g_patchEditsStart = g_patchEdits;
			g_patchEditsLoaded = true;
		}
	}

	const std::string& ChoiceName(const std::string& a_key)
	{
		static std::map<std::string, std::string> names;
		if (names.empty()) {
			for (const auto& [name, key] : FC::PatchItemChoices()) names.emplace(key, name);
		}
		static const std::string none;
		const auto it = names.find(a_key);
		return it != names.end() ? it->second : none;
	}

	// plain text, not star signs: the menu font may not have them
	std::string Stars(int a_n) { return a_n <= 1 ? std::string(Tr("1 star")) : FC::TrF("{0} stars", (std::min)(a_n, 3)); }

	// "Tomato + any big game", as the cooking screen writes a recipe
	std::string SlotsText(const std::vector<FCPatch::SlotEdit>& a_slots)
	{
		std::string out;
		for (const auto& s : a_slots) {
			if (!out.empty()) out += " + ";
			if (s.item) {
				out += ChoiceName(s.text);
			} else if (const auto* cat = FCPatch::SlotCategoryOf(s.text)) {
				out += Tr(cat->label);
			} else {
				std::string g = Tr(s.text.c_str());
				std::ranges::transform(g, g.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				out += FC::TrF("any {0}", g);
			}
		}
		return out;
	}

	const char* NeedsText(const std::string& a_needs)
	{
		if (a_needs == "alc") return Tr("needs alcohol in the pot");
		if (a_needs == "garlic") return Tr("needs garlic in the pot");
		if (a_needs == "ring") return Tr("needs the Ring of Namira");
		if (a_needs == "dragon") return Tr("a dragon part");
		return "";
	}

	// an item's result in our words: "Game meat, Hearty 1 star" or "Elaborate: Tomato + any big game"
	std::string ResultText(const std::string& a_kind, const std::string& a_group, const std::string& a_flavour, int a_stars, const std::string& a_needs,
		const std::string& a_tier, const std::vector<FCPatch::SlotEdit>& a_slots, bool a_oven = false)
	{
		const auto o = OutcomeOf(a_kind);
		if (o == Outcome::NewIngredient) {
			std::string out = Tr(a_group.c_str());
			if (!a_flavour.empty()) out += FC::TrF(", {0} {1}", Tr(a_flavour.c_str()), Stars(a_stars));
			if (!a_needs.empty()) out += std::format(", {}", NeedsText(a_needs));
			return out;
		}
		if (o == Outcome::NewDish) return FC::TrF(a_oven ? "{0} (at an oven): {1}" : "{0}: {1}", Tr(a_tier.c_str()), SlotsText(a_slots));
		return {};
	}

	// the patcher's reason in plain words ("word \"eye\"" -> the name has "eye")
	std::string PlainWhy(std::string a_why)
	{
		struct Swap { const char* from; const char* to; };
		static const Swap swaps[] = {
			{ "editor ID word \"", "the editor ID has \"" },
			{ "found or sold; dish word \"", "a meal you find or buy (the name has \"" },
			{ "found or sold; ", "a meal you find or buy: " }, { "dish word \"", "a finished dish its own mod makes (the name has \"" }, { "word \"", "the name has \"" },
			{ "no known word", "no word in the name matches one of our groups" }, { "keyword says not a food", "a keyword says it isn't food" },
			{ "keyword", "its mod marks it with a keyword" }, { "Namira word", "a person or creature word: needs the Ring of Namira" },
			{ "dragon + body part", "a dragon part" }, { "vanilla mesh", "looks like a vanilla dish, our rules already make it" },
			{ "our rules don't cook its ingredients into a dish like it", "it looks like a dish we have, but our rules don't cook its ingredients into one like it" },
			{ "same look as a modded dish read before it", "looks like another mod's dish already added" },
			{ "variant of an existing dish", "a variant of a dish we already have" }, { "variant of a modded dish read before it", "a variant of another mod's dish already added" },
			{ "one ingredient, not cooked (preparation step or conversion)", "one ingredient, not cooked: a preparation step" },
			{ "quest recipe", "a quest item: its quest needs it" }, { "special case (a Misc word in the name)", "a special case (for example poisoned food)" },
			{ "from its recipe", "its mod's recipe, put into our slots: one per named ingredient or group, at most three" },
			{ "name word", "the tier comes from a cooking word in its name" }, { "named ingredients", "its name names more ingredients" },
			{ "fewer ingredients than its name suggests", "its recipe has fewer ingredients than its name suggests" }, { "hunger size", "how filling it is" }, { "no mesh", "it has no mesh" },
			// (whole reasons the patcher gives on its own, translated too; Codex review 2026-11-16)
			{ "salt or sea water: only boiled into drinking water", "salt or sea water: only boiled into drinking water" },
			{ "drinking water: a staple and a drink ingredient", "drinking water: a staple and a drink ingredient" },
			{ "same name as one of our dishes, made at another station", "same name as one of our dishes, but made at another station" },
			{ "same recipe as ", "same recipe as " },
			{ "its name names an ingredient no recipe cooks", "its name names an ingredient no recipe cooks" },  // (Codex review 2026-11-16 afternoon, L3)
			{ "a recipe slot nothing can fill", "a recipe slot nothing can fill" },
			{ "set by hand", "set by hand" },
			{ "two foods joined by \"", "two foods joined by \"" },
			{ "set by a patch file", "set by a patch file" }, { "(a finished dish)", "(a finished dish)" },  // (Claude review 2026-11-16c)
		};
		std::string out;
		std::size_t i = 0;
		while (i < a_why.size()) {
			bool hit = false;
			for (const auto& s : swaps) {
				const std::string_view f(s.from);
				if (a_why.compare(i, f.size(), f) == 0) {
					out += Tr(s.to);
					i += f.size();
					hit = true;
					break;
				}
			}
			if (!hit) out += a_why[i++];
		}
		if (out.ends_with(" kinds")) out = out.substr(0, out.size() - 6) + " " + Tr("ingredients in its recipe");
		// keyword names are for modders: "its mod marks it with a keyword _DS_KW_Food_Soup (...)" loses the name
		if (auto k = out.find(Tr("its mod marks it with a keyword")); k != std::string::npos) {
			const auto from = k + std::string_view(Tr("its mod marks it with a keyword")).size();
			auto       to = out.find_first_of("(,", from);
			out.erase(from, (to == std::string::npos ? out.size() : to) - from);
			if (to != std::string::npos) out.insert(from, " ");
		}
		// a dish word gives "(the name has "x"" its closing bracket
		if (std::ranges::count(out, '(') > std::ranges::count(out, ')')) out += ")";
		return out;
	}

	std::string EntryText(const FCPatch::EditSet& a_set, const FC::PatchRow& a_row)  // one item's lines in an edits file
	{
		FCPatch::EditSet one;  // both kinds: a found food can be an ingredient or made a dish
		if (auto it = a_set.dishes.find(a_row.key); it != a_set.dishes.end()) one.dishes.insert(*it);
		if (auto it = a_set.ingredients.find(a_row.key); it != a_set.ingredients.end()) one.ingredients.insert(*it);
		return one.Write("");
	}
	// a dish waiting for an ingredient (Pineapple Pie for Pineapple): true once the player gave that ingredient a group
	bool WaitFixed(const FC::PatchRow& a_r)
	{
		if (a_r.waits.empty()) return false;
		for (const auto& o : FC::PatchRows()) {
			if (o.dish || o.name != a_r.waits) continue;
			const auto it = g_patchEdits.ingredients.find(o.key);
			if (it != g_patchEdits.ingredients.end() && !it->second.group.empty() && it->second.group != "OUT") return true;
		}
		return false;
	}
	// the item's edit is a dish edit: a dish, or a found food the player made a dish with its own recipe
	bool DishEdited(const FC::PatchRow& a_r)
	{
		if (a_r.dish) return true;
		const auto it = g_patchEdits.dishes.find(a_r.key);
		return a_r.food && it != g_patchEdits.dishes.end() && it->second.choice == FCPatch::DishChoice::New;
	}
	bool Pending(const FC::PatchRow& a_row) { return EntryText(g_patchEdits, a_row) != EntryText(g_patchEditsStart, a_row); }
	bool SetByYou(const FC::PatchRow& a_row) { return g_patchEdits.dishes.contains(a_row.key) || g_patchEdits.ingredients.contains(a_row.key); }

	bool PageSave()
	{
		const bool ok = FC::SavePlayerEdits(g_patchEdits);
		g_patchMessage = ok ? Tr("Saved. Quit to the desktop and start Skyrim again to use it.") : Tr("Couldn't save the change (the file can't be written).");
		return ok;
	}

	// items with the same name in the same mod (a mod can add several records of one food): shown with their form ID, and
	// a change to one applies to all of them
	std::string CopyKey(const FC::PatchRow& a_r) { return (a_r.dish ? "d|" : "i|") + a_r.plugin + "|" + a_r.name; }
	int Copies(const FC::PatchRow& a_r)
	{
		static std::map<std::string, int> counts;
		if (counts.empty()) {
			for (const auto& r : FC::PatchRows()) ++counts[CopyKey(r)];
		}
		const auto it = counts.find(CopyKey(a_r));
		return it != counts.end() ? it->second : 1;
	}

	void LabelText(const char* a_label, const std::string& a_text)
	{
		UI::TextDisabled("%s", a_label);
		UI::SameLine();
		UI::TextWrapped("%s", a_text.c_str());
	}

	// ---- Patcher: the overview
	void __stdcall RenderPatcher()
	{
		LoadPatchEdits();
		if (g_patchPage != 1) {  // a message from the other page ("Back to automatic.") doesn't belong here
			g_patchMessage.clear();
			g_patchPage = 1;
		}
		UI::SeparatorText(Tr("Foods and ingredients from other mods"));
		if (!FC::PatcherRan()) {
			UI::TextWrapped("%s", Tr("The patcher didn't run this time (its word file or the pot rules are missing). See FreeCooking.log in the SKSE log folder."));
			return;
		}
		UI::TextWrapped("%s", Tr("At game start Cooking Skill Overhaul reads the foods and alchemy ingredients other mods add and fits them into its rules: ingredients get a group, a flavour and stars; dishes with a new look get a recipe. Nothing is written to your save or to any mod."));
		UI::Spacing();
		UI::PushTextWrapPos(0.0f);  // wraps at the page edge: translations run longer
		UI::TextColored(UI::ImVec4{ 1.0f, 0.85f, 0.4f, 1.0f }, "%s", Tr("These results are made automatically from names and recipes. Check them on the Patched foods page and change what looks wrong."));
		UI::PopTextWrapPos();
		UI::Spacing();

		const auto& rows = FC::PatchRows();
		std::map<Outcome, int> counts;
		int changed = 0, pending = 0;
		for (const auto& r : rows) {
			++counts[OutcomeOf(r.kind)];
			if (SetByYou(r)) ++changed;
			if (Pending(r)) ++pending;
		}
		UI::Text("%s", FC::TrF("{0} items from other mods in your load order", rows.size()).c_str());
		// one bar, a segment per outcome
		const float width = (std::min)(UI::GetContentRegionAvail().x - 8.0f, 640.0f);
		const auto  start = UI::GetCursorScreenPos();
		auto*       dl = UI::GetWindowDrawList();
		float       x = start.x;
		UI::ImDrawListManager::AddRectFilled(dl, UI::ImVec2{ start.x - 1, start.y - 1 }, UI::ImVec2{ start.x + width + 1, start.y + 21 }, Col(30, 30, 30), 3.0f, 0);
		for (const auto& s : OUTCOMES) {
			const int n = counts[s.outcome];
			if (n == 0 || rows.empty()) continue;
			const float w = width * static_cast<float>(n) / static_cast<float>(rows.size());
			UI::ImDrawListManager::AddRectFilled(dl, UI::ImVec2{ x, start.y }, UI::ImVec2{ x + w, start.y + 20 }, s.color, 0.0f, 0);
			x += w;
		}
		UI::Dummy(UI::ImVec2{ width, 24.0f });
		for (const auto& s : OUTCOMES) {
			const int  n = counts[s.outcome];  // every outcome, even at 0, so each word on the next page is explained here
			const auto p = UI::GetCursorScreenPos();
			UI::ImDrawListManager::AddRectFilled(dl, UI::ImVec2{ p.x, p.y + 3 }, UI::ImVec2{ p.x + 12, p.y + 15 }, s.color, 2.0f, 0);
			UI::SetCursorPosX(UI::GetCursorPosX() + 20.0f);
			UI::TextWrapped("%s", std::format("{:4}  {} ({})", n, Tr(s.name), Tr(s.meaning)).c_str());
		}
		UI::Spacing();
		if (changed) UI::Text("%s", (changed == 1 ? std::string(Tr("1 item changed by you")) : FC::TrF("{0} items changed by you", changed)).c_str());
		if (pending) UI::TextColored(Vec(YOURS), "%s", FC::TrF("{0} of your changes apply after you quit to the desktop and start Skyrim again.", pending).c_str());

		UI::Spacing();
		UI::SeparatorText(Tr("Share your changes"));
		if (UI::Button(Tr("Export"))) {
			const auto file = FC::ExportPatch();
			g_patchMessage = file.empty() ? std::string(Tr("Couldn't write the export file.")) : FC::TrF("Written: {0}", file);
		}
		Tip(Tr("Writes the foods and ingredients as they are now (except those still to check) to FreeCooking_PatcherExport.txt in My Games\\Skyrim Special Edition\\SKSE. Food mod authors can ship it in Data\\SKSE\\Plugins\\FreeCooking\\Patches; players can share it."));
		UI::SameLine();
		if (UI::Button(Tr("Import"))) {
			std::size_t lines = 0, bad = 0;
			if (FC::ImportPatch(lines, bad)) {
				g_patchEdits = FC::PlayerEdits();
				g_patchMessage = FC::TrF("Items read: {0}. Used the next time you start the game.", lines);
				if (bad) g_patchMessage += " " + FC::TrF("Lines skipped: {0}", bad);
			} else {
				g_patchMessage = FC::TrF("No file to import. Put it in {0} and name it {1}", "My Games\\Skyrim Special Edition\\SKSE", FC::ImportFileName());
			}
		}
		Tip(Tr("Adds the changes in FreeCooking_PatcherImport.txt (My Games\\Skyrim Special Edition\\SKSE) to yours. Where both set the same item, the imported file wins."));
		if (!g_patchMessage.empty()) UI::TextWrapped("%s", g_patchMessage.c_str());
		UI::Spacing();
		// the folder and file names are slots, not text to translate
		UI::PushTextWrapPos(0.0f);
		UI::TextDisabled("%s", FC::TrF("Your changes: {0}. Full list: {1} in the same folder.", "My Games\\Skyrim Special Edition\\SKSE\\" + FC::EditsFileName(), "FreeCooking_Patcher.txt").c_str());
		UI::PopTextWrapPos();
	}

	// ---- Patched foods: the list and the editor
	struct Editor
	{
		std::string                    key;
		std::string                    group;   // ingredient
		int                            stars = 1;
		std::string                    flavour;  // "" = automatic (from the name), "None" = no flavour, else one of the group's flavours
		std::string                    tier;    // dish
		std::vector<FCPatch::SlotEdit> slots;   // up to 3
		bool                           used[3] = {};
		bool                           asDish = false;  // a food the player is making a dish with its own recipe
	} g_ed;

	int SlotsOfTier(const std::string& a_tier) { return a_tier == "Elaborate" ? 3 : a_tier == "Prepared" ? 2 : 1; }

	// the editor starts from the item's current result; a group or a recipe the patcher only guessed is marked "suggested",
	// and an item still to check starts with no group chosen
	void LoadEditor(const FC::PatchRow& a_r)
	{
		g_ed = {};
		g_ed.key = a_r.key;
		g_ed.group = a_r.kind == "Ingredient" ? a_r.group : std::string();
		g_ed.stars = std::clamp(a_r.stars, 1, 3);
		if (const auto it = g_patchEdits.ingredients.find(a_r.key); it != g_patchEdits.ingredients.end()) {  // saved, waiting for a restart
			g_ed.flavour = it->second.flavour;
			if (!it->second.group.empty() && it->second.group != "OUT") g_ed.group = it->second.group;
			if (it->second.stars) g_ed.stars = it->second.stars;
		}
		g_ed.tier = !a_r.tier.empty() ? a_r.tier : !a_r.autoTier.empty() ? a_r.autoTier : "Simple";
		g_ed.slots = !a_r.slots.empty() ? a_r.slots : a_r.autoSlots;
		if (const auto it = g_patchEdits.dishes.find(a_r.key); it != g_patchEdits.dishes.end() && it->second.choice == FCPatch::DishChoice::New) {
			g_ed.asDish = !a_r.dish;  // a found food the player made a dish
			if (!it->second.tier.empty()) g_ed.tier = it->second.tier;
			if (it->second.slotsSet) g_ed.slots = it->second.slots;
		}
		g_ed.slots.resize(3);
		for (int i = 0; i < 3; ++i) g_ed.used[i] = i < SlotsOfTier(g_ed.tier) && !g_ed.slots[static_cast<std::size_t>(i)].text.empty();
		for (auto& s : g_slotSearch) s[0] = '\0';
		g_patchMessage.clear();  // the last item's "Saved." doesn't belong to this one
	}

	bool Combo(const char* a_label, std::string& a_value, const std::vector<std::pair<std::string, std::string>>& a_options)
	{
		std::string preview;
		for (const auto& [v, t] : a_options) {
			if (v == a_value) preview = t;
		}
		bool changed = false;
		if (UI::BeginCombo(a_label, preview.c_str())) {
			for (const auto& [v, t] : a_options) {
				if (UI::Selectable(t.c_str(), v == a_value)) {
					a_value = v;
					changed = true;
				}
			}
			UI::EndCombo();
		}
		return changed;
	}

	std::string FlavourName(const std::string& a_f) { return a_f.empty() ? std::string(Tr("no flavour")) : std::string(Tr(a_f.c_str())); }

	// "Mighty", or "Sneaky or Hearty" for a group whose items can have more than one flavour
	std::string FlavoursText(const std::string& a_group)
	{
		const auto& opts = FC::GroupFlavours(a_group);
		if (opts.empty()) return FlavourName(FC::GroupMeaning(a_group).first);
		std::string out;
		for (std::size_t i = 0; i < opts.size(); ++i) out += (i == 0 ? "" : i + 1 == opts.size() ? std::string(" ") + Tr("or") + " " : ", ") + FlavourName(opts[i]);
		return out;
	}

	// "Claws, teeth & horns (Mighty, needs alcohol in the pot)": a new player sees what a group does before picking it
	std::vector<std::pair<std::string, std::string>> GroupOptions(bool a_meaning = true)
	{
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto& g : FC::PatchGroups()) {
			std::string text = Tr(g.c_str());
			if (a_meaning) {
				const auto [flavour, needs] = FC::GroupMeaning(g);
				std::string what = FlavoursText(g);
				if (!needs.empty()) what += std::string(", ") + NeedsText(needs);
				text += " (" + what + ")";
			}
			out.emplace_back(g, text);
		}
		return out;
	}

	// one recipe slot: nothing, one ingredient (with a search box) or any ingredient of a group
	void EditSlot(int a_i)
	{
		auto& slot = g_ed.slots[static_cast<std::size_t>(a_i)];
		bool& used = g_ed.used[a_i];
		UI::PushID(a_i);
		const char* kinds[] = { Tr("Nothing"), Tr("An ingredient"), Tr("Any of a group") };
		int         kind = !used ? 0 : slot.item ? 1 : 2;
		UI::SetNextItemWidth(UI::GetFontSize() * 9.0f);
		if (UI::BeginCombo("##kind", kinds[kind])) {
			for (int k = 0; k < 3; ++k) {
				if (UI::Selectable(kinds[k], k == kind) && k != kind) {
					used = k != 0;
					slot = { k == 1, std::string() };
					if (k == 1 && !FC::PatchItemChoices().empty()) slot.text = FC::PatchItemChoices().front().second;
					if (k == 2 && !FC::PatchGroups().empty()) slot.text = FC::PatchGroups().front();
				}
			}
			UI::EndCombo();
		}
		if (used) {
			UI::SameLine();
			UI::SetNextItemWidth(UI::GetFontSize() * 14.0f);
			if (slot.item) {
				if (UI::BeginCombo("##item", ChoiceName(slot.text).c_str())) {
					UI::InputTextWithHint("##find", Tr("Search"), g_slotSearch[a_i], sizeof(g_slotSearch[a_i]));
					std::string find = g_slotSearch[a_i];
					std::ranges::transform(find, find.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					int shown = 0;
					for (const auto& [name, key] : FC::PatchItemChoices()) {
						std::string lower = name;
						std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						if (!find.empty() && lower.find(find) == std::string::npos) continue;
						if (++shown > 200) break;  // type to narrow it down
						UI::PushID(key.c_str());
						if (UI::Selectable(name.c_str(), key == slot.text)) slot.text = key;
						UI::PopID();
					}
					UI::EndCombo();
				}
			} else {
				auto options = GroupOptions(false);
				for (auto& [g, text] : options) {
					std::string lower = text;
					std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					text = FC::TrF("any {0}", lower);  // the label the cooking screen shows for this slot
				}
				std::vector<std::pair<std::string, std::string>> cats;  // our recipes' own categories first: any flour, any dairy...
				for (const auto& c : FCPatch::SLOT_CATEGORIES) cats.emplace_back(c.name, Tr(c.label));
				options.insert(options.begin(), cats.begin(), cats.end());
				Combo("##group", slot.text, options);
			}
		}
		UI::PopID();
	}

	void Apply(const FC::PatchRow& a_row, bool a_dish, FCPatch::IngredientEdit a_ing, FCPatch::DishEdit a_d)
	{
		for (const auto& r : FC::PatchRows()) {
			if (CopyKey(r) != CopyKey(a_row)) continue;  // this item and its copies (same name, same mod)
			if (a_dish) {
				g_patchEdits.dishes[r.key] = a_d;
				if (!r.dish) g_patchEdits.ingredients.erase(r.key);  // a found food made a dish: no longer an ingredient
			} else {
				g_patchEdits.ingredients[r.key] = a_ing;
				if (r.dish && r.food && r.autoKind != "New dish" && r.autoKind != "Covered") g_patchEdits.dishes.erase(r.key);  // back to an ingredient
			}
		}
		PageSave();
	}

	void RenderEditor(const FC::PatchRow& r)
	{
		if (g_ed.key != r.key) LoadEditor(r);
		UI::TextColored(UI::ImVec4{ 1.0f, 0.84f, 0.55f, 1.0f }, "%s", r.name.c_str());
		UI::SameLine();
		UI::TextDisabled("%s", std::format("{} | {}", r.plugin, r.key.substr(r.key.rfind('|') + 1)).c_str());
		const auto autoText = std::format("{} {}", Tr(StyleOf(OutcomeOf(r.autoKind)).name),
			ResultText(r.autoKind, r.autoGroup, r.autoFlavour, r.autoStars, r.autoNeeds, r.autoTier, r.autoSlots, r.oven));
		LabelText(Tr("Automatic:"), autoText + " (" + PlainWhy(r.why) + ")");
		if (r.edited || r.patched) {
			LabelText(Tr("Now:"), std::format("{} {} ({})", Tr(StyleOf(OutcomeOf(r.kind)).name),
				ResultText(r.kind, r.group, r.flavour, r.stars, r.needs, r.tier, r.slots, r.oven), r.edited ? Tr("set by you") : Tr("set by a patch file")));
		}
		// always one line here, so the buttons don't jump when the notice appears
		if (Pending(r)) UI::TextColored(Vec(YOURS), "%s", Tr("You changed this item. Quit to the desktop and start Skyrim again to use the change (loading a save isn't enough)."));
		else UI::TextColored(Vec(YOURS), "%s", g_patchMessage.c_str());
		if (const int copies = Copies(r); copies > 1) {
			UI::TextDisabled("%s", FC::TrF("{0} items in this mod have this name; a change here applies to all of them.", copies).c_str());
		}
		if (!r.clash.empty()) {
			UI::TextColored(Vec(StyleOf(Outcome::ToCheck).color), "%s", FC::TrF("Not added: same recipe as {0}. The pot would always make that one instead. Give it another recipe.", Tr(r.clash.c_str())).c_str());
		}
		if (WaitFixed(r)) {
			UI::TextColored(Vec(YOURS), "%s", FC::TrF("{0} now has a group (your change). Quit to the desktop and start Skyrim again: this dish is then added with it in its recipe.", r.waits).c_str());
		} else if (!r.waits.empty()) {
			UI::PushStyleColor(UI::ImGuiCol_Text, Vec(StyleOf(Outcome::ToCheck).color));
			UI::TextWrapped("%s", FC::TrF("Not added yet. This dish is named after {0}, so its recipe needs {0} in a slot. But the patcher didn't recognise {0}, so it has no group and the pot can't use it.", r.waits).c_str());
			UI::TextWrapped("%s", FC::TrF("To fix it: find {0} in the list (it's under \"To check\"), give it a group, then quit to the desktop and start Skyrim again. The dish is then added with it in its recipe.", r.waits).c_str());
			UI::PopStyleColor();
		}
		UI::Spacing();
		if (r.quest) {  // its quest needs it as its mod made it
			UI::TextWrapped("%s", Tr("This is a quest item: a quest from its mod needs it. It stays in the Misc section exactly as its mod made it, so the quest keeps working, and it can't be changed here."));
			return;
		}

		const bool madeElsewhere = r.autoKind == "A dish from another station";
		if (!r.dish && r.food) {
			if (UI::Checkbox(Tr("Make it a dish with its own recipe"), &g_ed.asDish) && g_ed.asDish) {
				g_ed.tier = "Simple";
				g_ed.slots.assign(3, {});
				for (auto& u : g_ed.used) u = false;
			}
			Tip(Tr("A meal of its own instead of an ingredient: you choose its recipe, and the cooking screen makes it. It no longer goes in the pot as an ingredient."));
		}
		if (!r.dish && !g_ed.asDish) {
			if (madeElsewhere && r.why.starts_with("found or sold; ")) {
				UI::TextWrapped("%s", Tr("A finished meal you find or buy, so no recipe makes it. You eat it as it is; meals don't go in the pot. To use it in the pot anyway, give it a group:"));
			} else if (madeElsewhere) {
				UI::TextWrapped("%s", Tr("A finished dish its own mod makes at its own station, so Cooking Skill Overhaul doesn't make it. You can still use it as an ingredient in the pot:"));
			}
			auto options = GroupOptions();
			options.insert(options.begin(), { std::string(), Tr("(choose a group)") });
			UI::SetNextItemWidth(UI::GetFontSize() * 20.0f);
			if (Combo(Tr("Group"), g_ed.group, options)) g_ed.flavour.clear();  // another group: its own flavours
			Tip(Tr("The group decides the flavour and what the ingredient needs in the pot (alcohol, garlic, the Ring of Namira)."));
			// a group whose items can have more than one flavour: the player picks one, or lets the name decide
			if (const auto& opts = FC::GroupFlavours(g_ed.group); opts.size() > 1) {
				std::vector<std::pair<std::string, std::string>> flavours;
				// the patcher's own pick is known for the item's own group; for another group it comes from the name at the restart
				flavours.emplace_back(std::string(), g_ed.group == r.autoGroup ? FC::TrF("Automatic ({0})", FlavourName(r.autoFlavour))
																			   : std::string(Tr("Automatic (from its name)")));
				for (const auto& f : opts) flavours.emplace_back(f.empty() ? std::string("None") : f, FlavourName(f));
				UI::SetNextItemWidth(UI::GetFontSize() * 12.0f);
				Combo(Tr("Flavour"), g_ed.flavour, flavours);
				Tip(Tr("Items of this group can have more than one flavour. The patcher picks one from a word in the item's name; choose one here if it picked wrong."));
			}
			std::string stars = std::to_string(g_ed.stars);
			UI::SetNextItemWidth(UI::GetFontSize() * 6.0f);
			if (Combo(Tr("Stars"), stars, { { "1", Stars(1) }, { "2", Stars(2) }, { "3", Stars(3) } })) g_ed.stars = std::stoi(stars);
			Tip(Tr("How strong its flavour is. Every new ingredient starts at one star. Neutral groups (vegetables, staples, cheese, drinks) have no flavour, so stars don't count for them."));
			UI::BeginDisabled(g_ed.group.empty());
			if (UI::Button(Tr("Use it like this"))) Apply(r, false, { g_ed.group, g_ed.stars, g_ed.flavour }, {});
			UI::EndDisabled();
			Tip(Tr("It goes in the pot as an ingredient of this group, with these stars."));
			UI::SameLine();
			if (UI::Button(Tr("Leave it out"))) Apply(r, false, { "OUT", 0 }, {});
			Tip(Tr("Cooking Skill Overhaul doesn't use it. The item stays in the game as its mod made it."));
		} else {
			UI::SetNextItemWidth(UI::GetFontSize() * 8.0f);
			if (Combo(Tr("Tier"), g_ed.tier, { { "Simple", Tr("Simple") }, { "Prepared", Tr("Prepared") }, { "Elaborate", Tr("Elaborate") } })) {
				for (int i = 0; i < 3; ++i) g_ed.used[i] = i < SlotsOfTier(g_ed.tier) && (g_ed.used[i] || !g_ed.slots[static_cast<std::size_t>(i)].text.empty());
			}
			Tip(Tr("Simple dishes take one ingredient, Prepared two, Elaborate three. The tier sets the dish's base strength."));
			UI::SameLine();
			UI::TextDisabled("%s", SlotsOfTier(g_ed.tier) == 1 ? Tr("(1 ingredient; Prepared takes 2, Elaborate 3)") : SlotsOfTier(g_ed.tier) == 2 ? Tr("(2 ingredients; Simple takes 1, Elaborate 3)") : Tr("(3 ingredients; Simple takes 1, Prepared 2)"));
			UI::TextDisabled("%s", r.kind == "New dish" || g_ed.asDish ? Tr("Recipe") : Tr("Recipe (suggested by the patcher)"));
			if (!r.waits.empty()) UI::TextDisabled("%s", FC::TrF(WaitFixed(r) ? "Also in its recipe: {0} (it gets its slot after a restart)" : "Also in its recipe: {0} (waiting for a group, see above)", r.waits).c_str());
			for (int i = 0; i < SlotsOfTier(g_ed.tier); ++i) EditSlot(i);  // one row per ingredient the tier takes
			FCPatch::DishEdit d;
			d.choice = FCPatch::DishChoice::New;
			d.tier = g_ed.tier;
			for (int i = 0; i < SlotsOfTier(g_ed.tier); ++i) {
				if (g_ed.used[i] && !g_ed.slots[static_cast<std::size_t>(i)].text.empty()) d.slots.push_back(g_ed.slots[static_cast<std::size_t>(i)]);
			}
			d.slotsSet = !d.slots.empty();
			// a recipe the pot already makes: the pot would always make the other dish, so it can't be saved
			const auto clash = FC::RecipeClash(d.slots, r.key, r.oven);
			if (!clash.empty()) {
				UI::TextColored(Vec(StyleOf(Outcome::ToCheck).color), "%s", FC::TrF("Same recipe as {0}. The pot would make that one instead: change an ingredient.", Tr(clash.c_str())).c_str());
			}
			UI::BeginDisabled(!clash.empty());
			if (UI::Button(Tr("Make it a new dish like this"))) Apply(r, true, {}, d);
			UI::EndDisabled();
			Tip(Tr("The dish gets its own recipe in the cooking screen."));
			if (r.dish) {  // a food being made a dish: "Not a new dish" and "Misc" don't apply to it
				UI::SameLine();
				if (UI::Button(Tr("Not a new dish"))) {
					FCPatch::DishEdit d;
					d.choice = FCPatch::DishChoice::Not;
					Apply(r, true, {}, d);
				}
				Tip(Tr("It gets no recipe of its own: the pot makes it from our ingredient rules, or it doesn't belong in the pot. The item stays in the game as its mod made it."));
				UI::SameLine();
				if (UI::Button(Tr("Misc"))) {
					FCPatch::DishEdit d;
					d.choice = FCPatch::DishChoice::Misc;
					Apply(r, true, {}, d);
				}
				Tip(Tr("Made from the Misc section of the cooking screen, as it is in its own mod."));
			}
		}
		UI::SameLine();
		if (UI::Button(Tr("Back to automatic"))) {
			for (const auto& c : FC::PatchRows()) {
				if (CopyKey(c) != CopyKey(r)) continue;
				if (c.patched) {  // an all-automatic entry overrides the patch file: the patcher's own result
					if (c.dish) g_patchEdits.dishes[c.key] = {};
					else g_patchEdits.ingredients[c.key] = {};
				} else {
					g_patchEdits.ingredients.erase(c.key);
					g_patchEdits.dishes.erase(c.key);
				}
			}
			PageSave();
			LoadEditor(r);
			g_patchMessage = Tr("Back to automatic. Quit to the desktop and start Skyrim again to use it.");
		}
		Tip(Tr("Uses the patcher's own result for this item again, even where a patch file shipped by a food mod set something."));
	}

	// what the player's change makes of an item, as an outcome word ("Ingredient", "Left out"...)
	std::string PendingKind(const FC::PatchRow& a_r)
	{
		if (DishEdited(a_r)) {
			const auto it = g_patchEdits.dishes.find(a_r.key);
			if (it == g_patchEdits.dishes.end() || it->second.choice == FCPatch::DishChoice::Auto) return a_r.autoKind;
			return it->second.choice == FCPatch::DishChoice::New ? "New dish" : it->second.choice == FCPatch::DishChoice::Misc ? "Misc" : "Left out";
		}
		const auto it = g_patchEdits.ingredients.find(a_r.key);
		if (it == g_patchEdits.ingredients.end() || it->second.group.empty()) return a_r.autoKind;
		return it->second.group == "OUT" ? "Left out" : "Ingredient";
	}

	// what the player's change will make of an item ("Prepared: Tomato + any herb or salt"), shown in the list until a restart
	std::string PendingText(const FC::PatchRow& a_r)
	{
		if (DishEdited(a_r)) {
			const auto it = g_patchEdits.dishes.find(a_r.key);
			if (it == g_patchEdits.dishes.end() || it->second.Empty()) return Tr("back to automatic");
			const auto& e = it->second;
			if (e.choice == FCPatch::DishChoice::Not) return Tr("Left out");
			if (e.choice == FCPatch::DishChoice::Misc) return Tr("Misc");
			return std::format("{}: {}", Tr((e.tier.empty() ? a_r.tier : e.tier).c_str()), SlotsText(e.slotsSet ? e.slots : a_r.slots));
		}
		const auto it = g_patchEdits.ingredients.find(a_r.key);
		if (it == g_patchEdits.ingredients.end() || it->second.Empty()) return Tr("back to automatic");
		if (it->second.group == "OUT") return Tr("Left out");
		const auto group = it->second.group.empty() ? a_r.group : it->second.group;
		std::string out = std::format("{}, {}", Tr(group.c_str()), Stars(it->second.stars ? it->second.stars : a_r.stars));
		if (!it->second.flavour.empty()) out += ", " + FlavourName(it->second.flavour == "None" ? std::string() : it->second.flavour);
		return out;
	}

	void __stdcall RenderPatchedFoods()
	{
		LoadPatchEdits();
		if (g_patchPage != 2) {
			g_patchMessage.clear();
			g_patchPage = 2;
		}
		if (!FC::PatcherRan()) {
			UI::TextDisabled("%s", Tr("The patcher didn't run this time: nothing to show."));
			return;
		}
		const auto& rows = FC::PatchRows();
		UI::TextWrapped("%s", Tr("Each ingredient from another mod gets one of Cooking Skill Overhaul's ingredient groups, which sets its flavour and what it needs in the pot; its stars say how strong it is. Dishes with a look of their own get a recipe. Click an item to change it."));
		int counts[8] = {};
		for (const auto& r : rows) {
			const auto o = OutcomeOf(r.kind);
			counts[1]++;
			if (o == Outcome::ToCheck) counts[0]++;
			if (o == Outcome::NewIngredient) counts[2]++;
			if (o == Outcome::NewDish) counts[3]++;
			if (o == Outcome::Covered) counts[4]++;
			if (o == Outcome::Misc) counts[5]++;
			if (o == Outcome::LeftOut) counts[6]++;
			if (SetByYou(r)) counts[7]++;
		}
		const char*              names[] = { "To check", "All", "Ingredient", "New dish", "Covered", "Misc", "Left out", "Changed by you" };
		std::vector<std::string> labels;
		for (int i = 0; i < 8; ++i) labels.push_back(std::format("{} ({})", Tr(names[i]), counts[i]));
		const char* filters[8];
		for (int i = 0; i < 8; ++i) filters[i] = labels[static_cast<std::size_t>(i)].c_str();
		UI::TextUnformatted(Tr("Show"));
		UI::SameLine();
		UI::SetNextItemWidth(UI::GetFontSize() * 11.0f);
		// a search looks through all items, so the box says so while one is typed
		if (UI::BeginCombo("##show", g_patchSearch[0] ? filters[1] : filters[g_patchFilter])) {
			for (int i = 0; i < 8; ++i) {
				if (UI::Selectable(filters[i], i == g_patchFilter)) {
					g_patchFilter = i;
					g_patchSearch[0] = '\0';  // picking a group ends the search
				}
			}
			UI::EndCombo();
		}
		Tip(Tr("The same groups as the bar on the Patcher page. To check: items the patcher couldn't place, which need you."));
		UI::SameLine();
		UI::SetNextItemWidth(UI::GetFontSize() * 13.0f);
		UI::InputTextWithHint("##search", Tr("Search all items by name or mod"), g_patchSearch, sizeof(g_patchSearch));
		std::string find = g_patchSearch;
		std::ranges::transform(find, find.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		std::vector<const FC::PatchRow*> shown;
		for (const auto& r : rows) {
			bool keep;
			if (!find.empty()) {  // a search looks through every item, whatever the "Show" list says
				std::string hay = r.name + " " + r.plugin;
				std::ranges::transform(hay, hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				keep = hay.find(find) != std::string::npos;
			} else {
				static constexpr Outcome byFilter[] = { Outcome::ToCheck, Outcome::ToCheck, Outcome::NewIngredient, Outcome::NewDish, Outcome::Covered, Outcome::Misc, Outcome::LeftOut };
				keep = g_patchFilter == 1 || (g_patchFilter == 7 ? SetByYou(r) : OutcomeOf(r.kind) == byFilter[g_patchFilter]);
			}
			if (keep) shown.push_back(&r);
		}
		// the editor follows the list: an item the list no longer shows isn't edited by mistake
		if (!g_patchSelected.empty() && std::ranges::none_of(shown, [](const FC::PatchRow* a_r) { return a_r->key == g_patchSelected; })) {
			g_patchSelected.clear();
			g_ed.key.clear();
		}

		UI::TextDisabled("%s", FC::TrF("Showing {0} of {1}", shown.size(), rows.size()).c_str());
		// the list fills the page until an item is picked; then the editor below needs about 12 lines
		const float line = UI::GetTextLineHeightWithSpacing();
		const float reserve = g_patchSelected.empty() ? 3.0f : 12.5f;
		const float tableHeight = (std::max)(line * 8.0f, UI::GetContentRegionAvail().y - line * reserve);
		if (UI::BeginTable("##patched", 4, UI::ImGuiTableFlags_RowBg | UI::ImGuiTableFlags_Borders | UI::ImGuiTableFlags_ScrollY | UI::ImGuiTableFlags_Resizable,
				UI::ImVec2{ 0.0f, tableHeight })) {
			UI::TableSetupScrollFreeze(0, 1);
			UI::TableSetupColumn(Tr("Food or ingredient"), UI::ImGuiTableColumnFlags_WidthStretch, 2.2f);
			UI::TableSetupColumn(Tr("What it is"), UI::ImGuiTableColumnFlags_WidthFixed, UI::GetFontSize() * 6.0f);
			UI::TableSetupColumn(Tr("In Cooking Skill Overhaul"), UI::ImGuiTableColumnFlags_WidthStretch, 3.8f);
			UI::TableSetupColumn(Tr("Mod"), UI::ImGuiTableColumnFlags_WidthStretch, 1.4f);
			UI::TableHeadersRow();
			auto clipper = UI::ImGuiListClipperManager::Create();
			UI::ImGuiListClipperManager::Begin(clipper, static_cast<int>(shown.size()), -1.0f);
			while (UI::ImGuiListClipperManager::Step(clipper)) {
				for (int i = clipper->DisplayStart; i < clipper->DisplayEnd; ++i) {
					const auto& r = *shown[static_cast<std::size_t>(i)];
					UI::TableNextRow();
					UI::TableNextColumn();
					UI::PushID(r.key.c_str());
					const std::string label = (SetByYou(r) ? std::string("* ") : std::string()) + r.name;  // * changed by you
					if (UI::Selectable(label.c_str(), g_patchSelected == r.key, UI::ImGuiSelectableFlags_SpanAllColumns)) {
						g_patchSelected = r.key;
						LoadEditor(r);
					}
					UI::PopID();
					UI::TableNextColumn();
					if (Pending(r)) {  // the player's result, in their colour, until the restart
						Pill(std::format("{} *", Tr(StyleOf(OutcomeOf(PendingKind(r))).name)).c_str(), YOURS);
					} else {
						const auto& st = StyleOf(OutcomeOf(r.kind));
						Pill(Tr(st.name), st.color);
					}
					UI::TableNextColumn();
					std::string result = ResultText(r.kind, r.group, r.flavour, r.stars, r.needs, r.tier, r.slots, r.oven);
					if (result.empty()) result = r.edited ? std::string(Tr("set by you")) : PlainWhy(r.why);
					if (r.quest) result = Tr("Quest item: kept as its mod made it, so its quest works. Can't be changed.");
					if (!r.clash.empty()) result = FC::TrF("Not added: same recipe as {0}", Tr(r.clash.c_str()));
					if (!r.waits.empty()) result = WaitFixed(r) ? FC::TrF("{0} now has a group: added after a restart", r.waits)
														: FC::TrF("Not added yet: give {0} a group first", r.waits);
					if (Pending(r)) {
						UI::TextColored(Vec(YOURS), "%s", FC::TrF("Yours: {0} (after a restart)", PendingText(r)).c_str());
						result = FC::TrF("Now: {0}", result) + "\n" + FC::TrF("Yours: {0} (after a restart)", PendingText(r));
					} else if (!r.flavour.empty()) {
						UI::TextColored(Vec(FlavourColor(r.flavour)), "%s", result.c_str());
					} else {
						UI::TextUnformatted(result.c_str());
					}
					if (UI::IsItemHovered()) Tip(result.c_str());  // the whole text when the column cuts it
					UI::TableNextColumn();
					const std::string mod = Copies(r) > 1 ? std::format("{} | {}", r.plugin, r.key.substr(r.key.rfind('|') + 1)) : r.plugin;  // copies: tell them apart
					UI::TextUnformatted(mod.c_str());
					Tip(mod.c_str());
				}
			}
			UI::ImGuiListClipperManager::End(clipper);
			UI::ImGuiListClipperManager::Destroy(clipper);
			UI::EndTable();
		}
		if (shown.empty()) UI::TextDisabled("%s", !find.empty() ? Tr("Nothing matches.") : g_patchFilter == 0 ? Tr("Nothing to check: every item was recognised.") : Tr("Nothing here."));

		// the flavour colours of the list, in one line
		UI::TextDisabled("%s", Tr("Colours:"));
		for (const char* f : { "Mighty", "Brainy", "Hearty", "Sneaky", "Spicy", "Chilly", "Zesty", "Earthy" }) {
			UI::SameLine();
			UI::TextColored(Vec(FlavourColor(f)), "%s", Tr(f));
		}
		UI::SameLine();
		UI::SameLine();
		UI::TextColored(Vec(YOURS), "%s", Tr("your change"));
		UI::SameLine();
		UI::TextDisabled("%s", Tr("(white: no flavour)"));

		const FC::PatchRow* sel = nullptr;
		for (const auto& r : rows) {
			if (r.key == g_patchSelected) sel = &r;
		}
		UI::Separator();
		if (!sel) {
			UI::TextDisabled("%s", Tr("Click a food or ingredient in the list to see what the patcher did with it and to change it. A * marks items you changed."));
			return;
		}
		RenderEditor(*sel);
	}

	// ---- Misc recipes: every recipe at the pot and oven that our rules don't cook (the cooking screen's Misc section),
	// and whether the cooking screen shows it. His call 2026-11-15 ("we let players edit everything in the patcher"):
	// nothing is hidden on its own any more (the preparation-step rule is gone); the player hides what he doesn't want.
	// A switch takes effect at once and is saved to the edits file (MISC lines) for later games.
	char g_miscSearch[128] = {};
	int  g_miscFilter = 0;  // 0 all, 1 shown, 2 hidden

	void __stdcall RenderMiscRecipes()
	{
		LoadPatchEdits();
		if (g_patchPage != 3) {
			g_patchMessage.clear();
			g_patchPage = 3;
		}
		const auto& rows = FC::MiscRows();
		UI::TextWrapped("%s", Tr("Recipes from other mods at the cooking pot and the oven that Cooking Skill Overhaul doesn't cook. The cooking screen lists them under Misc, made exactly as their mod makes them. Turn one off to hide it from the cooking screen. The change works at once."));
		if (rows.empty()) {
			UI::TextDisabled("%s", Tr("No recipes here: your mods add none at the cooking pot or the oven."));
			return;
		}
		int shownCount = 0;
		for (const auto& r : rows) shownCount += FC::MiscHidden(r.key) ? 0 : 1;
		const std::string labels[3] = { FC::TrF("All ({0})", rows.size()), FC::TrF("Shown ({0})", shownCount), FC::TrF("Hidden ({0})", rows.size() - static_cast<std::size_t>(shownCount)) };
		UI::TextUnformatted(Tr("Show"));
		UI::SameLine();
		UI::SetNextItemWidth(UI::GetFontSize() * 9.0f);
		if (UI::BeginCombo("##miscshow", labels[g_miscFilter].c_str())) {
			for (int i = 0; i < 3; ++i) {
				if (UI::Selectable(labels[i].c_str(), i == g_miscFilter)) g_miscFilter = i;
			}
			UI::EndCombo();
		}
		UI::SameLine();
		UI::SetNextItemWidth(UI::GetFontSize() * 13.0f);
		UI::InputTextWithHint("##miscsearch", Tr("Search by name or mod"), g_miscSearch, sizeof(g_miscSearch));
		std::string find = g_miscSearch;
		std::ranges::transform(find, find.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		// two items with the same name from the same mod (SunHelm has three Bottles of Water): the form ID tells them apart
		std::map<std::string, int> sameName;
		for (const auto& r : rows) ++sameName[r.name + "|" + r.plugin];
		std::vector<const FC::MiscRow*> shown;
		for (const auto& r : rows) {
			const bool hidden = FC::MiscHidden(r.key);
			if ((g_miscFilter == 1 && hidden) || (g_miscFilter == 2 && !hidden)) continue;
			if (!find.empty()) {
				std::string hay = r.name + " " + r.plugin;
				std::ranges::transform(hay, hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (hay.find(find) == std::string::npos) continue;
			}
			shown.push_back(&r);
		}
		const float line = UI::GetTextLineHeightWithSpacing();
		const float tableHeight = (std::max)(line * 8.0f, UI::GetContentRegionAvail().y - line * 3.0f);
		if (UI::BeginTable("##misc", 4, UI::ImGuiTableFlags_RowBg | UI::ImGuiTableFlags_Borders | UI::ImGuiTableFlags_ScrollY | UI::ImGuiTableFlags_Resizable,
				UI::ImVec2{ 0.0f, tableHeight })) {
			UI::TableSetupScrollFreeze(0, 1);
			UI::TableSetupColumn(Tr("Shown"), UI::ImGuiTableColumnFlags_WidthFixed, UI::GetFontSize() * 4.0f);
			UI::TableSetupColumn(Tr("Recipe for"), UI::ImGuiTableColumnFlags_WidthStretch, 2.4f);
			UI::TableSetupColumn(Tr("Made at"), UI::ImGuiTableColumnFlags_WidthStretch, 1.2f);
			UI::TableSetupColumn(Tr("Mod"), UI::ImGuiTableColumnFlags_WidthStretch, 1.6f);
			UI::TableHeadersRow();
			auto clipper = UI::ImGuiListClipperManager::Create();
			UI::ImGuiListClipperManager::Begin(clipper, static_cast<int>(shown.size()), -1.0f);
			while (UI::ImGuiListClipperManager::Step(clipper)) {
				for (int i = clipper->DisplayStart; i < clipper->DisplayEnd; ++i) {
					const auto& r = *shown[static_cast<std::size_t>(i)];
					UI::TableNextRow();
					UI::TableNextColumn();
					UI::PushID(r.key.c_str());
					bool on = !FC::MiscHidden(r.key);
					if (UI::Checkbox("##on", &on)) {
						FC::SetMiscHidden(r.key, !on);
						// a patch file's choice is the starting point; the player's line is kept only where it differs
						const auto& files = FC::PatchFileEdits().misc;
						const auto  f = files.find(r.key);
						const bool  base = f != files.end() && f->second;
						if (!on == base) {
							g_patchEdits.misc.erase(r.key);
						} else {
							g_patchEdits.misc[r.key] = !on;
						}
						g_patchMessage = FC::SavePlayerEdits(g_patchEdits) ? std::string() : std::string(Tr("Couldn't save the change (the file can't be written)."));
					}
					Tip(on ? Tr("Shown in the cooking screen's Misc section. Click to hide it.") : Tr("Hidden from the cooking screen. Click to show it again."));
					UI::PopID();
					UI::TableNextColumn();
					const std::string name = r.recipes > 1 ? FC::TrF("{0} ({1} recipes)", r.name, r.recipes) : r.name;
					if (FC::MiscHidden(r.key)) {
						UI::TextDisabled("%s", name.c_str());
					} else {
						UI::TextUnformatted(name.c_str());
					}
					UI::TableNextColumn();
					UI::TextUnformatted(r.pot && r.oven ? Tr("Pot and oven") : r.oven ? Tr("Oven") : Tr("Cooking pot"));
					UI::TableNextColumn();
					std::string mod = sameName[r.name + "|" + r.plugin] > 1 ? std::format("{} | {}", r.plugin, r.key.substr(r.key.rfind('|') + 1)) : r.plugin;
					if (r.byPatchFile) mod = FC::TrF("{0} (set by a patch file)", mod);
					UI::TextUnformatted(mod.c_str());
					Tip(mod.c_str());
				}
			}
			UI::ImGuiListClipperManager::End(clipper);
			UI::ImGuiListClipperManager::Destroy(clipper);
			UI::EndTable();
		}
		if (shown.empty()) UI::TextDisabled("%s", Tr("Nothing here."));
		if (!g_patchMessage.empty()) UI::TextColored(UI::ImVec4{ 1.0f, 0.5f, 0.4f, 1.0f }, "%s", g_patchMessage.c_str());
	}

	// ---- the Needs mods page (his call 2026-11-15): only when SunHelm, Last Seed, iNeed or Realistic Needs is loaded.
	// Those mods know foods by their own lists and ask the player about unknown ones. Cooking Skill Overhaul answers for him: while
	// it feeds that need itself (Survival Mode page switches), our dishes are "not food" there; with the switch off they
	// count as that mod's food of their size, or what the player picks here per recipe. Works at once; saved to the edits
	// file (NEEDS lines).
	char g_needsSearch[128] = {};

	void __stdcall RenderNeedsMods()
	{
		LoadPatchEdits();
		if (g_patchPage != 4) {
			g_patchMessage.clear();
			g_patchPage = 4;
		}
		LoadEdit();
		const auto& rows = FC::NeedsRows();
		UI::TextWrapped("%s", Tr("SunHelm, Last Seed, iNeed and Realistic Needs and Diseases only know foods from their own lists. Cooking Skill Overhaul puts its dishes on those lists for you, so you are never asked what a dish is."));
		UI::Spacing();
		const bool hungerOn = g_edit.hunger;
		const bool thirstOn = g_edit.thirst;
		if (hungerOn || thirstOn) {
			UI::TextWrapped("%s", Tr("While Cooking Skill Overhaul fills your needs mod's hunger (meals) or thirst (brewed drinks) itself, its dishes count as not food there, so nothing is counted twice. Turn that switch off on the Survival Mode page to let your needs mod use its own rules; the choices below then apply."));
			UI::Spacing();
		}
		UI::SetNextItemWidth(UI::GetFontSize() * 13.0f);
		UI::InputTextWithHint("##needssearch", Tr("Search by name"), g_needsSearch, sizeof(g_needsSearch));
		std::string find = g_needsSearch;
		std::ranges::transform(find, find.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		const char* choices[] = { Tr("Automatic"), Tr("Not food"), Tr("Light meal"), Tr("Medium meal"), Tr("Heavy meal"), Tr("Drink") };
		std::vector<const FC::NeedsRow*> shown;
		for (const auto& r : rows) {
			if (!find.empty()) {
				std::string hay = r.name;
				std::ranges::transform(hay, hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (hay.find(find) == std::string::npos) continue;
			}
			shown.push_back(&r);
		}
		const float line = UI::GetTextLineHeightWithSpacing();
		const float tableHeight = (std::max)(line * 8.0f, UI::GetContentRegionAvail().y - line * 3.0f);
		if (UI::BeginTable("##needs", 2, UI::ImGuiTableFlags_RowBg | UI::ImGuiTableFlags_Borders | UI::ImGuiTableFlags_ScrollY | UI::ImGuiTableFlags_Resizable,
				UI::ImVec2{ 0.0f, tableHeight })) {
			UI::TableSetupScrollFreeze(0, 1);
			UI::TableSetupColumn(Tr("Recipe"), UI::ImGuiTableColumnFlags_WidthStretch, 2.0f);
			UI::TableSetupColumn(Tr("In your needs mod"), UI::ImGuiTableColumnFlags_WidthStretch, 1.4f);
			UI::TableHeadersRow();
			auto clipper = UI::ImGuiListClipperManager::Create();
			UI::ImGuiListClipperManager::Begin(clipper, static_cast<int>(shown.size()), -1.0f);
			while (UI::ImGuiListClipperManager::Step(clipper)) {
				for (int i = clipper->DisplayStart; i < clipper->DisplayEnd; ++i) {
					const auto& r = *shown[static_cast<std::size_t>(i)];
					UI::TableNextRow();
					UI::TableNextColumn();
					const std::string name = r.byPatchFile ? FC::TrF("{0} (set by a patch file)", r.name) : r.name;
					UI::TextUnformatted(name.c_str());
					UI::TableNextColumn();
					UI::PushID(r.key.c_str());
					const bool locked = r.drink ? thirstOn : hungerOn;
					const int  current = FC::NeedsChoice(r.key);  // -1 automatic
					UI::BeginDisabled(locked);
					UI::SetNextItemWidth(-FLT_MIN);
					const char* label = locked ? Tr("Not food (Cooking Skill Overhaul fills it)") : choices[current + 1];
					if (UI::BeginCombo("##need", label)) {
						for (int c = -1; c <= 4; ++c) {
							if (UI::Selectable(choices[c + 1], c == current) && c != current) {
								FC::SetNeedsChoice(r.key, c);
								constexpr const char* WORDS[] = { "NOTFOOD", "LIGHT", "MEDIUM", "HEAVY", "DRINK" };
								if (c < 0) {
									// over a patch file's choice, Automatic is kept as AUTO: otherwise the patch file's
									// choice came back on the next start (Codex review 2026-11-15, L2)
									if (FC::PatchFileEdits().needs.contains(r.key)) {
										g_patchEdits.needs[r.key] = "AUTO";
									} else {
										g_patchEdits.needs.erase(r.key);
									}
								} else {
									g_patchEdits.needs[r.key] = WORDS[c];
								}
								g_patchMessage = FC::SavePlayerEdits(g_patchEdits) ? std::string() : std::string(Tr("Couldn't save the change (the file can't be written)."));
							}
						}
						UI::EndCombo();
					}
					UI::EndDisabled();
					Tip(locked ? Tr("Cooking Skill Overhaul fills this need itself. Turn its switch off on the Survival Mode page to choose here.") :
								 Tr("What your needs mod counts this dish as. Automatic uses the dish's size: light, medium or heavy meal, or drink."));
					UI::PopID();
				}
			}
			UI::ImGuiListClipperManager::End(clipper);
			UI::ImGuiListClipperManager::Destroy(clipper);
			UI::EndTable();
		}
		if (shown.empty()) UI::TextDisabled("%s", Tr("Nothing here."));
		if (!g_patchMessage.empty()) UI::TextColored(UI::ImVec4{ 1.0f, 0.5f, 0.4f, 1.0f }, "%s", g_patchMessage.c_str());
	}

}

namespace
{
	// SKSE Menu Framework up to 3.22 leaves the game in text-entry mode when the menu is closed while a text box (our
	// search box) still has the cursor: the game ignores the keyboard until another menu opens (seen 2026-11-14; fixed in
	// the framework's source on 2026-11-04). On close, give the keyboard back if a text box was still active. The game's
	// text-entry count never goes below zero, so this is harmless with a framework that already does it.
	void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
	{
		if (a_type != SKSEMenuFramework::Model::EventType::kCloseMenu) {
			return;
		}
		const auto io = ImGuiMCP::GetIO();
		if (io && io->WantTextInput) {
			if (auto controls = RE::ControlMap::GetSingleton()) {
				controls->AllowTextInput(false);
				logger::info("Settings menu closed with a text box active: keyboard given back to the game");
			}
		}
	}
}

void FC::RegisterMenu()
{
	static bool registered = false;  // once only
	if (registered) {
		return;
	}
	registered = true;
	if (!SKSEMenuFramework::IsInstalled()) {
		logger::info("SKSE Menu Framework not installed: no in-game settings pages (edit FreeCooking.ini instead)");
		return;
	}
	SKSEMenuFramework::SetSection("Cooking Skill Overhaul");  // the mod's name stays as it is in every language
	SKSEMenuFramework::AddSectionItem(Tr("Cooking"), RenderCooking);
	SKSEMenuFramework::AddSectionItem(Tr("Survival Mode"), RenderSurvival);
	SKSEMenuFramework::AddSectionItem(Tr("Patcher"), RenderPatcher);
	SKSEMenuFramework::AddSectionItem(Tr("Patched foods"), RenderPatchedFoods);
	SKSEMenuFramework::AddSectionItem(Tr("Misc recipes"), RenderMiscRecipes);
	const bool needs = FC::NeedsPageShown();  // only with SunHelm, Last Seed, iNeed or Realistic Needs (his call 2026-11-15)
	if (needs) {
		SKSEMenuFramework::AddSectionItem(Tr("Needs mods"), RenderNeedsMods);
	}
	SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);  // kept for the game's lifetime
	logger::info("Settings pages added to SKSE Menu Framework (Cooking Skill Overhaul > Cooking, Survival Mode, Patcher, Patched foods, Misc recipes{})", needs ? ", Needs mods" : "");
}
