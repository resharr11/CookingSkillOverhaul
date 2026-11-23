#pragma once
// Cooking Skill Overhaul patcher: decides what another mod's food or alchemy ingredient is in Cooking Skill Overhaul's rules.
// Plain C++ with no game types, so the same code runs in the plugin and in the offline test (test_main.cpp).
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace FCPatch
{
	enum class Kind
	{
		Skip,        // left out entirely: spoiled stage, test record, trap bait, poison, nameless
		Ingredient,  // goes into the pot, in group `group`
		Dish,        // a finished dish: never a pot ingredient (a new Cooking Skill Overhaul dish only when DishRules say so)
		Misc,        // a special case (a poisoned food made for reverse pickpocketing): kept out of the pot, listed in Misc
		Unknown      // no rule recognised it: shown on the review page for the player to set
	};

	enum class Restriction
	{
		None,
		Gross,      // needs any alcoholic drink (or a dragon part) in the pot
		Poisonous,  // needs garlic (or a dragon part) in the pot
		Namira,     // needs the Ring of Namira worn
		Dragon      // Dragon parts (Ultimate)
	};

	struct Input
	{
		std::string              name;      // display name as the game shows it (UTF-8)
		std::string              editorId;  // empty unless po3's Tweaks supplied it
		bool                     isIngredient = false;  // INGR record (else a food-flagged ALCH)
		bool                     isPoison = false;
		bool                     playable = true;
		std::vector<std::string> keywords;  // keyword editor IDs (the game keeps these)
	};

	struct Result
	{
		Kind        kind = Kind::Unknown;
		std::string group;              // ingredient group (our 30), empty unless Ingredient
		Restriction restriction = Restriction::None;
		int         stars = 1;          // every new ingredient starts at 1 star
		std::string dishTier;           // Simple / Prepared / Elaborate when kind == Dish and a term was found
		std::string why;                // short reason shown on the review page and in the log
	};

	class Words
	{
	public:
		// reads one word file (several can be loaded: one per language); returns false if it can't be opened
		bool LoadFile(const std::string& a_path);
		bool LoadText(std::string_view a_text);

		Result Classify(const Input& a_in) const;

		// helpers for the dish rules
		std::string GroupOf(const std::string& a_name) const;      // the group word in a name, ignoring dish words ("" if none)
		std::string KeyPhrase(const std::string& a_name) const;    // the word that gave that group ("raw beef" -> "beef")
		bool        Mentions(const std::string& a_text, const std::string& a_phrase) const;
		std::string DishTier(const std::string& a_name) const;     // Simple / Prepared / Elaborate from a dish word, or ""
		std::string BaseKey(const std::string& a_name) const;      // lower-case name without leading variant and flavour words
		std::string WithoutFlavour(const std::string& a_name) const;  // display name without a leading flavour word
		bool        IsSalt(const std::string& a_name) const;
		bool        IsMisc(const std::string& a_name) const;
		// a cut of meat cooked whole ("Elk Steak", "Goat Haunch"): one ingredient is a Simple dish, not a preparation step,
		// unless the name says it's uncooked ("Raw Bear Steak" is cut, not cooked)
		bool IsCookedCut(const std::string& a_name) const;
		// a baked food: its name ends in a baked dish word ("Blackbird Pie", "Walnut Bread") or has a baking word anywhere
		// ("Twice Baked Potatoes"): made at an oven, like our own pies and breads
		bool IsBaked(const std::string& a_name) const;
		// made of a flour dough or batter but not baked (pasta, waffles, pancakes, fritters): keeps its flour, any station
		bool IsDough(const std::string& a_name) const;
		// an ingredient's name as a dish's main word: "Raw Goat Meat" -> "Goat", "Leg of Goat" -> "Goat", "Catfish" -> "Catfish"
		// (SHORTDROP words removed, the rest kept as written); "" when nothing is left
		std::string ShortName(const std::string& a_name) const;
		// already preserved (jerky, cured, smoked): not a raw main ingredient for a general dish
		bool IsPreserved(const std::string& a_name) const { return Any(_preserved, a_name, false); }
		// the words of a name that say which item it is (stemmed; plain, joining, salt, dish, variant, flavour and generic group
		// words left out): "Raw Horker Meat" -> {horker}
		std::vector<std::string> DistinctWords(const std::string& a_name) const;
		// a dish name names an ingredient when it has a distinctive word of the ingredient's name ("Mammoth Tomato Stew"
		// names "Raw Mammoth Meat": "mammoth"); PLAIN words (raw, fresh, sliced...) and generic group words (meat, fish) don't count
		bool        NamesItem(const std::string& a_dishName, const std::string& a_itemName) const;  // a MISC word ("poisoned"): a special case for the Misc section
		// recipe categories ("dairy", "grain", "flour"...): an ingredient of one of the listed groups, or whose name has one of its words
		bool        InCategory(const std::string& a_category, const std::string& a_group, const std::string& a_name) const;
		// a group's flavour exception by word ("Game meat" + "beef" -> Hearty); "" when none applies
		std::string FlavourWord(const std::string& a_group, const std::string& a_name) const;
		// the other flavours a group's items can have by a word in their name (FLAVOURWORD lines), in file order
		std::vector<std::string> FlavourWordsOf(const std::string& a_group) const;
		// a potion with several flavours: the flavour its name gives (POTION lines), only one of a_allowed (the potion's
		// own flavours); "" when no word of those flavours is in the name. Same choice as for groups: a specific word
		// before a generic one, then the word nearest the end, then the longer phrase (2026-11-22, his call: "the name is
		// more important, like we did with the foods")
		std::string PotionWord(const std::string& a_name, const std::vector<std::string>& a_allowed) const;

		struct Phrase
		{
			std::vector<std::string> tokens;  // space languages: normalized words
			std::string              raw;     // languages without spaces: matched as a substring
			bool                     cjk = false;
			bool                     generic = false;  // written with a leading ~: loses to any specific word
		};

	private:
		struct Entry
		{
			Phrase      phrase;
			std::string target;  // group name, or tier for DISH
		};

		struct Match
		{
			const Entry* entry = nullptr;
			std::size_t  end = 0;  // token index (or byte offset) where the match ends
		};

		bool         EndsIn(const std::vector<Entry>& a_end, const std::vector<Entry>* a_except, const std::string& a_name) const;
		const Entry* Best(const std::vector<Entry>& a_list, const std::string& a_text, bool a_isId) const;
		bool         Any(const std::vector<Entry>& a_list, const std::string& a_text, bool a_isId) const;

		std::vector<Entry>                               _groups, _dish, _skip, _variant, _namira, _people, _join, _combine, _safeEnd, _dragonBody, _flavour, _salt, _misc, _plain, _cut, _uncooked, _bakedEnd, _bakedAny, _serving, _notBaked, _doughEnd, _doughAny, _shortDrop, _preserved, _potion;
		std::vector<std::pair<std::string, std::string>> _keywords;  // keyword editor ID -> group / DISH / SKIP
		struct Category
		{
			std::vector<std::string> groups;
			std::vector<Entry>       words;
		};
		std::map<std::string, Category>                 _categories;
		struct FlavourWordEntry
		{
			std::string group;
			Entry       entry;          // word -> flavour
			bool        option = true;  // offered to the player as one of the group's flavours (SPECIES lines aren't)
		};
		std::vector<FlavourWordEntry> _flavourWords;
	};

	// helpers shared with the plugin
	std::string              Lower(std::string_view a_text);
	std::vector<std::string> Tokens(std::string_view a_text, bool a_splitCamel);
	bool                     LooksLikeId(std::string_view a_name);
	Restriction              GroupRestriction(std::string_view a_group);
	const char*              RestrictionName(Restriction a_r);
}
