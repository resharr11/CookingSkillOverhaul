#include "Settings.h"

#include <atomic>
#include <fstream>
#include <mutex>
#include <unordered_map>

// The settings file and the translation hook. Settings used to live on a FLICK page (SettingsTool.cpp, removed
// 2026-11-12): FLICK's API header is GPL-3, so the pages moved to SKSE Menu Framework (Menu.cpp).

namespace
{
	std::mutex  g_lock;
	FC::Options g_options;

	void SaveIni(const FC::Options& o)
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(FC::IniToRead().wstring().c_str());  // keeps lines the menu doesn't own (sLanguage for translators)
		ini.SetBoolValue("Cooking", "bShowInSkillsMenu", o.skillsMenu,
			"; Cooking in the vanilla Skills menu, right of Alchemy (not used when another mod replaces the Skills menu)");
		ini.Delete("Cooking", "sSkillsMenuAfter");  // the placement option was dropped (his call, 2026-11-12: Cooking's place is fixed)
		ini.SetDoubleValue("SharedTable", "fRangeMetres", o.shareRangeMetres,
			"; how far (in metres) a following follower can be and still share your meals");
		ini.SetBoolValue("General", "bFeet", o.feet, "; show distances in feet instead of metres (the file always keeps metres)");
		ini.SetBoolValue("Cooking", "bVanillaCrafting", o.vanillaCrafting,
			"; emergency fallback: the cooking screen offers the pot's or oven's own vanilla crafting menu (recipes made there skip Cooking Skill Overhaul's rules)");
		ini.SetBoolValue("Cooking", "bPlayerEatIdle", o.playerEatIdle,
			"; you play the game's eating or drinking idle when you have a Cooking Skill Overhaul dish or drink (third person; skipped when Eating Animations and Sounds is installed)");
		// only the switches the player set: a missing line follows Cooking Skill Overhaul's default, which depends on the other
		// survival mods found (his call 2026-11-14)
		const auto survival = [&](const char* a_key, bool a_value, FC::Feature a_feature) {
			if (o.survivalChosen & FC::FeatureBit(a_feature)) {
				ini.SetBoolValue("Survival", a_key, a_value);
			} else {
				ini.Delete("Survival", a_key);
			}
		};
		survival("bHunger", o.hunger, FC::Feature::kHunger);
		survival("bSpicyCold", o.spicyCold, FC::Feature::kSpicyCold);
		survival("bSpicyWater", o.spicyWater, FC::Feature::kSpicyWater);
		survival("bCampRest", o.campRest, FC::Feature::kCampRest);
		survival("bCampRestRested", o.campRestBonus, FC::Feature::kCampRestBonus);
		survival("bSteppedDrains", o.steppedDrains, FC::Feature::kSteppedDrains);
		survival("bHeartyRegen", o.heartyRegen, FC::Feature::kHeartyRegen);
		survival("bDragonWarmth", o.dragonWarmth, FC::Feature::kDragonWarmth);
		survival("bThirst", o.thirst, FC::Feature::kThirst);
		survival("bAfflictions", o.afflictions, FC::Feature::kAfflictions);
		ini.SetLongValue("Survival", "iAlcohol", o.alcohol, "; how much alcohol quenches thirst: 0 as much as any drink, 1 half as much, 2 a fifth as much");
		const auto path = FC::UserIniPath();
		if (path.empty()) {
			logger::warn("Settings: no Documents\\My Games\\...\\SKSE folder found; the settings can't be saved");
			return;
		}
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		if (ini.SaveFile(path.wstring().c_str()) < 0) {
			logger::error("Settings: couldn't write {}", path.string());
		}
	}

	// ---- translation hook (same format as NeedsHUD's, his rule 2026-11-09: every mod ships an English file for
	// translators). Skyrim's own translation file format: Interface/Translations/FreeCooking_<LANGUAGE>.txt, UTF-16 LE
	// with BOM, one "$FC_English text<TAB>Translated text" per line, picked by the game's language (sLanguage in
	// Skyrim.ini, or sLanguage= under [General] in FreeCooking.ini for translators' tests). The $FC_ prefix keeps our
	// lines apart in the game's shared table.
	std::unordered_map<std::string, std::string> g_tr;
	const std::string KEY_PREFIX = "$FC_";

	std::string Utf16ToUtf8(const std::u16string& a_in)
	{
		std::string out;
		for (size_t i = 0; i < a_in.size(); ++i) {
			std::uint32_t c = a_in[i];
			if (c >= 0xD800 && c <= 0xDBFF && i + 1 < a_in.size() && a_in[i + 1] >= 0xDC00 && a_in[i + 1] <= 0xDFFF) {
				c = 0x10000 + ((c - 0xD800) << 10) + (a_in[i + 1] - 0xDC00);
				++i;
			}
			if (c < 0x80) {
				out += static_cast<char>(c);
			} else if (c < 0x800) {
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else if (c < 0x10000) {
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
		}
		return out;
	}

	// every byte belongs to a whole, well-formed UTF-8 letter
	bool ValidUtf8(const std::string& a_s)
	{
		for (std::size_t i = 0; i < a_s.size();) {
			const auto b = static_cast<unsigned char>(a_s[i]);
			const std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
			if (n == 0 || i + n > a_s.size()) {
				return false;
			}
			for (std::size_t k = 1; k < n; ++k) {
				if ((static_cast<unsigned char>(a_s[i + k]) & 0xC0) != 0x80) {
					return false;
				}
			}
			i += n;
		}
		return true;
	}

	std::string Upper(std::string a_s)
	{
		for (auto& ch : a_s) {
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}
		return a_s;
	}

	void LoadTranslation(const std::string& a_override)
	{
		std::string lang = "ENGLISH";
		if (auto ini = RE::INISettingCollection::GetSingleton()) {
			if (auto s = ini->GetSetting("sLanguage:General")) {
				if (const char* v = s->GetString(); v && *v) {
					lang = v;
				}
			}
		}
		if (!a_override.empty()) {
			lang = a_override;
			logger::info("Translation: FreeCooking.ini asks for {}", lang);
		}
		lang = Upper(lang);
		g_tr.clear();
		if (lang == "ENGLISH") {
			return;
		}
		const std::string path = "Data/Interface/Translations/FreeCooking_" + lang + ".txt";
		std::ifstream f(path, std::ios::binary);
		if (!f) {
			logger::info("Translation: no {} found, the menu text stays in English", path.substr(5));
			return;
		}
		const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		std::string text;
		if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF && static_cast<unsigned char>(raw[1]) == 0xFE) {
			std::u16string u;
			for (size_t i = 2; i + 1 < raw.size(); i += 2) {
				u += static_cast<char16_t>(static_cast<unsigned char>(raw[i]) | (static_cast<unsigned char>(raw[i + 1]) << 8));
			}
			text = Utf16ToUtf8(u);
		} else {  // saved as UTF-8 by mistake: read it anyway (skipping a UTF-8 BOM)
			text = raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0 ? raw.substr(3) : raw;
			if (!ValidUtf8(text)) {
				// saved as ANSI (Notepad's old default): read every byte as a Western European letter, so no broken
				// letter ever reaches the screen (a broken one crashed the cooking screen, Codex review 2026-11-15)
				logger::warn("Translation: {} is not UTF-16 or UTF-8; read as Windows-1252. Save it as UTF-16 LE to be sure", path.substr(5));
				// Windows-1252 is Latin-1 except 0x80-0x9F, which hold curly quotes, dashes, the euro sign... (before, those
				// came out as control characters; Codex review 2026-11-16). Its five unused bytes become U+FFFD
				static constexpr char16_t HIGH[32] = { 0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
					0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122,
					0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178 };
				std::u16string u;
				for (const unsigned char ch : text) {
					u += ch >= 0x80 && ch <= 0x9F ? HIGH[ch - 0x80] : static_cast<char16_t>(ch);
				}
				text = Utf16ToUtf8(u);
			}
		}
		int lines = 0, used = 0;
		size_t pos = 0;
		while (pos < text.size()) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const size_t tab = line.find('\t');
			if (line.rfind(KEY_PREFIX, 0) != 0 || tab == std::string::npos) {
				continue;
			}
			++lines;
			const std::string key = line.substr(KEY_PREFIX.size(), tab - KEY_PREFIX.size()), value = line.substr(tab + 1);
			if (!value.empty()) {
				g_tr[key] = value;
				++used;
			}
		}
		logger::info("Translation: {} read, {} of {} lines translated", path.substr(5), used, lines);
	}
}

