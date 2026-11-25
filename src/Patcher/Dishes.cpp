#include "Dishes.h"

#include <algorithm>
#include <map>
#include <cctype>
#include <limits>
#include <set>

namespace FCPatch
{
	const char* VerdictName(DishVerdict a_v)
	{
		switch (a_v) {
		case DishVerdict::NewDish: return "NEW DISH";
		case DishVerdict::VanillaLook: return "vanilla look";
		case DishVerdict::SameLook: return "same look";
		case DishVerdict::Variant: return "variant";
		case DishVerdict::Misc: return "Misc";
		case DishVerdict::NoLook: return "no mesh";
		case DishVerdict::LeftOut: return "left out by hand";
		default: return "unusable";
		}
	}

	namespace
	{
		std::string LowerAscii(std::string s)
		{
			for (auto& c : s) {
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			return s;
		}

		struct Prepared
		{
			std::vector<DishItem> items;  // salt and duplicates removed
			int                   kinds = 0;
			bool                  quest = false;  // it checks a quest stage
			bool                  token = false;  // it takes a hidden quest item (a nameless one; a named non-playable item is a category token, CACO's "Salt")
			std::uint32_t         recipeId = 0;
			bool                  salt = false;  // the recipe had salt (a seasoning slot can take "any herb or salt", like our own dishes)
		};

		Prepared Prepare(const DishRecipe& a_r, const Words& a_w)
		{
			Prepared              p;
			std::set<std::string> seen;
			p.quest = a_r.questConditions;
			p.recipeId = a_r.recipeId;
			for (auto& it : a_r.items) {
				if (it.hidden) p.token = true;
				if (a_w.IsSalt(it.name)) {
					p.salt = true;
					continue;
				}
				const std::string k = LowerAscii(it.name);
				if (!seen.insert(k).second) continue;
				p.items.push_back(it);
			}
			p.kinds = static_cast<int>(p.items.size());
			return p;
		}

		// the raw items a cooked dish in a recipe can stand for, indexed by their distinctive words (built once per raw map;
		// the patcher reads every recipe item against it, so a scan of every item for each would cost hundreds of ms)
		struct RawIndex
		{
			struct Item
			{
				std::string              name;
				std::vector<std::string> words;
			};
			const std::map<std::string, std::string>* source = nullptr;
			std::size_t                               size = 0;
			std::vector<Item>                         items;   // "#" order: vanilla first, then A-Z
			std::map<std::string, std::vector<std::size_t>> byWord;
		};
		const RawIndex& RawWordIndex(const std::map<std::string, std::string>& a_raw, const Words& a_w)
		{
			thread_local RawIndex index;
			if (index.source == &a_raw && index.size == a_raw.size()) return index;
			index = {};
			index.source = &a_raw;
			index.size = a_raw.size();
			for (auto r = a_raw.lower_bound("#"); r != a_raw.end() && r->first.starts_with("#"); ++r) {
				RawIndex::Item item{ r->second, a_w.DistinctWords(r->second) };
				for (auto& w : item.words) index.byWord[w].push_back(index.items.size());
				index.items.push_back(std::move(item));
			}
			return index;
		}

		// rule 7 (our conventions): an ingredient the dish name names is a named slot and always comes first; every other
		// ingredient becomes its group, same groups merge. The slots match the tier (Simple 1, Prepared 2, Elaborate 3):
		// a_target slots from the name's cooking word (0: no such word, use the whole recipe), more if the name names more
		// ingredients (at most 3); short of groups, the name's tier (or a recipe that had salt) gets one "any herb or salt"
		// seasoning slot, like our own dishes (Cabbage Soup: Cabbage + any herb or salt)
		std::vector<DishSlot> Slots(const std::string& a_dishName, const Prepared& a_p, const Words& a_w, const std::map<std::string, std::string>& a_raw, std::size_t a_target)
		{
			std::vector<DishSlot>      named;
			std::set<std::string>      namedGroups;  // a group a named item already stands for takes no second slot ("Barley Flour + any flour")
			std::map<std::string, int> groups;
			std::vector<std::string>   groupOrder;
			std::map<std::string, std::vector<std::string>> groupItems;  // the raw items behind each group slot
			for (auto& it : a_p.items) {
				// a dish used as an ingredient counts as its raw base ("Grilled Chicken Breast" -> chicken)
				std::string group = it.group.empty() || it.isFood ? a_w.GroupOf(it.name) : it.group;
				if (group.empty()) group = it.group;
				// a fish the dish doesn't name can be any fish (a stew doesn't care where its fish came from); fish eggs and roe
				// are eggs
				if (group == "River fish" || group == "Cold-water fish" || group == "Exotic fish") group = a_w.InCategory("egg", group, it.name) ? "Egg" : "Fish";
				if (group == "Staples") {  // flour, bread, egg, butter or milk, grain: the category our own recipes use
					for (const char* cat : { "Flour", "Bread", "Egg", "Dairy", "Grain" }) {
						if (a_w.InCategory(SlotCategoryOf(cat)->category, group, it.name)) {
							group = cat;
							break;
						}
					}
					// salt, garlic, spices, oil, vinegar: a seasoning, the "any herb or salt" slot our own recipes use; water is
					// the pot's, not an ingredient slot ("any staples" is no slot of ours)
					if (group == "Staples") {
						if (a_w.InCategory("nonalcohol", "", it.name) && !a_w.InCategory("dairy", "", it.name)) continue;
						group = HERB_OR_SALT;
					}
				}
				const std::string key = a_w.KeyPhrase(it.name);
				// the name rule comes first: an item the dish name names is its slot even with no known word ("Pineapple
				// Pie": the pie waits for Pineapple to get a group, it doesn't become a pie of whatever else is in it)
				const bool namedByDish = a_w.NamesItem(a_dishName, it.name);
				if (it.notFood && !namedByDish) continue;  // coins, tools: not cooking ingredients
				if (it.isFood && group.empty() && !namedByDish) continue;  // a broth or marinade with no known word: the pot makes its parts anyway
				// an ingredient of a restricted group (poisonous, gross, Namira, dragon) is never an "any ..." slot: it stays
				// itself, and the pot asks for its helper as it does for a seasoning (his call 2026-11-24: Gourmet's
				// Roasted Mushrooms became "any poisonous plants" from its Namira's Rot)
				if (!namedByDish && !it.isFood && !group.empty() && GroupRestriction(group) != Restriction::None) {
					bool dup = false;
					for (auto& n : named) dup = dup || n.text == it.name;
					if (!dup) named.push_back({ true, it.name });
					namedGroups.insert(group);
					continue;
				}
				if (namedByDish || group.empty()) {
					std::string text = it.name;
					if (it.isFood) {
						// the edible raw item the cooked dish's name names, vanilla first ("Cooked Horker Loaf" -> Horker Meat)
						// (the one sharing the most words wins: "Grilled Chicken Breast" -> Chicken Breast, not Chicken's Egg)
						std::string byName;
						{
							const auto& index = RawWordIndex(a_raw, a_w);
							const auto  mineList = a_w.DistinctWords(it.name);
							const std::set<std::string> mine(mineList.begin(), mineList.end());
							std::set<std::size_t> seen;
							int         bestScore = (std::numeric_limits<int>::min)();
							std::size_t bestOrder = 0;
							for (const auto& w : mineList) {
								const auto hit = index.byWord.find(w);
								if (hit == index.byWord.end()) continue;
								for (const auto order : hit->second) {
									if (!seen.insert(order).second) continue;
									const auto& cand = index.items[order];
									if (!a_w.NamesItem(it.name, cand.name)) continue;
									// most shared words, then fewest words of its own ("Orange", not "Orange Dartwing", for an orange
									// slice), then vanilla and reading order
									int shared = 0;
									for (auto& cw : cand.words) shared += mine.contains(cw) ? 1 : 0;
									const int score = shared * 10 - static_cast<int>(cand.words.size() - shared);
									if (score > bestScore || (score == bestScore && order < bestOrder)) {
										bestScore = score;
										bestOrder = order;
										byName = cand.name;
									}
								}
							}
						}
						const auto raw = a_raw.find(Lower(key));
						if (!byName.empty()) {
							text = byName;
						} else if (raw != a_raw.end()) {
							text = raw->second;
						} else if (group.empty()) {
							// a raw item not recognised yet that both names ("Pineapple" in "Pineapple Half" and in "Pineapple
							// Pie"): the slot names it, so the dish waits for the player to set it
							bool found = false;
							for (auto it2 = a_raw.lower_bound("?"); it2 != a_raw.end() && it2->first.starts_with("?"); ++it2) {
								if (a_w.NamesItem(it.name, it2->second) && a_w.NamesItem(a_dishName, it2->second)) {
									text = it2->second;
									found = true;
									break;
								}
							}
							if (!found) continue;  // a broth or sauce with no known word: the pot makes its parts anyway
						} else {  // no raw item with that word: it counts as its group
							if (!groups.count(group)) groupOrder.push_back(group);
							groups[group] += std::max(1, it.count);
							continue;
						}
					}
					bool dup = false;
					for (auto& n : named) dup = dup || n.text == text;
					if (!dup) named.push_back({ true, text });
					if (!group.empty()) namedGroups.insert(group);
					continue;
				}
				if (!groups.count(group)) groupOrder.push_back(group);
				groups[group] += std::max(1, it.count);
				if (!it.isFood) groupItems[group].push_back(it.name);
			}
			// one main ingredient (everything else is seasoning): the dish is made of that item, named, not of its group
			// (his rule 2026-11-13 "single-meat dishes: named and fallback", which reached only items the dish's name names;
			// 2026-11-24: Roasted Mushrooms)
			if (named.empty()) {
				std::vector<std::string> mains;
				for (auto& g : groupOrder) {
					if (g != HERB_OR_SALT) mains.push_back(g);
				}
				if (mains.size() == 1 && groups[mains[0]] == 1 && groupItems[mains[0]].size() == 1) {
					named.push_back({ true, groupItems[mains[0]].front() });
					namedGroups.insert(mains[0]);
					std::erase(groupOrder, mains[0]);
				}
			}
			std::stable_sort(groupOrder.begin(), groupOrder.end(), [&](auto& a, auto& b) { return groups[a] > groups[b]; });
			// a seasoning is the last slot a dish keeps, as in our own dishes (Cabbage Soup: Cabbage + any herb or salt)
			std::stable_partition(groupOrder.begin(), groupOrder.end(), [](auto& g) { return g != HERB_OR_SALT; });
			// a baked dish keeps its flour
			// a baked dish keeps its flour, like our pies and breads (Apple Pie: an apple + Wheat Flour + any sweetener); so does
			// a dough dish that isn't baked (pasta, waffles, pancakes, fritters)
			if (a_w.IsBaked(a_dishName) || a_w.IsDough(a_dishName)) {
				std::stable_partition(groupOrder.begin(), groupOrder.end(), [](auto& g) { return g == "Flour"; });
			}
			std::vector<DishSlot> out;
			for (auto& n : named) {
				if (out.size() < 3) out.push_back(n);
			}
			const std::size_t want = std::min<std::size_t>(3, a_target ? std::max(a_target, out.size()) : out.size() + groupOrder.size());
			for (auto& g : groupOrder) {
				if (out.size() < want && !namedGroups.contains(g)) out.push_back({ false, g });
			}
			bool hasSeasoning = false;
			for (auto& s : out) hasSeasoning = hasSeasoning || (!s.named && s.text == HERB_OR_SALT);
			if (out.size() < want && !hasSeasoning && (a_p.salt || a_target > 0)) out.push_back({ false, HERB_OR_SALT });  // the seasoning slot, as in our own dishes
			// the same one-main rule on the dish as it ends up: when the tier left a single main slot and that group came from
			// one raw item, the slot is that item (his call 2026-11-24: Gourmet's Roasted Wild Mushrooms, Scaly Pholiota +
			// Butter + Garlic, is Simple, keeps only its mushroom slot, and showed "any mushrooms")
			{
				DishSlot*   onlyMain = nullptr;
				std::size_t mainCount = 0;
				for (auto& s : out) {
					if (s.named || s.text != HERB_OR_SALT) {
						++mainCount;
						onlyMain = &s;
					}
				}
				if (mainCount == 1 && !onlyMain->named && groups[onlyMain->text] == 1 && groupItems[onlyMain->text].size() == 1) {
					*onlyMain = { true, groupItems[onlyMain->text].front() };
				}
			}
			return out;
		}

		std::string TierFrom(const std::string& a_name, const Prepared* a_p, const std::string& a_hunger, const Words& a_w, std::string& a_why)
		{
			// rule 6, in this order: a culinary word in the name, else the recipe's kinds (salt ignored), else the hunger size
			if (auto t = a_w.DishTier(a_name); !t.empty()) {
				a_why = "name word";
				return t;
			}
			if (a_p && a_p->kinds > 0) {
				a_why = std::to_string(a_p->kinds) + " kinds";
				return a_p->kinds >= 3 ? "Elaborate" : a_p->kinds == 2 ? "Prepared" : "Simple";
			}
			a_why = "hunger size";
			if (a_hunger == "Large") return "Elaborate";
			if (a_hunger == "VerySmall") return "Prepared";
			return "Simple";
		}
	}

