#include "CookingMenu.h"
#include "Settings.h"
#include "Patcher/Dishes.h"  // the modded-foods patcher's rules (plain code, no game types)
#include "PatcherPage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <chrono>
#include <deque>
#include <cfloat>
#include <cmath>
#include <optional>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

static void RegisterMenuListener();  // below: the pot's script starts listening for the plugin's mod events

namespace
{
	#include "Core/Timers.inl"
	#include "Core/Common.inl"
	#include "Cooking/PlayerView.inl"
	#include "Cooking/ChefHat.inl"
	#include "Cooking/Rules.inl"
	#include "Cooking/DishPool.inl"
	#include "Cooking/SharedTable.inl"
	#include "Cooking/HolyDamage.inl"
	#include "Cooking/OtherMods.inl"
	#include "Cooking/Survival.inl"
	#include "Cooking/Thirst.inl"
	#include "Cooking/Afflictions.inl"
	#include "Cooking/Camping.inl"
	#include "Cooking/Patcher.inl"
	#include "Cooking/Misc.inl"
	#include "Cooking/Needs.inl"
	#include "Cooking/Hooks.inl"
}

namespace
{
	// which dish a set of pot keys makes among all dishes, remembered for one list build: the recipe shortcut and the
	// card ask about the same sets many times (Claude review 2026-11-16g, M4). Cleared by Populate. The order of the
	// keys doesn't change the dish, so they are sorted for the memo
	std::mutex                                    g_winMemoLock;
	std::unordered_map<std::string, std::int32_t> g_winMemo;

	void ClearWinMemo()
	{
		std::scoped_lock lock(g_winMemoLock);
		g_winMemo.clear();
	}

	// (a_prefer: the dish that wins a tie, -2 the player's pick: see ChooseDish)
	std::int32_t WinningDish(std::vector<std::string> a_keys, bool a_longSimmer, std::int32_t a_prefer = -2)
	{
		std::ranges::sort(a_keys);
		const std::int32_t prefer = a_prefer == -2 ? g_preferDish.load() : a_prefer;
		std::string memoKey = std::format("{}|{}|{}", a_longSimmer ? 1 : 0, g_place.load(), prefer);
		for (const auto& k : a_keys) {
			memoKey += '|';
			memoKey += k;
		}
		{
			std::scoped_lock lock(g_winMemoLock);
			if (const auto it = g_winMemo.find(memoKey); it != g_winMemo.end()) {
				return it->second;
			}
		}
		const auto dish = ChooseDish(a_keys, a_longSimmer, false, -1, true, prefer).dish;  // (quiet: the preview's main ingredient stays)
		std::scoped_lock lock(g_winMemoLock);
		g_winMemo.emplace(std::move(memoKey), dish);
		return dish;
	}

	// these keys make this dish when cooked: the dish's own rules (the cheap check on it alone first), then the cook's
	// pick among all dishes (a tie counts: picking this recipe makes it win one; 2026-11-24)
	bool MakesDish(const std::vector<std::string>& a_keys, bool a_longSimmer, std::int32_t a_dish)
	{
		return ChooseDish(a_keys, a_longSimmer, false, a_dish, true).dish == a_dish && WinningDish(a_keys, a_longSimmer, a_dish) == a_dish;
	}

	// the alcoholic drink the recipe shortcut adds for insect eggs: one in the pot first, then the cheapest carried. One
	// chooser for the search and the fill, so both test the same pot (Claude review 2026-11-16g, L5)
	// (2026-11-24: the same chooser finds the Garlic or Earthy potion a poisonous recipe ingredient needs: a_garlic)
	RE::TESBoundObject* ShortcutDrink(const std::unordered_map<RE::TESBoundObject*, std::int32_t>& a_counts,
		const std::unordered_map<RE::TESBoundObject*, std::int32_t>& a_used, const std::vector<RE::TESBoundObject*>& a_pot, bool a_garlic = false)
	{
		RE::TESBoundObject* drink = nullptr;
		std::tuple<std::int32_t, std::int32_t, RE::FormID> bestKey{ 2, 0, 0 };
		for (const auto& [item, have] : a_counts) {
			const auto taken = a_used.contains(item) ? a_used.at(item) : 0;
			if (!item || have <= taken || !(a_garlic ? IngOf(item).garlic : IngOf(item).alcohol)) {
				continue;
			}
			const bool inPot = std::ranges::find(a_pot, item) != a_pot.end();
			// (the form ID last: the same drink whatever order the inventory map holds them in)
			const std::tuple<std::int32_t, std::int32_t, RE::FormID> key{ inPot ? 0 : 1, item->GetGoldValue(), item->GetFormID() };
			if (!drink || key < bestKey) {
				drink = item;
				bestKey = key;
			}
		}
		return drink;
	}
}

namespace
{
	// The helpers the recipe shortcut may add so these keys make the dish (2026-11-24: an unappetizing recipe ingredient
	// needs an alcoholic drink, a poisonous one Garlic or an Earthy potion; was the drink for insect eggs only): none,
	// a drink, Garlic, or both, in that order; the items added go to a_added. False: no set makes it.
	bool WithShortcutHelpers(std::vector<std::string> a_keys, std::unordered_map<RE::TESBoundObject*, std::int32_t> a_used,
		const std::unordered_map<RE::TESBoundObject*, std::int32_t>& a_counts, const std::vector<RE::TESBoundObject*>& a_pot,
		bool a_longSimmer, std::int32_t a_dish, std::vector<RE::TESBoundObject*>* a_added = nullptr)
	{
		if (MakesDish(a_keys, a_longSimmer, a_dish)) {
			return true;
		}
		for (const int set : { 1, 2, 3 }) {  // 1 a drink, 2 Garlic, 3 both
			auto keys = a_keys;
			auto used = a_used;
			std::vector<RE::TESBoundObject*> added;
			bool ok = true;
			for (const bool garlic : { false, true }) {
				if (!(set & (garlic ? 2 : 1))) {
					continue;
				}
				auto item = ShortcutDrink(a_counts, used, a_pot, garlic);
				if (!item) {
					ok = false;
					break;
				}
				keys.push_back(IngOf(item).key);
				++used[item];
				added.push_back(item);
			}
			if (ok && MakesDish(keys, a_longSimmer, a_dish)) {
				if (a_added) {
					*a_added = std::move(added);
				}
				return true;
			}
		}
		return false;
	}
}

void CookingMenu::Register()
{
	g_mainThread = std::this_thread::get_id();  // kDataLoaded runs on the game's main thread (Cooking/PlayerView.inl)
	// the copy of the player the cooking screen and item cards read: perks, ring and Fortify Cooking ten times a second,
	// the whole inventory only while the cooking screen is open
	RepeatAlways(100, []() {
		auto ui = RE::UI::GetSingleton();
		CaptureView(ui && ui->IsMenuOpen(MENU_NAME));
		return true;
	});
	CampingSetup();  // plugin lookups kept from now on (Cooking/Camping.inl)
	HoldRememberSurvivalRate();  // the hold gives Survival's constant rate back before every load (Cooking/Thirst.inl)
	ChefHatSetup();  // the caravans sell the enchanted Chef's Hat (Cooking/ChefHat.inl)
	WatchBrews();
	RegisterConsoleCommand();
	if (auto ui = RE::UI::GetSingleton()) {
		ui->Register(MENU_NAME, []() -> RE::IMenu* { return new CookingMenu(); });
		ui->AddEventSink<RE::MenuOpenCloseEvent>(PotWatcher::GetSingleton());
		ui->AddEventSink<RE::MenuOpenCloseEvent>(BrewWatcher::GetSingleton());  // merchants: sold brews don't age there
		logger::info("Cooking menu registered");
	}
	if (auto input = RE::BSInputDeviceManager::GetSingleton()) {
		input->AddEventSink(MenuKeys::GetSingleton());
	}
	if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
		events->AddEventSink<RE::TESEquipEvent>(EatWatcher::GetSingleton());
		events->AddEventSink<RE::TESContainerChangedEvent>(BrewWatcher::GetSingleton());  // brews in chests age (Cooking/Hooks.inl)
		events->AddEventSink<RE::TESActivateEvent>(BrewWatcher::GetSingleton());
	}
	PlayerUpdateHook::Install();
	InstallHolyHooks();  // the element meals' share of spell hits (Cooking/HolyDamage.inl)
	InstallWeaponHitHook();  // and their small hit on weapon hits
	StartHolyWatch();
	StartSurvivalWatch();  // FC_SurvivalOn mirrors Survival Mode (Cooking/Survival.inl)
	StartSleepWatch();     // camp rest: outdoor sleeps (Cooking/Survival.inl)
	StartSkipWatch();      // the hold lets go for sleeps, waits and travel (Cooking/Thirst.inl)
	CardHook<RE::InventoryMenu>::Install();
	CardHook<RE::BarterMenu>::Install();
	CardHook<RE::ContainerMenu>::Install();
	logger::info("Item cards show cooking tags (inventory, barter, containers)");
	LoadData();
	LoadRules();
	BuildQuestItems();  // items quests hold: their recipes stay as their mods made them (Cooking/Patcher.inl)
	try {
		RunPatcher();  // other mods' foods and ingredients join the rules (Cooking/Patcher.inl)
	} catch (const std::exception& e) {
		logger::error("Patcher stopped: {} (modded foods may be only partly added)", e.what());
	}
	BuildMisc();  // the Misc section: the pot's and oven's recipes our rules don't cook (Cooking/Misc.inl)
	BuildPotionRows();  // the Potions page: every good potion and what it does in the pot (Cooking/Patcher.inl)
	BuildNeedsRows();  // SunHelm, Last Seed, iNeed, Realistic Needs: what our dishes are in their food lists (Cooking/Needs.inl)
}