namespace FC
{
	namespace
	{
		SurvivalMods g_survivalMods;

		bool Loaded(RE::TESDataHandler* a_data, std::string_view a_name)
		{
			return a_data->LookupLoadedModByName(a_name) || a_data->LookupLoadedLightModByName(a_name);
		}

		void DetectSurvivalMods()
		{
			auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			SurvivalMods m;
			m.smi = Loaded(data, "SurvivalModeImproved.esp");
			m.starfrost = Loaded(data, "Starfrost.esp");
			m.sunhelm = Loaded(data, "SunHelmSurvival.esp");
			m.frostfall = Loaded(data, "Frostfall.esp");
			m.lastSeed = Loaded(data, "LastSeed.esp");
			m.rnd = Loaded(data, "RealisticNeedsandDiseases.esp") || Loaded(data, "MVC Nirnfeel Immersion.esp");
			m.ineed = Loaded(data, "iNeed.esp");
			g_survivalMods = m;
			logger::info("Other survival mods: {}", m.Any() ? m.Names() : "none");
		}
	}

	std::string SurvivalMods::Names() const
	{
		std::string out;
		const auto add = [&](bool a_on, const char* a_name) {
			if (a_on) {
				out += out.empty() ? "" : ", ";
				out += a_name;
			}
		};
		add(smi && !starfrost, "Survival Mode Improved");
		add(starfrost, "Starfrost");
		add(sunhelm, "SunHelm");
		add(frostfall, "Frostfall");
		add(lastSeed, "Last Seed");
		add(rnd, "Realistic Needs and Diseases");
		add(ineed, "iNeed");
		return out;
	}

