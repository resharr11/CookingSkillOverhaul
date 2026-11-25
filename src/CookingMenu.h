#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Cooking Skill Overhaul: the cooking screen.
// The game's own crafting menu file, shipped as Interface/freecookingmenu.swf and driven from here.
// Stage 2a: categories (Food, Recipes, All, Untasted, the eight flavours, Fillers, Enablers), a pot of up
// to 5 ingredients filled right on the screen (E add or remove, F one more copy, E on a recipe puts its
// ingredients in), a live preview of the dish on the item card, and R Cook.
// Recipes and ingredient lists: Data/SKSE/Plugins/FreeCooking/menu.tsv. Pot rules: data.json, the same
// file the scripts read, so the preview follows the same rules as FC_Core.Recalc.
class CookingMenu : public RE::IMenu
{
public:
	static constexpr std::string_view MENU_NAME = "FreeCookingMenu";
	static constexpr std::string_view FILE_NAME = "freecookingmenu";  // Interface/freecookingmenu.swf
	static constexpr std::size_t      POT_SIZE = 5;

	static void Register();                  // at kDataLoaded: menu, recipe data, keys
	static void RegisterPapyrusFunctions();  // at plugin load: FC_Menu natives
	static void RegisterSerialization();     // at plugin load: what Cooking Skill Overhaul keeps in the save
	static void Open();
	static void GameLoading();              // a save is about to load (the watchers pause)
	static void GameLoaded(bool a_newGame = false);               // a save was loaded or a new game started
	static void GameLoadFailed();          // a save failed to load
	static void Close();

	CookingMenu();

	// IMenu
	void                   Accept(CallbackProcessor* a_processor) override;
	void                   AdvanceMovie(float a_interval, std::uint32_t a_currentTime) override;
	void                   PostDisplay() override;  // draws the dish's 3D model, like the vanilla crafting menu's item
	RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

	// recipe data (menu.tsv)
	struct Slot
	{
		std::string                      label;    // "any vegetable"; empty when the slot is one named ingredient
		std::vector<RE::TESBoundObject*> options;  // what can fill it
	};
	struct Dish
	{
		std::int32_t      index{ -1 };  // the dish's index in data.json (what FC_Core calls "best")
		std::string       name;
		std::int32_t      tier{ 0 };    // recipe (base) stars: 1 simple (0 until 2026-11-23), 2 prepared, 4 elaborate
		bool              drink{ false };
		std::vector<Slot> slots;
	};
	struct Pick
	{
		const Slot*         slot{ nullptr };
		RE::TESBoundObject* item{ nullptr };  // nullptr: nothing suitable carried (or only rare ones)
	};
	using Pot = std::vector<RE::TESBoundObject*>;  // one entry per copy, in the order they went in

	// keys and bottom bar buttons
	void Cook();         // R
	void AddAnother();   // F
	void CycleSort();    // Q
	void ShowPlatform();  // keyboard or controller buttons, whichever was used last
	void RemoveAtChar(std::int32_t a_index);  // a click on the "In the pot" line
	void Press(std::int32_t a_index);  // E
	void Populate();
	bool CookRunning() const { return _cookSent; }  // this screen's cook is still running (its own answer refreshes it)
	void ShowEntry(std::int32_t a_index);
	void Cooked(const std::string& a_message, std::uint32_t a_request);  // the scripts finished cooking (a_request: which cook)
	void Refused(const std::string& a_message);  // the scripts refused the cook (one still runs): the pot stays
	void MiscMade(bool a_made, const std::string& a_name);  // a Misc recipe was made (or couldn't be) on the main thread

private:
	enum class Kind
	{
		kRecipe,      // a discovered dish
		kUnknown,     // ??? : an undiscovered dish you could make now
		kMore,        // "+N more dishes you could make"
		kEmpty,       // no recipes yet
		kIngredient,  // something you carry that can go in the pot
		kMisc,        // another mod's recipe at this pot or oven, made as its mod made it (Misc, at the bottom)
		kVanilla      // the emergency fallback: open the station's own vanilla crafting menu (an option, off by default)
	};
	struct Entry
	{
		Kind                kind{ Kind::kEmpty };
		const Dish*         dish{ nullptr };
		std::int32_t        more{ 0 };
		RE::TESBoundObject* item{ nullptr };
		bool                enablerCopy{ false };  // a Resist Poison potion listed again under Enablers, by its plain name
		std::size_t         misc{ 0 };             // kMisc: its index in g_misc (Cooking/Misc.inl)
	};