// Cooking Skill Overhaul's own data in the save: the recipe book, young brews, the dish pool (Cooking/DishPool.inl) and the
// camp rest owed ('REST', Cooking/Survival.inl). Records it
// doesn't know are skipped: older saves' 'CAMP' (the camp's state, before 2026-11-10: Solid Ground Camping keeps its own
// copy) and 'CHNT' (the retired cookware tutorial, before 2026-11-11)
void CookingMenu::RegisterSerialization()
{
	auto serialization = SKSE::GetSerializationInterface();
	if (!serialization) {
		return;
	}
	static constexpr std::uint32_t DISH_USES = 'USES';  // the recipe book: times cooked and when last, per dish
	static constexpr std::uint32_t RECIPE_SORT = 'SORT';  // the recipe book's chosen order
	static constexpr std::uint32_t YOUNG_BREWS = 'BREW';
	static constexpr std::uint32_t INGREDIENT_USES = 'INGU';  // the ingredient lists: times cooked with and when last
	// what this character tasted and the dishes it knows: the scripts keep the real lists and send them when the screen
	// opens; this copy lets item cards and eating use this character's lists from the moment a save loads (2026-11-13)
	static constexpr std::uint32_t TASTED_KNOWN = 'TAST';
	serialization->SetUniqueID('FCOK');
	serialization->SetSaveCallback([](SKSE::SerializationInterface* a_intfc) {
		CookLock lock(g_cookLock);  // the cooking screen may be counting a cook on the UI thread
		DishPoolSave(a_intfc);
		ShareIdleSave(a_intfc);
		CampRestSave(a_intfc);
		AfflictionsSave(a_intfc);
		// version 2 (2026-11-15, brew aging): also each finished and aged brew step and the chests' mornings
		if (a_intfc->OpenRecord(YOUNG_BREWS, 2)) {
			const auto writeText = [&](const std::string& a_text) {
				const auto length = static_cast<std::uint16_t>((std::min<std::size_t>)(a_text.size(), 0xFFFF));
				a_intfc->WriteRecordData(length);
				a_intfc->WriteRecordData(a_text.data(), length);
			};
			a_intfc->WriteRecordData(g_lastMorning);
			const auto count = static_cast<std::uint32_t>(g_youngBrews.size());
			a_intfc->WriteRecordData(count);
			for (const auto& [young, finished] : g_youngBrews) {
				a_intfc->WriteRecordData(young);
				a_intfc->WriteRecordData(finished);
			}
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_brewAges.size()));
			for (const auto& [form, brew] : g_brewAges) {
				a_intfc->WriteRecordData(form);
				a_intfc->WriteRecordData(brew.next);
				a_intfc->WriteRecordData(brew.stars);
				a_intfc->WriteRecordData(brew.step);
				a_intfc->WriteRecordData(brew.baseValue);
				a_intfc->WriteRecordData(brew.scale);
				const std::uint8_t flags = (brew.crit ? 1 : 0) | (brew.house ? 2 : 0) | (brew.water ? 4 : 0);
				a_intfc->WriteRecordData(flags);
				writeText(brew.cat);
				writeText(brew.name);
			}
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_brewStamps.size()));
			for (const auto& [where, morning] : g_brewStamps) {
				a_intfc->WriteRecordData(where.first);
				a_intfc->WriteRecordData(where.second);
				a_intfc->WriteRecordData(morning);
			}
		}
		if (a_intfc->OpenRecord(RECIPE_SORT, 1)) {
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(g_recipeSort));
		}
		if (a_intfc->OpenRecord(INGREDIENT_USES, 1)) {
			// forms that no longer exist (a brewed potion from an older save, a removed mod) are left out
			std::erase_if(g_ingUses, [](const auto& a_entry) {
				return (a_entry.first >> 24) == 0xFF || !RE::TESForm::LookupByID(a_entry.first);
			});
			const auto count = static_cast<std::uint32_t>(g_ingUses.size());
			a_intfc->WriteRecordData(count);
			for (const auto& [item, times] : g_ingUses) {
				const auto when = g_ingLastUsed.contains(item) ? g_ingLastUsed.at(item) : 0u;
				a_intfc->WriteRecordData(item);
				a_intfc->WriteRecordData(times);
				a_intfc->WriteRecordData(when);
			}
		}
		// version 2 (2026-11-13): a dish the modded-foods patcher added is saved by its key (Cooking/Patcher.inl)
		if (a_intfc->OpenRecord(DISH_USES, 2)) {
			a_intfc->WriteRecordData(g_cookClock);
			const auto count = static_cast<std::uint32_t>(g_dishUses.size());
			a_intfc->WriteRecordData(count);
			for (const auto& [dish, times] : g_dishUses) {
				const auto when = g_dishLastCooked.contains(dish) ? g_dishLastCooked.at(dish) : 0u;
				WriteDishRef(a_intfc, dish);
				a_intfc->WriteRecordData(times);
				a_intfc->WriteRecordData(when);
			}
		}
		if (a_intfc->OpenRecord(TASTED_KNOWN, 2)) {
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_tasted.size()));
			for (const auto id : g_tasted) {
				a_intfc->WriteRecordData(id);
			}
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_known.size()));
			for (const auto index : g_known) {
				WriteDishRef(a_intfc, index);
			}
		}
	});
	serialization->SetLoadCallback([](SKSE::SerializationInterface* a_intfc) {
		CookLock lock(g_cookLock);
		g_cosaveRead = true;
		std::uint32_t type = 0, version = 0, length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (DishPoolLoadRecord(a_intfc, type, version) ||
				ShareIdleLoadRecord(a_intfc, type, version) ||
				CampRestLoadRecord(a_intfc, type, version) ||
				AfflictionsLoadRecord(a_intfc, type, version)) {
				continue;
			}
			if (type == YOUNG_BREWS && (version == 1 || version == 2)) {
				// each part is read into a copy and taken only when it read whole, and every brew must be one of our dish
				// slots: an older test save could still turn ordinary Ale into a cooked brew (Codex review 2026-11-16, M11)
				// the whole record is read before any of it is taken: a damaged one can't leave young pairs in place without
				// their aging data (Codex review 2026-11-16 afternoon, L1)
				std::uint32_t count = 0;
				std::int64_t lastMorning = 0;
				static_assert(sizeof(lastMorning) == sizeof(g_lastMorning));
				if (!ReadAll(a_intfc, lastMorning) || !ReadAll(a_intfc, count)) {
					continue;  // a cut-off record: skip it
				}
				bool whole = true;
				std::unordered_map<RE::FormID, RE::FormID> pairs;
				for (std::uint32_t k = 0; k < count; ++k) {
					RE::FormID young = 0, finished = 0, y = 0, f = 0;
					if (!ReadAll(a_intfc, young) || !ReadAll(a_intfc, finished)) {
						whole = false;
						break;  // the record ended early: stop instead of looping through a bad count
					}
					if (a_intfc->ResolveFormID(young, y) && a_intfc->ResolveFormID(finished, f) && y != f && IsPoolDish(y) && IsPoolDish(f)) {
						pairs[y] = f;
					}
				}
				const auto readText = [&](std::string& a_text) {
					std::uint16_t length = 0;
					if (!ReadAll(a_intfc, length)) {
						return false;
					}
					a_text.resize(length);
					return length == 0 || a_intfc->ReadRecordData(a_text.data(), length) == length;
				};
				std::unordered_map<RE::FormID, BrewAge> ages;
				std::map<std::pair<RE::FormID, RE::FormID>, std::int64_t> chests;
				if (whole && version >= 2) {  // version 1 (before brew aging): brews of older saves start aging from their next cook
					std::uint32_t brews = 0;
					whole = ReadAll(a_intfc, brews);
					for (std::uint32_t k = 0; whole && k < brews; ++k) {
						RE::FormID form = 0, next = 0, f = 0, nx = 0;
						BrewAge brew;
						std::uint8_t flags = 0;
						whole = ReadAll(a_intfc, form) && ReadAll(a_intfc, next) && ReadAll(a_intfc, brew.stars) && ReadAll(a_intfc, brew.step) &&
								ReadAll(a_intfc, brew.baseValue) && ReadAll(a_intfc, brew.scale) && ReadAll(a_intfc, flags) && readText(brew.cat) &&
								readText(brew.name);
						if (!whole) {
							break;
						}
						brew.crit = (flags & 1) != 0;
						brew.house = (flags & 2) != 0;
						brew.water = (flags & 4) != 0;
						brew.next = next != 0 && a_intfc->ResolveFormID(next, nx) && IsPoolDish(nx) ? nx : 0;
						if (a_intfc->ResolveFormID(form, f) && IsPoolDish(f)) {
							ages[f] = brew;
						}
					}
					std::uint32_t stamps = 0;
					whole = whole && ReadAll(a_intfc, stamps);
					for (std::uint32_t k = 0; whole && k < stamps; ++k) {
						RE::FormID where = 0, form = 0, w = 0, f = 0;
						std::int64_t morning = 0;
						whole = ReadAll(a_intfc, where) && ReadAll(a_intfc, form) && ReadAll(a_intfc, morning);
						// a chest that no longer exists is left out, and so is a dropped bottle's 0 (bottles in the world don't
						// age any more: his call 2026-11-16)
						if (whole && where != 0 && a_intfc->ResolveFormID(where, w) && a_intfc->ResolveFormID(form, f) && IsPoolDish(f)) {
							chests[{ w, f }] = morning;
						}
					}
				}
				if (!whole) {
					logger::error("Brewing: this save's brew record is cut short; none of its brews are loaded");
					continue;
				}
				g_lastMorning = lastMorning;
				g_youngBrews.insert(pairs.begin(), pairs.end());
				g_brewAges.insert(ages.begin(), ages.end());
				g_brewStamps.insert(chests.begin(), chests.end());
			} else if (type == INGREDIENT_USES && version == 1) {
				std::uint32_t count = 0;
				if (!ReadAll(a_intfc, count)) {
					continue;
				}
				for (std::uint32_t k = 0; k < count; ++k) {
					RE::FormID item = 0, resolved = 0;
					std::uint32_t times = 0, when = 0;
					if (!ReadAll(a_intfc, item) || !ReadAll(a_intfc, times) || !ReadAll(a_intfc, when)) {
						break;
					}
					if (a_intfc->ResolveFormID(item, resolved) && (resolved >> 24) != 0xFF) {
						g_ingUses[resolved] = times;
						g_ingLastUsed[resolved] = when;
					}
				}
			} else if (type == RECIPE_SORT && version == 1) {
				std::uint8_t sort = 0;
				if (ReadAll(a_intfc, sort) && sort < RECIPE_SORTS.size()) {
					g_recipeSort = sort;
				}
			} else if (type == DISH_USES && (version == 1 || version == 2)) {
				std::uint32_t count = 0;
				if (!ReadAll(a_intfc, g_cookClock) || !ReadAll(a_intfc, count)) {
					continue;
				}
				for (std::uint32_t k = 0; k < count; ++k) {
					std::int32_t dish = 0;
					std::uint32_t times = 0, when = 0;
					bool found = true;
					if (!(version == 2 ? ReadDishRef(a_intfc, dish, found) : ReadAll(a_intfc, dish)) ||
						!ReadAll(a_intfc, times) || !ReadAll(a_intfc, when)) {
						break;
					}
					if (found) {
						g_dishUses[dish] = times;
						g_dishLastCooked[dish] = when;
					}
				}
			} else if (type == TASTED_KNOWN && (version == 1 || version == 2)) {
				std::uint32_t count = 0;
				if (!ReadAll(a_intfc, count)) {
					continue;
				}
				bool ok = true;
				for (std::uint32_t k = 0; k < count; ++k) {
					RE::FormID id = 0, resolved = 0;
					if (!ReadAll(a_intfc, id)) {
						ok = false;
						break;
					}
					if (a_intfc->ResolveFormID(id, resolved)) {
						g_tasted.insert(resolved);
					}
				}
				if (ok && ReadAll(a_intfc, count)) {
					for (std::uint32_t k = 0; k < count; ++k) {
						std::int32_t index = 0;
						bool found = true;
						if (!(version == 2 ? ReadDishRef(a_intfc, index, found) : ReadAll(a_intfc, index))) {
							break;
						}
						if (found) {
							g_known.insert(index);
						}
					}
				}
			}
		}
		// brews whose dish slots have no dish in this save (its DISH record was damaged and left out) are dropped: aging
		// would swap one blank slot for another (Codex review 2026-11-16d, L12). Checked once every record is read
		{
			const auto defined = [](RE::FormID a_id) {
				const auto form = RE::TESForm::LookupByID(a_id);
				std::scoped_lock lock(g_dishLock);
				return form && g_dishDefs.contains(form->GetLocalFormID());
			};
			const auto before = g_youngBrews.size() + g_brewAges.size();
			std::erase_if(g_youngBrews, [&](const auto& a_pair) { return !defined(a_pair.first) || !defined(a_pair.second); });
			std::erase_if(g_brewAges, [&](const auto& a_pair) { return !defined(a_pair.first); });
			for (auto& [form, brew] : g_brewAges) {  // (a next step with no dish is made again when it is needed; review 2026-11-16e, L3)
				if (brew.next != 0 && !defined(brew.next)) {
					brew.next = 0;
				}
			}
			std::erase_if(g_brewStamps, [&](const auto& a_pair) { return !defined(a_pair.first.second); });
			if (const auto after = g_youngBrews.size() + g_brewAges.size(); after < before) {
				logger::warn("Brewing: {} brew(s) left out: their dishes are missing from this save", before - after);
			}
		}
	});
	serialization->SetRevertCallback([](SKSE::SerializationInterface*) {
		CookLock lock(g_cookLock);
		g_cosaveRead = false;
		DishPoolRevert();
		SharedTableReset();
		HolyReset();
		SurvivalReset();  // stepped drains read afresh, camp rest cleared (Cooking/Survival.inl)
		g_dishUses.clear();
		g_dishLastCooked.clear();
		g_ingUses.clear();
		g_ingLastUsed.clear();
		g_youngBrews.clear();
		g_brewAges.clear();
		g_brewStamps.clear();
		g_brewSwapAdds.clear();
		g_brewSwapRemoves.clear();
		g_lastMorning = -1;
		g_cookClock = 0;
		g_recipeSort = 0;
		// the character being left: what it tasted and knows, and potion flavours cached by form ID (a player-made
		// potion's ID means another potion in another save). This save's lists come from its 'TAST' record, and the
		// scripts send theirs when the screen opens
		g_tasted.clear();
		g_known.clear();
		g_sessionTasted.clear();
		g_sessionLearned.clear();
		g_flavourCache.clear();
		{
			// the copy of the player holds the left game's items (a player-made potion is freed by the load)
			std::scoped_lock viewLock(g_viewLock);
			g_view = {};
		}
	});
}

void CookingMenu::RegisterPapyrusFunctions()
{
	SKSE::GetPapyrusInterface()->Register(RegisterPapyrus);
}

void CookingMenu::GameLoading()
{
	g_loadingSave = true;
	++g_gameGeneration;
	HolyReset();
	SurvivalReset();  // stepped drains read afresh, camp rest cleared (Cooking/Survival.inl)
	DropGameTimers();  // the previous game's delayed jobs dropped (Core/Timers.inl)
	HoldResetForLoad();  // Survival's constant hunger rate back to its own value (Cooking/Thirst.inl)
}

// The pot's script only hears the screen's Cook / Season / Taste requests after it registered for them, which used to
// happen only in FC_Core.OpenMenu: sitting straight at a pot in a new game never ran it, so cooking did nothing
// (found 2026-11-03). This hidden self spell's effect script registers them; the game runs it, we only cast.
static void RegisterMenuListener()
{
	auto data = RE::TESDataHandler::GetSingleton();
	auto spell = data ? data->LookupForm<RE::SpellItem>(0xA75, PLUGIN) : nullptr;
	auto player = Player();
	auto caster = player ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
	if (spell && caster) {
		caster->CastSpellImmediate(spell, false, player, 1.0f, false, 0.0f, player);
		logger::info("Cooking scripts' listener cast (the pot's script hears the plugin's mod events)");
	} else {
		logger::warn("Cooking screen listener: FC_MenuListen not found in {}; cooking may not reach the scripts", PLUGIN);
	}
}

void CookingMenu::GameLoadFailed()
{
	g_loadingSave = false;  // the game is left as it was (often the main menu): the watchers mustn't wait forever
	logger::warn("A save failed to load: Cooking Skill Overhaul sets nothing up for it");
}

void CookingMenu::GameLoaded(bool a_newGame)
{
	g_loadingSave = false;
	g_cookPlace = 0;  // no cook of the game we left is running here
	CaptureView(false);  // this character's perks and ring for the item cards, right away
	ReconcileDishPool();  // this game's dishes filled in, other games' blank again (Cooking/DishPool.inl)
	if (a_newGame) {
		DropGameTimers();  // a new game has no kPreLoadGame before it
		HoldResetForLoad();  // nor does it reset the hold (Cooking/Thirst.inl)
	}
	// another save: what this session learned by eating belongs to the one we left
	{
		CookLock lock(g_cookLock);
		g_sessionTasted.clear();
		g_sessionLearned.clear();
	}
	// the hidden ability whose script gives the vampire and werewolf feeding bonuses (FC_FeedWatchEffectScript)
	auto data = RE::TESDataHandler::GetSingleton();
	auto watch = data ? data->LookupForm<RE::SpellItem>(0x979, PLUGIN) : nullptr;
	if (auto player = Player(); watch && player && !player->HasSpell(watch)) {
		player->AddSpell(watch);
	}
	// a moment later, once the player is fully in the world: the pot's script listens for the cooking screen
	After(1000, []() { RegisterMenuListener(); });

	// a save whose .skse file is missing (saved while SKSE wasn't running, or copied without it: Steam Cloud only copies
	// the .ess), or whose dish record is damaged, has lost what every cooked dish is: they show as blanks. New dishes take
	// slots no old dish has (FC_DishPoolNext, Cooking/DishPool.inl), so none turns into another dish. Said plainly in a
	// message box, hard to miss (his call 2026-11-14), when the player carries such dishes or has cooked in this game
	if (!a_newGame && !g_dishRecordRead) {
		std::int32_t dishes = 0;
		if (auto player = Player()) {
			for (const auto& [item, data] : player->GetInventory()) {
				const auto* file = item ? item->GetFile(0) : nullptr;
				const auto local = item ? item->GetLocalFormID() : 0u;
				if (file && Lower(std::string(file->GetFilename())) == Lower(PLUGIN) && local >= DISH_POOL_FIRST && local <= DISH_POOL_LAST && data.first > 0) {
					dishes += data.first;
				}
			}
		}
		if (dishes > 0 || DishPoolNextSaved() > DISH_POOL_FIRST) {
			logger::warn("This save has no Cooking Skill Overhaul dish data (.skse file missing or damaged); the player carries {} cooked dishes", dishes);
			After(3000, []() {
				// three paragraphs, each its own line in the translation file (a line there can't hold a line break)
				const std::string text = std::string(FC::Tr("Cooking Skill Overhaul: this save is missing its SKSE data (the .skse file saved next to it). The dishes you cooked before, carried or stored, have lost their names and effects. Dishes you cook from now on are fine.")) +
				                         "\n\n" + FC::Tr("This happens when the game was saved while SKSE wasn't running, or when a save was copied without its .skse file (Steam Cloud copies only the .ess file).") +
				                         "\n\n" + FC::Tr("To get your dishes back, load an older save that still has its .skse file.");
				RE::DebugMessageBox(text.c_str());
			});
		}
	}

}

void CookingMenu::Open()
{
	// again here: cheap, and covers a save whose listener never got registered. Cast from the main thread (Open is also
	// called from the pot's menu event)
	SKSE::GetTaskInterface()->AddTask([]() { RegisterMenuListener(); });
	CookLock lock(g_cookLock);
	// What was learned by eating this session: keep it, and tell the scripts again now that the pot's script
	// is surely listening (it starts listening when a pot is first used). Storing twice changes nothing.
	for (auto id : g_sessionTasted) {
		g_tasted.insert(id);
		// the typed lookup rejects every item (TESBoundObject has no form type of its own)
		if (auto form = RE::TESForm::LookupByID(id); form && form->IsBoundObject()) {
			SendToScripts("FC_MenuTaste", { static_cast<RE::TESBoundObject*>(form) });
		}
	}
	for (auto index : g_sessionLearned) {
		g_known.insert(index);
		EatWatcher::SendLearned(index);
	}
	logger::info("Opening the cooking menu ({} dishes known, {} ingredients tasted)", g_known.size(), g_tasted.size());
	if (auto queue = RE::UIMessageQueue::GetSingleton()) {
		queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
	}
}

void CookingMenu::Close()
{
	if (auto queue = RE::UIMessageQueue::GetSingleton()) {
		queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
	}
}

bool CookingMenu::LoadData()
{
	g_dishes.clear();
	g_stars.clear();
	std::ifstream file(MENU_FILE);
	if (!file) {
		logger::error("Could not open {}: the cooking screen has no recipes", MENU_FILE);
		return false;
	}
	std::size_t skipped = 0;
	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const auto f = Split(line, '\t');
		if (f.size() >= 4 && f[0] == "I") {
			// I <plugin:id> <name> <stars>
			if (auto item = LookupItem(f[1])) {
				g_stars[item] = std::atoi(f[3].c_str());
			}
		} else if (f.size() >= 7 && f[0] == "D") {
			// D <index> <name> <tier> <drink> <ferment> <slot>... ; slot = "label~plugin:id|plugin:id"
			Dish dish;
			dish.index = std::atoi(f[1].c_str());
			dish.name = f[2];
			dish.tier = std::atoi(f[3].c_str());
			dish.drink = f[4] == "1";
			bool ok = true;
			for (std::size_t i = 6; i < f.size(); ++i) {
				Slot slot;
				const auto tilde = f[i].find('~');
				slot.label = tilde == std::string::npos ? std::string() : f[i].substr(0, tilde);
				for (const auto& id : Split(tilde == std::string::npos ? f[i] : f[i].substr(tilde + 1), '|')) {
					if (auto item = LookupItem(id)) {
						slot.options.push_back(item);
					}
				}
				if (slot.options.empty()) {
					ok = false;  // needs content that is not installed
					break;
				}
				dish.slots.push_back(std::move(slot));
			}
			if (ok && !dish.slots.empty()) {
				g_dishes.push_back(std::move(dish));
			} else {
				++skipped;
			}
		}
	}
	logger::info("Recipe data: {} dishes ({} skipped: content not installed), {} ingredients", g_dishes.size(), skipped, g_stars.size());
	return !g_dishes.empty();
}