	const SurvivalMods& OtherSurvivalMods() { return g_survivalMods; }

	namespace
	{
		std::atomic<int> g_regenStopped{ -1 };
	}
	int RegenStopped() { return g_regenStopped.load(); }
	void SetRegenStopped(bool a_stopped) { g_regenStopped = a_stopped ? 1 : 0; }

	// The switch-by-mod table agreed 2026-11-14 (each mod read from its own scripts):
	// - the cold switches (Spicy cold, Spicy water) stay on everywhere: each mod gets them through its own door;
	// - Survival Mode Improved / Starfrost: sleep rests fully anywhere (no camp rest to give), they apply their own bar
	//   penalties every half second (no stepped drains); Starfrost has its own "fed for the buff + 4 hours" (no hunger);
	//   Hearty's regen, thirst and afflictions fit (they keep Survival's lower regen and afflictions and have no thirst);
	// - SunHelm, Last Seed, Realistic Needs, iNeed: their own hunger, tiredness and penalties, normal health regen;
	// - Frostfall: cold only, nothing else changes.
	Options DefaultOptions()
	{
		// his call 2026-11-14 (evening): every Survival switch starts on whatever survival mods run; with another mod, a switch
		// on means Cooking Skill Overhaul applies that feature to the mod's own values (Cooking/OtherMods.inl), and a player who wants
		// the mod's own behaviour turns that switch off. The Rested option stays off (Options{}).
		return Options{};
	}