	std::vector<DishResult> DecideDishes(std::vector<DishInput>& a_dishes, const Words& a_w, const std::vector<std::string>& a_existingNames,
		const std::map<std::string, std::string>& a_rawByKey, const std::function<bool(const DishResult&)>& a_accept,
		const std::function<bool(const std::string&)>& a_ourRecipeTakes, const std::function<bool(std::size_t, const DishResult&)>& a_rulesCook,
		const std::function<std::int32_t(std::size_t, const DishResult&)>& a_ourGeneral)
	{
		std::set<std::string> namedByNewDishes;  // items a new dish read before names (its own recipe takes them)
		// fixed reading order, so which item wins never depends on the load order
		std::stable_sort(a_dishes.begin(), a_dishes.end(), [](const DishInput& a, const DishInput& b) {
			const auto la = LowerAscii(a.plugin), lb = LowerAscii(b.plugin);
			return la != lb ? la < lb : a.localId < b.localId;
		});

		std::map<std::string, std::size_t> byLook;  // look -> first new dish with it
		std::map<std::string, std::size_t> byName;  // base name -> first new dish with it
		std::set<std::string>              existing;
		for (auto& n : a_existingNames) existing.insert(a_w.BaseKey(n));

		std::vector<DishResult> out(a_dishes.size());
		std::vector<std::vector<std::string>> ownItems(a_dishes.size());  // the raw items of the recipe each dish's slots came from
		for (std::size_t i = 0; i < a_dishes.size(); ++i) {
			const DishInput& d = a_dishes[i];
			DishResult&      r = out[i];
			r.displayName = a_w.WithoutFlavour(d.name);

			// the recipe used: the one with the most named items, then the fewest kinds
			std::vector<Prepared> recipes;
			for (auto& rc : d.recipes) recipes.push_back(Prepare(rc, a_w));
			const Prepared* best = nullptr;
			std::size_t     bestNamed = 0;
			for (auto& p : recipes) {
				std::size_t named = 0;
				for (auto& it : p.items) {
					if (a_w.Mentions(d.name, a_w.KeyPhrase(it.name))) ++named;
				}
				if (!best || named > bestNamed || (named == bestNamed && p.kinds < best->kinds)) {
					best = &p;
					bestNamed = named;
				}
			}

			// tier and slots first, even for a dish that ends up Misc or left out: the player can still make it a new dish by hand
			// the slots decide the tier (Simple 1, Prepared 2, Elaborate 3); the name's cooking word sets how many to aim for
			const auto        nameTier = a_w.DishTier(d.name);
			const std::size_t target = nameTier == "Elaborate" ? 3 : nameTier == "Prepared" ? 2 : nameTier == "Simple" ? 1 : 0;
			r.baked = a_w.IsBaked(d.name);
			if (best) r.slots = Slots(d.name, *best, a_w, a_rawByKey, target);
			if (best) {
				for (const auto& it : best->items) {
					if (!it.hidden && !it.notFood && !it.isFood && !it.name.empty()) ownItems[i].push_back(it.name);
				}
			}
			if (!r.slots.empty()) {
				r.tier = r.slots.size() >= 3 ? "Elaborate" : r.slots.size() == 2 ? "Prepared" : "Simple";
				r.why = target == 0 ? "from its recipe" : r.slots.size() == target ? "name word" : r.slots.size() > target ? "named ingredients" : "fewer ingredients than its name suggests";
			} else {
				r.tier = TierFrom(d.name, best, d.hunger, a_w, r.why);
			}

			// a quest item stays as its mod made it, whatever is set by hand: its quest checks for that exact item. Any of
			// the dish's recipes counts, not only the one used for its slots (a mod can have a plain recipe and a quest one
			// for the same food; Codex review 2026-11-15, H8). A quest item: a quest holds it, or a recipe takes a nameless
			// quest item, with or without quest conditions on its recipe: the same test the Misc list uses (Cooking/Misc.inl
			// QuestItemRecipe; reviews 2026-11-16g). A recipe a quest only unlocks makes a dish like any other, and a new
			// dish keeps the lock: ours unlocks when theirs does (his call 2026-11-16: "respect design and intent"; a
			// vanilla look stays under our rules with no lock: nothing unique to unlock)
			const bool anyQuest = std::ranges::any_of(recipes, [](const Prepared& a_p) { return a_p.quest; });
			if (std::ranges::any_of(recipes, [](const Prepared& a_p) { return a_p.token; }) || d.questItem) {
				r.verdict = DishVerdict::Misc;
				r.why = "quest recipe";
				continue;
			}
			if (anyQuest && std::ranges::all_of(recipes, [](const Prepared& a_p) { return a_p.quest; })) {
				for (const auto& p : recipes) {
					r.lockRecipes.push_back(p.recipeId);  // any of them opens ours (two quests can each unlock one)
				}
			}

			// the player's choice comes first: a dish left out or sent to Misc doesn't take its look and name, so the next
			// dish with that look can still become new; a dish made new by hand takes them
			if (d.forced == 2 || d.forced == 3) {
				r.verdict = d.forced == 2 ? DishVerdict::LeftOut : DishVerdict::Misc;
				r.why = "set by hand";
				continue;
			}
			if (d.forced == 1) {
				r.verdict = DishVerdict::NewDish;
				r.why = "set by hand";
				byLook.emplace(d.model + "|" + d.textureSwap, i);
				byName.emplace(a_w.BaseKey(d.name), i);
				continue;
			}

			// Misc: special cases ("Poisoned ..."), quest recipes, and one-kind recipes that aren't cooking (slices, halves,
			// conversions, jerky)
			if (a_w.IsMisc(d.name)) {
				r.verdict = DishVerdict::Misc;
				r.why = "special case (a Misc word in the name)";
				continue;
			}
			if (best && best->kinds <= 1 && a_w.DishTier(d.name).empty() && !a_w.IsCookedCut(d.name)) {
				r.verdict = DishVerdict::Misc;
				r.why = "one ingredient, not cooked (preparation step or conversion)";
				continue;
			}

			const std::string base = a_w.BaseKey(d.name);
			// a model of its own (a mod's mesh, or the game's with the mod's textures) that no dish shows yet is never a
			// variant: we respect the author's art whatever the name (his rule 2026-11-24: "it cant become a variant. i said
			// we respect art"); it goes on to the look rules and becomes a dish of its own
			const bool ownArt = !d.model.empty() && (!d.vanillaModel || !d.textureSwap.empty()) && !byLook.contains(d.model + "|" + d.textureSwap);
			if (existing.count(base) && !ownArt) {
				r.verdict = DishVerdict::Variant;
				r.why = "variant of an existing dish";
				continue;
			}
			if (auto it = byName.find(base); it != byName.end() && !ownArt) {
				r.verdict = DishVerdict::Variant;
				r.sameAs = it->second;
				r.why = "variant of a modded dish read before it";
				continue;
			}
			if (d.model.empty()) {
				r.verdict = DishVerdict::NoLook;
				r.why = "no mesh";
				continue;
			}
			// the name rule first: an ingredient the name names that no recipe takes makes it a dish whatever its look
			bool namesUncooked = false;
			if (a_ourRecipeTakes) {
				for (auto& s : r.slots) {
					if (s.named && !namedByNewDishes.contains(LowerAscii(s.text)) && !a_ourRecipeTakes(s.text)) namesUncooked = true;
				}
			}
			const std::string look = d.model + "|" + d.textureSwap;
			// a dish of ours of the same kind takes its main ingredient: our recipe stays and this model becomes a look of that dish (his rules
			// 2026-11-24: "we only take the model from the mods. our mod does the cooking")
			if (ownArt && !namesUncooked && a_ourGeneral) {
				if (const auto ours = a_ourGeneral(i, r); ours >= 0) {
					r.verdict = DishVerdict::OwnLook;
					r.ourDish = ours;
					r.why = "a dish of ours of the same kind takes its main ingredient: its model is a look of that dish";
					byLook.emplace(look, i);
					continue;
				}
			}
			bool notCooked = false;  // a look we have, but our rules don't cook its ingredients into a dish like it
			if (!namesUncooked) {
				if (d.vanillaModel && d.textureSwap.empty()) {
					if (!a_rulesCook || a_rulesCook(i, r)) {
						r.verdict = DishVerdict::VanillaLook;
						r.why = "vanilla mesh";
						continue;
					}
					notCooked = true;
				} else if (auto it = byLook.find(look); it != byLook.end()) {
					// a look a modded dish read before already has: covered when our rules cook this one too (its general
					// dish: "Creamy Catfish Bisque" after "Creamy Pogfish Bisque" is a fish stew); the first dish of the look
					// doesn't decide it (our rules can't cook catfish into a Pogfish Bisque)
					if (!a_rulesCook || a_rulesCook(i, r)) {
						r.verdict = DishVerdict::SameLook;
						r.sameAs = it->second;
						r.why = "same look as a modded dish read before it";
						continue;
					}
					notCooked = true;
				}
			}
			if (namesUncooked && ((d.vanillaModel && d.textureSwap.empty()) || byLook.contains(look))) {
				r.why = "its name names an ingredient no recipe cooks";
			} else if (notCooked) {
				r.why = "our rules don't cook its ingredients into a dish like it";
			}
			r.verdict = DishVerdict::NewDish;
			if (a_accept && !a_accept(r)) {  // checked before the look and name are taken, so a later dish can still use them
				r.verdict = DishVerdict::Unusable;
				r.why = "a recipe slot nothing can fill";
				continue;
			}
			byLook.emplace(look, i);
			byName.emplace(base, i);
			for (auto& s : r.slots) {
				if (s.named) namedByNewDishes.insert(LowerAscii(s.text));
			}
		}
		// Same recipe, different models (his call 2026-11-24: an author's own models are respected, so each new dish needs
		// a recipe the pot can tell apart; before, the dish listed first always won and the others could never be made).
		// New dishes whose slots came out the same (and the same station) each keep the ingredient of their own mod's
		// recipe that the fewest of them use, as a named slot in place of their last "any ..." slot. Measured on the 18
		// studied food mods: 96 of 105 tied dishes get a recipe of their own; the rest are left to the player's pick at
		// the pot (ChooseDish prefers the recipe picked from the Recipes list on a tie). Repeated (at most 3 rounds) while
		// it still finds ties: dishes from different groups can pick the same item and meet again.
		for (int round = 0; round < 3; ++round) {
			bool changedAny = false;
			std::map<std::string, std::vector<std::size_t>> sameRecipe;
			for (std::size_t i = 0; i < out.size(); ++i) {
				if (out[i].verdict != DishVerdict::NewDish || a_dishes[i].forced != 0 || out[i].slots.empty()) continue;
				std::string key = out[i].baked ? "oven" : "pot";
				for (const auto& sl : out[i].slots) key += (sl.named ? "|=" : "|~") + LowerAscii(sl.text);
				sameRecipe[key].push_back(i);
			}
			for (const auto& [key, ids] : sameRecipe) {
				if (ids.size() < 2) continue;
				std::map<std::string, int> uses;  // how many of the tied dishes use each item
				std::map<std::size_t, std::vector<std::string>> candidates;
				for (const auto i : ids) {
					auto& list = candidates[i];
					for (const auto& name : ownItems[i]) {
						const bool inSlots = std::ranges::any_of(out[i].slots, [&](const DishSlot& a_s) { return a_s.named && LowerAscii(a_s.text) == LowerAscii(name); });
						if (!inSlots && std::ranges::find(list, name) == list.end()) list.push_back(name);
					}
					for (const auto& name : list) ++uses[LowerAscii(name)];
				}
				for (const auto i : ids) {
					const auto& list = candidates[i];
					if (list.empty()) continue;
					const auto pick = std::ranges::min_element(list, [&](const std::string& a, const std::string& b) { return uses[LowerAscii(a)] < uses[LowerAscii(b)]; });
					std::size_t last = out[i].slots.size();
					for (std::size_t j = 0; j < out[i].slots.size(); ++j) {
						if (!out[i].slots[j].named) last = j;
					}
					if (last == out[i].slots.size()) continue;  // every slot already names an item
					DishResult changed = out[i];
					changed.slots[last] = { true, *pick };
					std::stable_partition(changed.slots.begin(), changed.slots.end(), [](const DishSlot& a_s) { return a_s.named; });  // named slots first
					if (a_accept && !a_accept(changed)) continue;  // nothing could fill it: keep the old recipe
					if (changed.why.find("same recipe as another dish") == std::string::npos) changed.why += "; own ingredient (same recipe as another dish)";
					out[i] = std::move(changed);
					changedAny = true;
				}
			}
			if (!changedAny) break;
		}
		return out;
	}