CookingMenu::Counts CookingMenu::PlayerCounts()
{
	Counts counts;
	// the screen runs on the UI thread: it reads the copy the main thread keeps (Cooking/PlayerView.inl)
	const bool ringWorn = RingWorn();
	for (const auto& [item, count] : CarriedCounts()) {
		if (item && count > 0 && !Hidden(item, ringWorn)) {
			counts[item] = count;
		}
	}
	return counts;
}

// Fills each recipe slot: what is already in the pot first, then what you carry, always the cheapest
// match (fewest stars, then lowest value, then the one carried most). A flexible slot never picks a
// three-star ingredient for you, and two slots never take the same copy ("a second, different vegetable").
std::vector<CookingMenu::Pick> CookingMenu::Resolve(const Dish& a_dish, const Counts& a_counts, const Pot& a_pot)
{
	std::vector<Pick> picks;
	std::unordered_map<RE::TESBoundObject*, std::int32_t> taken;
	// two slots may take two copies of the same item when you carry both, except in a dish that wants different
	// items (Vegetable Soup, Wine): a patched recipe can ask for two of one thing (reviews 2026-11-15)
	const bool different = Int(std::format("dish.{}.dif", a_dish.index)) == 1;
	for (const auto& slot : a_dish.slots) {
		Pick pick{ &slot, nullptr };
		std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t> bestKey{ 2, 99, std::numeric_limits<std::int32_t>::max(), 0 };
		for (auto item : slot.options) {
			const auto it = a_counts.find(item);
			const std::int32_t have = it != a_counts.end() ? it->second : 0;
			const auto used = taken.contains(item) ? taken.at(item) : 0;
			if (have <= used || (different && used > 0)) {
				continue;
			}
			const auto st = g_stars.contains(item) ? g_stars.at(item) : 0;
			const bool inPot = std::ranges::find(a_pot, item) != a_pot.end();
			// never pick a rare (three-star) ingredient for you, unless you put it in the pot yourself
			if (st >= 3 && slot.options.size() > 1 && !inPot) {
				continue;
			}
			// nor a forbidden ingredient as one choice among others: only if you put it in the pot yourself
			if (!inPot && slot.options.size() > 1 && IngOf(item).need == "ring") {
				continue;
			}
			const std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t> key{ inPot ? 0 : 1, st, item->GetGoldValue(), -have };
			if (!pick.item || key < bestKey) {
				pick.item = item;
				bestKey = key;
			}
		}
		if (pick.item) {
			taken[pick.item] += 1;
		}
		picks.push_back(pick);
	}
	// a full set still has to make this dish under the cooking rules (an alcoholic drink carried may be added, as
	// RecipeFill does): Chaurus Eggs left in the pot fill Braided Bread's egg slot, but without a drink or Long Simmer
	// the bread isn't made from them, while the Chicken's Egg carried would make it (Codex review 2026-11-16e, L9)
	// ...and the pot must make THIS dish when cooked: the cook picks among all dishes, so a set that also fits a dish
	// with more named ingredients makes that one (Vegetable Soup's cabbage, potato and ash yam make Cabbage Potato Soup;
	// Codex review 2026-11-16f, M1). The cheap check on this dish alone first
	const bool longSimmer = HasTreePerk(PERK_LONG_SIMMER);
	const auto cooks = [&](const std::vector<RE::TESBoundObject*>& a_items) {
		std::vector<std::string> keys;
		std::unordered_map<RE::TESBoundObject*, std::int32_t> used;
		for (auto item : a_items) {
			keys.push_back(IngOf(item).key);
			++used[item];
		}
		return WithShortcutHelpers(std::move(keys), std::move(used), a_counts, a_pot, longSimmer, a_dish.index);
	};
	if (AllPicked(picks)) {
		std::vector<RE::TESBoundObject*> items;
		for (const auto& pick : picks) {
			items.push_back(pick.item);
		}
		if (cooks(items)) {
			return picks;
		}
	}
	// a slot left empty because an earlier one took what it needed while it could have used something else ("any
	// vegetable" took the only Carrot, then "Carrot" found none; Codex review 2026-11-16d, L10): every way of filling
	// the slots is tried, each slot's choices in the same order as above, and the first that fills them all is used
	// (insect eggs and other unappetizing items need a drink in an egg slot: with no drink carried and no Long Simmer
	// they can't make it, so they aren't tried; fewer combinations on a big inventory; Codex review 2026-11-16f, L7)
	const bool drinkCarried = std::ranges::any_of(a_counts, [](const auto& a_e) { return a_e.first && a_e.second > 0 && IngOf(a_e.first).alcohol; });
	const bool garlicCarried = std::ranges::any_of(a_counts, [](const auto& a_e) { return a_e.first && a_e.second > 0 && IngOf(a_e.first).garlic; });
	// (every slot since 2026-11-24, not only egg slots: see ChooseDish; brews need no drink, "nh" dishes no helper)
	const bool helperFree = longSimmer || Int(std::format("dish.{}.nh", a_dish.index)) == 1;
	const bool brewed = Int(std::format("dish.{}.fe", a_dish.index)) == 1;
	// the search runs twice at most: what is in the pot first, then (when the step cap stopped it) without that
	// preference, so a pot of items that fit but never make the dish can't use up every try (Codex review 2026-11-16g, 7)
	const auto rank = [&](bool a_potFirst) {
		std::vector<std::vector<RE::TESBoundObject*>> choices;
		for (const auto& slot : a_dish.slots) {
			std::vector<std::pair<std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t>, RE::TESBoundObject*>> ranked;
			for (auto item : slot.options) {
				const auto it = a_counts.find(item);
				const std::int32_t have = it != a_counts.end() ? it->second : 0;
				if (have <= 0) {
					continue;
				}
				const auto st = g_stars.contains(item) ? g_stars.at(item) : 0;
				const bool inPot = std::ranges::find(a_pot, item) != a_pot.end();
				if ((st >= 3 || IngOf(item).need == "ring") && slot.options.size() > 1 && !inPot) {
					continue;
				}
				const auto& need = IngOf(item).need;
				if (!helperFree && ((need == "alc" && !brewed && !drinkCarried) || (need == "garlic" && !garlicCarried))) {
					continue;
				}
				ranked.push_back({ { inPot && a_potFirst ? 0 : 1, st, item->GetGoldValue(), -have }, item });
			}
			std::ranges::stable_sort(ranked, [](const auto& a, const auto& b) { return a.first < b.first; });
			std::vector<RE::TESBoundObject*> list;
			for (const auto& r : ranked) {
				list.push_back(r.second);
			}
			choices.push_back(std::move(list));
		}
		return choices;
	};
	auto choices = rank(true);
	// a slot nothing carried can fill: no way of filling them all, so the search isn't walked (it would try every
	// choice of the slots before it, on every list build; review 2026-11-16e, L1)
	if (std::ranges::any_of(choices, [](const auto& a_list) { return a_list.empty(); })) {
		return picks;
	}
	std::vector<RE::TESBoundObject*> chosen(choices.size(), nullptr);
	std::unordered_map<RE::TESBoundObject*, std::int32_t> used;
	int steps = 0;  // (a few slots of a few choices each; never more than this many tries per pass)
	std::function<bool(std::size_t)> fill = [&](std::size_t a_slot) -> bool {
		if (a_slot == choices.size()) {
			return cooks(chosen);
		}
		for (auto item : choices[a_slot]) {
			if (++steps > 1500) {
				return false;
			}
			const auto n = used.contains(item) ? used.at(item) : 0;
			if (n >= a_counts.at(item) || (different && n > 0)) {
				continue;
			}
			used[item] = n + 1;
			chosen[a_slot] = item;
			if (fill(a_slot + 1)) {
				return true;
			}
			used[item] = n;
		}
		return false;
	};
	bool found = fill(0);
	if (!found && steps > 1500 && !a_pot.empty()) {
		choices = rank(false);
		used.clear();
		std::ranges::fill(chosen, nullptr);
		steps = 0;
		found = fill(0);
	}
	if (found) {
		for (std::size_t i = 0; i < picks.size() && i < chosen.size(); ++i) {
			picks[i].item = chosen[i];
		}
	}
	return picks;
}

bool CookingMenu::AllPicked(const std::vector<Pick>& a_picks)
{
	return std::ranges::all_of(a_picks, [](const Pick& a_pick) { return a_pick.item != nullptr; });
}

std::string CookingMenu::PickName(const Pick& a_pick)
{
	// labels read like item names on the Requires line: "Any vegetable", "An apple"
	// the rules' label ("any dairy") in the player's language, first letter a capital
	std::string label = a_pick.slot && !a_pick.slot->label.empty() ? FC::UpperFirst(FC::Tr(a_pick.slot->label.c_str())) : std::string();
	if (a_pick.item) {
		const std::string name = a_pick.item->GetName();
		return label.empty() ? name : FC::TrF("{0} ({1})", label, name);
	}
	if (!label.empty()) {
		return label;
	}
	return a_pick.slot && !a_pick.slot->options.empty() ? a_pick.slot->options.front()->GetName() : "?";
}

CookingMenu::CookingMenu()
{
	g_auxArt = -1;  // a new screen still has the menu file's own art on its third button: set it on the first update
	g_preferDish = -1;  // a new screen: no recipe picked yet
	using Flag = RE::UI_MENU_FLAGS;
	for (auto flag : { Flag::kPausesGame, Flag::kUsesCursor, Flag::kUsesMenuContext, Flag::kDisablePauseMenu,
			 Flag::kUpdateUsesCursor, Flag::kTopmostRenderedMenu, Flag::kHasButtonBar }) {
		menuFlags.set(flag);
	}
	// at a pot or campfire the game keeps running, like vanilla crafting menus, so the stirring animation plays
	// behind the screen (a paused game freezes the player mid-animation)
	if (g_overVanilla) {
		menuFlags.reset(Flag::kPausesGame);
	}
	depthPriority = 3;
	inputContext = Context::kMenuMode;

	auto scaleform = RE::BSScaleformManager::GetSingleton();
	bool gotDef = false;
	const bool loaded = scaleform->LoadMovieEx(this, FILE_NAME, [&gotDef](RE::GFxMovieDef* a_def) { gotDef = a_def != nullptr; });
	if (!loaded || !uiMovie) {
		logger::error("Could not load Interface/{}.swf (file read: {}, movie created: {})", FILE_NAME, gotDef, uiMovie.get() != nullptr);
		// never leave an empty, game-pausing menu on screen
		menuFlags.reset(RE::UI_MENU_FLAGS::kPausesGame);
		Close();
		return;
	}

	// route gfx.io.GameDelegate.call(...) from the menu to Accept() below
	fxDelegate.reset(new RE::FxDelegate());
	fxDelegate->RegisterHandler(this);
	uiMovie->SetState(RE::GFxState::StateType::kExternalInterface, fxDelegate.get());
	fxDelegate->Release();
	logger::info("Cooking menu loaded");
}

void CookingMenu::Accept(CallbackProcessor* a_processor)
{
	a_processor->Process("CloseMenu", OnCloseMenu);
	a_processor->Process("PlaySound", OnPlaySound);
	a_processor->Process("SetSelectedItem", OnSetSelectedItem);
	a_processor->Process("ChooseItem", OnChooseItem);
	a_processor->Process("CraftSelectedItem", OnChooseItem);
	a_processor->Process("CraftButtonPress", OnCraftButton);
	a_processor->Process("AuxButtonPress", OnAuxButton);
	a_processor->Process("CanFadeItemInfo", OnCanFadeItemInfo);
	// calls the vanilla menu makes that the cooking screen does not use
	for (const char* name : { "SetSelectedCategory", "ShowItem3D", "StartMouseRotation", "StopMouseRotation",
			 "EndItemRename", "CalculateCharge", "SliderClose" }) {
		a_processor->Process(name, OnIgnore);
	}
}

void CookingMenu::AdvanceMovie(float a_interval, std::uint32_t a_currentTime)
{
	RE::IMenu::AdvanceMovie(a_interval, a_currentTime);
	// the menu's classes register on its first frame, so set it up after that
	if (!_initialized && uiMovie) {
		_initialized = true;
		Setup();
	} else if (_initialized && _framesAfterSetup >= 0 && ++_framesAfterSetup > 3) {
		// like the vanilla pot: open the list right away, once the category blade has slid in
		_framesAfterSetup = -1;
		// Food first, unless it is empty (nothing tasted yet): then Recipes, so the list never opens blank
		if (!_anyFood) {
			uiMovie->SetVariable("_root.Menu.InventoryLists.CategoriesList.selectedIndex", 1.0);
		}
		RE::GFxValue show[2]{ false, true };
		uiMovie->Invoke("_root.Menu.InventoryLists.ShowItemsList", nullptr, show, 2);
	}
}

