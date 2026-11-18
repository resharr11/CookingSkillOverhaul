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
		const std::function<bool(const std::string&)>& a_ourRecipeTakes, const std::function<bool(std::size_t, const DishResult&)>& a_rulesCook)
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
			if (existing.count(base)) {
				r.verdict = DishVerdict::Variant;
				r.why = "variant of an existing dish";
				continue;
			}
			if (auto it = byName.find(base); it != byName.end()) {
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