	namespace
	{
		// the family of an ingredient group: the meats are one, the fish one, fruit and berries one, the mushrooms one
		std::string FamilyOf(const std::string& a_group)
		{
			if (a_group == "Game meat" || a_group == "Big game") return "Meat";
			if (a_group == "River fish" || a_group == "Cold-water fish" || a_group == "Exotic fish" || a_group == "Fish") return "Fish";
			if (a_group == "Fruit & nuts" || a_group == "Berries") return "Fruit";
			if (a_group == "Mushrooms" || a_group == "Glowing fungi") return "Mushrooms";
			return a_group;
		}
		// groups that are never a dish's main ingredient: what our own recipes use as their second or third slot
		bool Structural(const std::string& a_group)
		{
			static const std::set<std::string> s{ "", "?", "Flour", "Egg", "Dairy", "Bread", "Staples", HERB_OR_SALT, "Hot herbs", "Cold herbs",
				"Drinks", "Grain", "Sweeteners", "Cheese" };
			return s.contains(a_group);
		}
		// the name word of a family, as one and as many ("Vegetable Stew", "Roasted Vegetables")
		std::pair<std::string, std::string> FamilyWords(const std::string& a_family)
		{
			if (a_family == "Vegetables") return { "Vegetable", "Vegetables" };
			if (a_family == "Mushrooms") return { "Mushroom", "Mushrooms" };
			if (a_family == "Flowers & herbs") return { "Flower", "Flowers" };
			if (a_family == "Fat & flesh") return { "Offal", "Offal" };
			if (a_family == "Fruit") return { "Fruit", "Fruit" };
			return { a_family, a_family };
		}
		std::string Capitalised(const std::string& a_words)
		{
			std::string out = a_words;
			bool start = true;
			for (auto& ch : out) {
				if (start && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
				start = ch == ' ';
			}
			return out;
		}
		std::size_t SlotsOfTier(const std::string& a_tier) { return a_tier == "Elaborate" ? 3 : a_tier == "Prepared" ? 2 : 1; }
		void FillSlots(GeneralKind& a_k)
		{
			a_k.slots.resize(1);
			const std::size_t want = SlotsOfTier(a_k.tier);
			for (const auto& g : a_k.moreGroups) {
				if (a_k.slots.size() >= want) break;
				a_k.slots.push_back({ g });
			}
			// short of groups: the seasoning slot our own dishes use (Cabbage Soup: Cabbage + any herb or salt)
			const bool hasSeasoning = std::ranges::any_of(a_k.slots, [](const auto& a_s) { return a_s.size() == 1 && a_s[0] == HERB_OR_SALT; });
			if (a_k.slots.size() < want && !hasSeasoning) a_k.slots.push_back({ HERB_OR_SALT });
			// still short (Elaborate with one known group): the next common partner of our own recipes
			for (const char* g : { "Vegetables", "Dairy", "Flour" }) {
				if (a_k.slots.size() >= want) break;
				if (FamilyOf(g) == a_k.family) continue;
				if (std::ranges::any_of(a_k.slots, [&](const auto& a_s) { return a_s.size() == 1 && a_s[0] == g; })) continue;
				a_k.slots.push_back({ g });
			}
		}
	}