RE::UI_MESSAGE_RESULTS CookingMenu::ProcessMessage(RE::UIMessage& a_message)
{
	// closing the cooking screen also closes the pot's hidden vanilla menu, so the player stands up
	if (a_message.type == RE::UI_MESSAGE_TYPE::kHide && (_shown3D || _began3D)) {
		if (auto manager = RE::Inventory3DManager::GetSingleton()) {
			manager->UnloadInventoryItem();
			if (_began3D) {
				manager->End3D();
			}
		}
		_shown3D = nullptr;
		_began3D = false;
	}
	if (a_message.type == RE::UI_MESSAGE_TYPE::kHide && g_overVanilla) {
		g_overVanilla = false;
		g_place = 0;  // the pot's close event then finds g_overVanilla already off and wouldn't reset it (2026-11-13 review)
		if (g_toVanilla) {
			// the emergency fallback: the vanilla crafting menu under us stays open and shows again
			g_toVanilla = false;
			if (auto ui = RE::UI::GetSingleton()) {
				if (auto vanilla = ui->GetMenu(RE::CraftingMenu::MENU_NAME); vanilla && vanilla->uiMovie) {
					vanilla->uiMovie->SetVisible(true);
				}
			}
			logger::info("Emergency fallback: showing the station's vanilla crafting menu");
		} else if (auto queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::CraftingMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
	}
	return RE::IMenu::ProcessMessage(a_message);
}

void CookingMenu::Setup()
{
	auto movie = uiMovie.get();
	movie->Invoke("_root.Menu.Initialize", nullptr, nullptr, 0);

	// The vanilla menu crafts the highlighted item when you click anywhere right of the list (and on a quick click
	// over the item preview). Here that would put whatever is highlighted in the pot, so only clicks on the list count.
	static auto* potLineClick = new PotLineClick();  // kept for the game's lifetime; the movie holds its own reference
	RE::GFxValue onClick;
	movie->CreateFunction(&onClick, potLineClick);
	movie->SetVariable("_root.Menu.ItemsListInputCatcher.onMouseDown", onClick);
	movie->SetVariable("_root.Menu.onMouseRotationFastClick", RE::GFxValue());

	// keyboard or controller buttons, whichever was used last (MenuKeys follows it while the screen is open)
	g_platformShown = g_padInUse ? 1 : 0;
	RE::GFxValue platform[2]{ static_cast<double>(g_platformShown), false };
	movie->Invoke("_root.Menu.SetPlatform", nullptr, platform, 2);

	movie->SetVariable("_root.Menu.MenuDescription.text", FC::Tr("Cooking Pot: up to 5 ingredients"));
	movie->SetVariable("_root.Menu.bCanCraft", true);  // shows the R button and lets it be clicked

	// Food first, Recipes, then the ingredients: All, Untasted, the eight flavours, Fillers, Enablers.
	// Food, Recipes and All always show; the others hide while you carry nothing for them.
	std::vector<RE::GFxValue> categories;
	auto add = [&](const std::string& a_text, std::uint32_t a_flag, bool a_alwaysShow) {
		categories.push_back(MakeString(movie, a_text));
		categories.emplace_back(static_cast<double>(a_flag));
		categories.emplace_back(a_alwaysShow);
	};
	add(FC::Tr("Food"), FLAG_FOOD, true);
	add(FC::Tr("Recipes"), FLAG_RECIPES, true);
	add("", 0, true);  // divider
	add(FC::Tr("All"), FLAG_ALL, true);
	add(FC::Tr("Untasted"), FLAG_UNTASTED, false);
	for (std::size_t i = 0; i < FLAVOURS.size(); ++i) {
		add(FC::Tr(FLAVOURS[i]), FLAG_FLAVOUR0 << i, false);
	}
	add("", 0, true);  // divider
	add(FC::Tr("Fillers"), FLAG_FILLERS, false);
	add(FC::Tr("Enablers"), FLAG_ENABLERS, false);
	add(FC::Tr("Potions"), FLAG_POTIONS, false);
	add(FC::Tr("Misc"), FLAG_MISC, false);  // at the bottom: rarely used (his call, 2026-11-14)
	add(FC::Tr("Vanilla"), FLAG_VANILLA, false);  // the emergency fallback, only with its option on
	for (std::size_t i = 0; i < categories.size(); ++i) {
		if (categories[i].IsString()) {
			// category names in capitals, like the vanilla crafting categories
			std::string text = categories[i].GetString();
			text = FC::UpperText(text);  // any language's letters, not only A-Z
			categories[i] = MakeString(movie, text);
		}
	}
	movie->Invoke("_root.Menu.InventoryLists.SetCategoriesList", nullptr, categories.data(), static_cast<std::uint32_t>(categories.size()));
	movie->SetVariable("_root.Menu.AdditionalDescription.text", "");  // the file's placeholder until an entry is shown

	_lastSkill = static_cast<std::int32_t>(CookingSkill());
	Populate();
	UpdateSkillBar();
	UpdateButtons();
}

// Bottom right: the Cooking skill bar, like Alchemy and Smithing show theirs. Refreshed after every
// cook, so the bar fills as you gain experience.
void CookingMenu::UpdateSkillBar()
{
	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	RE::GFxValue skill[3]{ MakeString(movie, FC::Tr("Cooking")), static_cast<double>(CookingSkill()), static_cast<double>(CookingRatio()) * 100.0 };
	movie->Invoke("_root.Menu.BottomBarInfo.UpdateCraftingInfo", nullptr, skill, 3);
}

std::size_t CookingMenu::Copies(RE::TESBoundObject* a_item) const
{
	return static_cast<std::size_t>(std::ranges::count(_pot, a_item));
}

// Rebuilds the list: recipes (discovered dishes, the ones you can make first, then up to three ??? lines
// for dishes you could make with what you carry, then a count of the rest), then every ingredient you
// carry, each tagged with the categories it belongs to.
void CookingMenu::Populate()
{
	CookLock lock(g_cookLock);  // what is known and tasted, and the counts, can't change while the list is built
	ClearWinMemo();  // the inventory, perks and unlocked dishes may have changed since the last build
	_busy = false;
	_anyFood = false;
	_entries.clear();
	const auto counts = PlayerCounts();

	// the pot never holds more copies than you carry (you may have eaten or dropped some)
	{
		Pot kept;
		for (auto item : _pot) {
			const auto it = counts.find(item);
			if (it != counts.end() && std::ranges::count(kept, item) < it->second) {
				kept.push_back(item);
			}
		}
		_pot = std::move(kept);
	}

	std::vector<std::pair<const Dish*, bool>> known;
	std::vector<const Dish*> could;
	const bool ringWorn = RingWorn();
	for (const auto& dish : g_dishes) {
		// Namira's dishes (a slot only forbidden ingredients fill) stay off the list unless the ring is worn
		if (!ringWorn && std::ranges::any_of(dish.slots, [](const Slot& a_slot) {
				return !a_slot.options.empty() && std::ranges::all_of(a_slot.options, [](RE::TESBoundObject* a_item) { return Hidden(a_item, false); });
			})) {
			continue;
		}
		// each cooking place lists only what it can make: grill and roast at a campfire, baking at an oven, the rest
		// at a pot
		if (!AllowedHere(dish.index)) {
			continue;
		}
		const bool canMake = !RecipeFill(dish, counts).empty();
		if (g_known.contains(dish.index)) {
			known.emplace_back(&dish, canMake);
		} else if (canMake) {
			could.push_back(&dish);
		}
	}
	// the recipe book's order (Q): dishes you can make now first, then the most cooked, the latest cooked, or A to Z
	const auto uses = [](const Dish* a_dish) { const auto it = g_dishUses.find(a_dish->index); return it != g_dishUses.end() ? it->second : 0u; };
	const auto last = [](const Dish* a_dish) { const auto it = g_dishLastCooked.find(a_dish->index); return it != g_dishLastCooked.end() ? it->second : 0u; };
	std::ranges::sort(known, [&](const auto& a, const auto& b) {
		if (a.second != b.second) {
			return a.second;
		}
		if (g_recipeSort == 0 && uses(a.first) != uses(b.first)) {
			return uses(a.first) > uses(b.first);
		}
		if (g_recipeSort == 1 && last(a.first) != last(b.first)) {
			return last(a.first) > last(b.first);
		}
		// by the name the player sees (his language's, not the English key; review 2026-11-15)
		return std::string_view(FC::Tr(a.first->name.c_str())) < std::string_view(FC::Tr(b.first->name.c_str()));
	});
	std::ranges::sort(could, [](const Dish* a, const Dish* b) {
		return a->slots.size() != b->slots.size() ? a->slots.size() < b->slots.size() : a->name < b->name;
	});
	for (const auto& item : known) {
		_entries.push_back({ Kind::kRecipe, item.first, 0, nullptr });
	}
	for (std::size_t i = 0; i < could.size() && i < 3; ++i) {
		_entries.push_back({ Kind::kUnknown, could[i], 0, nullptr });
	}
	if (could.size() > 3) {
		_entries.push_back({ Kind::kMore, nullptr, static_cast<std::int32_t>(could.size() - 3), nullptr });
	}
	if (_entries.empty()) {
		_entries.push_back({ Kind::kEmpty, nullptr, 0, nullptr });
	}

	std::vector<std::pair<std::string, RE::TESBoundObject*>> carried;  // (name as listed, item)
	for (const auto& [item, count] : counts) {
		if (item->Is(RE::FormType::Ingredient, RE::FormType::AlchemyItem, RE::FormType::Misc) && IngOf(item).Known()) {
			const char* name = item->GetName();
			if (name && *name) {
				// potions sort by the flavour they add: "Chilly: Potion of Resist Fire"
				const auto flavour = PotionFlavour(item);
				carried.emplace_back(flavour.empty() ? std::string(name) : FC::TrF("{0}: {1}", FC::Tr(flavour.c_str()), name), item);
			}
		}
	}
	// the same order as the recipe book (Q): the ingredients cooked with most, the latest used, or A to Z
	const auto ingUses = [](const RE::TESBoundObject* a_item) { const auto it = g_ingUses.find(a_item->GetFormID()); return it != g_ingUses.end() ? it->second : 0u; };
	const auto ingLast = [](const RE::TESBoundObject* a_item) { const auto it = g_ingLastUsed.find(a_item->GetFormID()); return it != g_ingLastUsed.end() ? it->second : 0u; };
	std::ranges::sort(carried, [&](const auto& a, const auto& b) {
		if (g_recipeSort == 0 && ingUses(a.second) != ingUses(b.second)) {
			return ingUses(a.second) > ingUses(b.second);
		}
		if (g_recipeSort == 1 && ingLast(a.second) != ingLast(b.second)) {
			return ingLast(a.second) > ingLast(b.second);
		}
		return a.first < b.first;
	});
	for (const auto& entry : carried) {
		_entries.push_back({ Kind::kIngredient, nullptr, 0, entry.second });
		// an Earthy potion also neutralizes poison (his OK 2026-11-22: every Earthy potion): list it under Enablers too, without its flavour prefix
		if (const auto ing = IngOf(entry.second); ing.potion && ing.garlic) {
			_entries.push_back({ Kind::kIngredient, nullptr, 0, entry.second, true });
		}
	}

	// Misc, at the bottom: other mods' recipes at this pot or oven that our rules don't cook, those whose own conditions
	// pass (like vanilla, a recipe the mod hasn't given you yet stays hidden); the ones you can make first, then A to Z
	{
		const auto all = CarriedCounts();
		std::vector<std::pair<bool, std::size_t>> misc;
		for (std::size_t i = 0; i < g_misc.size(); ++i) {
			if (ViewMiscShown(i) && !g_misc[i].hidden) {  // hidden: the player's choice on the Misc recipes page
				misc.emplace_back(MiscMakeable(i, all), i);
			}
		}
		std::ranges::sort(misc, [](const auto& a, const auto& b) {
			if (a.first != b.first) {
				return a.first;
			}
			return NameOf(g_misc[a.second].recipe->createdItem) < NameOf(g_misc[b.second].recipe->createdItem);
		});
		for (const auto& [makeable, index] : misc) {
			Entry entry{ Kind::kMisc, nullptr, 0, nullptr };
			entry.misc = index;
			_entries.push_back(entry);
		}
	}

	// the emergency fallback (an option): the pot's or oven's own vanilla crafting menu, under ours
	if (FC::GetOptions().vanillaCrafting && g_overVanilla && (g_place == 1 || g_place == 3)) {
		_entries.push_back({ Kind::kVanilla, nullptr, 0, nullptr });
	}

	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	const auto allCarried = CarriedCounts();
	RE::GFxValue list;
	movie->GetVariable(&list, "_root.Menu.InventoryLists.ItemsList.entryList");
	list.ClearElements();
	for (const auto& entry : _entries) {
		std::string text;
		bool enabled = true;
		std::uint32_t flags = FLAG_RECIPES;
		double count = 0.0;
		double equip = 0.0;
		switch (entry.kind) {
		case Kind::kRecipe:
			text = FC::Tr(entry.dish->name.c_str());  // the recipe names are translatable lines too
			enabled = !NeedsPot(*entry.dish) && !RecipeFill(*entry.dish, counts).empty();
			break;
		case Kind::kUnknown:
			text = "???";
			enabled = false;
			break;
		case Kind::kMore:
			text = FC::TrF(entry.more == 1 ? "+{0} more dish you could make" : "+{0} more dishes you could make", entry.more);
			enabled = false;
			break;
		case Kind::kEmpty:
			text = FC::Tr("No recipes yet");
			enabled = false;
			break;
		case Kind::kVanilla:
			text = FC::Tr("Vanilla crafting menu");  // short: the list column cuts longer names (seen in game 2026-11-14)
			flags = FLAG_VANILLA;
			break;
		case Kind::kMisc:
			{
				const auto* recipe = g_misc[entry.misc].recipe;
				text = NameOf(recipe->createdItem);
				if (recipe->data.numConstructed > 1) {
					text += std::format(" ({})", recipe->data.numConstructed);
				}
				enabled = MiscMakeable(entry.misc, allCarried);
				flags = FLAG_MISC;
				break;
			}
		case Kind::kIngredient:
			{
				const auto ing = IngOf(entry.item);
				text = ing.potion && !entry.enablerCopy ? FC::TrF("{0}: {1}", FC::Tr(ing.flavour.c_str()), entry.item->GetName()) : std::string(entry.item->GetName());
				count = static_cast<double>(counts.at(entry.item));
				equip = Copies(entry.item) > 0 ? 1.0 : 0.0;  // the vanilla "equipped" mark: it is in the pot
				flags = FLAG_ALL;
				if (entry.enablerCopy) {
					flags = FLAG_ENABLERS;  // only under Enablers, next to Garlic
					break;
				}
				if (ing.potion) {
					flags |= FLAG_POTIONS;  // potions have their own category, not the flavour lists
					break;
				}
				// untasted things only show in All and Untasted: what they do is learned by tasting
				if (!Tasted(entry.item, ing)) {
					flags |= FLAG_UNTASTED;
					break;
				}
				if (!ing.flavour.empty()) {
					for (std::size_t i = 0; i < FLAVOURS.size(); ++i) {
						if (ing.flavour == FLAVOURS[i]) {
							flags |= FLAG_FLAVOUR0 << i;
						}
					}
				}
				if (ing.Food()) {
					flags |= FLAG_FOOD;
					_anyFood = true;
				}
				if (!ing.filler.empty()) {
					flags |= FLAG_FILLERS;
				}
				if (ing.alcohol || ing.garlic || ing.dragon > 0) {
					flags |= FLAG_ENABLERS;
				}
				break;
			}
		}
		RE::GFxValue item;
		movie->CreateObject(&item);
		item.SetMember("text", MakeString(movie, text));
		item.SetMember("filterFlag", static_cast<double>(flags));
		item.SetMember("enabled", enabled);
		if (count > 0.0) {
			item.SetMember("count", count);
		}
		item.SetMember("equipState", equip);
		list.PushBack(item);
	}
	movie->Invoke("_root.Menu.InventoryLists.InvalidateListData", nullptr, nullptr, 0);
	if (_selected >= static_cast<std::int32_t>(_entries.size())) {
		_selected = static_cast<std::int32_t>(_entries.size()) - 1;
	}
	if (_selected >= 0) {
		ShowEntry(_selected);
	} else {
		ShowPotCard(_pot, {});
	}
	UpdateSkillBar();
	logger::info("Cooking menu: {} known, {} more you could make, {} ingredients carried, {} in the pot", known.size(), could.size(), carried.size(), _pot.size());
}

// After the pot changes: update the "in the pot" marks in place, keeping the list where it is.
void CookingMenu::RefreshMarks()
{
	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	RE::GFxValue list;
	movie->GetVariable(&list, "_root.Menu.InventoryLists.ItemsList.entryList");
	for (std::uint32_t i = 0; i < list.GetArraySize() && i < _entries.size(); ++i) {
		if (_entries[i].kind != Kind::kIngredient) {
			continue;
		}
		RE::GFxValue item;
		if (list.GetElement(i, &item)) {
			item.SetMember("equipState", Copies(_entries[i].item) > 0 ? 1.0 : 0.0);
		}
	}
	RE::GFxValue full{ false };
	movie->Invoke("_root.Menu.UpdateItemList", nullptr, &full, 1);
}

void CookingMenu::UpdateButtons()
{
	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	std::string select;
	std::string make = FC::Tr("Cook");
	// the third button: Q Sort on every list, except on an ingredient already in the pot that has another copy to
	// add, where it is F Add another (Q still sorts there)
	std::string aux = FC::TrF("Sort: {0}", FC::Tr(RECIPE_SORTS[g_recipeSort]));
	bool auxIsSort = true;
	if (_selected >= 0 && _selected < static_cast<std::int32_t>(_entries.size())) {
		const auto& entry = _entries[_selected];
		if (entry.kind == Kind::kVanilla) {
			select = FC::Tr("Open");
			make.clear();  // nothing to cook here: R also opens it, but one button says so
		} else if (entry.kind == Kind::kMisc) {
			make = FC::Tr("Make");  // R makes it as its mod made it; E has nothing to do here
		} else if (entry.kind == Kind::kRecipe) {
			select = FC::Tr("Use recipe");
		} else if (entry.kind == Kind::kIngredient) {
			const auto copies = Copies(entry.item);
			select = FC::Tr(copies > 1 ? "Remove one" : copies > 0 ? "Remove" : "Add");
			const auto counts = PlayerCounts();
			const auto it = counts.find(entry.item);
			if (copies > 0 && _pot.size() < POT_SIZE && it != counts.end() && static_cast<std::size_t>(it->second) > copies) {
				aux = FC::Tr("Add another");
				auxIsSort = false;
			}
		}
	}
	RE::GFxValue buttons;
	movie->CreateArray(&buttons);
	for (const auto& text : { select, std::string(FC::Tr("Exit")), aux, make }) {
		buttons.PushBack(MakeString(movie, text));
	}
	if ((auxIsSort ? 1 : 0) != g_auxArt) {
		g_auxArt = auxIsSort ? 1 : 0;
		RE::GFxValue art;
		movie->CreateObject(&art);
		art.SetMember("PCArt", auxIsSort ? "Q" : "F");
		art.SetMember("XBoxArt", "360_Y");
		art.SetMember("PS3Art", "PS3_Y");
		RE::GFxValue args[2]{ art, RE::GFxValue(2.0) };
		movie->Invoke("_root.Menu.BottomBarInfo.SetButtonArt", nullptr, args, 2);
	}
	movie->SetVariable("_root.Menu.ButtonText", buttons);
	movie->Invoke("_root.Menu.UpdateButtonText", nullptr, nullptr, 0);
}

// The item card: what the pot would make, how good it is (with Taste as You Go), and what stays unused.
// Below it, the pot's contents on the line the vanilla menu uses for "Requires".
// the dish the pot (or a recipe) would make turns in the middle of the screen, like the item a vanilla crafting menu
// would make; nothing when the pot makes no dish yet
void CookingMenu::ShowDish3D(std::int32_t a_dish)
{
	RE::TESBoundObject* dish = nullptr;
	if (a_dish >= 0) {
		// the look the cook will get: a mod's model for the pot's main ingredient on one of our general dishes (Rules.inl
		// DishLookForm), else the dish's own (the screen showed the plain dish while the cook had the mod's model)
		auto form = DishLookForm(a_dish, false);
		dish = form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
	}
	ShowItem3D(dish);
}

// any item turning in the middle of the screen (a dish, or what a Misc recipe makes); nullptr clears it
void CookingMenu::ShowItem3D(RE::TESBoundObject* a_item)
{
	RE::TESBoundObject* dish = a_item;
	if (dish == _shown3D) {
		return;
	}
	auto manager = RE::Inventory3DManager::GetSingleton();
	if (!manager) {
		return;
	}
	if (!_began3D && !g_overVanilla) {
		manager->Begin3D(RE::INTERFACE_LIGHT_SCHEME::kInventory);
		_began3D = true;
	}
	if (dish) {
		manager->LoadInventoryItem(dish, nullptr);
	} else {
		manager->UnloadInventoryItem();
	}
	_shown3D = dish;
}

void CookingMenu::PostDisplay()
{
	RE::IMenu::PostDisplay();
	if (_shown3D) {
		if (auto manager = RE::Inventory3DManager::GetSingleton()) {
			manager->Render();
		}
	}
}

// The pot preview names what each flavour does in two short items, so the player knows before cooking (decided
// 2026-11-04; the cooked dish's own card has the full numbers): "Spicy · Fire, resists Frost"
static std::string FlavourTag(const std::string& a_flavour)
{
	static const std::pair<const char*, const char*> tags[] = {
		{ "Mighty", "Weapons, Stamina regen" }, { "Brainy", "Magic, Magicka regen" }, { "Hearty", "Defense, Health regen" },
		{ "Sneaky", "Stealth, Pickpocket, Lockpicking" }, { "Spicy", "Fire, resists Frost" }, { "Chilly", "Frost, resists Fire" },
		{ "Zesty", "Shock, resists Shock" }, { "Earthy", "Holy, resists Poison and Disease" },
	};
	for (const auto& [flavour, tag] : tags) {
		if (a_flavour == flavour) {
			return FC::TrF("{0} \u00B7 {1}", FC::Tr(flavour), FC::Tr(tag));  // U+00B7 middle dot, written as UTF-8
		}
	}
	return FC::Tr(a_flavour.c_str());
}

// "Spicy Venison Stew": the flavour word and the dish name, in the order the player's language puts them
static std::string FlavouredName(const std::string& a_flavour, const std::string& a_name)
{
	return FC::TrF("{0} {1}", FC::Tr(a_flavour.c_str()), a_name);
}

void CookingMenu::ShowPotCard(const Pot& a_pot, const std::string& a_lead)
{
	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	const bool taste = HasTreePerk(PERK_TASTE);
	const auto r = Project(a_pot);
	ShowDish3D(r.dish);

	std::vector<std::string> lines;
	if (!a_lead.empty()) {
		lines.push_back(a_lead);
	}
	// only seasonings keep the dish a mystery; the foods it is made of are recognised
	std::vector<std::string> untasted;
	for (std::size_t i = 0; i < a_pot.size(); ++i) {
		auto item = a_pot[i];
		const auto ing = IngOf(item);
		bool isBase = false;  // any copy of it makes the dish (Spiced Beef's Fire Salts: the name already says Spicy)
		for (std::size_t j = 0; j < a_pot.size() && j < r.base.size(); ++j) {
			isBase = isBase || (r.base[j] && a_pot[j] == item);
		}
		isBase = isBase || static_cast<std::int32_t>(i) == r.cure;  // Garlic doing its job as the cure
		if (!isBase && !Tasted(item, ing) && std::ranges::find(untasted, std::string(item->GetName())) == untasted.end()) {
			untasted.emplace_back(item->GetName());
		}
	}

	std::string name;
	if (a_pot.empty()) {
		name = FC::Tr(g_place == 2 ? "Nothing on the fire" : g_place == 3 ? "Empty oven" : "Empty pot");
		lines.push_back(FC::Tr("Pick a recipe, or add ingredients (E)."));
	} else if (r.dish < 0) {
		name = FC::Tr("Nothing to cook yet");
		lines.push_back(FC::Tr("These ingredients do not make a dish yet. Add more, or pick a recipe."));
	} else {
		const bool known = [&] { CookLock lock(g_cookLock); return g_known.contains(r.dish); }();
		const std::string unknownWord = FC::Tr(r.drink ? "drink" : "dish");  // an undiscovered drink is not a dish
		// a dragon part that counts shows in the name at once ("Auri-El's Ale", "Akatosh's unknown dish"), even while
		// untasted ingredients keep the flavour a mystery: the player sees the dragonfeast worked (decided 2026-11-03)
		const std::string dishName = r.dragon ? FC::TrF(r.drink ? "Auri-El's {0}" : "Akatosh's {0}", known ? r.baseName : std::string(FC::Tr(r.drink ? "unknown drink" : "unknown dish"))) :
		                             known    ? r.baseName :
		                                        std::string(FC::Tr(r.drink ? "Unknown drink" : "Unknown dish"));
		if (!untasted.empty()) {
			name = dishName;
			// a clash among untasted ingredients is told without saying which flavours (his pick 2026-11-14): the pot line
			// marks the clashing ingredients, and the cook reveals the rest
			lines.push_back(FC::TrF(r.clash ? "Not tasted yet: {0}. Some flavours in the pot may not go together." : "Not tasted yet: {0}. The result is a mystery.", Join(untasted)));
		} else if (r.clash) {
			name = dishName;
			lines.push_back(FC::Tr("The flavours clash: it cooks plain, with no effect."));
		} else if (r.level == 0) {
			name = dishName;
			lines.push_back(FC::Tr(r.drink ? "No seasoning: a plain drink with no effect." : "No seasoning: a plain dish with no effect."));
		} else if (r.dragon) {
			// "Akatosh's Chilly Venison Stew": the name says it is the best, so no quality word
			const auto seasoned = DishName(r.dish, r.flavour);
			name = FC::TrF(r.drink ? "Auri-El's {0}" : "Akatosh's {0}", known ? (seasoned.empty() ? FlavouredName(r.flavour, r.baseName) : seasoned) : FlavouredName(r.flavour, unknownWord));
			lines.push_back(FC::TrF("{0}. A Dragonfeast dish: level 4, lasts half a day ({1}).", FlavourTag(r.flavour), TimeText(r.dur)));
		} else {
			const auto seasoned = DishName(r.dish, r.flavour);
			name = known ? (seasoned.empty() ? FlavouredName(r.flavour, r.baseName) : seasoned) : FlavouredName(r.flavour, unknownWord);
			if (taste) {
				lines.push_back(FC::TrF(r.total == 1 ? "{0}. {1} star. Level {2}, lasts {3}." : "{0}. {1} stars. Level {2}, lasts {3}.", FlavourTag(r.flavour), r.total, r.level, TimeText(r.dur)));
			} else {
				lines.push_back(FC::TrF("{0}. Cook it to learn how good it is.", FlavourTag(r.flavour)));
			}
		}
		if (r.spice >= 0 && untasted.empty()) {
			lines.push_back(FC::Tr("The spice also seasons it."));
		}
		// a seasoning used in the recipe itself (a herb brewed into tea, the herb or salt a soup needs) gives no flavour:
		// say so, once per name
		{
			std::vector<std::string> wasted;
			for (std::size_t i = 0; i < a_pot.size() && i < r.base.size(); ++i) {
				if (!r.base[i] || static_cast<std::int32_t>(i) == r.spice) {
					continue;
				}
				const auto ing = IngOf(a_pot[i]);
				const std::string name = a_pot[i]->GetName();
				if (!ing.flavour.empty() && !ing.Food() && Tasted(a_pot[i], ing) && std::ranges::find(wasted, name) == wasted.end()) {
					wasted.push_back(name);
				}
			}
			for (const auto& name : wasted) {
				lines.push_back(FC::TrF("{0} is part of the recipe here, so it adds no flavour.", name));
			}
		}
		if (r.blocked) {
			// say exactly what is missing, per need: "Nightshade needs Garlic or an Earthy potion."
			for (const char* need : { "alc", "garlic", "ring" }) {
				std::vector<std::string> names;
				for (const auto& [index, q] : r.blockedBy) {
					const std::string name = a_pot[index]->GetName();
					if (q == need && std::ranges::find(names, name) == names.end()) {
						names.push_back(name);
					}
				}
				if (names.empty()) {
					continue;
				}
				const bool many = names.size() > 1;
				const std::string n(need);
				lines.push_back(FC::TrF(n == "alc" ? (many ? "{0} need an alcoholic drink." : "{0} needs an alcoholic drink.") :
				                        n == "garlic" ? (many ? "{0} need Garlic or an Earthy potion." : "{0} needs Garlic or an Earthy potion.") :
				                                        (many ? "{0} need the Ring of Namira worn." : "{0} needs the Ring of Namira worn."),
					Join(names)));
			}
		}
		// a dragon part the dish cannot use: say what it is missing
		if (r.dragonIdle >= 0) {
			const std::string part = a_pot[r.dragonIdle]->GetName();
			const bool scales = r.drink;  // "Dragon Scales" is plural: "need", "have"
			if (r.dragonWhy == "full" && scales) {
				lines.push_back(FC::TrF("{0} need a full pot: five ingredients that all go into the brew, at least three of them drink ingredients (water counts).", part));
			} else if (r.dragonWhy == "full") {
				lines.push_back(FC::TrF("{0} needs a full pot: five ingredients that all go into the meal, one a vegetable or staple.", part));
			} else if (r.dragonWhy == "clash") {
				lines.push_back(FC::TrF(scales ? "The flavours clash, so {0} have nothing to strengthen." : "The flavours clash, so {0} has nothing to strengthen.", part));
			} else {
				lines.push_back(FC::TrF(scales ? "{0} need a seasoning to strengthen." : "{0} needs a seasoning to strengthen.", part));
			}
		} else if (r.wrongPart == 1) {
			lines.push_back(FC::Tr("Dragon Bone only goes into meals."));
		} else if (r.wrongPart == 2) {
			lines.push_back(FC::Tr("Dragon Scales only go into drinks."));
		}
		std::vector<std::string> unused;
		for (std::size_t i = 0; i < a_pot.size(); ++i) {
			if (!r.used[i] && std::ranges::find(unused, std::string(a_pot[i]->GetName())) == unused.end()) {
				unused.emplace_back(a_pot[i]->GetName());
			}
		}
		if (!unused.empty() && !(r.clash && !untasted.empty())) {  // a mystery clash: the pot line says it instead
			lines.push_back(FC::TrF("Unused: {0}.", Join(unused)));
		}
	}

	std::string text;
	for (const auto& line : lines) {
		text += (text.empty() ? "" : "\n") + line;
	}
	RE::GFxValue card;
	movie->CreateObject(&card);
	card.SetMember("type", ICT_FOOD);
	card.SetMember("name", MakeString(movie, name));
	card.SetMember("value", static_cast<double>(DishValue(r)));
	card.SetMember("weight", 0.5);
	card.SetMember("effects", MakeString(movie, text));
	RE::GFxValue itemInfo;
	movie->GetVariable(&itemInfo, "_root.Menu.ItemInfo");
	itemInfo.SetMember("itemInfo", card);

	ShowPotLine(a_pot);

	RE::GFxValue fadeOut{ false };
	movie->Invoke("_root.Menu.FadeInfoCard", nullptr, &fadeOut, 1);
}

// The line under the card: "In the pot (3/5): Venison, Potato, Fire Salts (2)"; unused ones are greyed out, and a
// click on a name takes it out (see PotLineClick)
void CookingMenu::ShowPotLine(const Pot& a_pot)
{
	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	const auto r = Project(a_pot);

	// each pot entry's role; copies of one ingredient can have different roles (Spiced Beef's spice and a second one)
	enum Role { kBase, kSpice, kSeasoning, kSide, kEnabler, kUnused, kClash, kPlain };
	const auto roleOf = [&](std::size_t a_i) {
		if (r.dish < 0) {
			return kPlain;  // no dish yet: nothing has a role
		}
		if (static_cast<std::int32_t>(a_i) == r.spice) {
			return kSpice;  // a spiced dish's spice: part of the dish and its seasoning
		}
		if (a_i < r.base.size() && r.base[a_i]) {
			return kBase;
		}
		if (a_i < r.enabler.size() && r.enabler[a_i]) {
			return kEnabler;
		}
		if (a_i < r.side.size() && r.side[a_i]) {
			return kSide;
		}
		if (a_i < r.used.size() && r.used[a_i]) {
			return kSeasoning;
		}
		// in a clash, a flavoured ingredient left out is one of the clashing ones (fillers are only unused: a plain dish
		// has no effect to make longer)
		return r.clash && !IngOf(a_pot[a_i]).flavour.empty() ? kClash : kUnused;
	};
	struct Part
	{
		Role                role;
		RE::TESBoundObject* item;
		std::int32_t        copies;
	};
	std::vector<Part> parts;
	for (std::size_t i = 0; i < a_pot.size(); ++i) {
		const auto role = roleOf(i);
		auto it = std::ranges::find_if(parts, [&](const Part& a_p) { return a_p.role == role && a_p.item == a_pot[i]; });
		if (it != parts.end()) {
			++it->copies;
		} else {
			parts.push_back({ role, a_pot[i], 1 });
		}
	}

	// "In the pot (4/5): Base: Raw Beef | Seasoning: Fire Salts (2) | Enabler: Ale", labels grey, unused greyed out
	const char* labels[]{ "Main", "Spice", "Seasoning", "Filler", "Enabler", "Unused", "Clash", "" };
	const auto escape = [](std::string a_text) {
		std::string out;
		for (char c : a_text) {
			out += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : std::string(1, c);
		}
		return out;
	};
	const bool real = &a_pot == &_pot;  // a recipe preview is not in the pot yet: its names are not clickable
	_potLine.clear();
	std::string plain = FC::TrF("In the pot ({0}/{1}):", a_pot.size(), POT_SIZE);
	std::string html = escape(plain);
	bool firstGroup = true;
	for (auto role : { kBase, kSpice, kSeasoning, kSide, kEnabler, kClash, kUnused, kPlain }) {
		bool firstInGroup = true;
		for (const auto& part : parts) {
			if (part.role != role) {
				continue;
			}
			std::string lead;
			if (firstInGroup) {
				lead = firstGroup ? " " : " | ";
				if (role != kPlain) {
					const std::string label = FC::TrF("{0}: ", FC::Tr(labels[role]));
					plain += lead + label;
					html += escape(lead) + "<font color='#9a9a9a'>" + escape(label) + "</font>";
					lead.clear();
				}
				firstGroup = false;
				firstInGroup = false;
			} else {
				lead = ", ";
			}
			plain += lead;
			html += escape(lead);
			const auto name = std::string(part.item->GetName()) + (part.copies > 1 ? std::format(" ({})", part.copies) : "");
			const auto start = Utf16Length(plain);
			plain += name;
			if (real) {
				_potLine.push_back({ start, start + Utf16Length(name), part.item });
			}
			html += role == kUnused ? "<font color='#777777'>" + escape(name) + "</font>" :
			        role == kClash  ? "<font color='#c98a7a'>" + escape(name) + "</font>" :  // muted red: these do not go together
			                          escape(name);
		}
	}
	RE::GFxValue field;
	if (movie->GetVariable(&field, "_root.Menu.AdditionalDescription") && field.IsObject()) {
		field.SetMember("html", true);
		field.SetMember("htmlText", MakeString(movie, html));
	}
}

// another mod's item as the vanilla crafting menu shows it: the card the game itself builds for that form (UI thread,
// reading the item's own record, as the vanilla menu does)
void CookingMenu::ShowFormCard(RE::TESBoundObject* a_item)
{
	auto movie = uiMovie.get();
	if (!movie || !a_item) {
		return;
	}
	RE::ItemCard card(movie);
	card.SetForm(a_item);
	RE::GFxValue itemInfo;
	movie->GetVariable(&itemInfo, "_root.Menu.ItemInfo");
	itemInfo.SetMember("itemInfo", card.obj);
	RE::GFxValue fadeOut{ false };
	movie->Invoke("_root.Menu.FadeInfoCard", nullptr, &fadeOut, 1);
}

void CookingMenu::ShowSimpleCard(const std::string& a_name, const std::string& a_text, double a_weight, double a_value)
{
	auto movie = uiMovie.get();
	if (!movie) {
		return;
	}
	RE::GFxValue card;
	movie->CreateObject(&card);
	card.SetMember("type", ICT_FOOD);
	card.SetMember("name", MakeString(movie, a_name));
	card.SetMember("value", a_value);
	card.SetMember("weight", a_weight);
	card.SetMember("effects", MakeString(movie, a_text));
	RE::GFxValue itemInfo;
	movie->GetVariable(&itemInfo, "_root.Menu.ItemInfo");
	itemInfo.SetMember("itemInfo", card);
	RE::GFxValue fadeOut{ false };
	movie->Invoke("_root.Menu.FadeInfoCard", nullptr, &fadeOut, 1);
}

// The pot after E on a recipe: the recipe's ingredients, plus the seasonings, fillers and enablers
// already in the pot (other foods come out: a pot makes one dish), up to 5.
CookingMenu::Pot CookingMenu::PotWithRecipe(const Dish& a_dish, const Counts& a_counts, std::string& a_notice) const
{
	Pot next;
	Pot rest = _pot;
	for (auto item : RecipeFill(a_dish, a_counts)) {
		next.push_back(item);
		if (auto it = std::ranges::find(rest, item); it != rest.end()) {
			rest.erase(it);
		}
	}
	// what stays must not change the dish: a staple or an ale left in the pot can make another recipe win (Crab Stew
	// becomes Potato Crab Chowder with a staple; Salmon Steak and Yellow Mountain Flower make Grilled Salmon). Each kept
	// item is checked with the pot as it is so far, and one that changes the dish comes out (reviews 2026-11-16g)
	const bool longSimmer = HasTreePerk(PERK_LONG_SIMMER);
	const bool check = !next.empty();
	std::vector<std::string> keys;
	for (auto item : next) {
		keys.push_back(IngOf(item).key);
	}
	std::vector<std::string> out;
	std::vector<std::string> changed;
	const auto add = [](std::vector<std::string>& a_names, RE::TESBoundObject* a_item) {
		if (std::ranges::find(a_names, std::string(a_item->GetName())) == a_names.end()) {
			a_names.emplace_back(a_item->GetName());
		}
	};
	for (auto item : rest) {
		if (IngOf(item).Food()) {
			continue;
		}
		if (next.size() >= POT_SIZE) {
			add(out, item);
			continue;
		}
		if (check) {
			keys.push_back(IngOf(item).key);
			if (WinningDish(keys, longSimmer, a_dish.index) != a_dish.index) {
				keys.pop_back();
				add(changed, item);
				continue;
			}
		}
		next.push_back(item);
	}
	if (!out.empty()) {
		a_notice = FC::TrF(out.size() > 1 ? "No room in the pot: {0} were taken out." : "No room in the pot: {0} was taken out.", Join(out));
	}
	if (!changed.empty()) {
		const auto line = FC::TrF(changed.size() > 1 ? "{0} would make another dish, so they were taken out." : "{0} would make another dish, so it was taken out.", Join(changed));
		a_notice = a_notice.empty() ? line : a_notice + " " + line;
	}
	return next;
}

// What the recipe shortcut puts in the pot: the picked ingredients, checked with the same rules the cook uses
// (ChooseDish), so the shortcut never offers a pot the cook rejects. Insect eggs in an egg slot need an alcoholic
// drink besides the recipe: one in the pot is used first, else the cheapest one you carry goes in with the recipe
// (Braided Bread with Chaurus Eggs; reviews 2026-11-15). Empty: the recipe can't be made from what you carry.
CookingMenu::Pot CookingMenu::RecipeFill(const Dish& a_dish, const Counts& a_counts) const
{
	if (DishLocked(a_dish.index)) {
		return {};  // its mod's quest hasn't unlocked it yet (the card says so)
	}
	const auto picks = Resolve(a_dish, a_counts, _pot);
	if (!AllPicked(picks)) {
		return {};
	}
	Pot items;
	std::vector<std::string> keys;
	std::unordered_map<RE::TESBoundObject*, std::int32_t> used;
	for (const auto& pick : picks) {
		items.push_back(pick.item);
		keys.push_back(IngOf(pick.item).key);
		++used[pick.item];
	}
	const bool longSimmer = HasTreePerk(PERK_LONG_SIMMER);
	// (this dish, and the dish the cook would pick from all of them: Codex review 2026-11-16f, M1); with the helpers the
	// search tried (a drink, Garlic or both: the same chooser)
	std::vector<RE::TESBoundObject*> added;
	if (!WithShortcutHelpers(keys, used, a_counts, _pot, longSimmer, a_dish.index, &added)) {
		return {};
	}
	items.insert(items.end(), added.begin(), added.end());
	return items;
}

void CookingMenu::ShowEntry(std::int32_t a_index)
{
	auto movie = uiMovie.get();
	if (!movie || a_index < 0 || a_index >= static_cast<std::int32_t>(_entries.size())) {
		return;
	}
	// while a cook runs, the list still holds what the scripts are using up: the last copy of a home-made potion may be
	// gone from the game before the list is refreshed, so its entry isn't read until then (Cooked -> Populate shows
	// the selected entry again; reviews 2026-11-15)
	if (_busy) {
		_selected = a_index;
		return;
	}
	if (a_index != _selected) {
		_notice.clear();  // a message from the last action fades once you move on
	}
	_selected = a_index;
	const auto& entry = _entries[a_index];
	const bool taste = HasTreePerk(PERK_TASTE);
	std::string info;

	switch (entry.kind) {
	case Kind::kIngredient:
		{
			// the top line describes the highlighted ingredient; the card shows the pot
			const auto ing = IngOf(entry.item);
			const std::string name = entry.item->GetName();
			if (ing.potion) {
				info = FC::TrF(ing.flavour == "Earthy" ? "{0} adds an Earthy flavour, 1 star." : "{0} adds a {1} flavour, 1 star.",
					name, FC::Tr(ing.flavour.c_str()));
				if (ing.garlic) {
					info += std::string(" ") + FC::Tr("It also neutralizes poisonous plants.");
				}
			} else if (!Tasted(entry.item, ing)) {
				info = FC::TrF("You have not tasted {0} yet. Eat one, or cook with it, to learn what it does.", name);
			} else if (ing.garlic) {
				info = FC::TrF("{0} is an Earthy seasoning that neutralizes poisonous plants in the pot.", name);
			} else if (!ing.flavour.empty() && ing.Food()) {
				info = !taste ? FC::TrF("{0} is a food. It can be a dish's main ingredient, or season a dish as {1}.", name, FC::Tr(ing.flavour.c_str())) :
				       FC::TrF(ing.stars == 1 ? "{0} is a food. It can be a dish's main ingredient, or season a dish as {1} ({2} star)." :
				                                "{0} is a food. It can be a dish's main ingredient, or season a dish as {1} ({2} stars).",
						   name, FC::Tr(ing.flavour.c_str()), ing.stars);
			} else if (!ing.flavour.empty()) {
				{
					const bool earthy = ing.flavour == "Earthy";
					// (Earthy has its own sentences, "an": name and stars only, so its slots run {0} {1} with no gap a
					// translator could "fix"; Claude review 2026-11-15, L6)
					if (!taste) {
						info = earthy ? FC::TrF("{0} is an Earthy seasoning.", name) : FC::TrF("{0} is a {1} seasoning.", name, FC::Tr(ing.flavour.c_str()));
					} else if (earthy) {
						info = FC::TrF(ing.stars == 1 ? "{0} is an Earthy seasoning, {1} star." : "{0} is an Earthy seasoning, {1} stars.", name, ing.stars);
					} else {
						info = FC::TrF(ing.stars == 1 ? "{0} is a {1} seasoning, {2} star." : "{0} is a {1} seasoning, {2} stars.", name, FC::Tr(ing.flavour.c_str()), ing.stars);
					}
				}
			} else if (ing.alcohol) {
				info = FC::TrF("{0} is a drink that masks the bad taste of unappetizing ingredients in the pot.", name);
			} else if (ing.filler == "veg") {
				info = FC::TrF("{0} is a vegetable. It can be a dish's main ingredient, or a filler that makes the meal last longer.", name);
			} else if (ing.filler == "staple") {
				info = FC::TrF("{0} is a staple. As a filler, it makes the meal last longer.", name);
			} else {
				info = FC::TrF("{0} is a food. The foods in the pot decide the dish.", name);
			}
			if (Tasted(entry.item, ing)) {
				// Long Simmer lifts the drink and Garlic needs; forbidden ingredients only show while the ring is worn
				const bool simmer = HasTreePerk(PERK_LONG_SIMMER);
				if (ing.need == "alc" && !simmer) {
					info += std::string(" ") + FC::Tr("Needs an alcoholic drink in the pot.");
				} else if (ing.need == "garlic" && !simmer) {
					info += std::string(" ") + FC::Tr("Needs Garlic or an Earthy potion in the pot.");
				} else if (ing.need == "ring") {
					info += std::string(" ") + FC::Tr("A forbidden ingredient: only Namira's followers cook with it.");
				}
			}
			ShowPotCard(_pot, {});
			break;
		}
	case Kind::kRecipe:
		{
			const auto& dish = *entry.dish;
			const auto counts = PlayerCounts();
			const auto picks = Resolve(dish, counts, _pot);
			// the top line: the recipe's ingredients and how many you carry, like Smithing's "Iron Ingot (5)"
			std::vector<std::string> needs;
			for (const auto& pick : picks) {
				const auto have = pick.item && counts.contains(pick.item) ? counts.at(pick.item) : 0;
				needs.push_back(have > 0 ? std::format("{} ({})", PickName(pick), have) : PickName(pick));
			}
			// the recipe's own stars, before seasonings and enablers add theirs
			const auto base = dish.tier == 0 ? std::string(FC::Tr("No base stars.")) : FC::TrF(dish.tier == 1 ? "{0} base star." : "{0} base stars.", dish.tier);
			std::string full;
			if (DishLocked(dish.index)) {
				// learned by eating it (Cookbook Memory), but its mod's quest hasn't unlocked it (Claude review 2026-11-16g, L2)
				ShowSimpleCard(FC::Tr(dish.name.c_str()), FC::Tr("This recipe unlocks with its quest."));
				info = FC::TrF("{0} Requires: {1}.", base, Join(needs));
			} else if (NeedsPot(dish)) {
				ShowSimpleCard(FC::Tr(dish.name.c_str()), WrongPlaceText());
				info = FC::TrF("{0} Requires: {1}.", base, Join(needs));
			} else if (AllPicked(picks) && RecipeFill(dish, counts).empty()) {
				// every slot has an ingredient, but they don't make the dish: the pot would make another dish with them (more
				// named ingredients wins: Cabbage Potato Soup over Vegetable Soup), or insect eggs need a drink (in-game test
				// 2026-11-16: the egg text showed for the soup)
				std::vector<std::string> keys;
				for (const auto& p : picks) {
					keys.push_back(IngOf(p.item).key);
				}
				const auto other = WinningDish(keys, HasTreePerk(PERK_LONG_SIMMER), dish.index);
				const std::string otherName = other >= 0 && other != dish.index ? Str(std::format("dish.{}.n", other)) : std::string();
				ShowSimpleCard(FC::Tr(dish.name.c_str()), !otherName.empty() ?
					FC::TrF("These ingredients would make {0} instead.", FC::Tr(otherName.c_str())) :
					std::string(FC::Tr("These ingredients don't make this dish on their own. An unappetizing ingredient needs an alcoholic drink, and a poisonous one needs Garlic.")));
				info = FC::TrF("{0} Requires: {1}.", base, Join(needs));
			} else if (AllPicked(picks)) {
				// the card shows the dish this recipe would make with what is in the pot now
				const auto preview = PotWithRecipe(dish, counts, full);
				ShowPotCard(preview, {});
				info = FC::TrF("{0} Requires: {1}. E puts it in the pot.", base, Join(needs));
			} else {
				ShowSimpleCard(FC::Tr(dish.name.c_str()), FC::Tr("You are missing an ingredient. Rare ones are never picked for you."));
				info = FC::TrF("{0} Requires: {1}.", base, Join(needs));
			}
			if (!full.empty()) {
				info += " " + full;
			}
			// the line under the card always shows the real pot (and stays clickable)
			ShowPotLine(_pot);
			break;
		}
	case Kind::kVanilla:
		ShowSimpleCard(FC::Tr("Vanilla crafting"), FC::Tr("An emergency fallback. The station's own crafting menu lists every recipe as the game and your mods made them. What you make there skips Cooking Skill Overhaul's rules: no flavours, no stars, no discoveries."));
		ShowItem3D(nullptr);
		info = FC::Tr("E opens the vanilla crafting menu. Leaving it gets you up from the station.");
		break;
	case Kind::kMisc:
		{
			// as the vanilla crafting menu shows a recipe (his 2026-11-14 "our misc is not helpful compared to vanilla"):
			// the item's own card built by the game (its effects, weight and value), and the Requires line under it in
			// the vanilla format: "Requires: 2 Jug of Milk, Salt Pile (3)", what you lack greyed out
			const auto* recipe = g_misc[entry.misc].recipe;
			const auto  all = CarriedCounts();
			RE::GFxValue ingredients;
			movie->CreateArray(&ingredients);
			for (const auto& [item, need] : MiscNeeds(entry.misc)) {
				const auto it = all.find(item);
				RE::GFxValue one;
				movie->CreateObject(&one);
				one.SetMember("Name", MakeString(movie, NameOf(item)));
				one.SetMember("RequiredCount", static_cast<double>(need));
				one.SetMember("PlayerCount", static_cast<double>(it != all.end() ? it->second : 0));
				ingredients.PushBack(one);
			}
			auto made = static_cast<RE::TESBoundObject*>(recipe->createdItem);
			ShowFormCard(made);
			ShowItem3D(made);
			RE::GFxValue line;
			if (movie->GetVariable(&line, "_root.Menu.AdditionalDescription") && line.IsObject()) {
				line.SetMember("html", false);  // the pot line is html; the vanilla Requires line is plain text with colours
			}
			_potLine.clear();  // nothing on this line can be clicked out of the pot
			RE::GFxValue args[3]{ MakeString(movie, FC::Tr("Requires")), ingredients, RE::GFxValue(true) };
			movie->Invoke("_root.Menu.UpdateIngredients", nullptr, args, 3);
			info = FC::Tr(MiscMakeable(entry.misc, all) ? "Made as its own mod makes it: no pot, no effect from Cooking Skill Overhaul. R makes it." :
			                                               "Made as its own mod makes it: no pot, no effect from Cooking Skill Overhaul.");
			break;
		}
	case Kind::kUnknown:
		info = FC::Tr("A dish you could make with what you carry. Put foods in the pot and cook to discover it.");
		ShowPotCard(_pot, {});
		break;
	case Kind::kMore:
		info = FC::Tr("More dishes to discover: combine foods in the pot and cook.");
		ShowPotCard(_pot, {});
		break;
	case Kind::kEmpty:
		info = FC::Tr("No recipes yet. Put foods in the pot and cook to discover dishes.");
		ShowPotCard(_pot, {});
		break;
	}
	// the top line: what just happened, or else what the highlighted entry is and does
	movie->SetVariable("_root.Menu.MenuDescription.text", (_notice.empty() ? info : _notice).c_str());
	UpdateButtons();
}

// Curious Taster: an untasted ingredient is tasted as it goes in
void CookingMenu::Taste(RE::TESBoundObject* a_item)
{
	const auto ing = IngOf(a_item);
	if (Tasted(a_item, ing) || !HasTreePerk(PERK_CURIOUS)) {
		return;
	}
	{
		CookLock lock(g_cookLock);
		g_tasted.insert(a_item->GetFormID());
	}
	SendToScripts("FC_MenuTaste", { a_item });
	const bool stars = !ing.flavour.empty() && HasTreePerk(PERK_TASTE);
	_notice = !stars ? FC::TrF("You taste it as it goes in. {0}: {1}.", a_item->GetName(), ShortRole(ing)) :
	          FC::TrF(ing.stars == 1 ? "You taste it as it goes in. {0}: {1}, {2} star." : "You taste it as it goes in. {0}: {1}, {2} stars.",
				  a_item->GetName(), ShortRole(ing), ing.stars);
	Populate();  // it moves from Untasted to its flavour
}

// E: on an ingredient, put one in the pot, or take one copy out (F adds another: E and F work one copy at a time);
// on a recipe, put its ingredients in the pot
void CookingMenu::Press(std::int32_t a_index)
{
	if (_busy || a_index < 0 || a_index >= static_cast<std::int32_t>(_entries.size())) {
		return;
	}
	_selected = a_index;
	_notice.clear();
	const auto& entry = _entries[a_index];
	if (entry.kind == Kind::kVanilla) {
		OpenVanilla();
		return;
	}
	if (entry.kind == Kind::kIngredient) {
		auto item = entry.item;
		if (Copies(item) > 0) {
			TakeOut(item, true);
			RE::PlaySound("UIAlchemyRemoveIngredient");
		} else if (_pot.size() >= POT_SIZE) {
			_notice = FC::Tr("The pot is full. It holds up to 5 ingredients.");
			RE::PlaySound("UIMenuCancel");
		} else {
			_pot.push_back(item);
			RE::PlaySound("UIAlchemySelectIngredient");
			if (!Tasted(item, IngOf(item)) && HasTreePerk(PERK_CURIOUS)) {
				Taste(item);  // repopulates the list
				return;
			}
		}
		RefreshMarks();
		ShowEntry(a_index);
	} else if (entry.kind == Kind::kRecipe) {
		const auto counts = PlayerCounts();
		if (NeedsPot(*entry.dish) || RecipeFill(*entry.dish, counts).empty()) {
			RE::PlaySound("UIMenuCancel");
			return;
		}
		std::string notice;
		_pot = PotWithRecipe(*entry.dish, counts, notice);
		// remember the recipe's own ingredients: taking one out takes the whole recipe out
		_recipeName = FC::Tr(entry.dish->name.c_str());
		_recipeDish = entry.dish;
		g_preferDish = entry.dish->index;  // on a tie with another dish's recipe, the pot makes this one (2026-11-24)
		_recipeBase.clear();
		for (const auto& pick : Resolve(*entry.dish, counts, _pot)) {
			if (pick.item) {
				_recipeBase.push_back(pick.item);
			}
		}
		_notice = notice.empty() ? FC::TrF("{0} is in the pot. Add a seasoning to give it an effect, then cook (R). {1}", FC::Tr(entry.dish->name.c_str()), SeasoningHint()) : notice;
		RE::PlaySound("UIMenuOK");
		RefreshMarks();
		ShowEntry(a_index);
	}
}

// Takes an ingredient (all its copies) out of the pot. If it was one of a chosen recipe's own ingredients, the
// recipe no longer holds, so its other ingredients come out too; seasonings and extras stay.
void CookingMenu::TakeOut(RE::TESBoundObject* a_item, bool a_oneCopy)
{
	if (a_oneCopy) {
		// one copy only (E): the last one put in, so the order of what went in first stays
		if (const auto it = std::ranges::find(_pot.rbegin(), _pot.rend(), a_item); it != _pot.rend()) {
			_pot.erase(std::next(it).base());
		}
	} else {
		std::erase(_pot, a_item);
	}
	if (std::ranges::find(_recipeBase, a_item) != _recipeBase.end()) {
		// can what is left in the pot still make the recipe? (Fire Salts takes the spice slot from Dragon's Tongue)
		if (_recipeDish) {
			Counts inPot;
			for (auto item : _pot) {
				++inPot[item];
			}
			const auto picks = Resolve(*_recipeDish, inPot, _pot);
			if (AllPicked(picks)) {
				_recipeBase.clear();
				for (const auto& pick : picks) {
					_recipeBase.push_back(pick.item);
				}
				return;
			}
		}
		std::vector<std::string> names;
		for (auto base : _recipeBase) {
			std::erase(_pot, base);
			if (std::ranges::find(names, std::string(base->GetName())) == names.end()) {
				names.emplace_back(base->GetName());
			}
		}
		_notice = FC::TrF("Took out the {0} ingredients: {1}.", _recipeName, Join(names));
		_recipeBase.clear();
		_recipeName.clear();
		_recipeDish = nullptr;
	}
}

// A click on the "In the pot" line: take that ingredient (all its copies) out of the pot
void CookingMenu::RemoveAtChar(std::int32_t a_index)
{
	if (_busy || a_index < 0) {
		return;
	}
	for (const auto& part : _potLine) {
		if (a_index >= part.start && a_index < part.end) {
			_notice.clear();
			TakeOut(part.item);
			RE::PlaySound("UIAlchemyRemoveIngredient");
			RefreshMarks();
			if (_selected >= 0) {
				ShowEntry(_selected);
			} else {
				ShowPotCard(_pot, {});
			}
			return;
		}
	}
}

// Q: the next order (Most used, Recent, A-Z) for the recipe book and every ingredient list
void CookingMenu::CycleSort()
{
	if (_busy) {
		return;
	}
	{
		CookLock lock(g_cookLock);
		g_recipeSort = (g_recipeSort + 1) % RECIPE_SORTS.size();
	}
	RE::PlaySound("UIMenuFocus");
	Populate();
	UpdateButtons();
}

// F: one more copy of the highlighted ingredient (every copy counts its stars)
void CookingMenu::AddAnother()
{
	if (_busy || _selected < 0 || _selected >= static_cast<std::int32_t>(_entries.size())) {
		return;
	}
	const auto& entry = _entries[_selected];
	if (entry.kind != Kind::kIngredient || Copies(entry.item) == 0) {
		return;
	}
	const auto counts = PlayerCounts();
	const auto it = counts.find(entry.item);
	if (_pot.size() >= POT_SIZE) {
		_notice = FC::Tr("The pot is full. It holds up to 5 ingredients.");
	} else if (it == counts.end() || static_cast<std::size_t>(it->second) <= Copies(entry.item)) {
		_notice = FC::TrF("You have no more {0}.", entry.item->GetName());
	} else {
		_pot.push_back(entry.item);
		_notice.clear();
		RE::PlaySound("UIAlchemySelectIngredient");
	}
	ShowEntry(_selected);
}

// R: cook what is in the pot. The scripts cook it (FC_Core.CookFromMenu), then call FC_Menu.Cooked and Refresh.
void CookingMenu::Cook()
{
	if (_busy) {
		return;
	}
	if (_selected >= 0 && _selected < static_cast<std::int32_t>(_entries.size()) && _entries[_selected].kind == Kind::kMisc) {
		MakeMiscEntry();
		return;
	}
	if (_selected >= 0 && _selected < static_cast<std::int32_t>(_entries.size()) && _entries[_selected].kind == Kind::kVanilla) {
		OpenVanilla();
		return;
	}
	const auto r = Project(_pot);
	if (_pot.empty() || r.dish < 0) {
		_notice = FC::Tr(_pot.empty() ? "The pot is empty." : "Nothing to cook yet. Add more ingredients, or pick a recipe.");
		RE::PlaySound("UIMenuCancel");
		ShowEntry(_selected);
		return;
	}
	// a cook still running (sent from an earlier visit to the pot): this one isn't sent at all, so it can't slip in once
	// that one ends and be answered as if it were the other (Codex review 2026-11-16f, M4)
	// (an answer that never came: after 30 seconds the next cook is sent again, twice the scripts' own 15, so a slow cook's
	// late answer can't be taken for the next one; Claude reviews 2026-11-16g M3 and 16h M1)
	if (g_cookPlace.load() != 0) {
		if (SteadyMs() - g_cookPlaceSince.load() < 30000) {
			_notice = FC::Tr("The pot is still cooking.");
			RE::PlaySound("UIMenuCancel");
			ShowEntry(_selected);
			return;
		}
		logger::info("Cook: no answer to the last cook after 30 seconds; this one is sent");
		g_cookPlace = 0;
	}
	_busy = true;
	_cookingDish = r.dish;
	_cookingDragon = r.dragon;
	CookLock lock(g_cookLock);  // the tasted list and the ingredient counts are saved from the main thread
	// everything that goes in the pot is tasted, even what the dish ends up not using (a clash, a spare copy)
	_learned.clear();
	std::vector<RE::TESBoundObject*> newlyTasted;
	for (auto item : _pot) {
		const auto ing = IngOf(item);
		if (!Tasted(item, ing) && std::ranges::find(newlyTasted, item) == newlyTasted.end()) {
			newlyTasted.push_back(item);
			_learned.push_back(std::format("{} ({})", item->GetName(), ShortRole(ing)));
		}
	}
	_tastedNow.clear();
	for (auto item : newlyTasted) {
		g_tasted.insert(item->GetFormID());
		_tastedNow.push_back(item->GetFormID());
	}
	if (!newlyTasted.empty()) {
		SendToScripts("FC_MenuTaste", newlyTasted);  // FC_Core keeps the list in the save
	}
	// the ingredient lists' Most used and Recent: each ingredient the dish uses counts once per cook
	// (not a potion the player brewed: each one is a new form that is gone once used, so the list would only grow;
	// Codex review 2026-11-15, M12)
	// (counted when the scripts say it cooked: Cooking/Rules.inl PapyrusCooked; no other cook runs here, see above)
	g_cookPendingUses.clear();
	for (std::size_t i = 0; i < _pot.size() && i < r.used.size(); ++i) {
		if (r.used[i] && !_pot[i]->IsDynamicForm()) {
			g_cookPendingUses.insert(_pot[i]->GetFormID());
		}
	}
	logger::info("Cook: {} ingredients, {} tasted for the first time", _pot.size(), newlyTasted.size());
	RE::PlaySound("UISmithingCreateGeneric");  // the crafting sound vanilla plays when you make something
	{
		// the quest-locked dishes open now, before the ingredients leave you (Codex review 2026-11-16g, 5)
		std::unordered_set<std::int32_t> open;
		{
			std::scoped_lock viewLock(g_viewLock);
			open = g_view.openDishes;
		}
		std::scoped_lock openLock(g_cookOpenLock);
		g_cookOpenDishes = std::move(open);
	}
	g_cookPlace = g_place.load();  // the dish is chosen for this station even if the screen closes before the scripts run
	g_cookPlaceSince = SteadyMs();
	_cookRequest = ++g_cookRequest;
	_cookSent = true;
	SendToScripts("FC_MenuCook", _pot);
}

// The emergency fallback: our screen closes and the station's vanilla crafting menu under it shows (it was only hidden);
// leaving that menu gets the player up as usual
void CookingMenu::OpenVanilla()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui || !g_overVanilla || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
		RE::PlaySound("UIMenuCancel");
		return;
	}
	g_toVanilla = true;
	RE::PlaySound("UIMenuOK");
	Close();
}