	void LoadSettings()
	{
		DetectSurvivalMods();
		Options o = DefaultOptions();
		CSimpleIniA ini;
		ini.SetUnicode();
		std::string language;
		if (ini.LoadFile(FC::IniToRead().wstring().c_str()) >= 0) {
			o.skillsMenu = ini.GetBoolValue("Cooking", "bShowInSkillsMenu", o.skillsMenu);
			o.shareRangeMetres = std::clamp(static_cast<float>(ini.GetDoubleValue("SharedTable", "fRangeMetres", o.shareRangeMetres)),
				SHARE_RANGE_MIN, SHARE_RANGE_MAX);
			o.feet = ini.GetBoolValue("General", "bFeet", o.feet);
			o.vanillaCrafting = ini.GetBoolValue("Cooking", "bVanillaCrafting", o.vanillaCrafting);
			o.playerEatIdle = ini.GetBoolValue("Cooking", "bPlayerEatIdle", o.playerEatIdle);
			// a line in the file is the player's own choice; a missing one keeps the default for the mods found
			const auto survival = [&](const char* a_key, bool& a_value, Feature a_feature) {
				if (ini.GetValue("Survival", a_key, nullptr)) {
					a_value = ini.GetBoolValue("Survival", a_key, a_value);
					o.survivalChosen |= FeatureBit(a_feature);
				}
			};
			survival("bHunger", o.hunger, Feature::kHunger);
			survival("bSpicyCold", o.spicyCold, Feature::kSpicyCold);
			survival("bSpicyWater", o.spicyWater, Feature::kSpicyWater);
			survival("bCampRest", o.campRest, Feature::kCampRest);
			survival("bCampRestRested", o.campRestBonus, Feature::kCampRestBonus);
			survival("bSteppedDrains", o.steppedDrains, Feature::kSteppedDrains);
			survival("bHeartyRegen", o.heartyRegen, Feature::kHeartyRegen);
			survival("bDragonWarmth", o.dragonWarmth, Feature::kDragonWarmth);
			survival("bThirst", o.thirst, Feature::kThirst);
			survival("bAfflictions", o.afflictions, Feature::kAfflictions);
			o.alcohol = std::clamp(static_cast<int>(ini.GetLongValue("Survival", "iAlcohol", o.alcohol)), 0, 2);
			if (const char* lang = ini.GetValue("General", "sLanguage", nullptr); lang && *lang) {
				language = lang;
			}
		}
		{
			std::scoped_lock lock(g_lock);
			g_options = o;
		}
		logger::info("Settings: Survival features hunger {}, Spicy cold {}, Spicy water {}, camp rest {}, stepped drains {}, Hearty regen {}, dragon warmth {}, thirst {} (alcohol {}), afflictions {}; Shared Table {:.0f} m",
			o.hunger, o.spicyCold, o.spicyWater, o.campRest, o.steppedDrains, o.heartyRegen, o.dragonWarmth, o.thirst, o.alcohol, o.afflictions, o.shareRangeMetres);
		LoadTranslation(language);
	}

	// Eating Animations and Sounds: its plugin TaberuAnimation.esp is loaded (its keywords can't tell: KID makes them at
	// load from EAS's own ini, so they come from no plugin file)
	bool EatingAnimationsInstalled()
	{
		auto data = RE::TESDataHandler::GetSingleton();
		return data && (data->LookupLoadedModByName("TaberuAnimation.esp") || data->LookupLoadedLightModByName("TaberuAnimation.esp"));
	}

	Options GetOptions()
	{
		std::scoped_lock lock(g_lock);
		return g_options;
	}

	void SetOptions(const Options& a_options, bool a_save)
	{
		Options o = a_options;
		o.shareRangeMetres = std::clamp(o.shareRangeMetres, SHARE_RANGE_MIN, SHARE_RANGE_MAX);
		{
			std::scoped_lock lock(g_lock);
			g_options = o;
		}
		SetSkillsMenuShown(o.skillsMenu);  // takes effect at once (SkillsMenu.cpp); no-op when unchanged
		if (a_save) {
			SaveIni(o);
		}
	}