	std::vector<std::string> FamilyGroups(const std::string& a_family)
	{
		if (a_family == "Meat") return { "Game meat", "Big game" };
		if (a_family == "Fish") return { "Fish" };  // the slot category: any river, cold-water or exotic fish
		if (a_family == "Fruit") return { "Fruit & nuts", "Berries" };
		if (a_family == "Mushrooms") return { "Mushrooms", "Glowing fungi" };
		return { a_family };
	}

	bool RaiseGeneralKind(GeneralKind& a_kind)
	{
		if (a_kind.tier == "Elaborate") return false;
		a_kind.tier = a_kind.tier == "Simple" ? "Prepared" : "Elaborate";
		FillSlots(a_kind);
		return true;
	}

	bool LowerGeneralKind(GeneralKind& a_kind)
	{
		if (a_kind.tier != "Elaborate") return false;
		a_kind.tier = "Prepared";
		a_kind.slots.resize(1);
		FillSlots(a_kind);
		return true;
	}

	bool SwapGeneralKindLastSlot(GeneralKind& a_kind, std::size_t& a_next)
	{
		if (a_kind.slots.size() < 2) return false;
		std::vector<std::string> options = a_kind.moreGroups;
		for (const char* g : { "Vegetables", "Dairy", "Flour", "Egg", "Mushrooms", "Grain", "Sweeteners", HERB_OR_SALT }) {
			if (std::ranges::find(options, g) == options.end()) options.push_back(g);
		}
		while (a_next < options.size()) {
			const auto& g = options[a_next++];
			if (FamilyOf(g) == a_kind.family) continue;
			bool used = false;
			for (std::size_t j = 0; j < a_kind.slots.size(); ++j) {
				used = used || std::ranges::find(a_kind.slots[j], g) != a_kind.slots[j].end();
			}
			if (used) continue;
			a_kind.slots.back() = { g };
			return true;
		}
		return false;
	}