// R on a Misc recipe: made on the main thread (the inventory changes there), then the list is drawn again
void CookingMenu::MakeMiscEntry()
{
	const auto index = _entries[_selected].misc;
	if (!MiscMakeable(index, CarriedCounts())) {
		_notice = FC::Tr("You don't have everything this recipe needs.");
		RE::PlaySound("UIMenuCancel");
		ShowEntry(_selected);
		return;
	}
	_busy = true;
	SKSE::GetTaskInterface()->AddTask([index]() {
		const bool made = MakeMisc(index);
		CaptureView(true);
		const auto name = NameOf(g_misc[index].recipe->createdItem);
		SKSE::GetTaskInterface()->AddUITask([made, name]() {
			if (auto menu = GetOpenMenu()) {
				menu->MiscMade(made, name);
			}
		});
	});
}

void CookingMenu::MiscMade(bool a_made, const std::string& a_name)
{
	RE::PlaySound(a_made ? "UISmithingCreateGeneric" : "UIMenuCancel");
	const auto keep = _selected;
	Populate();  // also clears _busy
	if (keep >= 0 && keep < static_cast<std::int32_t>(_entries.size())) {
		ShowEntry(keep);
	}
	_notice = a_made ? FC::TrF("You made {0}.", a_name) : std::string(FC::Tr("You don't have everything this recipe needs."));
	if (auto movie = uiMovie.get()) {
		movie->SetVariable("_root.Menu.MenuDescription.text", _notice.c_str());
	}
}