	using Counts = std::unordered_map<RE::TESBoundObject*, std::int32_t>;

	static bool              LoadData();
	static Counts            PlayerCounts();
	static std::vector<Pick> Resolve(const Dish& a_dish, const Counts& a_counts, const Pot& a_pot);
	static bool              AllPicked(const std::vector<Pick>& a_picks);
	static std::string       PickName(const Pick& a_pick);

	void        Setup();
	void        UpdateSkillBar();
	void        UpdateButtons();
	void        RefreshMarks();
	void        Taste(RE::TESBoundObject* a_item);
	std::size_t Copies(RE::TESBoundObject* a_item) const;
	Pot         PotWithRecipe(const Dish& a_dish, const Counts& a_counts, std::string& a_notice) const;
	Pot         RecipeFill(const Dish& a_dish, const Counts& a_counts) const;  // empty: the recipe can't be made
	void        ShowPotCard(const Pot& a_pot, const std::string& a_lead);
	void        ShowFormCard(RE::TESBoundObject* a_item);  // the game's own card for an item (Misc)
	void        ShowSimpleCard(const std::string& a_name, const std::string& a_text, double a_weight = 0.5, double a_value = 0.0);
	void        ShowPotLine(const Pot& a_pot);
	void        TakeOut(RE::TESBoundObject* a_item, bool a_oneCopy = false);

	// callbacks from the menu's ActionScript (gfx.io.GameDelegate.call)
	static void OnCloseMenu(const RE::FxDelegateArgs& a_args);
	static void OnPlaySound(const RE::FxDelegateArgs& a_args);
	static void OnSetSelectedItem(const RE::FxDelegateArgs& a_args);
	static void OnChooseItem(const RE::FxDelegateArgs& a_args);
	static void OnCraftButton(const RE::FxDelegateArgs& a_args);
	static void OnAuxButton(const RE::FxDelegateArgs& a_args);
	static void OnCanFadeItemInfo(const RE::FxDelegateArgs& a_args);
	static void OnIgnore(const RE::FxDelegateArgs& a_args);

	bool               _initialized{ false };
	RE::TESBoundObject* _shown3D{ nullptr };  // the dish model on screen
	bool               _began3D{ false };      // we started the 3D view (no vanilla crafting menu under us)
	void               ShowDish3D(std::int32_t a_dish);
	void               ShowItem3D(RE::TESBoundObject* a_item);
	void               MakeMiscEntry();  // R on a Misc recipe
	void               OpenVanilla();    // E or R on the Vanilla entry
	std::int32_t       _framesAfterSetup{ 0 };
	std::vector<Entry> _entries;
	std::int32_t       _selected{ -1 };
	bool               _busy{ false };  // a cook is running in Papyrus; ignore R until it refreshes us
	std::uint32_t      _cookRequest{ 0 };  // the number of the cook this screen sent (g_cookRequest)
	bool               _cookSent{ false };  // this screen sent the running cook (a screen opened during it only shows the result)
	std::int32_t       _cookingDish{ -1 };     // what the running cook makes, worked out when R was pressed (Cooked reads it:
	bool               _cookingDragon{ false };  // by then the pot's items may be gone from the game)
	Pot                _pot;
	bool               _anyFood{ false };  // the Food category has something to show
	std::int32_t       _lastSkill{ 0 };  // Cooking level when the screen opened or last cooked
	std::string        _notice;
	struct PotLinePart
	{
		std::int32_t        start{ 0 };  // character range of the name on the line
		std::int32_t        end{ 0 };
		RE::TESBoundObject* item{ nullptr };
	};
	std::vector<PotLinePart> _potLine;
	std::vector<RE::TESBoundObject*> _recipeBase;  // the chosen recipe's own ingredients (E on a recipe)
	std::string        _recipeName;
	const Dish*        _recipeDish{ nullptr };
	std::vector<RE::FormID>  _tastedNow;  // the same ingredients' form IDs
	std::vector<std::string> _learned;  // "Fire Salts is Spicy": flavours learned by the cook in progress         // one line at the top of the card after an action ("The pot is full.")
};