	std::vector<GeneralKind> FindGeneralKinds(const std::vector<DishInput>& a_dishes, const std::vector<DishResult>& a_results, const std::vector<bool>& a_oven,
		const Words& a_w, const std::function<std::string(const std::string&)>& a_groupOf,
		const std::function<bool(const std::string&, const std::string&, bool, std::size_t)>& a_covered)
	{
		static const std::vector<std::string> PRIORITY{ "Meat", "Fish", "Shellfish", "Mushrooms", "Fruit", "Vegetables" };
		const auto rank = [](const std::string& a_f) {
			const auto at = std::ranges::find(PRIORITY, a_f);
			return at == PRIORITY.end() ? PRIORITY.size() : static_cast<std::size_t>(at - PRIORITY.begin());
		};
		struct Member
		{
			std::size_t dish;
			std::string key;      // the main item (its recipe item, or the name's ingredient word)
			std::string written;  // the kind word as the name writes it
			bool        general;  // its main slot is "any <family>": its model shows the dish for any item of the group
		};
		std::map<std::pair<std::string, std::string>, std::vector<Member>> families;  // (word, family) -> dishes
		for (std::size_t i = 0; i < a_dishes.size() && i < a_results.size(); ++i) {
			const auto& r = a_results[i];
			// a dish of ours, or one our rules already cook: Misc, left out, unusable and lookless dishes are no evidence
			if (r.verdict == DishVerdict::Misc || r.verdict == DishVerdict::LeftOut || r.verdict == DishVerdict::Unusable || r.verdict == DishVerdict::NoLook) continue;
			const std::string& name = r.displayName.empty() ? a_dishes[i].name : r.displayName;
			// a restricted item the name names stays itself (Namira's dishes, a dragon heart, a poisonous mushroom): no family
			bool restricted = false;
			std::string group, key;
			std::size_t best = PRIORITY.size() + 1;
			for (const auto& s : r.slots) {
				if (!s.named) continue;
				const std::string g = a_groupOf ? a_groupOf(s.text) : std::string();
				if (!g.empty() && GroupRestriction(g) != Restriction::None) {
					if (a_w.NamesItem(name, s.text)) restricted = true;
					continue;
				}
				if (Structural(g)) continue;
				if (rank(FamilyOf(g)) < best) {
					best = rank(FamilyOf(g));
					group = g;
					key = LowerAscii(s.text);
				}
			}
			if (restricted) continue;
			if (group.empty()) {
				group = a_w.GroupOf(name);
				key = LowerAscii(a_w.KeyPhrase(name));
				if (!group.empty() && GroupRestriction(group) != Restriction::None) continue;
				if (Structural(group)) {
					group.clear();
					for (const auto& s : r.slots) {  // a named slot the name mentions ("Stuffed Chicken" with Chicken Breast)
						if (!s.named || !a_w.NamesItem(name, s.text)) continue;
						const std::string g = a_groupOf ? a_groupOf(s.text) : std::string();
						if (g.empty() || Structural(g) || GroupRestriction(g) != Restriction::None) continue;
						group = g;
						key = LowerAscii(s.text);
						break;
					}
				}
			}
			if (group.empty()) continue;
			bool cut = false;
			std::string written;
			const std::string word = a_w.KindWord(name, a_w.KeyPhrase(key.empty() ? name : key), &cut, &written);
			if (word.empty() || cut) continue;  // no kind word, or a cut of meat (our Grilled Meat)
			// a general dish of the mod's own (its main slot "any ..." of the family, not one named item): only its model can
			// be a general dish's look; a model of one named item shows that item (Gourmet's Roasted Carrots: carrots)
			const bool general = std::ranges::any_of(r.slots, [&](const DishSlot& a_s) { return !a_s.named && FamilyOf(a_s.text) == FamilyOf(group); });
			families[{ word, FamilyOf(group) }].push_back({ i, key, written, general });
		}

		std::vector<GeneralKind> out;
		for (const auto& [wf, members] : families) {
			const auto& [word, family] = wf;
			GeneralKind k;
			k.word = word;
			k.family = family;
			std::set<std::string> items;
			std::map<std::string, std::size_t> modelUses;
			std::map<std::string, std::size_t> modelFirst;  // model -> first dish with it (reading order)
			std::size_t ovens = 0;
			std::map<std::string, int> tiers;
			std::map<std::string, int> partners;
			std::map<std::string, int> written;
			for (const auto& m : members) {
				const auto& d = a_dishes[m.dish];
				const auto& r = a_results[m.dish];
				k.sources.push_back(m.dish);
				items.insert(m.key);
				++written[m.written];
				if (m.dish < a_oven.size() && a_oven[m.dish]) ++ovens;
				++tiers[r.tier];
				// a model a mod ships (or a vanilla mesh with the mod's own textures): the art a general dish can show
				// (his rule 2026-11-24: "we cant make a dish if we have no model"; Roasted Cabbage showed Gourmet's carrots)
				if (m.general && !d.model.empty() && (!d.vanillaModel || !d.textureSwap.empty())) {
					const std::string look = d.model + "|" + d.textureSwap;
					++modelUses[look];
					modelFirst.emplace(look, m.dish);
				}
				// the groups its mod's recipes use besides the main one (the whole recipes: a Simple dish's slots keep only
				// its main item, "Roasted Carrots" lost its butter), each counted once per dish
				std::set<std::string> uses;
				for (const auto& rc : d.recipes) {
					for (const auto& it : rc.items) {
						if (it.hidden || it.notFood) continue;
						std::string g = a_w.IsSalt(it.name) ? std::string(HERB_OR_SALT) : !it.group.empty() && !it.isFood ? it.group : (a_groupOf ? a_groupOf(it.name) : std::string());
						if (g.empty() && it.isFood) g = a_w.GroupOf(it.name);
						if (g == "River fish" || g == "Cold-water fish" || g == "Exotic fish") g = "Fish";
						if (g == "Staples") {  // a staple counts as its category, as in the dishes' own slots (Bread, Flour, Dairy...)
							if (a_w.InCategory("nonalcohol", "", it.name) && !a_w.InCategory("dairy", "", it.name)) continue;  // water: the pot's
							std::string cat = HERB_OR_SALT;
							for (const char* x : { "Flour", "Bread", "Egg", "Dairy", "Grain" }) {
								if (a_w.InCategory(SlotCategoryOf(x)->category, g, it.name)) {
									cat = x;
									break;
								}
							}
							g = cat;
						}
						if (g.empty() || g == "?" || FamilyOf(g) == family || GroupRestriction(g) != Restriction::None) continue;
						if (rank(FamilyOf(g)) < rank(family)) continue;  // one main: a vegetable stew's fish would make it a fish dish
						uses.insert(g);
					}
				}
				for (const auto& g : uses) ++partners[g];
			}
			k.items = items.size();
			if (k.items < 2 || modelUses.empty()) continue;  // one item is that item's own dish; no mod model of a general dish: no dish
			k.oven = ovens * 2 > members.size();
			// the tier from our own table for the kind word; a word with none (pasty, cobbler) takes its dishes' usual tier
			k.tier = a_w.DishTier(word);
			if (k.tier.empty()) {
				int most = -1;
				for (const auto& [t, n] : tiers) {
					if (!t.empty() && n > most) {
						most = n;
						k.tier = t;
					}
				}
				if (k.tier.empty()) k.tier = "Simple";
			}
			// needed only when no dish we have of that kind at that station already takes most of the group in its first slot
			// (Vegetable Soup makes every vegetable soup; for a Simple kind any one-slot Simple dish counts: our Grilled
			// Meat makes every cooked, fried or seared meat at the pot)
			if (a_covered && a_covered(word, family, k.oven, SlotsOfTier(k.tier))) continue;
			std::vector<std::pair<std::string, int>> ranked(partners.begin(), partners.end());
			std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			// the seasoning goes last, as in our own dishes (Cabbage Soup: Cabbage + any herb or salt)
			std::stable_partition(ranked.begin(), ranked.end(), [](const auto& a_g) { return a_g.first != HERB_OR_SALT; });
			for (const auto& [g, n] : ranked) k.moreGroups.push_back(g);
			k.slots.push_back(FamilyGroups(family));
			FillSlots(k);
			// its look: the mod model most of its dishes use (then reading order); every mod model is kept in the list
			std::vector<std::pair<std::string, std::size_t>> models(modelUses.begin(), modelUses.end());
			std::stable_sort(models.begin(), models.end(), [&](const auto& a, const auto& b) {
				return a.second != b.second ? a.second > b.second : modelFirst[a.first] < modelFirst[b.first];
			});
			for (const auto& [mdl, n] : models) k.models.push_back(mdl.substr(0, mdl.find('|')));
			k.look = modelFirst[models.front().first];
			// its name: one pattern per kind (a word in "ed" goes first: "Roasted Vegetables"; others after: "Vegetable Stew")
			std::string shown = word;
			int most = -1;
			for (const auto& [wr, n] : written) {
				if (n > most) {
					most = n;
					shown = wr;
				}
			}
			const auto [one, many] = FamilyWords(family);
			if (shown.size() > 2 && shown.ends_with("ed")) {
				k.name = Capitalised(shown) + " " + many;
				k.generalWord = many;
			} else {
				k.name = one + " " + Capitalised(shown);
				k.generalWord = one;
			}
			k.id = "k:" + word + "|" + family + "|" + (k.oven ? "oven" : "pot");
			out.push_back(std::move(k));
		}
		return out;
	}