void CookingMenu::Cooked(const std::string& a_message, std::uint32_t a_request)
{
	// a cook sent from an earlier visit to the pot: this screen's pot and its tasted notes are not that cook's, so they
	// stay; only the result is said (Codex review 2026-11-16 afternoon, M6). By number: this screen may have sent a new
	// cook in the moment before the earlier one's answer arrived (Codex review 2026-11-16d, M8)
	if (!_cookSent || a_request != _cookRequest) {
		_notice = a_message;
		// this screen's newer cook still running: the screen stays as it is (a refresh now unlocked R and pruned the pot
		// while that cook was moving ingredients); its own answer refreshes it (Codex review 2026-11-16e, M4)
		if (!_cookSent) {
			Populate();
		}
		return;
	}
	_cookSent = false;
	// what the pot made, worked out when R was pressed (the scripts' message is in the player's language, so its words
	// can't be matched): the recipe's own name from the rules, and whether it is a brew that ferments (young). Not from
	// the pot's items now: the last copy of a home-made potion may already be gone from the game (reviews 2026-11-15)
	struct
	{
		std::int32_t dish;
		bool         dragon;
	} const cookedResult{ _cookingDish, _cookingDragon };
	_cookingDish = -1;
	// the cooked ingredients are gone; what was not used went back to the pack, and the pot starts empty
	_pot.clear();
	_recipeBase.clear();
	_recipeName.clear();
	_recipeDish = nullptr;
	_notice = a_message;  // kept: Populate shows the same entry again
	// what the pot did, said before what it made: water for tea, juice and soups (no dish lists it, the pot always
	// holds some), fermenting for a brew. The name may carry a flavour ("Brainy Nord Mead") and a quality word
	// (the recipe names in the rules are English whatever the game's language: matched here, never shown)
	if (cookedResult.dish >= 0) {
		const std::string made = Str(std::format("dish.{}.n", cookedResult.dish));
		const bool young = Int(std::format("dish.{}.fe", cookedResult.dish)) == 1 && !cookedResult.dragon;
		auto has = [&](std::string_view a_word) { return made.find(a_word) != std::string::npos; };
		const char* how = nullptr;
		if (young) {
			how = FC::Tr(has("Mead") ? "You stir the honey into warm water and set it to ferment. It will be ready by morning." :
			      has("Wine") ? "You crush the fruit and set it to ferment. It will be ready by morning." :
			                    "You boil the grain into a wort and set it to ferment. It will be ready by morning.");
		} else if (has("Herbal Tea")) {
			how = FC::Tr("You boil water and steep the herbs.");
		} else if (has("Fruit Juice")) {
			how = FC::Tr("You crush the fruit and thin it with fresh water.");
		} else if (has(" Soup") || has(" Stew") || has(" Chowder")) {
			how = FC::Tr("You bring the water to a boil and let it simmer.");
		}
		if (how) {
			_notice = std::string(how) + " " + a_message;
		}
	}
	// the scripts may send their tasted list before they stored these; keep them either way
	{
		CookLock lock(g_cookLock);
		for (auto id : _tastedNow) {
			g_tasted.insert(id);
		}
	}
	_tastedNow.clear();
	if (!_learned.empty()) {
		_notice += " " + FC::TrF("You learned: {0}.", Join(_learned));
		_learned.clear();
	}
	// the skill framework only shows "Cooking increased" after the screen closes, so say it here too
	const auto skill = static_cast<std::int32_t>(CookingSkill());
	if (_lastSkill > 0 && skill > _lastSkill) {
		_notice += " " + FC::TrF("Your Cooking skill increased to {0}.", skill);
	}
	_lastSkill = skill;
	Populate();
}

