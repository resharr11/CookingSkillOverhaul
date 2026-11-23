#pragma once
// Cooking Skill Overhaul patcher, hand edits: what the player (or a food mod's patch file) set for an item, by its plugin and local
// form ID. Plain C++ with no game types, so the offline test reads and writes the same files as the plugin.
//
// File format (UTF-8 text, also what Export writes and what a food mod can ship in Data\SKSE\Plugins\FreeCooking\Patches):
//   # a note
//   ING   <plugin>|<local ID hex>   <group, or OUT to leave it out, or AUTO>   <stars 1-3, or AUTO>   [flavour]
//         (flavour, optional: for a group with more than one, e.g. Game meat is Sneaky or Hearty; None = no flavour)
//   DISH  <plugin>|<local ID hex>   <AUTO | NEW | NOT | MISC>   <AUTO | Simple | Prepared | Elaborate>   <AUTO, or up to 3 slots split by ; >
//   MISC  <plugin>|<local ID hex>   <SHOW | HIDE>
//         (a recipe at the pot or oven our rules don't cook, by the item it makes: shown in the cooking screen's Misc
//         section or not; added 2026-11-15, his call: "let them decide if a misc item shows on the crafting menu")
//   NEEDS <plugin>|<local ID hex>   <NOTFOOD | LIGHT | MEDIUM | HEAVY | DRINK>
//         (a Cooking Skill Overhaul recipe, by the dish it looks like: what SunHelm, Last Seed, iNeed and Realistic Needs count our
//         cooked copies as while Cooking Skill Overhaul doesn't feed them itself; added 2026-11-15, his call: "let the player choose
//         by hand with the patcher")
//   POTION <plugin>|<local ID hex>   <AUTO | NONE | Mighty | Brainy | Hearty | Sneaky | Spicy | Chilly | Zesty | Earthy>
//         (a potion as a pot seasoning: its flavour, or NONE = not a seasoning; added 2026-11-22, his call: "the player
//         can change" what a potion with several flavours does)
// A slot is "item:<plugin>|<local ID hex>" (that ingredient) or "group:<group>" (any ingredient of that group).
// Columns are split by tabs (not spaces: group names have spaces). Plugin names are compared without regard to case;
// a plugin name with letters outside English is matched only if the file is saved in the same encoding the game uses.
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace FCPatch
{
	struct IngredientEdit
	{
		std::string group;      // "" = automatic, "OUT" = leave it out, else one of our groups
		int         stars = 0;  // 0 = automatic, else 1-3
		std::string flavour;    // "" = automatic (from the name), "None" = no flavour, else Mighty, Sneaky...
		bool        Empty() const { return group.empty() && stars == 0 && flavour.empty(); }
	};

	enum class DishChoice
	{
		Auto,
		New,   // a new Cooking Skill Overhaul dish
		Not,   // not a dish: left out
		Misc   // the Misc section
	};

	struct SlotEdit
	{
		bool        item = false;  // true: one ingredient (text = its key "<plugin>|<ID>"); false: any of a group (text = group)
		std::string text;
	};

	struct DishEdit
	{
		DishChoice            choice = DishChoice::Auto;
		std::string           tier;               // "" = automatic, else Simple / Prepared / Elaborate
		bool                  slotsSet = false;   // false = automatic slots
		std::vector<SlotEdit> slots;              // at most 3
		bool                  Empty() const { return choice == DishChoice::Auto && tier.empty() && !slotsSet; }
	};

	// "<plugin>|<local ID hex>" with the plugin in lower case and the ID as 6 upper-case hex digits, so keys from any
	// file or from the game compare equal
	std::string ItemKey(std::string_view a_plugin, std::uint32_t a_localId);
	std::string NormalizeKey(std::string_view a_key);  // "" if it isn't a key

	struct EditSet
	{
		std::map<std::string, IngredientEdit> ingredients;  // by normalized key
		std::map<std::string, DishEdit>       dishes;
		std::map<std::string, bool>           misc;  // by the made item's key: true = hidden from the Misc section
		std::map<std::string, std::string>    needs;  // by the recipe's dish key: NOTFOOD / LIGHT / MEDIUM / HEAVY / DRINK
		std::map<std::string, std::string>    potions;  // by the potion's key: AUTO / NONE / a flavour (Mighty...)

		// adds the lines of a file; later lines and later files win. Returns the number of lines read; a_bad counts lines
		// that couldn't be read (they are skipped)
		std::size_t Read(std::string_view a_text, std::size_t* a_bad = nullptr);
		std::string Write(std::string_view a_header) const;
		// a_top's edits replace this set's for the same item (a part left automatic in a_top keeps this set's value):
		// how food mods' patch files stack
		void        Layer(const EditSet& a_top);
		// a_top's entries replace this set's whole entries, AUTO parts included: how the player's edits sit on top of the
		// patch files ("Automatic" on the page means the patcher's own result, even where a patch file set something)
		void        Replace(const EditSet& a_top);
		// drops entries that are all automatic, except for keys in a_keep (an all-AUTO player entry over a patch file)
		void        Clean(const std::vector<std::string>* a_keep = nullptr);
		bool        Empty() const { return ingredients.empty() && dishes.empty() && misc.empty() && needs.empty() && potions.empty(); }
	};

	const char* ChoiceName(DishChoice a_c);  // AUTO / NEW / NOT / MISC
	// a POTION line's value as the file and the game use it: "AUTO", "NONE" or a flavour with its capital ("Mighty");
	// "" if it isn't one of those
	std::string PotionValue(std::string_view a_text);
}
