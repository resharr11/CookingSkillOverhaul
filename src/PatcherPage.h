#pragma once
// What the patcher page (Menu.cpp) reads from the patcher (Cooking/Patcher.inl). Filled once at kDataLoaded and
// read-only after that; the player's edits are saved to a file and used at the next game start.
#include "Patcher/Edits.h"

#include <string>
#include <utility>
#include <vector>

namespace FC
{
	struct PatchRow
	{
		std::string key;     // "<plugin>|<local ID>" (FCPatch::ItemKey)
		std::string name;    // as the game shows it
		std::string plugin;  // the file that adds it
		bool        dish = false;
		bool        food = false;  // a food item (not an alchemy ingredient): the player can make it a dish with a recipe
		// what the patcher made of it on its own (auto...) and what it is now, edits included. kind is one of a fixed set of
		// English phrases (the page shows it through the translation file): Ingredient, Not recognised, Misc, Left out,
		// A dish from another station, New dish, Vanilla look, Same look, Variant, No mesh, No ingredient fits a slot
		std::string autoKind, kind;
		std::string why;  // the automatic reason (words from the word file, not translated)
		// ingredient: group and stars; dish: tier and slots (items by key)
		std::string                    autoGroup, group;
		std::string                    autoFlavour, flavour;  // Mighty, Brainy... ("" for neutral groups)
		std::string                    autoNeeds, needs;      // what it needs in the pot: alc, garlic, ring, dragon (a dragon part), 
		int                            autoStars = 1, stars = 1;
		std::string                    autoTier, tier;
		std::vector<FCPatch::SlotEdit> autoSlots, slots;
		std::string                    waits;            // a dish that waits for this ingredient (named in its name, not recognised yet)
		std::string                    clash;            // a new dish not added: the pot already makes this dish's recipe for clash
		bool                           quest = false;    // a quest recipe: stays in Misc as its mod made it, not editable
		bool                           oven = false;     // a new dish made only at an oven (baked)
		bool                           edited = false;   // the player's edits file changes it
		bool                           patched = false;  // a food mod's patch file changes it
	};

	// one row of the Misc recipes page: an item a recipe at the pot or oven makes that our rules don't cook (the
	// cooking screen's Misc section). The player decides whether the cooking screen shows it.
	struct MiscRow
	{
		std::string key;     // the made item's "<plugin>|<local ID>" (FCPatch::ItemKey)
		std::string name;    // as the game shows it
		std::string plugin;  // the file that adds the recipe
		int         recipes = 1;        // recipes that make it (SunHelm boils water three ways)
		bool        pot = false;        // made at the cooking pot
		bool        oven = false;       // made at the Hearthfire oven
		bool        hiddenAtStart = false;  // hidden when the game started
		bool        byPatchFile = false;    // a food mod's patch file set it
	};
	const std::vector<MiscRow>& MiscRows();
	bool                        MiscHidden(const std::string& a_key);  // hidden right now (changes at once, no restart)
	void                        SetMiscHidden(const std::string& a_key, bool a_hidden);

	// one row of the Needs mods page: a Cooking Skill Overhaul recipe (by the dish it looks like) and what SunHelm, Last Seed, iNeed
	// and Realistic Needs count our cooked copies as while Cooking Skill Overhaul doesn't feed that need itself (his call 2026-11-15:
	// a general rule, and the player can choose by hand). The page shows only when one of those mods is loaded.
	struct NeedsRow
	{
		std::string key;   // the dish's "<plugin>|<local ID>" (FCPatch::ItemKey)
		std::string name;  // the recipe's name, translated
		bool        drink = false;
		bool        byPatchFile = false;
	};
	bool                         NeedsPageShown();
	const std::vector<NeedsRow>& NeedsRows();
	int                          NeedsChoice(const std::string& a_key);  // -1 automatic, 0 not food, 1 light, 2 medium, 3 heavy, 4 drink
	void                         SetNeedsChoice(const std::string& a_key, int a_choice);  // works at once
	void                         RefreshNeedsLists();  // a Survival switch changed: every cooked dish is sorted again

	bool                                                    PatcherRan();
	const std::vector<PatchRow>&                            PatchRows();
	const std::vector<std::string>&                         PatchGroups();       // our ingredient groups, in the menu's order
	const std::vector<std::pair<std::string, std::string>>& PatchItemChoices();  // every ingredient a slot can name: (name, key), A-Z

	// what a group means in the pot, for the page's group list: its flavour ("Mighty", "Sneaky or Hearty", "" for neutral)
	// and what it needs (alc, garlic, ring, dragon or "")
	std::pair<std::string, std::string> GroupMeaning(const std::string& a_group);
	// the flavours a group's items can have: its own first, then the ones a word in the name gives ("" = no flavour).
	// More than one: the player can choose (Game meat: Sneaky, or Hearty)
	const std::vector<std::string>& GroupFlavours(const std::string& a_group);

	// the dish that already has this recipe (slots in any order), "" if none. a_selfKey: the item being edited (its own
	// current recipe doesn't count)
	// a_oven: the edited dish is made only at an oven (a recipe at another station doesn't clash; Codex review 2026-11-16)
	std::string RecipeClash(const std::vector<FCPatch::SlotEdit>& a_slots, const std::string& a_selfKey, bool a_oven);

	FCPatch::EditSet        PlayerEdits();                         // the player's edits file as it is now
	const FCPatch::EditSet& PatchFileEdits();                      // what food mods' patch files set (read at game start)
	bool             SavePlayerEdits(const FCPatch::EditSet& a_edits);  // writes the file; false if it can't be written
	std::string      EditsFileName();                              // shown on the page
	// writes every patcher item's current result as a patch file; returns the file name, "" on failure
	std::string ExportPatch();
	// reads FreeCooking_PatcherImport.txt into the player's edits; a_lines = items read, a_bad = lines skipped;
	// false if there is no such file or it can't be saved
	bool        ImportPatch(std::size_t& a_lines, std::size_t& a_bad);
	std::string ImportFileName();
}