void CookingMenu::Refused(const std::string& a_message)
{
	_cookingDish = -1;
	_cookSent = false;
	_notice = a_message;
	Populate();
}

void CookingMenu::OnCloseMenu(const RE::FxDelegateArgs&)
{
	Close();
}

void CookingMenu::OnPlaySound(const RE::FxDelegateArgs& a_args)
{
	if (a_args.GetArgCount() > 0 && a_args[0].IsString()) {
		RE::PlaySound(a_args[0].GetString());
	}
}

void CookingMenu::OnSetSelectedItem(const RE::FxDelegateArgs& a_args)
{
	auto menu = GetOpenMenu();
	if (menu && a_args.GetArgCount() > 0 && a_args[0].IsNumber()) {
		menu->ShowEntry(static_cast<std::int32_t>(a_args[0].GetNumber()));
	}
}

void CookingMenu::OnChooseItem(const RE::FxDelegateArgs& a_args)
{
	auto menu = GetOpenMenu();
	if (menu && a_args.GetArgCount() > 0 && a_args[0].IsNumber()) {
		menu->Press(static_cast<std::int32_t>(a_args[0].GetNumber()));
	}
}

void CookingMenu::OnCraftButton(const RE::FxDelegateArgs&)
{
	if (auto menu = GetOpenMenu()) {
		menu->Cook();
	}
}

