// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Cooking rules: constants, data.json, ingredients, the pot preview, Papyrus bridge.
	constexpr double ICT_FOOD = 5.0;  // InventoryDefines.ICT_FOOD in the menu's ActionScript
	constexpr std::uint32_t KEY_R = 0x13;
	constexpr std::uint32_t KEY_F = 0x21;
	constexpr std::uint32_t KEY_Q = 0x10;
	constexpr std::uint32_t KEY_TAB = 0x0F;
	constexpr std::uint32_t KEY_ESC = 0x01;
	// controller buttons as the game reports them (Interface\Controls\PC\controlmap.txt), in the vanilla crafting
	// menu's layout (craftingmenu.swf: E/A select, Tab/B exit, F/Y third button, R/X craft). A reaches the screen
	// like E does, through the movie
	constexpr std::uint32_t PAD_B = 0x2000;
	constexpr std::uint32_t PAD_X = 0x4000;
	constexpr std::uint32_t PAD_Y = 0x8000;
	std::atomic<bool> g_padInUse{ false };  // the last input came from a controller: the screen shows controller buttons
	constexpr RE::FormID KEYWORD_COOKPOT = 0x000A5CB3;  // CraftingCookpot (cooking pots and spits)
	constexpr RE::FormID PERK_TASTE = 0x862;            // Cook (root): the pot shows stars, level and duration; meals last half again as long
	constexpr RE::FormID PERK_CURIOUS = 0x854;          // Gourmand: tastes ingredients as they go in
	constexpr RE::FormID PERK_LONG_SIMMER = 0x85D;      // Long Simmer: no drink or garlic needed
	constexpr RE::FormID PERK_HOUSE_RECIPE = 0x85A;     // House Recipe: every dish and drink is worth half again as much
	constexpr RE::FormID PERK_DRAGONFEAST = 0x85E;      // Dragonfeast: Dragon Bone meals and Dragon Scales drinks
	constexpr RE::FormID PERK_COOKBOOK = 0x854;         // Gourmand: eating a dish you never cooked teaches its recipe
	constexpr const char* PLUGIN = "FreeCooking.esp";
	constexpr const char* MENU_FILE = "Data/SKSE/Plugins/FreeCooking/menu.tsv";
	constexpr const char* RULES_FILE = "Data/SKSE/Plugins/FreeCooking/data.json";

	// Category flags: an entry shows in every category whose flag it has (the list filters with flag & filterFlag).
	constexpr std::uint32_t FLAG_FOOD = 1u << 0;
	constexpr std::uint32_t FLAG_RECIPES = 1u << 1;
	constexpr std::uint32_t FLAG_ALL = 1u << 2;
	constexpr std::uint32_t FLAG_UNTASTED = 1u << 3;
	constexpr std::uint32_t FLAG_FLAVOUR0 = 1u << 4;  // + index in FLAVOURS
	constexpr std::uint32_t FLAG_FILLERS = 1u << 12;
	constexpr std::uint32_t FLAG_ENABLERS = 1u << 13;
	constexpr std::uint32_t FLAG_POTIONS = 1u << 14;
	constexpr std::uint32_t FLAG_MISC = 1u << 15;
	constexpr std::uint32_t FLAG_VANILLA = 1u << 16;  // the emergency fallback: the station's own vanilla crafting menu (an option)
	bool g_toVanilla{ false };  // the cooking screen is closing to show the vanilla crafting menu under it (UI thread)  // other mods' recipes at the pot and oven, made as their mod made them
	constexpr std::int32_t  POTION_VALUE = 5;  // a potion adds what a one-star seasoning would to a dish's value
	constexpr std::array<const char*, 8> FLAVOURS{ "Mighty", "Brainy", "Hearty", "Sneaky", "Earthy", "Spicy", "Chilly", "Zesty" };

	// the cooking screen's order (the cookbook prototype's three), for the recipe book and every ingredient list alike:
	// Q cycles it on any list, kept in the save. Dishes you can make now always come first.
	// (F only adds another copy of an ingredient already in the pot: one key, one job)
	constexpr std::array<const char*, 3> RECIPE_SORTS{ "Most used", "Recent", "A-Z" };
	std::size_t g_recipeSort{ 0 };
	// how often each dish was cooked and when last (a counter that goes up with every dish), kept in the save
	std::unordered_map<std::int32_t, std::uint32_t> g_dishUses;
	std::unordered_map<std::int32_t, std::uint32_t> g_dishLastCooked;
	std::uint32_t                                   g_cookClock{ 0 };
	// how often each ingredient went into a cooked dish and when last (the same counter), for the ingredient lists'
	// Most used and Recent; kept in the save ('INGU')
	std::unordered_map<RE::FormID, std::uint32_t> g_ingUses;
	std::unordered_map<RE::FormID, std::uint32_t> g_ingLastUsed;
	// the ingredients the cook now running uses, counted in g_ingUses only once the scripts say it cooked (a cook they
	// refuse, "The pot is still cooking.", counted too before; reviews 2026-11-16). UI thread only
	std::set<RE::FormID> g_cookPendingUses;
	// true while the scripts ask ChooseDish for the cook they are running (Hooks.inl PapyrusChooseDish): that cook's own
	// station counts, even if another station's screen is open by then (Codex review 2026-11-16, H3)
	thread_local bool t_scriptChoice = false;
	std::int32_t                                  g_platformShown{ -1 };  // the buttons the open screen shows: 0 keyboard, 1 controller (UI thread)
	std::int32_t                                  g_auxArt{ -1 };  // the bottom bar's third button: 1 Q Sort, 0 F Add another, -1 not set yet (a newly opened screen)

	std::vector<CookingMenu::Dish>                        g_dishes;
	std::unordered_map<RE::TESBoundObject*, std::int32_t> g_stars;  // seasoning stars per ingredient (menu.tsv)
	std::set<std::int32_t>                                g_known;  // discovered dish indices (from FC_Core)
	std::set<RE::FormID>                                  g_tasted;  // ingredients whose flavour you know (from FC_Core)
	std::atomic<bool>                                     g_overVanilla{ false };  // our screen covers a pot's vanilla crafting menu
	std::atomic<int>                                      g_place{ 0 };            // where the screen is open: 0 anywhere, 1 pot, 2 campfire (grill and roast), 3 oven (baking)
	// each cook sent gets a number; the scripts' answer is matched to it, so the answer to an earlier cook can't clear a
	// pot a new screen has just sent (Codex review 2026-11-16d, M8)
	std::atomic<std::uint32_t>                            g_cookRequest{ 0 };
	std::atomic<int>                                      g_cookPlace{ 0 };        // where the cook now running was asked for (the screen may close before the scripts choose the dish; reviews 2026-11-15); 0 when none runs
	// when it was asked for (steady clock, ms): a cook whose answer never came stops blocking new ones after the scripts'
	// own 15 seconds (Claude review 2026-11-16g, M3)
	std::atomic<std::int64_t>                             g_cookPlaceSince{ 0 };
	// the quest-locked dishes open when the cook was sent: the scripts choose the dish after the ingredients have left the
	// player, which can close a recipe that asks for one of them (Codex review 2026-11-16g, 5)
	std::mutex                                            g_cookOpenLock;
	std::unordered_set<std::int32_t>                      g_cookOpenDishes;
	// learned this session by eating, kept until FC_Core has surely stored them (sent again each time the screen opens)
	std::set<RE::FormID>                                  g_sessionTasted;
	std::set<std::int32_t>                                g_sessionLearned;
	std::unordered_map<RE::FormID, std::int32_t>          g_dishByBase;  // a vanilla dish's form -> its recipe index

	// ---- the pot rules: data.json as PapyrusUtil reads it ({"int":{key:n}, "string":{key:"text"}}, keys case-insensitive)
	std::unordered_map<std::string, std::int32_t> g_int;
	std::unordered_map<std::string, std::string>  g_str;
	std::unordered_set<std::string>               g_recipeKeys;  // ingredients that can be a dish's base

	std::string Lower(std::string a_text)
	{
		std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_text;
	}

	std::int32_t Int(const std::string& a_key, std::int32_t a_default = 0)
	{
		const auto it = g_int.find(Lower(a_key));
		return it != g_int.end() ? it->second : a_default;
	}
	bool HasInt(const std::string& a_key) { return g_int.contains(Lower(a_key)); }

	// a pool dish's look-alike as a patcher-made drink: 0 not one, 1 soft drink, 2 alcohol (Cooking/Patcher.inl dish.N.dr
	// and dish.N.al). Our own brews are told by their vanilla look-alikes (Cooking/Thirst.inl)
	int PatchedDrinkKind(RE::FormID a_base)
	{
		const auto it = g_dishByBase.find(a_base);
		if (it == g_dishByBase.end() || Int(std::format("dish.{}.dr", it->second)) != 1) {
			return 0;
		}
		return Int(std::format("dish.{}.al", it->second)) == 1 ? 2 : 1;
	}
	std::string Str(const std::string& a_key)
	{
		const auto it = g_str.find(Lower(a_key));
		return it != g_str.end() ? it->second : std::string();
	}

	// A small reader for that one flat file: objects, strings, whole numbers (anything else is skipped).
	class JsonReader
	{
	public:
		explicit JsonReader(std::string a_text) : _s(std::move(a_text)) {}

		bool ReadTop()
		{
			Ws();
			if (!Eat('{')) {
				return false;
			}
			while (true) {
				Ws();
				if (Eat('}')) {
					return true;
				}
				std::string section;
				if (!String(section) || (Ws(), !Eat(':'))) {
					return false;
				}
				Ws();
				if ((section == "int" || section == "string") && Peek() == '{') {
					if (!Section(section == "int")) {
						return false;
					}
				} else if (!Skip()) {
					return false;
				}
				Ws();
				Eat(',');
			}
		}

	private:
		bool Section(bool a_ints)
		{
			Eat('{');
			while (true) {
				Ws();
				if (Eat('}')) {
					return true;
				}
				std::string key;
				if (!String(key) || (Ws(), !Eat(':'))) {
					return false;
				}
				Ws();
				if (Peek() == '"') {
					std::string value;
					if (!String(value)) {
						return false;
					}
					if (!a_ints) {
						g_str[Lower(key)] = std::move(value);
					}
				} else if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek()))) {
					const auto start = _p;
					while (_p < _s.size() && (std::isdigit(static_cast<unsigned char>(_s[_p])) || _s[_p] == '-' || _s[_p] == '.' || _s[_p] == 'e' || _s[_p] == 'E' || _s[_p] == '+')) {
						++_p;
					}
					if (a_ints) {
						g_int[Lower(key)] = static_cast<std::int32_t>(std::strtoll(_s.c_str() + start, nullptr, 10));
					}
				} else if (!Skip()) {
					return false;
				}
				Ws();
				Eat(',');
			}
		}
		bool Skip()
		{
			Ws();
			const char c = Peek();
			if (c == '"') {
				std::string unused;
				return String(unused);
			}
			if (c == '{' || c == '[') {
				const char close = c == '{' ? '}' : ']';
				++_p;
				while (true) {
					Ws();
					if (Eat(close)) {
						return true;
					}
					if (c == '{') {
						std::string key;
						if (!String(key) || (Ws(), !Eat(':'))) {
							return false;
						}
					}
					if (!Skip()) {
						return false;
					}
					Ws();
					Eat(',');
				}
			}
			while (_p < _s.size() && _s[_p] != ',' && _s[_p] != '}' && _s[_p] != ']') {
				++_p;
			}
			return true;
		}
		bool String(std::string& a_out)
		{
			if (!Eat('"')) {
				return false;
			}
			while (_p < _s.size()) {
				const char c = _s[_p++];
				if (c == '"') {
					return true;
				}
				if (c != '\\') {
					a_out += c;
					continue;
				}
				if (_p >= _s.size()) {
					return false;
				}
				const char e = _s[_p++];
				switch (e) {
				case 'n': a_out += '\n'; break;
				case 't': a_out += '\t'; break;
				case 'r': a_out += '\r'; break;
				case 'b': a_out += '\b'; break;
				case 'f': a_out += '\f'; break;
				case 'u':
					{
						std::uint32_t cp = Hex4();
						if (cp >= 0xD800 && cp < 0xDC00 && _p + 1 < _s.size() && _s[_p] == '\\' && _s[_p + 1] == 'u') {
							_p += 2;
							const std::uint32_t lo = Hex4();
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
						Utf8(cp, a_out);
						break;
					}
				default: a_out += e; break;
				}
			}
			return false;
		}
		std::uint32_t Hex4()
		{
			std::uint32_t v = 0;
			for (int i = 0; i < 4 && _p < _s.size(); ++i, ++_p) {
				const char c = _s[_p];
				v = v * 16 + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
			}
			return v;
		}
		static void Utf8(std::uint32_t a_cp, std::string& a_out)
		{
			if (a_cp < 0x80) {
				a_out += static_cast<char>(a_cp);
			} else if (a_cp < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_cp >> 6));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else if (a_cp < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_cp >> 12));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_cp >> 18));
				a_out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			}
		}
		void Ws()
		{
			while (_p < _s.size() && std::isspace(static_cast<unsigned char>(_s[_p]))) {
				++_p;
			}
		}
		char Peek() const { return _p < _s.size() ? _s[_p] : '\0'; }
		bool Eat(char a_c)
		{
			if (Peek() == a_c) {
				++_p;
				return true;
			}
			return false;
		}

		std::string _s;
		std::size_t _p{ 0 };
	};

	bool LoadRules()
	{
		g_int.clear();
		g_str.clear();
		g_recipeKeys.clear();
		std::ifstream file(RULES_FILE, std::ios::binary);
		if (!file) {
			logger::error("Could not open {}: the cooking screen cannot preview dishes", RULES_FILE);
			return false;
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		JsonReader reader(buffer.str());
		if (!reader.ReadTop()) {
			logger::error("Could not read {} (not the expected format)", RULES_FILE);
		}
		for (const auto& [key, value] : g_int) {
			if (key.starts_with("rk.")) {
				g_recipeKeys.insert(key.substr(3));  // can be (part of) a dish's base (data.json, recipes.py)
			}
		}
		// the vanilla dish each recipe makes, so eating one can teach it (Cookbook Memory)
		g_dishByBase.clear();
		if (auto data = RE::TESDataHandler::GetSingleton()) {
			for (std::int32_t i = 0; g_str.contains(Lower(std::format("dish.{}.n", i))); ++i) {
				const auto plugin = Str(std::format("dish.{}.p", i));
				const auto id = static_cast<RE::FormID>(Int(std::format("dish.{}.id", i)));
				// removed dishes (named specialties like Spiced Wine) cannot be learned by eating them
				if (Int(std::format("dish.{}.off", i)) == 1) {
					continue;
				}
				if (auto form = plugin.empty() ? nullptr : data->LookupForm(id, plugin)) {
					g_dishByBase.emplace(form->GetFormID(), i);
				}
			}
		}
		logger::info("Pot rules: {} numbers, {} texts, {} base ingredients, {} dishes to recognise", g_int.size(), g_str.size(), g_recipeKeys.size(), g_dishByBase.size());
		return !g_int.empty();
	}

	// Plugins that share a name with an earlier one, extension aside (Foo.esp after Foo.esm): their items' keys keep the
	// extension ("foo.esp_123"), or both plugins' items would share rules. Every other plugin keeps the short key, so
	// vanilla keys (data.json) and keys already in saves don't change. Found once, after all plugins are loaded.
	const std::unordered_set<const RE::TESFile*>& ShadowedFiles()
	{
		static std::unordered_set<const RE::TESFile*> shadowed;
		static std::once_flag                         once;
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data || (data->compiledFileCollection.files.empty() && data->compiledFileCollection.smallFiles.empty())) {
			return shadowed;  // too early: no plugin list yet (asked again later)
		}
		std::call_once(once, [data]() {
			std::unordered_set<std::string> stems;
			auto add = [&](const RE::TESFile* a_file) {
				if (!a_file) return;
				std::string name(a_file->GetFilename());
				if (const auto dot = name.find('.'); dot != std::string::npos && dot > 0) name = name.substr(0, dot);
				if (!stems.insert(Lower(name)).second) {
					shadowed.insert(a_file);
					logger::warn("{} has the same name as an earlier plugin: its items' rule keys keep the extension", a_file->GetFilename());
				}
			};
			for (auto file : data->compiledFileCollection.files) add(file);
			for (auto file : data->compiledFileCollection.smallFiles) add(file);
		});
		return shadowed;
	}

	// The rules' key for an item: "<plugin without extension>_<local id in decimal>" (FC_Core.KeyOf asks for it through
	// FC_Menu.ItemKey)
	std::string PlainKeyOf(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		auto file = a_form->GetFile(0);
		std::string plugin = file ? std::string(file->GetFilename()) : std::string();
		if (const auto dot = plugin.find('.'); dot != std::string::npos && dot > 0 && !ShadowedFiles().contains(file)) {
			plugin = plugin.substr(0, dot);
		}
		const auto id = a_form->GetFormID();
		const auto local = (id >> 24) == 0xFE ? (id & 0xFFF) : (id & 0xFFFFFF);
		return Lower(plugin + "_" + std::to_string(local));
	}

	// The unholy, as Earthy defines them: one list for both readers, the holy strike (HolyDamage.inl HolyFoe, which also
	// counts the raised dead by their reanimate effect) and the potion rule below. Keywords ActorTypeUndead,
	// ActorTypeDaedra, Vampire, ActorTypeGhost; race WerewolfBeastRace.
	constexpr std::array<RE::FormID, 4> UNHOLY_KEYWORDS{ 0x13796, 0x13797, 0xA82BB, 0xD205E };
	constexpr RE::FormID                WEREWOLF_BEAST_RACE = 0xCDD84;

	// A yes/no condition asked as "is one": == 1, != 0, > 0 or >= 1 against a number, not a global. The two perk condition
	// readers below (the unholy target, the spell's school) both count only these.
	bool IsOneTest(const RE::CONDITION_ITEM_DATA& a_c)
	{
		using Op = RE::CONDITION_ITEM_DATA::OpCode;
		if (a_c.flags.global) {
			return false;
		}
		const float v = a_c.comparisonValue.f;
		const Op    op = a_c.flags.opCode;
		return (op == Op::kEqualTo && std::abs(v - 1.0f) < 0.001f) || (op == Op::kNotEqualTo && std::abs(v) < 0.001f) ||
		       (op == Op::kGreaterThan && std::abs(v) < 0.001f) || (op == Op::kGreaterThanOrEqualTo && std::abs(v - 1.0f) < 0.001f);
	}

	// A condition that is true only for an unholy target (Earthy's list above, or the IsUndead check), "is one" tests only.
	bool IsUnholyTargetTest(const RE::CONDITION_ITEM_DATA& a_c)
	{
		using Fn = RE::FUNCTION_DATA::FunctionID;
		if (!IsOneTest(a_c)) {
			return false;
		}
		const auto fn = a_c.functionData.function.get();
		if (fn == Fn::kIsUndead) {
			return true;
		}
		const auto* form = static_cast<const RE::TESForm*>(a_c.functionData.params[0]);
		if (!form) {
			return false;
		}
		if (fn == Fn::kHasKeyword) {
			return std::ranges::find(UNHOLY_KEYWORDS, form->GetFormID()) != UNHOLY_KEYWORDS.end();
		}
		if (fn == Fn::kGetIsRace) {
			return form->GetFormID() == WEREWOLF_BEAST_RACE;
		}
		return false;
	}

	// The school a spell condition names: EPMagic_SpellHasSkill asked as "is one". Its parameter is the skill's actor value
	// number, kept as a number in the parameter slot (not a form; other CommonLib plugins read it the same way, e.g.
	// SimpleTrainingOverhaul's VoidCaster<ActorValue>). Returns -1 when the condition names no school.
	std::int32_t SpellSchoolTest(const RE::CONDITION_ITEM_DATA& a_c)
	{
		if (a_c.functionData.function.get() != RE::FUNCTION_DATA::FunctionID::kEPMagic_SpellHasSkill || !IsOneTest(a_c)) {
			return -1;
		}
		return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(a_c.functionData.params[0]));
	}

	std::string ValueFlavour(std::int32_t a_av);  // the one flavour table for actor values (below EffectFlavour)

	// The element a damage keyword names (his call 2026-11-23: "make effects that add fire damage spicy, frost damage chilly,
	// and shock damage zesty"): vanilla's MagicDamageFire / MagicDamageFrost / MagicDamageShock. Empty: not one of them.
	std::string ElementOfKeyword(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		switch (a_form->GetFormID()) {
		case 0x1CEAD: return "Spicy";
		case 0x1CEAE: return "Chilly";
		case 0x1CEAF: return "Zesty";
		default:      return {};
		}
	}

	// What SpellElement returns for a spell whose harmful effects carry two different elements (Codex review 2026-11-24a
	// R3: "no element" and "two elements" were both empty, so a perk's other, single-element entry won the clash).
	constexpr const char* SPELL_MIXED = "mixed";

	// The element of a spell a perk casts on hits (Apply Combat Hit Spell and the like): the elements its harmful effects
	// carry, when they all agree. Empty: no element damage; SPELL_MIXED: two different elements.
	std::string SpellElement(const RE::SpellItem* a_spell)
	{
		if (!a_spell) {
			return {};
		}
		std::string found;
		for (const auto* effect : a_spell->effects) {
			const auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || !base->IsHostile()) {
				continue;
			}
			for (std::uint32_t k = 0; k < base->numKeywords; ++k) {
				const auto e = ElementOfKeyword(base->keywords ? base->keywords[k] : nullptr);
				if (e.empty()) {
					continue;
				}
				if (found.empty()) {
					found = e;
				} else if (found != e) {
					return SPELL_MIXED;
				}
			}
		}
		return found;
	}

	// What PerkDamageFlavour returns when a perk's elements clash and nothing else decides: no flavour for the effect.
	constexpr const char* PERK_CLASH = "-";

	// true when one of an entry's conditions is an unholy test asked of the target (see PerkDamageFlavour)
	bool HasUnholyTargetTest(const RE::BGSEntryPointPerkEntry* a_ep)
	{
		for (std::size_t tab = 0; tab < a_ep->conditions.size(); ++tab) {
			for (const auto* item = a_ep->conditions[tab].head; item; item = item->next) {
				const bool onTarget = tab >= 1 || item->data.object.get() == RE::CONDITIONITEMOBJECT::kCombatTarget;
				if (onTarget && IsUnholyTargetTest(item->data)) {
					return true;
				}
			}
		}
		return false;
	}

	// A damage effect done by a perk (his calls 2026-11-23: "make any effect that increases damage to unholy earthy", then
	// "we probably need a rule for non unholy potions", proposal approved): the effect's perk has an entry that raises
	// weapon damage (Calculate Weapon Damage, Mod Attack Damage, Mod Power Attack Damage: vanilla's silver and Dawnguard
	// bonuses use the first) or spell damage (Mod Spell Magnitude), multiply above 1 or add above 0.
	// Against the unholy (a condition that only the unholy pass, asked of the target: any tab after the perk owner's, or a
	// perk-owner condition run on the combat target; review 2026-11-23 L3/N1: vanilla's own perks never use "run on" in
	// perk tabs, and "Target" has no defined meaning there, so only the defined Combat Target counts): Earthy.
	// Against anyone else: Mighty for weapons; for spells the school decides as it does for a plain effect (his rule
	// 2026-11-23 "consistent design should dictate our actions", Claude review L1): a spell condition naming a school gives
	// that school's flavour from the value table (Alteration Hearty, the other four Brainy); no school, or schools whose
	// flavours differ, Brainy (review 2026-11-23 N3: the result never depends on condition order). A named skill with no
	// flavour (a non-magic skill) counts as naming no school (review 2026-11-23c C1/R1, his call "do the proposal").
	// Elements (his call 2026-11-23, "make effects that add fire damage spicy, frost damage chilly, and shock damage
	// zesty"): a spell-strength entry whose spell condition names a damage keyword (EPMagic_SpellHasKeyword
	// MagicDamageFire / Frost / Shock, as Augmented Flames does), and a perk that casts a spell on hits (Apply Combat Hit,
	// Bashing or Weapon Swing Spell) whose harmful effects carry one of those keywords, give that element: Spicy, Chilly,
	// Zesty. An element comes before a school or Mighty; the unholy still come first; two different elements in one perk
	// cancel out (the school or Mighty rule then decides).
	// A hit spell asked of an unholy target (any tab after the perk owner's, or a perk-owner condition run on the combat
	// target) gives Earthy like a damage entry does (Claude review 2026-11-24 Q6, his "yes": the unholy come first).
	// Two different elements with no damage entry to fall back on clash (Claude review 2026-11-24 Q5, his call "twin
	// elements make no sense to become mighty" then "yes" to the clash rule): the effect gets no flavour at all, like a pot
	// with two flavours, instead of being read by its value (PERK_CLASH above).
	// Empty: the perk raises no damage; PERK_CLASH: its elements clash. Only one-number entries are read (Claude review L2).
	// The perk decides before the effect's value: the game turns a value effect that names no value into Health (probe
	// 2026-11-23), which would read as Hearty.
	std::string PerkDamageFlavour(const RE::BGSPerk* a_perk)
	{
		if (!a_perk) {
			return {};
		}
		using EP = RE::BGSEntryPoint::ENTRY_POINTS;
		using Fn = RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION;
		std::string other;  // the flavour of the first damage entry that is not against the unholy
		std::string element;          // the element the damage entries name, when they agree
		bool        elementsDiffer = false;
		const auto  noteElement = [&](const std::string& a_e) {
			if (a_e.empty()) {
				return;
			}
			if (a_e == SPELL_MIXED) {
				elementsDiffer = true;  // one spell with two elements clashes like two entries with different elements
				return;
			}
			if (element.empty()) {
				element = a_e;
			} else if (element != a_e) {
				elementsDiffer = true;
			}
		};
		for (const auto* entry : a_perk->perkEntries) {
			if (!entry || entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
				continue;
			}
			const auto* ep = static_cast<const RE::BGSEntryPointPerkEntry*>(entry);
			const auto  point = ep->entryData.entryPoint.get();
			if (point == EP::kApplyCombatHitSpell || point == EP::kApplyBashingSpell || point == EP::kApplyWeaponSwingSpell) {
				// a spell cast on hits: its element, if it does element damage; against the unholy: Earthy
				if (ep->functionData && ep->functionData->GetType() == RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kSpellItem) {
					const auto e = SpellElement(static_cast<const RE::BGSEntryPointFunctionDataSpellItem*>(ep->functionData)->spell);
					if (!e.empty() && HasUnholyTargetTest(ep)) {
						return "Earthy";  // the unholy win over any other entry of the same perk
					}
					noteElement(e);
				}
				continue;
			}
			const bool weapon = point == EP::kCalculateWeaponDamage || point == EP::kModAttackDamage || point == EP::kModPowerAttackDamage;
			if (!weapon && point != EP::kModSpellMagnitude) {
				continue;
			}
			const auto fn = ep->entryData.function.get();
			if ((fn != Fn::kMultiplyValue && fn != Fn::kAddValue) || !ep->functionData ||
				ep->functionData->GetType() != RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
				continue;
			}
			const float value = static_cast<const RE::BGSEntryPointFunctionDataOneValue*>(ep->functionData)->data;
			if (fn == Fn::kMultiplyValue ? value <= 1.0f : value <= 0.0f) {
				continue;
			}
			std::string bySchool;        // the flavour every named school agrees on
			bool        schoolsDiffer = false;
			for (std::size_t tab = 0; tab < ep->conditions.size(); ++tab) {
				for (const auto* item = ep->conditions[tab].head; item; item = item->next) {
					const bool onTarget = tab >= 1 || item->data.object.get() == RE::CONDITIONITEMOBJECT::kCombatTarget;
					if (onTarget && IsUnholyTargetTest(item->data)) {
						return "Earthy";  // the unholy win over any other damage entry of the same perk
					}
					if (!weapon && item->data.functionData.function.get() == RE::FUNCTION_DATA::FunctionID::kEPMagic_SpellHasKeyword && IsOneTest(item->data)) {
						noteElement(ElementOfKeyword(static_cast<const RE::TESForm*>(item->data.functionData.params[0])));
					}
					if (const auto school = SpellSchoolTest(item->data); school >= 0) {
						const auto f = ValueFlavour(school);
						if (f.empty()) {
							// not a school with a flavour: ignored
						} else if (bySchool.empty()) {
							bySchool = f;
						} else if (f != bySchool) {
							schoolsDiffer = true;
						}
					}
				}
			}
			if (other.empty()) {
				if (weapon) {
					other = "Mighty";
				} else {
					other = bySchool.empty() || schoolsDiffer ? "Brainy" : bySchool;
				}
			}
		}
		if (!element.empty() && !elementsDiffer) {
			return element;  // never SPELL_MIXED: noteElement keeps it out of element
		}
		return elementsDiffer && other.empty() ? PERK_CLASH : other;
	}

	// What one potion effect does for the pot, judged by the value it changes, not by its form ID (2026-11-22, his rule
	// "d"): alchemy mods reuse vanilla effect IDs for other jobs (Apothecary's Fortify Heavy Armor became Fortify
	// Shouts; CACO's Fortify One-handed raises the skill itself), so an ID says nothing about what a potion does now.
	// The table is vanilla's own: the actor value of each of the 20 vanilla potion effects that had a flavour in
	// data.json ("pfx.<id>", read from Skyrim.esm 2026-11-22), the skill those values belong to (CACO and other mods
	// fortify the skill), plus his choices: armor rating, armor power, Alteration, Resist Magic, spell absorption, health (Fortify and
	// Restore) and Reflect Damage Hearty, stamina Mighty, magicka Brainy (his rule 2026-11-22: "effects that help tanking should be hearty"; the element resists keep
	// their flavours),
	// unarmed damage Mighty, Resist Disease Earthy.
	// Value Modifier and Peak Value Modifier effects change one value and are judged by it; of the other kinds only
	// Invisibility (Sneaky) and the two cures (Earthy, like the resists) have a flavour (his OK 2026-11-22 19:14).
	// Any kind: an effect whose perk raises weapon or spell damage is judged by that first (PerkDamageFlavour, 2026-11-23).
	std::string EffectFlavour(const RE::EffectSetting* a_effect)
	{
		using Arch = RE::EffectSetting::Archetype;
		const auto arch = a_effect->GetArchetype();
		if (auto byPerk = PerkDamageFlavour(a_effect->data.perk); !byPerk.empty()) {
			return byPerk == PERK_CLASH ? std::string{} : byPerk;  // clashing elements: no flavour, not the effect's value
		}
		if (arch == Arch::kInvisibility) {
			return "Sneaky";
		}
		if (arch == Arch::kCureDisease || arch == Arch::kCurePoison) {
			return "Earthy";
		}
		if (arch != Arch::kValueModifier && arch != Arch::kPeakValueModifier) {
			return {};
		}
		return ValueFlavour(static_cast<std::int32_t>(std::to_underlying(a_effect->data.primaryAV)));
	}

	// The flavour of one actor value: vanilla's table plus his choices (see EffectFlavour above); also read for the
	// school a spell-strength perk names (PerkDamageFlavour), so a perk and a plain effect of the same school agree.
	std::string ValueFlavour(std::int32_t a_av)
	{
		switch (a_av) {
		case 135:  // one-handed damage (vanilla Fortify One-handed)
		case 136:  // two-handed damage
		case 137:  // archery damage
		case 157:  // stamina regen
		case 6:    // the One-handed skill
		case 7:    // Two-handed
		case 8:    // Archery
		case 35:   // unarmed damage (his choice)
			return "Mighty";
		case 148:  // Conjuration
		case 149:  // Destruction
		case 150:  // Illusion
		case 151:  // Restoration
		case 156:  // magicka regen
		case 19:   // Conjuration
		case 20:   // Destruction
		case 21:   // Illusion
		case 22:   // Restoration
			return "Brainy";
		case 11:   // Heavy Armor
		case 12:   // Light Armor
		case 138:  // block power (vanilla Fortify Block)
		case 155:  // health regen
		case 9:    // the Block skill
		case 39:   // armor rating (his choice)
		case 140:  // heavy armor power (his choice 2026-11-22: "armor is hearty"; Claude review L4)
		case 141:  // light armor power
		case 147:  // Alteration power (vanilla Fortify Alteration; his choice 2026-11-22: Alteration is armor magic, Hearty)
		case 18:   // the Alteration skill
		case 44:   // Resist Magic (his choice 2026-11-22: "resist magic in general ... should also be hearty"; the element resists keep their flavours)
		case 83:   // spell absorption chance (his "atronach effects": the Atronach Stone's Absorb Spells)
		case 163:  // Reflect Damage (tanking)
			return "Hearty";
		case 24:   // Health, Fortify and Restore (his rule 2026-11-22: the flavours' second parts, the regens, say what these potions
		           // do: health Hearty, stamina Mighty, magicka Brainy)
			return "Hearty";
		case 26:   // Stamina, Fortify and Restore
			return "Mighty";
		case 25:   // Magicka, Fortify and Restore
			return "Brainy";
		case 144:  // sneak power (vanilla Fortify Sneak)
		case 142:  // pickpocket power
		case 15:   // the Sneak skill
		case 13:   // Pickpocket
		case 143:  // lockpicking power (vanilla Fortify Lockpicking; his "lockpicking is a thief skill", 2026-11-22)
		case 14:   // the Lockpicking skill
		case 92:   // movement noise (Muffle)
			return "Sneaky";
		case 43:   // Resist Frost
			return "Spicy";
		case 41:   // Resist Fire
			return "Chilly";
		case 42:   // Resist Shock
			return "Zesty";
		case 40:   // Resist Poison
		case 45:   // Resist Disease (his choice; Earthy meals give it too)
			return "Earthy";
		default:
			return {};
		}
	}

	// How a potion seasons the pot (2026-11-22, his rules "d" and "the name is more important, like we did with the
	// foods; the first good effect as a fall back; the player can change"):
	//  - any harmful effect: not a seasoning (poisons and foods never are);
	//  - good effects with no flavour (Waterbreathing, Fortify Smithing, Fortify Shouts) are ignored;
	//  - one flavour among the rest: that flavour (Restore Health is Hearty, Restore Stamina Mighty);
	//  - several flavours: a POTION word of one of them in the name (word file), else the first effect with a flavour;
	//  - a themed potion ("pov.<key>": Ice Wraith Essence is Chilly) follows its theme;
	//  - the player's choice on the Potions page (or a patch file's) wins over all of it.
	// Player-made potions (FF IDs: another potion in every save) are judged the same way, without a hand choice.
	FCPatch::Words g_potionWords;  // the patcher's word files, kept for the POTION words (set once at game start, read-only after)
	std::unordered_map<std::string, std::string> g_potionChoice;  // under g_cookLock: potion key -> "NONE" or a flavour

	struct PotionVerdict
	{
		std::string              flavour;   // "" = not a seasoning
		std::vector<std::string> flavours;  // the potion's own flavours, in effect order, each once
		bool                     harmful = false;
		std::string              why;       // one flavour / several: the name / several: the first effect / no flavour / harmful / theme / by hand
	};

	std::string PotionKey(const RE::AlchemyItem* a_alch)
	{
		auto file = a_alch ? a_alch->GetFile(0) : nullptr;
		return file ? FCPatch::ItemKey(file->GetFilename(), a_alch->GetLocalFormID()) : std::string();
	}

	// a_byHand: use the player's (or a patch file's) choice; the caller holds g_cookLock when it is true
	PotionVerdict JudgePotion(const RE::AlchemyItem* a_alch, bool a_byHand)
	{
		PotionVerdict v;
		for (auto effect : a_alch->effects) {
			auto base = effect ? effect->baseEffect : nullptr;
			if (!base) {
				continue;
			}
			// harmful: the game's flag, or an effect that lowers a value without it (a mod's built-in drawback, e.g. Fortify
			// Stamina -20): his pure rule (2026-11-22, Claude review L3: "we made the pure call to avoid situations like this")
			if (base->IsDetrimental() || effect->effectItem.magnitude < 0.0f) {
				v.harmful = true;
				continue;
			}
			if (const auto f = EffectFlavour(base); !f.empty() && std::ranges::find(v.flavours, f) == v.flavours.end()) {
				v.flavours.push_back(f);
			}
		}
		if (v.harmful) {
			v.why = "harmful";
			return v;  // never a seasoning, whatever the page says
		}
		if (a_byHand && !a_alch->IsDynamicForm()) {
			if (const auto it = g_potionChoice.find(PotionKey(a_alch)); it != g_potionChoice.end()) {
				v.flavour = it->second == "NONE" ? std::string() : it->second;
				v.why = "by hand";
				return v;
			}
		}
		if (const auto theme = Str("pov." + PlainKeyOf(a_alch)); !theme.empty()) {
			v.flavour = theme;
			v.why = "theme";
		} else if (v.flavours.empty()) {
			v.why = "no flavour";
		} else if (v.flavours.size() == 1) {
			v.flavour = v.flavours.front();
			v.why = "one flavour";
		} else if (const auto named = g_potionWords.PotionWord(std::string(a_alch->GetFullName()), v.flavours); !named.empty()) {
			v.flavour = named;
			v.why = "name";
		} else {
			v.flavour = v.flavours.front();
			v.why = "first effect";
		}
		return v;
	}

	std::unordered_map<RE::FormID, std::string> g_flavourCache;  // under g_cookLock; cleared on revert and when the page changes a potion

	// the hand choices in force: patch files' POTION lines with the player's on top (merged by the caller). Game start and
	// Import both come here, so an imported choice works at once (Claude review 2026-11-22, L1). Never called with
	// g_editsLock held (one lock order)
	void SetPotionChoices(const std::map<std::string, std::string>& a_merged)
	{
		CookLock lock(g_cookLock);
		g_potionChoice.clear();
		for (const auto& [key, value] : a_merged) {
			if (value != "AUTO") g_potionChoice[key] = value;  // AUTO: the player's "Automatic" over a patch file's choice
		}
		g_flavourCache.clear();
	}
	std::string PotionFlavour(const RE::TESForm* a_form)
	{
		CookLock lock(g_cookLock);
		auto& cache = g_flavourCache;
		auto alch = a_form ? a_form->As<RE::AlchemyItem>() : nullptr;
		if (!alch || alch->IsFood() || alch->IsPoison()) {
			return {};
		}
		if (const auto it = cache.find(alch->GetFormID()); it != cache.end()) {
			return it->second;
		}
		std::string flavour = JudgePotion(alch, true).flavour;
		if (!alch->IsDynamicForm()) {  // a player-made potion's ID (FF...) means another potion in another save
			cache.emplace(alch->GetFormID(), flavour);
		}
		return flavour;
	}

	// Pure potions share one rules entry per flavour: "potion_spicy" (one star; the Earthy one also neutralizes poison)
	std::string KeyOf(const RE::TESForm* a_form)
	{
		if (const auto flavour = PotionFlavour(a_form); !flavour.empty()) {
			return "potion_" + Lower(flavour);
		}
		return PlainKeyOf(a_form);
	}

	// What the rules say about one ingredient
	struct Ing
	{
		std::string key;
		std::string flavour;  // "Spicy" ... ; empty: not a seasoning
		std::string need;     // "alc", "garlic", "ring" or empty
		std::string filler;   // "veg", "staple" or empty
		std::int32_t stars{ 0 };
		bool alcohol{ false };
		bool garlic{ false };
		std::int32_t brew{ 0 };  // a drink ingredient: 1 sweetener, 2 grain, 3 fruit, 4 flower or herb, 5 drinking water
		std::int32_t dragon{ 0 };  // Dragonfeast: 1 Dragon Bone (meals), 2 Dragon Scales (drinks)
		bool base{ false };  // can be (part of) a dish's base

		bool main{ false };  // makes a dish on its own (meat, fish, a vegetable)
		bool potion{ false };  // a pure potion: a one-star seasoning like any other (several per pot since 2026-11-22), never needs tasting

		bool Known() const { return !flavour.empty() || !filler.empty() || alcohol || garlic || brew > 0 || dragon > 0 || base; }
		// What a dish is made of: anything that makes a dish on its own, a sweetener, grain or fruit (they brew
		// or bake), and a dish's other base ingredients (Crab Meat, Jug of Milk) except seasonings, herbs and garlic.
		// Meats and fish also season (Raw Beef is Hearty), so they show in Food and in their flavour.
		bool Food() const
		{
			if (alcohol || garlic || filler == "staple") {
				return false;
			}
			return main || (brew > 0 && brew < 4) || (base && flavour != "Spicy" && brew != 4);
		}
	};

	Ing IngOf(const RE::TESForm* a_item)
	{
		Ing ing;
		ing.key = KeyOf(a_item);
		ing.flavour = Str("ic." + ing.key);
		ing.need = Str("iq." + ing.key);
		ing.filler = Str("if." + ing.key);
		ing.stars = Int("is." + ing.key);
		ing.alcohol = Int("ia." + ing.key) == 1;
		ing.garlic = Int("ig." + ing.key) == 1;
		ing.brew = Int("ib." + ing.key);
		ing.dragon = Int("id." + ing.key);
		ing.base = g_recipeKeys.contains(ing.key);
		ing.main = HasInt("main." + ing.key);
		ing.potion = ing.key.starts_with("potion_");
		return ing;
	}

	bool Tasted(const RE::TESForm* a_item, const Ing& a_ing)
	{
		CookLock lock(g_cookLock);
		// everything the pot uses starts untasted: you learn what it does by eating one or cooking with it
		return !a_ing.Known() || a_ing.potion || a_ing.dragon > 0 || g_tasted.contains(a_item->GetFormID());  // (a dragon part has no flavour to learn)
	}

	// main thread or a frame-synced native only
	bool LiveHasTreePerk(RE::FormID a_id)
	{
		auto perk = PluginForm<RE::BGSPerk>(a_id, PLUGIN);
		auto player = Player();
		return perk && player && player->HasPerk(perk);
	}

	// any thread: off the main thread, the copy the main thread keeps (Cooking/PlayerView.inl)
	bool HasTreePerk(RE::FormID a_id)
	{
		return OnMainThread() ? LiveHasTreePerk(a_id) : ViewPerk(a_id);
	}

	// the Cooking skill (FreeCooking.esp 0x850) and its progress (0x851, 0-1): live on the main thread, else the copy
	float LiveCookingSkill()
	{
		auto level = PluginForm<RE::TESGlobal>(0x850, PLUGIN);
		return level ? level->value : 15.0f;
	}
	float LiveCookingRatio()
	{
		auto ratio = PluginForm<RE::TESGlobal>(0x851, PLUGIN);
		return ratio ? ratio->value : 0.0f;
	}
	float LiveTimescale()
	{
		auto calendar = RE::Calendar::GetSingleton();
		return calendar && calendar->GetTimescale() > 0.0f ? calendar->GetTimescale() : 20.0f;
	}
	float CookingSkill()
	{
		return OnMainThread() ? LiveCookingSkill() : ViewSkillAndTime().skill;
	}
	float CookingRatio()
	{
		return OnMainThread() ? LiveCookingRatio() : ViewSkillAndTime().skillRatio;
	}
	float Timescale()
	{
		return OnMainThread() ? LiveTimescale() : ViewSkillAndTime().timescale;
	}

	// (asked every frame by the item cards: PluginForm finds the plugin once, LookupForm searched the plugin list each
	// time; Claude review 2026-11-15, L9)
	RE::FormID RingFormID()
	{
		const auto plugin = Str("ring.p");
		auto ring = plugin.empty() ? nullptr : PluginForm(static_cast<RE::FormID>(Int("ring.id")), plugin);
		return ring ? ring->GetFormID() : 0;
	}

	// forbidden ingredients (need "ring") stay out of the cooking screen unless the Ring of Namira is worn; dragon parts
	// until the Dragonfeast perk
	bool Hidden(const RE::TESForm* a_item, bool a_ringWorn)
	{
		if (!a_item || !a_item->Is(RE::FormType::Ingredient, RE::FormType::AlchemyItem, RE::FormType::Misc)) {
			return false;
		}
		const auto ing = IngOf(a_item);
		return (!a_ringWorn && ing.need == "ring") || (ing.dragon > 0 && !HasTreePerk(PERK_DRAGONFEAST));
	}

	// main thread or a frame-synced native only
	bool LiveRingWorn()
	{
		// (runs ten times a second: PluginForm keeps the plugin's place instead of searching every plugin by name)
		auto ring = PluginForm(static_cast<RE::FormID>(Int("ring.id")), Str("ring.p"));
		auto player = Player();
		if (!ring || !player || !ring->IsBoundObject()) {
			return false;
		}
		auto inventory = player->GetInventory([ring](RE::TESBoundObject& a_object) { return &a_object == ring; });
		for (const auto& [object, entry] : inventory) {
			if (entry.second && entry.second->IsWorn()) {
				return true;
			}
		}
		return false;
	}

	// any thread: off the main thread, the copy the main thread keeps
	bool RingWorn()
	{
		return OnMainThread() ? LiveRingWorn() : ViewRingWorn();
	}

	// main thread or a frame-synced native: copies what the cooking screen and item cards read into the view.
	// a_counts: the whole inventory too (only needed while the cooking screen is open: GetInventoryCounts is not cheap)
	// which Misc recipes show: each recipe's own conditions, read on the main thread (only while a screen is open at a
	// pot or oven: the conditions are the mod's, so their cost is too)
	std::vector<bool> LiveMiscShown()
	{
		std::vector<bool> shown(g_misc.size(), false);
		auto              player = RE::PlayerCharacter::GetSingleton();
		const int         place = g_place.load();
		if (!player || (place != 1 && place != 3)) {
			return shown;
		}
		for (std::size_t i = 0; i < g_misc.size(); ++i) {
			const auto* recipe = g_misc[i].recipe;
			shown[i] = g_misc[i].place == place && (!recipe->conditions.head || recipe->conditions.IsTrue(player, player));
		}
		return shown;
	}

	// our dishes made from a mod's quest-locked recipe: open once that recipe's conditions pass for the player (main thread)
	std::unordered_set<std::int32_t> LiveOpenDishes()
	{
		std::unordered_set<std::int32_t> open;
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return open;
		}
		// any one of its mod's recipes for it opens it (two quests can each unlock one; Codex review 2026-11-16g, 2)
		for (const auto& [dish, recipes] : g_lockedDishes) {
			if (std::ranges::any_of(recipes, [&](const RE::BGSConstructibleObject* a_recipe) {
					return a_recipe && (!a_recipe->conditions.head || a_recipe->conditions.IsTrue(player, player));
				})) {
				open.insert(dish);
			}
		}
		return open;
	}

	bool ViewDishOpen(std::int32_t a_dish)
	{
		std::scoped_lock lock(g_viewLock);
		return g_view.openDishes.contains(a_dish);
	}

	// a dish made from a mod's quest-locked recipe whose quest hasn't unlocked it yet (none of its unlock recipes passes);
	// for the running cook, as it was when sent
	bool DishLocked(std::int32_t a_dish)
	{
		if (!g_lockedDishes.contains(a_dish)) {
			return false;
		}
		if (t_scriptChoice && g_cookPlace) {
			std::scoped_lock lock(g_cookOpenLock);
			return !g_cookOpenDishes.contains(a_dish);
		}
		return !ViewDishOpen(a_dish);
	}

	std::int64_t SteadyMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void CaptureView(bool a_counts)
	{
		PlayerView view;
		view.miscShown = LiveMiscShown();
		view.openDishes = LiveOpenDishes();
		for (std::size_t i = 0; i < VIEW_PERKS.size(); ++i) {
			view.perks[i] = LiveHasTreePerk(VIEW_PERKS[i]);
		}
		view.ringWorn = LiveRingWorn();
		view.fortifyCooking = LiveFortifyCookingPercent();
		view.skill = LiveCookingSkill();
		view.skillRatio = LiveCookingRatio();
		view.timescale = LiveTimescale();
		if (a_counts) {
			view.counts = UsableCounts();
		}
		std::scoped_lock lock(g_viewLock);
		if (!a_counts) {
			view.counts = std::move(g_view.counts);  // keep the last inventory copy
		}
		g_view = std::move(view);
	}

	// "5 minutes": rounded like the dish card's "Lasts 5 minutes." (FC_Core: (dur + 30) / 60), so the two agree
	std::string TimeText(std::int32_t a_seconds)
	{
		const auto minutes = (a_seconds + 30) / 60;
		return FC::TrF(minutes == 1 ? "{0} minute" : "{0} minutes", minutes);
	}

	// ---- FC_Core.Recalc, for the preview: which dish the pot makes, and how good it will be.
	// The scripts still cook (and stay the authority); keep the two in step.
	struct Result
	{
		std::int32_t      dish{ -1 };
		std::string       baseName;     // "Venison Stew"
		std::string       flavour;      // the dish's flavour, empty when plain
		std::int32_t      level{ 0 };
		std::int32_t      dur{ 0 };
		std::int32_t      total{ 0 };   // stars
		bool              drink{ false };
		bool              clash{ false };
		bool              blocked{ false };  // a seasoning that needs a drink, garlic or the ring could not be used
		std::vector<std::pair<std::size_t, std::string>> blockedBy;  // pot entry and what it needs ("alc", "garlic", "ring")
		std::vector<bool> used;              // per pot entry
		std::vector<bool> base;              // per pot entry: what makes the dish (its flavour does not matter)
		std::vector<bool> enabler;           // per pot entry: the drink or the cure doing its job
		std::vector<bool> side;              // per pot entry: the vegetable or staple that makes a meal last longer
		std::int32_t      cure{ -1 };        // the pot entry working as the cure (Garlic for poisonous plants)
		std::int32_t      spice{ -1 };       // a spiced dish's spice: part of the dish and still its seasoning
		std::int32_t      gold{ 0 };         // value of what gets used
		bool              dragon{ false };   // a Dragonfeast dish (level 4, half a day)
		std::int32_t      dragonPart{ -1 };  // the pot entry of its dragon part
		std::int32_t      dragonIdle{ -1 };  // a fitting dragon part the pot could not use (see dragonWhy)
		std::string       dragonWhy;         // why: "seasoning", "clash" or "full"
		std::int32_t      wrongPart{ 0 };    // a dragon part that does not fit this dish: 1 Bone (meals only), 2 Scales (drinks only)
	};

	// the value the cooked dish will have, worked out as FC_Core.Cook does: the ingredients plus 10%, half again for a
	// House Recipe perk (every dish and drink since 2026-11-23; before, only a perfect dish), then rounded to a coarse step so
	// similar pots share one saved dish. A critical cook does not change the value.
	std::int32_t DishValue(const Result& a_r)
	{
		std::int32_t value = (a_r.gold * 11 + 9) / 10;
		if (HasTreePerk(PERK_HOUSE_RECIPE)) {
			value = (value * 3 + 1) / 2;
		}
		const std::int32_t step = value >= 200 ? 25 : value >= 50 ? 10 : value >= 20 ? 5 : 1;
		return ((value + step / 2) / step) * step;
	}

	// half a day of game time, in real seconds (as FC_Core.DragonDuration)
	std::int32_t DragonDuration()
	{
		return static_cast<std::int32_t>(12.0f * 3600.0f / Timescale());
	}

	// which dishes this cooking place can make (dish index): a campfire grills and roasts, an oven bakes, a pot
	// makes the rest; the screen opened elsewhere (the test power) allows everything
	bool AllowedHere(std::int32_t a_idx)
	{
		const bool baked = Int(std::format("dish.{}.ov", a_idx)) == 1;
		// the screen closed right after Cook, or another station's screen opened: the cook's own place still counts
		const int place = t_scriptChoice && g_cookPlace ? g_cookPlace.load() : g_place ? g_place.load() : g_cookPlace.load();
		switch (place) {
		case 1:
			return !baked;
		case 2:
			return Int(std::format("dish.{}.spit", a_idx)) == 1;
		case 3:
			return baked;
		default:
			return true;
		}
	}

	// ---- Which dish? Three-tier recipes (sheet v122, decided 2026-11-11)
	// A recipe is up to three slots: a named item ("Venison", "an apple") or a category ("any dairy"). Order in the pot
	// does not matter. Of the dishes whose slots the pot fills, food comes before the brews (made only when no dish
	// matches); then the dish using the most ingredients wins, then the one with more named slots (Horker and Ash Yam
	// Stew over Horker Stew; Vegetable Soup, three vegetables and no names, gives way to a named dish of its size),
	// then the one with more of its vanilla recipe in the pot, and after that the dish listed first in the sheet. When several pot items fit a slot, the one that gives the least
	// fills it (no flavour first, then the fewest stars), so the rest season the dish. FC_Core.Recalc asks the same
	// function (FC_Menu.ChooseDish), so the preview and the cooking always agree.
	struct Choice
	{
		std::int32_t              dish{ -1 };
		std::vector<std::int32_t> entries;           // the pot entries filling the recipe's slots, in slot order
		bool                      needAlc{ false };     // a recipe ingredient is unappetizing: it needs an alcoholic drink
		bool                      needGarlic{ false };  // a recipe ingredient is poisonous: it needs Garlic (or an Earthy potion)
	};

	bool InKeySet(const std::string& a_set, const std::string& a_key)
	{
		return !a_key.empty() && a_set.find("|" + a_key + "|") != std::string::npos;
	}

	// general dishes (Meat Stew, Fish Pie...: "dish.N.gw" holds the general word): the cooked dish is named after what
	// filled its first slot ("Goat Stew", "Catfish Pie"). The last choice's main ingredient per dish, for the name
	std::mutex                                     g_mainLock;
	// dish -> short name of its main ingredient ("Catfish"), kept apart for the screen's preview and for the cook the
	// scripts are making: the cursor resting on another recipe while a cook ran renamed the cooked dish ("Beef Stew"
	// from goat; reviews 2026-11-15)
	std::unordered_map<std::int32_t, std::string> g_lastMain;  // the screen's preview
	std::unordered_map<std::int32_t, std::string> g_cookMain;  // the cook (FC_Menu.ChooseDish, then FC_Menu.DishName)
	// which look the dish takes, per dish: a mod's model ("lk.<dish>.<i>", Cooking/Patcher.inl) or -1 for the dish's own,
	// for the cook (DishPool.inl PapyrusMakeDish) and for the screen's preview (its turning 3D picture)
	std::unordered_map<std::int32_t, std::int32_t> g_cookLook;
	std::unordered_map<std::int32_t, std::int32_t> g_lastLook;
	// drinks named after what is in them ("dish.N.dk"; DrinkName below): the whole unseasoned name, per dish, kept apart
	// for the preview and the cook the same way (under g_mainLock)
	std::unordered_map<std::int32_t, std::string> g_lastDrink;
	std::unordered_map<std::int32_t, std::string> g_cookDrink;
	// The recipe the player last picked from the Recipes list (-1: none): when several dishes' recipes fit the pot equally
	// (an author's different models with the same ingredients), the pot makes this one (his call 2026-11-24). Set by the
	// cooking screen, cleared when it opens.
	std::atomic<std::int32_t> g_preferDish{ -1 };

	// The look of a dish our rules make (his rules 2026-11-24): the recipe is always ours; the model can be a food mod's.
	// A mod dish of the same kind as ours, with its main ingredient in our dish's first slot ("lk.<dish>.<i>": plugin
	// "p", form "id", its main ingredient's keys "m", its mod's recipe "v"; Cooking/Patcher.inl), is a look of that dish.
	// It shows when the pot's main ingredient (a_main, the first slot's item) is its main ingredient, even when the pot is
	// our dish's own recipe ("if someone downloads a mod and make a boar dish they will expect see the dish of the
	// downloaded mod"); any other main ingredient keeps the dish's own look. Several mods' looks for one ingredient: the
	// one made for the fewest main ingredients first (a carrot soup's model beats an "any vegetable" soup's on a carrot
	// soup, as a named slot beats an "any" slot), then the one whose original recipe the pot matches best, then the one
	// read first. -1: the dish's own look
	std::int32_t ChooseLook(std::int32_t a_dish, const std::vector<std::string>& a_keys, const std::string& a_main)
	{
		const auto count = Int(std::format("lk.{}.n", a_dish));
		if (count <= 0 || a_main.empty()) return -1;
		const auto overlap = [&](const std::string& a_set) {
			std::set<std::string> seen;
			for (const auto& k : a_keys) {
				if (InKeySet(a_set, k)) seen.insert(k);
			}
			return static_cast<std::int32_t>(seen.size());
		};
		std::int32_t best = -1;
		std::pair<std::int32_t, std::int32_t> bestScore{};
		for (std::int32_t i = 0; i < count; ++i) {
			const auto mains = Str(std::format("lk.{}.{}.m", a_dish, i));
			if (!InKeySet(mains, a_main)) continue;
			std::int32_t made = 0;  // how many main ingredients its model was made for
			for (std::size_t c = 1; c < mains.size(); ++c) made += (mains[c] == '|' && mains[c - 1] != '|') ? 1 : 0;  // "|a|b|": 2
			const std::pair<std::int32_t, std::int32_t> score{ -made, overlap(Str(std::format("lk.{}.{}.v", a_dish, i))) };
			if (best < 0 || score > bestScore) {
				best = i;
				bestScore = score;
			}
		}
		return best;
	}

	// the form a dish looks like: the look ChooseLook picked for the cook (a_cook) or the screen's preview; else the dish's
	// own (nullptr with a_modOnly)
	RE::TESForm* DishLookForm(std::int32_t a_dish, bool a_cook, bool a_modOnly = false)
	{
		std::int32_t look = -1;
		{
			std::scoped_lock lock(g_mainLock);
			const auto& looks = a_cook ? g_cookLook : g_lastLook;
			if (const auto it = looks.find(a_dish); it != looks.end()) look = it->second;
		}
		if (look >= 0) {
			const auto plugin = Str(std::format("lk.{}.{}.p", a_dish, look));
			if (auto form = plugin.empty() ? nullptr : PluginForm(static_cast<RE::FormID>(Int(std::format("lk.{}.{}.id", a_dish, look))), plugin)) return form;
		}
		if (a_modOnly) return nullptr;
		const auto plugin = Str(std::format("dish.{}.p", a_dish));
		return plugin.empty() ? nullptr : PluginForm(static_cast<RE::FormID>(Int(std::format("dish.{}.id", a_dish))), plugin);
	}

	// a dish's name as cooked: the general word replaced by the main ingredient, and the flavour in front when given
	// ("Mighty Catfish Pie"); every other dish as the rules have it ("dishc.N.<flavour>" when seasoned)
	std::string DishName(std::int32_t a_dish, const std::string& a_flavour, bool a_cook = false)
	{
		// Every name here is a line in the translation file (his call 2026-11-14: translators get free rein over dish
		// names). A seasoned name ("Mighty Flame Angelfish") is used whole when the translator wrote a line for it;
		// otherwise it is put together from the translated flavour and the translated name without the flavour
		// ("{0} {1}"), so a translation never shows half English.
		std::string flavour = a_flavour;
		if (!flavour.empty()) flavour[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(flavour[0])));
		if (Int(std::format("dish.{}.dk", a_dish)) != 0) {
			// a drink named after what is in it ("Juniper Mead", "Shein"): the name the last choice put together, already
			// translated; without one (a recipe card with no pot behind it) the recipe's own name
			std::string name;
			{
				std::scoped_lock lock(g_mainLock);
				const auto& drinks = a_cook ? g_cookDrink : g_lastDrink;
				if (const auto it = drinks.find(a_dish); it != drinks.end()) name = it->second;
			}
			if (name.empty()) name = FC::Tr(Str(std::format("dish.{}.n", a_dish)).c_str());
			if (flavour.empty()) return name;
			return FC::TrF("{0} {1}", FC::Tr(flavour.c_str()), name);
		}
		const auto general = Str(std::format("dish.{}.gw", a_dish));
		if (general.empty()) {
			if (flavour.empty()) return FC::Tr(Str(std::format("dish.{}.n", a_dish)).c_str());
			const auto seasoned = Str(std::format("dishc.{}.{}", a_dish, Lower(a_flavour)));
			if (FC::HasTr(seasoned) || !seasoned.starts_with(flavour + " ")) return FC::Tr(seasoned.c_str());
			return FC::TrF("{0} {1}", FC::Tr(flavour.c_str()), FC::Tr(seasoned.substr(flavour.size() + 1).c_str()));
		}
		// a dish named after its main ingredient: the general word ("Meat") in the translated name is replaced by the
		// ingredient's own name, which the game (or its translation) gives
		std::string name = FC::Tr(Str(std::format("dish.{}.n", a_dish)).c_str());
		{
			const std::string word = FC::Tr(general.c_str());
			std::scoped_lock lock(g_mainLock);
			const auto& mains = a_cook ? g_cookMain : g_lastMain;
			if (const auto it = mains.find(a_dish); it != mains.end() && !it->second.empty()) {
				if (const auto at = name.find(word); at != std::string::npos) name.replace(at, word.size(), it->second);
			}
		}
		if (flavour.empty()) return name;
		return FC::TrF("{0} {1}", FC::Tr(flavour.c_str()), name);  // "Spicy Venison Stew", in the player's language's order
	}

	// An ingredient's short name in a drink's name: our own list first ("Juniper Berries" -> "Juniper", both apples ->
	// "Apple"; a translation line each), then the patcher's short form of the game's name (food mods' ingredients), then
	// nothing (the drink keeps its plain name)
	std::string DrinkShort(const std::string& a_key)
	{
		if (const auto own = Str("dsn." + a_key); !own.empty()) return FC::Tr(own.c_str());
		return Str("ish." + a_key);
	}

	// A drink's name from the items that filled its recipe, in slot order (his calls 2026-11-24, after the drink-naming
	// research: the base decides the drink, "[ingredient] [drink]" as the lore and real drinks are named).
	// dk 1, ale, mead, tea and juice: "<addition> <base>": "Juniper Mead", "Snowberry Ale", "Lavender Tea"; the base word is
	// the recipe's ("db") unless the item in slot "dbs" has its own (Saltrice: "Matze").
	// dk 2, wine: named by its fruits, never by their order: a fruit there twice gives the wine (Jazbay "Wine", Comberry
	// "Shein", apples "Cider", others "Snowberry Wine"); two different fruits "Fruit Wine", or "Berry Wine" when both are
	// berries (US labelling, 27 CFR 4.21); the one ingredient that isn't the base fruit goes in front ("Lavender Shein").
	// Every pattern is a line of its own in the translation file ("{0} Mead", "{0} Shein"...), so translators can order it.
	std::string DrinkName(std::int32_t a_dish, const std::vector<std::string>& a_slotKeys)
	{
		const auto kind = Int(std::format("dish.{}.dk", a_dish));
		const auto count = static_cast<std::int32_t>(a_slotKeys.size());
		if (kind == 1) {
			std::string base = Str(std::format("dish.{}.db", a_dish));
			const auto dbs = Int(std::format("dish.{}.dbs", a_dish));
			if (dbs >= 0 && dbs < count) {
				if (const auto own = Str("dbn." + a_slotKeys[dbs]); !own.empty()) base = own;
			}
			const auto da = Int(std::format("dish.{}.da", a_dish));
			const std::string add = da >= 0 && da < count ? DrinkShort(a_slotKeys[da]) : std::string();
			if (base.empty()) return {};
			if (add.empty()) return FC::Tr(base.c_str());
			const std::string pattern = "{0} " + base;
			return FC::TrF(pattern.c_str(), add);
		}
		if (kind != 2 || count == 0) return {};
		// the fruits are the items of the first slot's kind; each counted by its short name (both apples are one fruit)
		const auto fruitSet = Str(std::format("dish.{}.s0", a_dish));
		std::vector<std::pair<std::string, std::string>> fruits;  // (type, key)
		std::string other;                                        // the key of the one ingredient that isn't a fruit
		for (const auto& k : a_slotKeys) {
			if (InKeySet(fruitSet, k)) {
				const auto s = DrinkShort(k);
				fruits.emplace_back(s.empty() ? k : s, k);
			} else {
				other = k;
			}
		}
		std::string baseKey;
		for (std::size_t i = 0; i < fruits.size() && baseKey.empty(); ++i) {
			for (std::size_t j = i + 1; j < fruits.size(); ++j) {
				if (fruits[i].first == fruits[j].first) {
					baseKey = fruits[i].second;
					break;
				}
			}
		}
		std::string prefix;  // the key named in front
		std::string base;    // English pattern of the wine's own name, {0} its fruit
		std::string fruit;
		if (!baseKey.empty()) {
			const auto baseType = DrinkShort(baseKey).empty() ? baseKey : DrinkShort(baseKey);
			for (const auto& [type, key] : fruits) {
				if (type != baseType) prefix = key;
			}
			if (prefix.empty()) prefix = other;
			if (const auto own = Str("wn." + baseKey); !own.empty()) {
				base = own;
			} else if (fruit = DrinkShort(baseKey); !fruit.empty()) {
				base = "{0} Wine";
			} else {
				base = "Fruit Wine";
			}
		} else {
			const bool berries = !fruits.empty() && std::ranges::all_of(fruits, [](const auto& a_f) { return Int("ber." + a_f.second) == 1; });
			base = berries ? "Berry Wine" : "Fruit Wine";
			prefix = other;  // three different fruits: no prefix
		}
		const std::string add = prefix.empty() ? std::string() : DrinkShort(prefix);
		if (base == "{0} Wine") {
			return add.empty() ? FC::TrF("{0} Wine", fruit) : FC::TrF("{0} {1} Wine", add, fruit);
		}
		if (add.empty()) return FC::Tr(base.c_str());
		const std::string pattern = "{0} " + base;
		return FC::TrF(pattern.c_str(), add);
	}

	// the patcher's coverage check: can any dish allowed at this place be filled from a_keys, using entry a_mustUse and at
	// least a_minSlots slots? (unlike ChooseDish, not only the best one: a named bread may outrank Fish Pie in a test pot)
	bool CanCookWith(const std::vector<std::string>& a_keys, std::int32_t a_mustUse, std::int32_t a_minSlots)
	{
		const auto cnt = static_cast<std::int32_t>(a_keys.size());
		for (std::int32_t d = 0; g_str.contains(Lower(std::format("dish.{}.n", d))); ++d) {
			const auto ns = Int(std::format("dish.{}.ns", d));
			if (ns < (std::max)(1, a_minSlots) || ns > 3 || Int(std::format("dish.{}.off", d)) == 1 || !AllowedHere(d)) continue;
			std::array<std::string, 3> slot;
			for (std::int32_t j = 0; j < ns; ++j) slot[j] = Str(std::format("dish.{}.s{}", d, j));
			std::array<std::int32_t, 3> pick{ -1, -1, -1 };
			const std::function<bool(std::int32_t)> fill = [&](std::int32_t a_j) -> bool {
				if (a_j == ns) return std::ranges::find(pick, a_mustUse) != pick.end();
				for (std::int32_t e = 0; e < cnt; ++e) {
					if (std::ranges::find(pick, e) != pick.end() || !InKeySet(slot[a_j], a_keys[e])) continue;
					pick[a_j] = e;
					if (fill(a_j + 1)) return true;
					pick[a_j] = -1;
				}
				return false;
			};
			if (fill(0)) return true;
		}
		return false;
	}

	// a_only >= 0: only that dish is tried (the cooking screen's recipe shortcut asks "can these make this dish?")
	// a_quiet: a check only (the recipe shortcut, the card): the preview's main ingredient (g_lastMain) is left as it is
	// (Claude review 2026-11-16g, L6)
	// a_prefer: the dish that wins a tie (-2: the player's pick, g_preferDish)
	Choice ChooseDish(const std::vector<std::string>& a_keys, bool a_longSimmer, bool a_cook = false, std::int32_t a_only = -1, bool a_quiet = false, std::int32_t a_prefer = -2)
	{
		const std::int32_t prefer = a_prefer == -2 ? g_preferDish.load() : a_prefer;
		Choice best;
		std::array<std::int32_t, 4> bestScore{};
		const auto cnt = a_keys.size();
		// what an entry gives up when it fills a slot: a flavour is worth more as a seasoning, then its stars
		std::vector<std::int32_t> cost(cnt, 0);
		std::vector<bool> gross(cnt, false);      // needs an alcoholic drink (unappetizing)
		std::vector<bool> poisonous(cnt, false);  // needs Garlic or an Earthy potion
		std::vector<bool> alcohol(cnt, false);
		std::vector<bool> garlicHelp(cnt, false);
		for (std::size_t i = 0; i < cnt; ++i) {
			const auto& k = a_keys[i];
			cost[i] = (Str("ic." + k).empty() ? 0 : 100) + Int("is." + k) * 10 + (Int("ia." + k) == 1 ? 5 : 0);
			gross[i] = Str("iq." + k) == "alc";
			poisonous[i] = Str("iq." + k) == "garlic";
			alcohol[i] = Int("ia." + k) == 1;
			garlicHelp[i] = Int("ig." + k) == 1;
		}
		// (a_only: that dish alone, not a walk past every other one; the recipe shortcut asks often; review 2026-11-16f, L3)
		for (std::int32_t d = (std::max)(a_only, 0); (a_only < 0 || d == a_only) && g_str.contains(Lower(std::format("dish.{}.n", d))); ++d) {
			const auto ns = Int(std::format("dish.{}.ns", d));
			if (ns <= 0 || ns > 3 || Int(std::format("dish.{}.off", d)) == 1 || !AllowedHere(d)) {
				continue;
			}
			if (DishLocked(d)) {
				continue;  // its mod's quest hasn't unlocked it yet (his rule 2026-11-16)
			}
			if (const auto stop = Str(std::format("dish.{}.x", d)); !stop.empty() &&
				std::ranges::any_of(a_keys, [&](const std::string& a_k) { return InKeySet(stop, a_k); })) {
				continue;  // Herbal Tea with a sweetener, grain or fruit; Fruit Juice with a sweetener; mead with fruit
			}
			std::array<std::string, 3> slot;
			for (std::int32_t j = 0; j < ns; ++j) {
				slot[j] = Str(std::format("dish.{}.s{}", d, j));
			}
			// A recipe ingredient keeps the helper it needs as a seasoning (the design's "a gross recipe ingredient needs
			// its enabler", his call 2026-11-24 "a dish made only of poison or gross should need an enabler" and "every
			// slot"): unappetizing -> an alcoholic drink, poisonous -> Garlic or an Earthy potion, each a pot entry of its
			// own. Long Simmer removes both; mead, ale and wine are alcoholic themselves (no drink); a dish marked "nh" needs
			// none (Charred Skeever Meat, his exception). Until 2026-11-24 only egg slots ("gr") asked for the drink.
			const bool helperFree = a_longSimmer || Int(std::format("dish.{}.nh", d)) == 1;
			const bool brewed = Int(std::format("dish.{}.fe", d)) == 1;
			const bool different = Int(std::format("dish.{}.dif", d)) == 1;  // Vegetable Soup, Wine: different items
			// every way to give each slot its own entry (at most three slots): keep the cheapest
			std::array<std::int32_t, 3> pick{ -1, -1, -1 };
			std::array<std::int32_t, 3> found{ -1, -1, -1 };
			std::int32_t foundCost = (std::numeric_limits<std::int32_t>::max)();
			bool foundGross = false;
			bool foundPoison = false;
			const std::function<void(std::int32_t, std::int32_t)> fill = [&](std::int32_t a_j, std::int32_t a_cost) {
				if (a_j == ns) {
					bool grossUsed = false;
					bool poisonUsed = false;
					for (std::int32_t j = 0; j < ns && !helperFree; ++j) {
						grossUsed = grossUsed || (gross[pick[j]] && !brewed);
						poisonUsed = poisonUsed || poisonous[pick[j]];
					}
					// the helpers are pot entries outside the recipe, one each (a drink and Garlic: two entries)
					const auto outside = [&](const std::vector<bool>& a_kind, std::int32_t a_not) {
						for (std::size_t e = 0; e < cnt; ++e) {
							const auto ei = static_cast<std::int32_t>(e);
							if (a_kind[e] && ei != a_not && std::ranges::find(pick, ei) == pick.end()) {
								return ei;
							}
						}
						return -1;
					};
					const auto drinkAt = grossUsed ? outside(alcohol, -1) : -1;
					if (grossUsed && drinkAt < 0) {
						return;
					}
					if (poisonUsed && outside(garlicHelp, drinkAt) < 0) {
						return;
					}
					if (a_cost < foundCost) {
						foundCost = a_cost;
						found = pick;
						foundGross = grossUsed;
						foundPoison = poisonUsed;
					}
					return;
				}
				for (std::size_t e = 0; e < cnt; ++e) {
					const auto ei = static_cast<std::int32_t>(e);
					bool taken = false;
					for (std::int32_t j = 0; j < a_j; ++j) {
						taken = taken || pick[j] == ei || (different && a_keys[pick[j]] == a_keys[e]);
					}
					if (taken || !InKeySet(slot[a_j], a_keys[e])) {
						continue;
					}
					pick[a_j] = ei;
					fill(a_j + 1, a_cost + cost[e]);
					pick[a_j] = -1;
				}
			};
			fill(0, 0);
			if (found[0] < 0) {
				continue;
			}
			std::int32_t named = 0;
			for (std::int32_t j = 0; j < ns; ++j) {
				named += Int(std::format("dish.{}.nm{}", d, j));
			}
			std::int32_t vanilla = 0;
			if (const auto van = Str(std::format("dish.{}.v", d)); !van.empty()) {
				std::set<std::string> counted;
				for (const auto& k : a_keys) {
					if (InKeySet(van, k) && counted.insert(k).second) {
						++vanilla;
					}
				}
			}
			const std::array<std::int32_t, 4> score{ -Int(std::format("dish.{}.pr", d)), ns, named, vanilla };
			if (best.dish < 0 || score > bestScore || (score == bestScore && d == prefer)) {  // equal: the dish listed first stays, unless the player picked this one
				best.dish = d;
				best.entries.assign(found.begin(), found.begin() + ns);
				best.needAlc = foundGross;
				best.needGarlic = foundPoison;
				bestScore = score;
			}
		}
		if (a_only < 0 && !a_quiet && best.dish >= 0) {
			const auto main = best.entries.empty() || best.entries.front() < 0 || best.entries.front() >= static_cast<std::int32_t>(a_keys.size()) ? std::string() : a_keys[best.entries.front()];
			const auto look = ChooseLook(best.dish, a_keys, main);
			std::scoped_lock lock(g_mainLock);
			(a_cook ? g_cookLook : g_lastLook)[best.dish] = look;
		}

		if (a_only < 0 && !a_quiet && best.dish >= 0 && !best.entries.empty() && !Str(std::format("dish.{}.gw", best.dish)).empty()) {
			std::scoped_lock lock(g_mainLock);
			(a_cook ? g_cookMain : g_lastMain)[best.dish] = Str("ish." + a_keys[best.entries.front()]);
		}
		if (a_only < 0 && !a_quiet && best.dish >= 0 && !best.entries.empty() && Int(std::format("dish.{}.dk", best.dish)) != 0) {
			std::vector<std::string> slotKeys;
			for (const auto e : best.entries) {
				if (e >= 0 && e < static_cast<std::int32_t>(a_keys.size())) slotKeys.push_back(a_keys[e]);
			}
			auto name = DrinkName(best.dish, slotKeys);
			std::scoped_lock lock(g_mainLock);
			(a_cook ? g_cookDrink : g_lastDrink)[best.dish] = std::move(name);
		}
		return best;
	}

	Result Project(const CookingMenu::Pot& a_pot)
	{
		Result r;
		const auto cnt = a_pot.size();
		std::vector<Ing> ing;
		ing.reserve(cnt);
		for (auto item : a_pot) {
			ing.push_back(IngOf(item));
		}
		r.used.assign(cnt, false);
		r.enabler.assign(cnt, false);
		r.side.assign(cnt, false);

		// 1. Which dish? (ChooseDish above: order does not matter)
		std::vector<std::string> keys;
		keys.reserve(cnt);
		for (const auto& i : ing) {
			keys.push_back(i.key);
		}
		const auto choice = ChooseDish(keys, HasTreePerk(PERK_LONG_SIMMER));
		if (choice.dish < 0) {
			return r;
		}
		const auto best = choice.dish;
		r.dish = best;
		std::vector<bool> recipe(cnt, false);
		for (const auto e : choice.entries) {
			r.used[e] = true;
			recipe[e] = true;
		}
		std::int32_t bestSlots = static_cast<std::int32_t>(choice.entries.size());
		const std::int32_t spiceIdx = -1;  // no spiced dishes any more (Spiced Beef, Wine and Tea are not cookable)

		r.baseName = DishName(best, "");
		const auto tier = Int(std::format("dish.{}.t", best));
		r.drink = Int(std::format("dish.{}.dr", best)) == 1;
		const bool ferment = Int(std::format("dish.{}.fe", best)) == 1;

		// Dragonfeast (perk): a Dragon Bone makes a meal and Dragon Scales a drink that always reach level 4 and last half
		// a day. The part stands in for any drink or garlic a seasoning needs (the ring is still needed). Either needs a
		// seasoning and a full pot (five ingredients, all of them used): a Bone meal with a vegetable or staple, a Scales
		// drink with at least three drink ingredients (sweeteners, grains, fruits, flowers or herbs, water) in the brew, its base
		// included: a two-slot Mead or Ale takes one more, a three-slot Wine is already there (fixed 2026-11-14: the old
		// "one beyond its base" could never fit a Wine: base 3 + Scales + seasoning fill the pot). In a
		// Scales drink the extra drink ingredients never clash: one of the drink's flavour seasons it, any other is the
		// brew's body (a filler: no flavour, no stars; decided 2026-11-03). A pot that falls short cooks as if the part
		// were not there (the part stays in the pot). Same as FC_Core.Recalc.
		// (a baked dish takes a Bone like any meal: his call 2026-11-15)
		const auto fits = [&](std::int32_t a_part) { return (a_part == 1 && !r.drink) || (a_part == 2 && r.drink); };
		std::int32_t dragonIdx = -1;
		if (HasTreePerk(PERK_DRAGONFEAST)) {
			for (std::size_t i = 0; i < cnt; ++i) {
				if (ing[i].dragon > 0 && !r.used[i]) {
					if (dragonIdx < 0 && fits(ing[i].dragon)) {
						dragonIdx = static_cast<std::int32_t>(i);
					} else if (r.wrongPart == 0 && !fits(ing[i].dragon)) {
						r.wrongPart = ing[i].dragon;
					}
				}
			}
		}
		const auto dishUsed = r.used;
		const bool ringWorn = RingWorn();
		const bool longSimmer = HasTreePerk(PERK_LONG_SIMMER);
		std::string cat;
		std::int32_t stars = 0;
		std::int32_t enablerStar = 0;
		bool gotVeg = false;
		bool gotStaple = false;
		std::int32_t recipeHelper = -1;  // the drink an unappetizing recipe ingredient needs (last pass): spent by a plain dish too
		std::int32_t recipeGarlic = -1;  // the Garlic (or Earthy potion) a poisonous recipe ingredient needs: the same
		for (int pass = dragonIdx >= 0 ? 0 : 1; pass < 2; ++pass) {
			const bool withDragon = pass == 0;
			r.used = dishUsed;
			r.enabler.assign(cnt, false);
			r.side.assign(cnt, false);
			r.blocked = false;
			r.blockedBy.clear();
			r.clash = false;
			r.cure = -1;
			r.level = 0;
			r.total = 0;
			cat.clear();
			stars = 0;
			enablerStar = 0;
			gotVeg = false;
			gotStaple = false;
			std::int32_t brewExtras = 0;  // drink ingredients beyond a dragon brew's base
			std::int32_t slotsLeft = 5 - bestSlots;
			if (withDragon) {
				r.used[dragonIdx] = true;
				r.enabler[dragonIdx] = true;
				slotsLeft -= 1;
			}

			// 2. Enablers in the pot (none with a dragon part: it does their job)
			std::int32_t alcIdx = -1;
			std::int32_t garlicIdx = -1;
			for (std::size_t i = 0; i < cnt && !withDragon; ++i) {
				if (!r.used[i]) {
					if (alcIdx < 0 && ing[i].alcohol) {
						alcIdx = static_cast<std::int32_t>(i);
					} else if (garlicIdx < 0 && ing[i].garlic) {
						garlicIdx = static_cast<std::int32_t>(i);
					}
				}
			}
			bool needAlc = false;
			bool needGarlic = false;
			for (std::size_t i = 0; i < cnt; ++i) {
				if (!r.used[i]) {
					if (ing[i].need == "alc" && alcIdx >= 0 && !ferment) {
						needAlc = true;
					} else if (ing[i].need == "garlic" && garlicIdx >= 0) {
						needGarlic = true;
					}
				}
			}
			recipeHelper = -1;
			recipeGarlic = -1;
			if (choice.needAlc && alcIdx >= 0 && !withDragon) {
				needAlc = true;  // an unappetizing recipe ingredient needs the drink too
				recipeHelper = alcIdx;
			}
			if (choice.needGarlic && garlicIdx >= 0 && !withDragon) {
				needGarlic = true;  // a poisonous recipe ingredient needs the Garlic too
				recipeGarlic = garlicIdx;
			}
			if (needAlc) {
				r.used[alcIdx] = true;
				r.enabler[alcIdx] = true;
				slotsLeft -= 1;
				enablerStar += 1;  // each helper adds its own star (his call 2026-11-15; FC_Core.Recalc the same)
			}
			if (needGarlic) {
				r.used[garlicIdx] = true;
				r.enabler[garlicIdx] = true;
				r.cure = garlicIdx;
				slotsLeft -= 1;
				enablerStar += 1;
			}

			// 3. Seasonings set the effect and add stars (every potion counts like any seasoning: his 2026-11-22 "drop the only
			// one potion counts rule; clashes and base ingredients already count for realism"). In a
			// dragon brew the other seasonings go first, then the extra drink ingredients: those of the drink's flavour
			// season it, the rest are its body
			const bool brewBody = withDragon && r.drink;
			for (int round = brewBody ? 0 : 1; round < 2; ++round) {
			for (std::size_t i = 0; i < cnt; ++i) {
				if (r.used[i]) {
					continue;
				}
				if (brewBody && (round == 0) == (ing[i].brew > 0)) {
					continue;  // the other seasonings in round 0, drink ingredients in round 1
				}
				const auto& q = ing[i].need;
				const bool usable = q.empty() || (q == "alc" && (ferment || alcIdx >= 0 || longSimmer || withDragon)) ||
				                    (q == "garlic" && (garlicIdx >= 0 || longSimmer || withDragon)) || (q == "ring" && ringWorn);
				if (brewBody && round == 1) {
					if (slotsLeft <= 0) {
						continue;
					}
					++brewExtras;
					r.used[i] = true;
					slotsLeft -= 1;
					if (usable && !ing[i].flavour.empty() && (cat.empty() || cat == ing[i].flavour)) {
						stars += ing[i].stars;
						cat = ing[i].flavour;
					} else {
						r.side[i] = true;  // the brew's body
					}
					continue;
				}
				if (ing[i].flavour.empty()) {
					continue;
				}
				if (!usable) {
					r.blocked = true;
					r.blockedBy.emplace_back(i, q);
				} else if (slotsLeft > 0) {
					stars += ing[i].stars;
					if (cat.empty()) {
						cat = ing[i].flavour;
					} else if (cat != ing[i].flavour) {
						r.clash = true;
					}
					r.used[i] = true;
					slotsLeft -= 1;
				}
			}
			}

			// 4. A vegetable and a staple make a food dish last longer (one of each counts). In a drink only water does, one
			// water (his call 2026-11-15: water is a staple and a drink ingredient); in a dragon brew it already counted
			// as one of the drink ingredients above
			{
				for (std::size_t i = 0; i < cnt; ++i) {
					if (!r.used[i] && slotsLeft > 0) {
						if (r.drink && ing[i].brew != 5) {
							continue;
						}
						if (ing[i].filler == "veg" && !gotVeg) {
							gotVeg = true;
							r.used[i] = true;
							r.side[i] = true;
							slotsLeft -= 1;
						} else if (ing[i].filler == "staple" && !gotStaple) {
							gotStaple = true;
							r.used[i] = true;
							r.side[i] = true;
							slotsLeft -= 1;
						}
					}
				}
			}

			// 5. Level
			if (r.clash) {
				cat.clear();
			} else if (!cat.empty()) {
				r.total = tier + stars + enablerStar;
				r.level = r.total >= 7 ? 3 : r.total >= 5 ? 2 : 1;
			}
			if (withDragon) {
				std::int32_t usedCount = 0;
				bool hasSide = false;  // a vegetable or staple anywhere in the meal (the main ingredient counts)
				for (std::size_t i = 0; i < cnt; ++i) {
					if (r.used[i]) {
						++usedCount;
						hasSide = hasSide || !ing[i].filler.empty();
					}
				}
				if (r.clash) {
					r.dragonWhy = "clash";
				} else if (cat.empty()) {
					r.dragonWhy = "seasoning";
				} else if (usedCount < 5 || (r.drink ? bestSlots + brewExtras < 3 : !hasSide)) {
					r.dragonWhy = "full";
				} else {
					r.dragon = true;
					r.dragonPart = dragonIdx;
					r.level = 4;
					break;
				}
			}
		}
		if (r.level == 0) {
			for (std::size_t i = 0; i < cnt; ++i) {
				const auto si = static_cast<std::int32_t>(i);
				r.used[i] = recipe[i] || si == spiceIdx;
				r.enabler[i] = false;
				r.side[i] = false;
			}
			// the drink the recipe itself needs is still used, as in FC_Core.Recalc (test 2026-11-16: Braided Bread's
			// preview said "Unused: Ale" while the cook spent it)
			if (recipeHelper >= 0) {
				r.used[recipeHelper] = true;
				r.enabler[recipeHelper] = true;
			}
			if (recipeGarlic >= 0) {
				r.used[recipeGarlic] = true;
				r.enabler[recipeGarlic] = true;
				r.cure = recipeGarlic;  // (a vampire eats a Garlic dish at half strength, as with a seasoning's Garlic)
			}
		}
		r.dragonIdle = dragonIdx >= 0 && !r.dragon ? dragonIdx : -1;
		r.flavour = cat;
		r.base.assign(cnt, false);
		r.spice = spiceIdx;
		for (std::size_t i = 0; i < cnt; ++i) {
			r.base[i] = recipe[i];
		}
		// durations: 20:00 is the ceiling, reached only by a perfect meal at Cooking 100 with the Cook perk; a dragon
		// dish lasts half a day of game time instead (FC_Core.DragonDuration)
		// sheet_v122 (built 2026-11-12): raw 4:00 / 6:00 / 8:20 by level; a meal adds 1:00 for every pot ingredient it uses
		// (recipe, seasonings, enabler drink, the one vegetable and one staple extra), so a perfect full pot of 5 lasts
		// 20:00 for a master cook at any tier; drinks last the level duration, plus 1:00 with water in them (2026-11-15)
		std::int32_t dur = r.level == 1 ? 240 : r.level == 2 ? 360 : r.level >= 3 ? 500 : 0;
		if (r.dragon) {
			dur = FortifyDuration(DragonDuration(), true);  // Fortify Cooking (Cooking/ChefHat.inl)
		} else {
			if (r.level > 0 && !r.drink) {
				dur += 60 * static_cast<std::int32_t>(std::ranges::count(r.used, true));
			} else if (r.level > 0 && gotStaple) {
				dur += 60;  // water in a drink
			}
			if (dur > 0 && HasTreePerk(PERK_TASTE)) {
				dur = dur * 3 / 2;  // Cook: meals and drinks last half again as long
			}
			if (dur > 0) {
				const float skill = std::clamp(CookingSkill(), 15.0f, 100.0f);
				const float scale = 0.8f + 0.2f * (skill - 15.0f) / 85.0f;
				dur = std::min(static_cast<std::int32_t>(dur * scale / 30.0f) * 30, 1200);
				dur = FortifyDuration(dur, false);  // Fortify Cooking can pass 20:00 a little (Cooking/ChefHat.inl)
			}
		}
		r.dur = dur;
		for (std::size_t i = 0; i < cnt; ++i) {
			if (r.used[i]) {
				r.gold += ing[i].potion ? POTION_VALUE : a_pot[i]->GetGoldValue();
			}
		}
		return r;
	}

	// Flash counts text in UTF-16 units: a name's length as the text field sees it
	std::int32_t Utf16Length(const std::string& a_text)
	{
		std::int32_t n = 0;
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const auto c = static_cast<unsigned char>(a_text[i]);
			if ((c & 0xC0) != 0x80) {
				n += c >= 0xF0 ? 2 : 1;
			}
		}
		return n;
	}

	// Where a new player finds seasonings: the flavour categories once something is tasted, Untasted before that
	std::string SeasoningHint()
	{
		bool tasted = false;
		bool untasted = false;
		{
			for (const auto& [item, count] : CarriedCounts()) {
				if (!item || count <= 0) {
					continue;
				}
				const auto ing = IngOf(item);
				if (ing.flavour.empty() || ing.garlic || ing.potion) {
					continue;
				}
				(Tasted(item, ing) ? tasted : untasted) = true;
			}
		}
		if (tasted) {
			return FC::Tr("Seasonings are in the flavour categories.");
		}
		if (untasted) {
			return FC::Tr("Your seasonings are in Untasted.");
		}
		return FC::Tr("You carry no seasonings yet.");
	}

	CookingMenu* GetOpenMenu()
	{
		auto ui = RE::UI::GetSingleton();
		auto menu = ui ? ui->GetMenu(CookingMenu::MENU_NAME) : nullptr;
		return static_cast<CookingMenu*>(menu.get());
	}

	// A mouse click anywhere on the screen (it replaces the vanilla "click to craft" handler): if it lands on a name in
	// the "In the pot" line under the card, that ingredient comes out of the pot.
	class PotLineClick : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			auto movie = a_params.movie;
			auto menu = GetOpenMenu();
			if (!movie || !menu) {
				return;
			}
			RE::GFxValue x, y, holder, field;
			if (!movie->GetVariable(&x, "_root._xmouse") || !movie->GetVariable(&y, "_root._ymouse") ||
				!movie->GetVariable(&holder, "_root.Menu.AdditionalDescriptionHolder") || !movie->GetVariable(&field, "_root.Menu.AdditionalDescription") ||
				!holder.IsObject() || !field.IsObject()) {
				return;
			}
			RE::GFxValue hit;
			RE::GFxValue hitArgs[3]{ x.GetNumber(), y.GetNumber(), false };  // the box around the line, not just the letters
			if (!holder.Invoke("hitTest", &hit, hitArgs, 3) || !hit.IsBool() || !hit.GetBool()) {
				return;
			}
			// the click in the text field's own coordinates
			RE::GFxValue point;
			movie->CreateObject(&point);
			point.SetMember("x", x);
			point.SetMember("y", y);
			holder.Invoke("globalToLocal", nullptr, &point, 1);
			RE::GFxValue px, py, fx, fy;
			point.GetMember("x", &px);
			point.GetMember("y", &py);
			field.GetMember("_x", &fx);
			field.GetMember("_y", &fy);
			if (!px.IsNumber() || !py.IsNumber() || !fx.IsNumber() || !fy.IsNumber()) {
				return;
			}
			RE::GFxValue index;
			RE::GFxValue charArgs[2]{ px.GetNumber() - fx.GetNumber(), py.GetNumber() - fy.GetNumber() };
			if (field.Invoke("getCharIndexAtPoint", &index, charArgs, 2) && index.IsNumber()) {
				menu->RemoveAtChar(static_cast<std::int32_t>(index.GetNumber()));
			}
		}
	};

	RE::GFxValue MakeString(RE::GFxMovieView* a_movie, const std::string& a_text)
	{
		RE::GFxValue value;
		a_movie->CreateString(&value, a_text.c_str());
		return value;
	}

	std::vector<std::string> Split(const std::string& a_text, char a_sep)
	{
		std::vector<std::string> parts;
		std::size_t start = 0;
		while (true) {
			const auto pos = a_text.find(a_sep, start);
			parts.push_back(a_text.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
			if (pos == std::string::npos) {
				break;
			}
			start = pos + 1;
		}
		return parts;
	}

	std::string Join(const std::vector<std::string>& a_parts)
	{
		std::string text;
		for (std::size_t i = 0; i < a_parts.size(); ++i) {
			text += (i == 0 ? std::string() : i + 1 == a_parts.size() ? FC::Tr(" and ") : FC::Tr(", ")) + a_parts[i];
		}
		return text;
	}

	// "Skyrim.esm:416921" -> the form, or nullptr when that plugin is not loaded
	RE::TESBoundObject* LookupItem(const std::string& a_id)
	{
		const auto colon = a_id.rfind(':');
		if (colon == std::string::npos) {
			return nullptr;
		}
		const std::string plugin = a_id.substr(0, colon);
		const auto localId = static_cast<RE::FormID>(std::strtoul(a_id.c_str() + colon + 1, nullptr, 10));
		auto data = RE::TESDataHandler::GetSingleton();
		// the typed LookupForm<TESBoundObject> rejects everything (TESBoundObject has no form type of its own),
		// so look up the plain form and check that it is an item
		auto form = data ? data->LookupForm(localId, plugin) : nullptr;
		return form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
	}

	// Tells the scripts what to do with an SKSE mod event (the pot's script listens for it), carrying the
	// ingredients' form IDs as text: "123,456". Calling FC_Core directly through the Papyrus VM crashed the
	// game on this runtime, while mod events are the well-trodden way for plugins to reach scripts.
	// a form as the scripts read it: "<local id>@<plugin>" for a plugin's own form (Game.GetForm can't find a light
	// plugin's form, FE..., by its full ID), or the full ID in two positive halves, "high:low", for a form made in game
	// (a player-made potion, FF...; Papyrus can't read a number past 0x7FFFFFFF from text)
	std::string IdText(const RE::TESForm* a_form)
	{
		const auto id = a_form->GetFormID();
		if (const auto file = a_form->GetFile(0); file && (id >> 24) != 0xFF) {
			const auto local = (id >> 24) == 0xFE ? (id & 0xFFF) : (id & 0xFFFFFF);
			return std::format("{}@{}", local, file->GetFilename());
		}
		return std::format("{}:{}", id >> 16, id & 0xFFFF);
	}

	void SendTextToScripts(const char* a_event, const std::string& a_text)
	{
		auto source = SKSE::GetModCallbackEventSource();
		if (!source) {
			logger::error("SKSE mod events not available: cannot send {}", a_event);
			return;
		}
		SKSE::ModCallbackEvent event{ a_event, RE::BSFixedString(a_text.c_str()), 0.0f, nullptr };
		source->SendEvent(&event);
	}

	void SendToScripts(const char* a_event, const std::vector<RE::TESBoundObject*>& a_items)
	{
		std::string ids;
		for (auto item : a_items) {
			if (item) {
				if (!ids.empty()) {
					ids += ',';
				}
				ids += IdText(item);
			}
		}
		// the scripts hear it from the main thread (the cooking screen calls this from the UI thread)
		SKSE::GetTaskInterface()->AddTask([event = std::string(a_event), ids = std::move(ids)]() {
			auto source = SKSE::GetModCallbackEventSource();
			if (!source) {
				logger::error("SKSE mod events not available: cannot send {}", event);
				return;
			}
			SKSE::ModCallbackEvent modEvent{ RE::BSFixedString(event.c_str()), RE::BSFixedString(ids.c_str()), 0.0f, nullptr };
			source->SendEvent(&modEvent);
		});
	}

	void SetKnownList(const std::vector<std::int32_t>& a_known)
	{
		CookLock lock(g_cookLock);
		g_known = std::set<std::int32_t>(a_known.begin(), a_known.end());
	}

	// Papyrus: FC_Menu.Open(int[] known) - pots call it through FC_Core.OpenMenu()
	void PapyrusOpen(RE::StaticFunctionTag*, std::vector<std::int32_t> a_known)
	{
		SetKnownList(a_known);
		CaptureView(true);  // the screen's first look at the inventory (natives run at the frame's sync point: safe here)
		CookingMenu::Open();
	}

	// Papyrus: FC_Menu.Refresh(int[] known) - FC_Core.CookFromMenu calls it after cooking
	void PapyrusRefresh(RE::StaticFunctionTag*, std::vector<std::int32_t> a_known)
	{
		SetKnownList(a_known);
		CaptureView(true);  // the cooked dish is in the bag and the ingredients gone: copy that for the screen
		SKSE::GetTaskInterface()->AddUITask([]() {
			// (a cook of this screen still running: its answer, Cooked, refreshes the screen; a refresh before that unlocked
			// R mid-cook; Codex review 2026-11-16e, M4)
			if (auto menu = GetOpenMenu(); menu && !menu->CookRunning()) {
				menu->Populate();
			}
		});
	}

	// Papyrus: FC_Menu.SetKnown(int[] known) - before the player sits at a pot; the screen opens when the
	// pot's vanilla crafting menu does (see PotWatcher), so the vanilla cooking animation plays
	void PapyrusSetKnown(RE::StaticFunctionTag*, std::vector<std::int32_t> a_known)
	{
		SetKnownList(a_known);
		CaptureView(true);  // the pot's screen opens next (PotWatcher): it needs this inventory, not the last visit's
	}

	// Papyrus: FC_Menu.SetTasted(int[] formIds) - the ingredients whose flavour the player knows
	void PapyrusSetTasted(RE::StaticFunctionTag*, std::vector<std::int32_t> a_tasted)
	{
		CookLock lock(g_cookLock);
		g_tasted.clear();
		for (auto id : a_tasted) {
			g_tasted.insert(static_cast<RE::FormID>(id));
		}
	}

	// Papyrus: FC_Menu.Cooked(string message, bool made) - what the pot made ("You cooked Spicy Beef, 6 stars."); made
	// false when nothing came out ("Nothing to cook yet.")
	void PapyrusCooked(RE::StaticFunctionTag*, std::string a_message, bool a_made)
	{
		std::uint32_t request = 0;
		{
			// the ingredient lists' Most used and Recent: each ingredient the dish used counts once per cook. Counted here,
			// under the lock the save takes, and before the cook is marked over, so a new cook pressed on the screen can't
			// swap in its own ingredients first (Codex review 2026-11-16 afternoon, H1)
			CookLock lock(g_cookLock);
			if (a_made) {
				++g_cookClock;
				for (const auto id : g_cookPendingUses) {
					++g_ingUses[id];
					g_ingLastUsed[id] = g_cookClock;
				}
			}
			g_cookPendingUses.clear();
			request = g_cookRequest.load();  // the cook this answers (read before it is marked over: a new one can start then)
			g_cookPlace = 0;  // the cook is over
		}
		CaptureView(true);
		SKSE::GetTaskInterface()->AddUITask([message = std::move(a_message), request]() {
			if (auto menu = GetOpenMenu()) {
				menu->Cooked(message, request);
			}
		});
	}

	// Papyrus: FC_Menu.CookBusy(string message) - the scripts refused the cook (one still runs): the pot on the screen
	// stays as it is and the running cook keeps its station (before, the refusal cleared both; reviews 2026-11-16)
	void PapyrusCookBusy(RE::StaticFunctionTag*, std::string a_message)
	{
		{
			// the cook this screen sent was refused, so it isn't running: it no longer blocks the next one (a quickload in the
			// scripts' 15 seconds keeps their own mark; Claude review 2026-11-16g, M3)
			CookLock lock(g_cookLock);
			g_cookPendingUses.clear();
			g_cookPlace = 0;
		}
		SKSE::GetTaskInterface()->AddUITask([message = std::move(a_message)]() {
			if (auto menu = GetOpenMenu()) {
				menu->Refused(message);
			}
		});
	}

