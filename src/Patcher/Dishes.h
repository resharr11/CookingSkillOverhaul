#pragma once
// Cooking Skill Overhaul patcher, dish half: which modded dishes become Cooking Skill Overhaul dishes, their tier and their recipe slots.
// Plain C++ with no game types (the plugin fills the inputs from the game; the offline test from plugin files).
#include "Classifier.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace FCPatch
{
	struct DishItem
	{
		std::string name;            // display name
		std::string group;           // its ingredient group (our table for vanilla, the classifier for modded items)
		bool        isFood = false;  // a food item made by a recipe (a dish used as an ingredient)
		bool        hidden = false;  // nameless: a quest token
		bool        notFood = false; // neither food nor an alchemy ingredient (gold coins, tools): ignored unless the name names it
		int         count = 1;
	};

	struct DishRecipe
	{
		std::vector<DishItem> items;
		bool                  questConditions = false;  // the recipe checks a quest stage
		std::uint32_t         recipeId = 0;             // the recipe's form ID (its lock carries over to our dish)
	};

	struct DishInput
	{
		std::string             plugin;  // the file that adds the dish
		std::uint32_t           localId = 0;
		std::string             name;
		std::string             model;         // mesh path, lower case
		std::string             textureSwap;   // alternate textures, "" if none
		bool                    vanillaModel = false;  // a mesh used by a vanilla or Creation food
		std::string             hunger;        // Survival hunger size: VerySmall / Small / Medium / Large / ""
		std::vector<DishRecipe> recipes;       // recipes at the stations Cooking Skill Overhaul covers
		int                     forced = 0;    // the player's choice: 0 automatic, 1 new dish, 2 not a dish, 3 Misc
		bool                    questItem = false;  // a quest holds this item (a quest object alias): its quest needs it as made
	};

	enum class DishVerdict
	{
		NewDish,       // added to Cooking Skill Overhaul's dishes
		VanillaLook,   // looks like a vanilla dish: our ingredient rules already make it
		SameLook,      // looks like a modded dish added before it
		Variant,       // "Hot Beef Stew", a second "Apple Pie": a variant of an existing dish
		Misc,          // made from the Misc section only (special cases, quest recipes, preparation steps, conversions)
		NoLook,        // no mesh
		Unusable,      // a recipe slot nothing can fill (a_accept said no)
		LeftOut        // the player said it isn't a dish
	};

	constexpr const char* HERB_OR_SALT = "Herbs or salt";  // a slot "group" that isn't one of our 30: any flower or herb, or a salt

	// recipe slots that aren't one of our 30 groups but our own recipes' categories (the word file's CATEGORY lines):
	// "any flour" for baked dishes, "any dairy", "any egg"... A Staples ingredient (flour, butter, milk, egg, grain) fills
	// its category's slot, never "any staples", which our recipes don't use
	struct SlotCategory
	{
		const char* name;      // the slot's "group" in edits files and on the page
		const char* category;  // CATEGORY id in the word file
		const char* label;     // the cooking screen's label, as our own recipes write it
		const char* exclude = nullptr;  // a CATEGORY whose items never fill it (eggs and roe are no "any fish")
	};
	inline constexpr SlotCategory SLOT_CATEGORIES[] = {
		{ HERB_OR_SALT, "herbsalt", "any herb or salt" }, { "Flour", "flour", "any flour" }, { "Bread", "bread", "any bread" },
		{ "Egg", "egg", "any egg" }, { "Dairy", "dairy", "any dairy" }, { "Grain", "grain", "any grain" },
		{ "Fish", "fish", "any fish", "egg" },  // a recipe's fish its name doesn't name: any river, cold-water or exotic fish
	};
	inline const SlotCategory* SlotCategoryOf(std::string_view a_name)
	{
		for (const auto& c : SLOT_CATEGORIES) {
			if (a_name.size() == std::char_traits<char>::length(c.name) && std::equal(a_name.begin(), a_name.end(), c.name, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); })) return &c;
		}
		return nullptr;
	}

	struct DishSlot
	{
		bool        named = false;  // a named item; otherwise "any <group>"
		std::string text;           // item name or group
		std::string key;            // a named item chosen by hand on the patcher page: that exact item ("<plugin>|<ID>"),
		                            // not every item of its name (Codex review 2026-11-15, M13)
	};

	struct DishResult
	{
		DishVerdict           verdict = DishVerdict::NewDish;
		std::string           displayName;  // without a leading flavour word
		std::string           tier;         // Simple / Prepared / Elaborate
		bool                  baked = false;  // its name says it's baked: made only at an oven
		std::vector<DishSlot> slots;        // at most 3
		std::string           why;
		std::size_t           sameAs = static_cast<std::size_t>(-1);  // index of the dish it repeats
		std::vector<std::uint32_t> lockRecipes;  // every recipe of it is quest-locked: as a new dish, ours unlocks with any of them
	};

	// a_dishes is reordered into the fixed reading order (plugin file name, then local form ID); results match that order.
	// a_existingNames: names of the dishes Cooking Skill Overhaul already has (vanilla and Creations).
	// a_rawByKey: for a dish used as an ingredient ("Grilled Chicken Breast"), the raw item with the same key word ("chicken" -> "Chicken Breast").
	// a_ourRecipeTakes: true when one of Cooking Skill Overhaul's own recipes has a slot that takes this item (by name). The name rule
	// comes first: a dish with a vanilla or already-used look still becomes a new dish when its name names an ingredient
	// no recipe takes ("Elk Steak" on the vanilla steak mesh: nothing else cooks elk). Without it the look alone decides.
	// a_rulesCook (dish index in a_dishes, its result): true when our own pot rules cook its main ingredient into a dish of its kind
	// at its station (the look gives the kind). A look we have (vanilla, or a modded dish read before) counts as covered only
	// then: "Catfish Pie" on the apple pie mesh is covered by Fish Pie; with no general dish for it, it would be a new dish.

	std::vector<DishResult> DecideDishes(std::vector<DishInput>& a_dishes, const Words& a_words, const std::vector<std::string>& a_existingNames,
		const std::map<std::string, std::string>& a_rawByKey, const std::function<bool(const DishResult&)>& a_accept = {},
		const std::function<bool(const std::string&)>& a_ourRecipeTakes = {},
		const std::function<bool(std::size_t, const DishResult&)>& a_rulesCook = {});

	struct RawCandidate
	{
		std::string   name;
		bool          vanilla = false;
		std::string   plugin;
		std::uint32_t localId = 0;
		bool          isIngredient = false;
		bool          accepted = false;  // already an ingredient in our rules (set by a patch file or by hand): no name check
		bool          waiting = false;   // not recognised yet: counts only for a key word nothing else has, so a dish named
		                                 // after it ("Pineapple Pie") names it and waits for the player to set it
		std::string   group;             // its group when known (our table, or the patcher's result); "" = classify by name
	};
	// the raw item for each key word: vanilla items first, then the fixed reading order; dishes never count
	std::map<std::string, std::string> BuildRawByKey(std::vector<RawCandidate> a_items, const Words& a_words);

	const char* VerdictName(DishVerdict a_v);
}