// the controller was picked up or put down: the buttons switch to its art, as in vanilla menus (UI thread)
void CookingMenu::ShowPlatform()
{
	auto movie = uiMovie.get();
	const std::int32_t platform = g_padInUse ? 1 : 0;  // 0 keyboard and mouse, 1 controller (the movie's own art)
	if (!movie || platform == g_platformShown) {
		return;
	}
	g_platformShown = platform;
	RE::GFxValue args[2]{ static_cast<double>(platform), false };
	movie->Invoke("_root.Menu.SetPlatform", nullptr, args, 2);
	g_auxArt = -1;  // the third button's art is set again
	UpdateButtons();
}

void CookingMenu::OnAuxButton(const RE::FxDelegateArgs&)
{
	if (auto menu = GetOpenMenu()) {
		if (g_auxArt != 0) {
			menu->CycleSort();  // the button shows Q Sort: a click does the same
		} else {
			menu->AddAnother();
		}
	}
}

void CookingMenu::OnCanFadeItemInfo(const RE::FxDelegateArgs& a_args)
{
	a_args.Respond(true);
}

void CookingMenu::OnIgnore(const RE::FxDelegateArgs&) {}

// ---- the patcher page (Menu.cpp) reads the patcher through these (Cooking/Patcher.inl)
bool FC::PatcherRan() { return g_patcherRan; }
const std::vector<FC::PatchRow>& FC::PatchRows() { return g_patchRows; }
const std::vector<std::string>& FC::PatchGroups() { return g_patchGroups; }
const std::vector<std::pair<std::string, std::string>>& FC::PatchItemChoices() { return g_patchChoices; }
std::string FC::EditsFileName() { return EDITS_FILE; }
const std::vector<FC::MiscRow>& FC::MiscRows() { return g_miscRows; }
// the Misc recipes by key, worked out once the list exists (the page asks for every row on every frame: a search of
// the whole list for each row grew with the square of the list; Codex review 2026-11-15, M14)
static const std::vector<std::size_t>* MiscIndexOf(const std::string& a_key)
{
	static std::unordered_map<std::string, std::vector<std::size_t>> index;
	static std::size_t                                            indexed = 0;
	if (indexed != g_misc.size()) {
		index.clear();
		for (std::size_t i = 0; i < g_misc.size(); ++i) {
			index[g_misc[i].key].push_back(i);
		}
		indexed = g_misc.size();
	}
	const auto it = index.find(a_key);
	return it != index.end() ? &it->second : nullptr;
}
bool FC::MiscHidden(const std::string& a_key)
{
	const auto found = MiscIndexOf(a_key);
	return found && !found->empty() && g_misc[found->front()].hidden;
}
// the page's switch takes effect at once: the next time the cooking screen draws its list
void FC::SetMiscHidden(const std::string& a_key, bool a_hidden)
{
	if (const auto found = MiscIndexOf(a_key)) {
		for (const auto i : *found) {
			g_misc[i].hidden = a_hidden;
		}
	}
}
const FCPatch::EditSet& FC::PatchFileEdits() { return g_patchFileEdits; }

// ---- the Potions page (Menu.cpp; Cooking/Rules.inl JudgePotion, Cooking/Patcher.inl BuildPotionRows)
const std::vector<FC::PotionRow>& FC::PotionRows() { return g_potionRows; }
std::unordered_map<std::string, std::string> FC::PotionChoices()
{
	CookLock lock(g_cookLock);
	return g_potionChoice;
}
std::string FC::PotionChoice(const std::string& a_key)
{
	CookLock lock(g_cookLock);
	const auto it = g_potionChoice.find(a_key);
	return it == g_potionChoice.end() ? std::string() : it->second;
}
// the page's choice takes effect at once: the next time the cooking screen judges a potion
void FC::SetPotionChoice(const std::string& a_key, const std::string& a_value)
{
	CookLock lock(g_cookLock);
	if (a_value.empty()) {
		g_potionChoice.erase(a_key);
	} else {
		g_potionChoice[a_key] = a_value;
	}
	g_flavourCache.clear();
}

// ---- the Needs mods page (Menu.cpp; Cooking/Needs.inl)
bool FC::NeedsPageShown() { return NeedsModLoaded(); }
const std::vector<FC::NeedsRow>& FC::NeedsRows() { return g_needsRows; }
int FC::NeedsChoice(const std::string& a_key)
{
	std::scoped_lock lock(g_needsLock);
	const auto it = g_needsChoice.find(a_key);
	return it == g_needsChoice.end() ? -1 : it->second;
}
// the page's choice takes effect at once: every cooked dish of the game is sorted again on the main thread
void FC::SetNeedsChoice(const std::string& a_key, int a_choice)
{
	{
		std::scoped_lock lock(g_needsLock);
		if (a_choice < 0) {
			g_needsChoice.erase(a_key);
		} else {
			g_needsChoice[a_key] = a_choice;
		}
	}
	FC::RefreshNeedsLists();
}
void FC::RefreshNeedsLists()
{
	if (auto tasks = SKSE::GetTaskInterface()) {
		tasks->AddTask([] { NeedsRefreshAll(); });
	}
}

std::string FC::RecipeClash(const std::vector<FCPatch::SlotEdit>& a_slots, const std::string& a_selfKey, bool a_oven)
{
	std::vector<std::vector<std::string>> slots;
	for (const auto& s : a_slots) {
		const auto& from = s.item ? g_itemSlotKeys : g_groupSlotKeys;
		const auto  it = from.find(s.text);
		if (it == from.end() || it->second.empty()) return {};  // a slot nothing fills: no recipe to compare
		slots.push_back(it->second);
	}
	if (slots.empty()) return {};
	const auto sig = RecipeSig(std::move(slots));
	const auto it = g_recipeSigs.find(PlaceSig(a_oven, sig));
	if (it == g_recipeSigs.end()) return {};
	for (const auto& [name, key] : it->second) {
		if (key != a_selfKey) return name;
	}
	return {};
}

std::pair<std::string, std::string> FC::GroupMeaning(const std::string& a_group)
{
	std::string flavour;
	for (auto& r : GROUP_RULES) {
		if (a_group == r.group) flavour = r.flavour;
	}
	switch (FCPatch::GroupRestriction(a_group)) {
	case FCPatch::Restriction::Gross: return { flavour, "alc" };
	case FCPatch::Restriction::Poisonous: return { flavour, "garlic" };
	case FCPatch::Restriction::Namira: return { flavour, "ring" };
	case FCPatch::Restriction::Dragon: return { flavour, "dragon" };
	default: return { flavour, "" };
	}
}
const std::vector<std::string>& FC::GroupFlavours(const std::string& a_group)
{
	static const std::vector<std::string> none;
	const auto it = g_groupFlavours.find(a_group);
	return it == g_groupFlavours.end() ? none : it->second;
}
std::string FC::ImportFileName() { return IMPORT_FILE; }

FCPatch::EditSet FC::PlayerEdits()
{
	std::scoped_lock lock(g_editsLock);
	return LoadPlayerEdits();
}

bool FC::SavePlayerEdits(const FCPatch::EditSet& a_edits)
{
	std::scoped_lock lock(g_editsLock);
	auto clean = a_edits;
	// an all-automatic entry stays where a patch file set the item: it says "the patcher's own result, not the patch's"
	std::vector<std::string> keep;
	for (const auto& [key, e] : g_patchFileEdits.ingredients) keep.push_back(key);
	for (const auto& [key, e] : g_patchFileEdits.dishes) keep.push_back(key);
	clean.Clean(&keep);
	return WriteMyGamesFile(EDITS_FILE, clean.Write(EDITS_HEADER));
}

// every patcher item that is settled (a new ingredient, a new dish, Misc, or left out by hand), written out as a patch
// file a food mod can ship. Items the patcher didn't recognise, and automatic "not a new dish" results, are not written:
// they stay automatic, so later improvements to the patcher still reach them
std::string FC::ExportPatch()
{
	FCPatch::EditSet out;
	for (const auto& row : g_patchRows) {
		const bool leftOutByHand = row.kind == "Left out" && row.autoKind != "Left out";
		if (row.dish) {
			FCPatch::DishEdit e;
			if (row.kind == "New dish") {
				e.choice = FCPatch::DishChoice::New;
				e.tier = row.tier;
				e.slotsSet = !row.slots.empty();
				e.slots = row.slots;
			} else if (row.kind == "Misc") {
				e.choice = FCPatch::DishChoice::Misc;
			} else if (leftOutByHand) {
				e.choice = FCPatch::DishChoice::Not;
			} else {
				continue;
			}
			out.dishes[row.key] = e;
		} else {
			FCPatch::IngredientEdit e;
			if (row.kind == "Ingredient") {
				e.group = row.group;
				e.stars = row.stars;
				if (FC::GroupFlavours(row.group).size() > 1) e.flavour = row.flavour.empty() ? "None" : row.flavour;  // the one this game uses
			} else if (leftOutByHand) {
				e.group = "OUT";
			} else {
				continue;
			}
			out.ingredients[row.key] = e;
		}
	}
	std::scoped_lock lock(g_editsLock);
	// the Misc section as it is in this game: what patch files and the player hid or showed
	out.misc = g_patchFileEdits.misc;
	const auto player = LoadPlayerEdits();
	for (const auto& [key, hidden] : player.misc) out.misc[key] = hidden;
	// what our recipes are in the needs mods, as this game has them
	out.needs = g_patchFileEdits.needs;
	for (const auto& [key, choice] : player.needs) out.needs[key] = choice;
	// potions as this game has them: patch files' choices, the player's on top, an explicit AUTO kept (it undoes a patch
	// file's choice on the importing side too; Claude and Codex review 2026-11-22)
	out.potions = g_patchFileEdits.potions;
	for (const auto& [key, value] : player.potions) out.potions[key] = value;
	const char* header =
		"# Cooking Skill Overhaul patch file, exported from the patcher page: the modded foods and ingredients as they are in this game\n"
		"# (items the patcher didn't recognise are left out, so they stay automatic).\n"
		"# A food mod can ship this file (renamed, e.g. MyFoodMod.txt) in Data\\SKSE\\Plugins\\FreeCooking\\Patches.\n"
		"# A player can share it: the other player renames it FreeCooking_PatcherImport.txt and presses Import.\n";
	return WriteMyGamesFile(EXPORT_FILE, out.Write(header)) ? std::string(EXPORT_FILE) : std::string();
}

bool FC::ImportPatch(std::size_t& a_lines, std::size_t& a_bad)
{
	a_lines = a_bad = 0;
	std::map<std::string, std::string> potions;  // the potion choices in force after the import
	bool                               ok = false;
	{
		std::scoped_lock lock(g_editsLock);
		std::error_code  ec;
		const auto       path = MyGamesFile(IMPORT_FILE);
		if (path.empty() || !std::filesystem::exists(path, ec)) {
			return false;
		}
		FCPatch::EditSet imported;
		a_lines = imported.Read(ReadWordFile(path), &a_bad);
		auto edits = LoadPlayerEdits();
		edits.Replace(imported);
		std::vector<std::string> keep;
		for (const auto& [key, e] : g_patchFileEdits.ingredients) keep.push_back(key);
		for (const auto& [key, e] : g_patchFileEdits.dishes) keep.push_back(key);
		edits.Clean(&keep);
		ok = WriteMyGamesFile(EDITS_FILE, edits.Write(EDITS_HEADER));
		potions = g_patchFileEdits.potions;
		for (const auto& [key, value] : edits.potions) potions[key] = value;
	}
	if (ok) {
		SetPotionChoices(potions);  // potions work at once, like the page (g_editsLock released first: one lock order)
	}
	return ok;
}
