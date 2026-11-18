#pragma once

#include <format>
#include <string>

// Cooking Skill Overhaul's settings: the switches and numbers on its SKSE Menu Framework pages (Menu.cpp), kept in
// My Games/Skyrim Special Edition/SKSE/FreeCooking.ini (never in Data: his rule, nothing written at runtime may land in
// Data or MO2's Overwrite; until that file exists the old Data/SKSE/Plugins/FreeCooking/FreeCooking.ini is read, so
// settings carry over). Read once at kDataLoaded; the pages change them live and save the file.
// The game reads them through Get() (any thread: a copy is handed out under a lock).
namespace FC
{
	// the player's settings file (SkillsMenu.cpp, which already includes windows.h) and the file to read now: that one,
	// or the old one in Data on the first run after updating
	std::filesystem::path UserIniPath();
	std::filesystem::path IniToRead();

	// Cooking Skill Overhaul's Survival Mode features, each with its own switch (his rule, 2026-11-12: "everything we added for
	// survival must have an option to be turned off", for purists and for players who use other mods for those needs).
	// The numbers are shared with Papyrus (FC_Menu.SurvivalFeature): don't renumber.
	enum class Feature : std::int32_t
	{
		kSurvivalOn = -1,   // Survival Mode itself is on (no switch: it's the game's own setting)
		kHunger = 0,        // cooked dishes ease hunger by their own amount (off: the vanilla dish's Survival hunger)
		kSpicyCold = 1,     // Spicy holds off the worst of the cold
		kSpicyWater = 2,    // Spicy protects from freezing water
		kCampRest = 3,      // a brewed drink gives back the rest a night outdoors didn't give
		kSteppedDrains = 4, // the bars' penalties go in steps instead of creeping
		kHeartyRegen = 5,   // Hearty dishes add a little health regeneration
		kDragonWarmth = 6,  // a Dragon Scales drink fully warms and refreshes you
		kCampRestBonus = 7, // the camp rest drink can also give the Rested bonus, like a bed (off by default)
		kThirst = 8,        // Cooking Skill Overhaul's thirst (Survival Mode has none)
		kAfflictions = 9,   // a dish of the right flavour clears Survival's afflictions (Mighty Weakened, Brainy Addled, Spicy Frostbitten)
	};

	struct Options
	{
		// Cooking
		bool  skillsMenu = true;       // Cooking in the vanilla Skills menu, right of Alchemy, on by default (his call 2026-11-14); steps aside by itself when another mod owns the Skills menu
		float shareRangeMetres = 50.0f;  // Shared Table: how far a follower can be and still share
		bool  feet = false;  // distances shown in feet instead of metres (his call 2026-11-12: metric default, imperial an option)
		bool  vanillaCrafting = false;  // emergency fallback (his idea 2026-11-14): the cooking screen offers the station's own vanilla crafting menu
		bool  playerEatIdle = false;    // the player plays the vanilla eat/drink idle for Cooking Skill Overhaul dishes (his call 2026-11-14: off by
		                                // default, the idle's bread or tankard doesn't match what is eaten)
		// Survival Mode
		bool hunger = true;
		bool spicyCold = true;
		bool spicyWater = true;
		bool campRest = true;
		bool campRestBonus = false;  // his call 2026-11-12: an option; off keeps Rested a reward for beds
		bool steppedDrains = true;
		bool heartyRegen = true;
		bool dragonWarmth = true;
		bool thirst = true;
		bool afflictions = true;  // his design 2026-11-12: the flavour that counters an affliction clears it
		int  alcohol = 0;  // how much alcohol quenches: 0 like any drink, 1 half, 2 a fifth (his options 2026-11-12)
		// which Survival switches the player set (bit = Feature value): only those are written to the ini, so the others
		// keep following the defaults (his call 2026-11-14)
		std::uint32_t survivalChosen = 0;
	};