	std::map<std::string, std::string> BuildRawByKey(std::vector<RawCandidate> a_items, const Words& a_w)
	{
		std::stable_sort(a_items.begin(), a_items.end(), [](const RawCandidate& a, const RawCandidate& b) {
			if (a.waiting != b.waiting) return b.waiting;
			if (a.vanilla != b.vanilla) return a.vanilla;
			const auto la = LowerAscii(a.plugin), lb = LowerAscii(b.plugin);
			return la != lb ? la < lb : a.localId < b.localId;
		});
		std::map<std::string, std::string> out;
		// a cooked dish in a recipe stands for an edible raw item: never a hide, horn, eye or other part ("Horse Haunch" is
		// horse meat, not Horse Hair)
		static const std::set<std::string> inedible{ "Hides, scales & shells", "Claws, teeth & horns", "Feathers & wings", "Eyes, ears & tongues",
			"Magical remains", "Poisonous plants" };
		for (auto& it : a_items) {
			Input x;
			x.name = it.name;
			x.isIngredient = it.isIngredient;
			std::string group = it.group;
			if (!it.accepted || group.empty()) {
				const auto r = a_w.Classify(x);
				if (!it.accepted && r.kind != Kind::Ingredient) continue;
				if (group.empty()) group = r.group;
			}
			if (inedible.contains(group)) continue;
			// by name: "#0<name>" vanilla first, "#1<name>" the rest (see Slots: the item the cooked dish's name names)
			if (!it.waiting) out.emplace(std::string(it.vanilla ? "#0" : "#1") + Lower(it.name), it.name);
			std::string key = Lower(a_w.KeyPhrase(it.name));
			if (key.empty() && it.accepted) key = "?" + Lower(it.name);  // no known word (set by hand, or not recognised yet): found by name (see Slots)
			if (!key.empty()) out.emplace(key, it.name);
		}
		return out;
	}
}