	bool FeatureSwitch(Feature a_feature)
	{
		const auto o = GetOptions();
		switch (a_feature) {
		case Feature::kHunger:
			return o.hunger;
		case Feature::kSpicyCold:
			return o.spicyCold;
		case Feature::kSpicyWater:
			return o.spicyWater;
		case Feature::kCampRest:
			return o.campRest;
		case Feature::kCampRestBonus:
			return o.campRest && o.campRestBonus;
		case Feature::kSteppedDrains:
			return o.steppedDrains;
		case Feature::kHeartyRegen:
			return o.heartyRegen;
		case Feature::kDragonWarmth:
			return o.dragonWarmth;
		case Feature::kThirst:
			return o.thirst;
		case Feature::kAfflictions:
			return o.afflictions;
		default:
			return true;
		}
	}

	const char* Tr(const char* a_en)
	{
		if (g_tr.empty() || !a_en) {
			return a_en;
		}
		const auto it = g_tr.find(a_en);
		return it != g_tr.end() ? it->second.c_str() : a_en;
	}

	bool HasTr(const std::string& a_en) { return g_tr.contains(a_en); }

	namespace
	{
		// one letter's capital: Latin (with the accented letters of Western, Central and Eastern European languages),
		// Greek and Cyrillic. Letters without a capital (Chinese, Japanese, Korean, digits, signs) stay as they are
		char32_t UpperOf(char32_t c)
		{
			if (c >= U'a' && c <= U'z') return c - 0x20;
			if (c < 0x80) return c;
			if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;  // à..þ (not ÷)
			if (c == 0xFF) return 0x178;                                  // ÿ
			if (c == 0x131) return U'I';  // dotless ı
			if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c & 1) ? c - 1 : c;  // Latin Extended-A pairs
			if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
			if (c >= 0x3B1 && c <= 0x3C9) return c == 0x3C2 ? 0x3A3 : c - 0x20;  // Greek (final sigma too)
			if (c == 0x3AC) return 0x386;
			if (c >= 0x3AD && c <= 0x3AF) return c - 0x25;
			if (c == 0x3CC) return 0x38C;
			if (c == 0x3CD || c == 0x3CE) return c - 0x3F;
			if (c >= 0x430 && c <= 0x44F) return c - 0x20;  // Cyrillic а..я
			if (c >= 0x450 && c <= 0x45F) return c - 0x50;  // ѐ..џ (ё, є, і, ї, ў...)
			return c;
		}
	}

	std::string UpperText(const std::string& a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		for (std::size_t i = 0; i < a_text.size();) {
			const auto b = static_cast<unsigned char>(a_text[i]);
			const std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
			if (n == 1 || i + n > a_text.size()) {  // a plain letter, or a broken byte kept as it is
				out += static_cast<char>(b < 0x80 ? static_cast<unsigned char>(UpperOf(b)) : b);
				++i;
				continue;
			}
			char32_t c = n == 2 ? (b & 0x1F) : n == 3 ? (b & 0x0F) : (b & 0x07);
			for (std::size_t k = 1; k < n; ++k) c = (c << 6) | (static_cast<unsigned char>(a_text[i + k]) & 0x3F);
			c = UpperOf(c);
			if (c < 0x80) {  // ı becomes a plain I
				out += static_cast<char>(c);
			} else if (c < 0x800) {
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else if (c < 0x10000) {
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			i += n;
		}
		return out;
	}

	std::string UpperFirst(const std::string& a_text)
	{
		if (a_text.empty()) return a_text;
		const auto b = static_cast<unsigned char>(a_text[0]);
		std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
		n = (std::min)(n, a_text.size());  // a broken last letter (a file saved in the wrong encoding): never past the end
		return UpperText(a_text.substr(0, n)) + a_text.substr(n);
	}
}