	// Other survival mods (his call 2026-11-14): found from the loaded plugins, for the log and the note on the Survival
	// Mode page. The switches start on with them too; Cooking/OtherMods.inl applies each feature to the mod's own values.
	struct SurvivalMods
	{
		bool smi = false;        // Survival Mode Improved - SKSE (runs Survival's needs in its own plugin)
		bool starfrost = false;  // Starfrost (runs on Survival Mode Improved, its own hunger rules)
		bool sunhelm = false;
		bool frostfall = false;  // cold only
		bool lastSeed = false;
		bool rnd = false;        // Realistic Needs and Diseases (or Nirnfeel, which carries it)
		bool ineed = false;
		bool Any() const { return smi || starfrost || sunhelm || frostfall || lastSeed || rnd || ineed; }
		bool NeedsMod() const { return sunhelm || lastSeed || rnd || ineed; }  // their own hunger, tiredness and penalties
		std::string Names() const;  // "Survival Mode Improved, Frostfall" (English names: they are the mods' own)
	};
	const SurvivalMods& OtherSurvivalMods();  // after kDataLoaded
	// whether the player has Survival_abLowerRegenSpell (natural health regeneration stopped), as the main-thread survival
	// tick last saw it: -1 not known yet, 0 no, 1 yes. The settings page reads this instead of the player's spells
	// (reviews 2026-11-16: the page runs on the render thread)
	int RegenStopped();
	void SetRegenStopped(bool a_stopped);
	Options DefaultOptions();  // the defaults (Options{}): every Survival switch on, the Rested option off
	constexpr std::uint32_t FeatureBit(Feature a_feature) { return 1u << static_cast<std::uint32_t>(a_feature); }

	inline constexpr float SHARE_RANGE_MIN = 5.0f;
	inline constexpr float SHARE_RANGE_MAX = 200.0f;
	inline constexpr float FEET_PER_METRE = 3.28084f;

	void    LoadSettings();  // kDataLoaded: the ini and the translation file
	Options GetOptions();
	void    SetOptions(const Options& a_options, bool a_save);  // a_save: also write the ini
	bool    FeatureSwitch(Feature a_feature);  // that feature's own switch (true for kSurvivalOn)
	bool    EatingAnimationsInstalled();  // Eating Animations and Sounds (Nexus SE 42602) is loaded: our player idle steps aside

	// Skills menu (SkillsMenu.cpp): at kPostLoad, before Custom Skills Framework reads its files, Cooking joins the vanilla
	// Skills menu through a SKILLS.json that only exists in memory, unless another mod replaces the Skills menu
	void InstallSkillsMenu();
	bool SkydomeAvailable();  // Cooking Skill Overhaul's 19-slot sky is installed (the switch needs it)
	void SetSkillsMenuShown(bool a_show);  // the switch changed: Custom Skills Framework reloads its menus at once
	void SyncSkillsPower();  // the Cooking Skills power only while Cooking isn't in the Skills menu (main thread)
	enum class OtherMenu
	{
		kNone,   // vanilla Skills menu without Cooking (switch off, or something missing)
		kOurs,   // Cooking is in the vanilla Skills menu this game start
		kOther,  // another mod's Skills menu (its own SKILLS.json): left as it is; Cooking keeps its own skill screen
	};
	OtherMenu OtherSkillsMenu();

	// the menu pages (Menu.cpp); without SKSE Menu Framework there are none and the ini is the way to change things
	void RegisterMenu();

	// in-game text in the player's language: Interface/Translations/FreeCooking_<LANGUAGE>.txt, "$FC_<English><TAB><text>"
	// lines; a missing file or line shows the English
	const char* Tr(const char* a_en);
	// true when the translation file has a line for this English text (a translator's own name for a dish, say)
	bool HasTr(const std::string& a_en);
	// capitals for any language's letters (UTF-8 in, UTF-8 out); std::toupper only knows A-Z
	std::string UpperText(const std::string& a_text);
	std::string UpperFirst(const std::string& a_text);  // only the first letter

	// a translated sentence with slots: the key is the whole English sentence with {0}, {1}... where the names and
	// numbers go, so a translation can put them in any order. A translation with broken slots shows the English
	template <class... Args>
	std::string TrF(const char* a_en, const Args&... a_args)
	{
		try {
			return std::vformat(Tr(a_en), std::make_format_args(a_args...));
		} catch (...) {  // any bad translation (broken slots, a huge width): the English instead
			try {
				return std::vformat(a_en, std::make_format_args(a_args...));
			} catch (...) {
				return a_en;
			}
		}
	}
}
