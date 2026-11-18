#include "Edits.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace FCPatch
{
	namespace
	{
		std::string Trim(std::string_view a_s)
		{
			std::size_t b = 0, e = a_s.size();
			while (b < e && (a_s[b] == ' ' || a_s[b] == '\t' || a_s[b] == '\r' || a_s[b] == '\n')) ++b;
			while (e > b && (a_s[e - 1] == ' ' || a_s[e - 1] == '\t' || a_s[e - 1] == '\r' || a_s[e - 1] == '\n')) --e;
			return std::string(a_s.substr(b, e - b));
		}

		std::vector<std::string> SplitOn(std::string_view a_s, char a_sep)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			for (std::size_t i = 0; i <= a_s.size(); ++i) {
				if (i == a_s.size() || a_s[i] == a_sep) {
					out.push_back(Trim(a_s.substr(start, i - start)));
					start = i + 1;
				}
			}
			return out;
		}

		std::string Upper(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			return s;
		}

		bool IsAuto(const std::string& a_s) { return a_s.empty() || Upper(a_s) == "AUTO"; }

		std::string TierName(const std::string& a_s)
		{
			const auto u = Upper(a_s);
			if (u == "SIMPLE") return "Simple";
			if (u == "PREPARED") return "Prepared";
			if (u == "ELABORATE") return "Elaborate";
			return {};
		}
	}

	const char* ChoiceName(DishChoice a_c)
	{
		switch (a_c) {
		case DishChoice::New: return "NEW";
		case DishChoice::Not: return "NOT";
		case DishChoice::Misc: return "MISC";
		default: return "AUTO";
		}
	}

	std::string ItemKey(std::string_view a_plugin, std::uint32_t a_localId)
	{
		std::string plugin(a_plugin);
		for (auto& c : plugin) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return std::format("{}|{:06X}", plugin, a_localId & 0xFFFFFF);
	}

	std::string NormalizeKey(std::string_view a_key)
	{
		// a general dish's own key on the Needs mods page ("f:Meat Stew": it has no item of its own; Needs.inl)
		if (const auto t = Trim(a_key); t.size() > 2 && t.starts_with("f:")) {
			return std::string(t);
		}
		const auto bar = a_key.rfind('|');
		if (bar == std::string_view::npos || bar == 0) return {};
		const auto plugin = Trim(a_key.substr(0, bar));
		auto       hex = Trim(a_key.substr(bar + 1));
		if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);  // 0x812
		if (plugin.empty() || hex.empty() || hex.size() > 8) return {};
		std::uint32_t id = 0;
		for (char c : hex) {
			int v = -1;
			if (c >= '0' && c <= '9') v = c - '0';
			else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
			if (v < 0) return {};
			id = (id << 4) | static_cast<std::uint32_t>(v);
		}
		if (hex.size() == 8 && (hex[0] == 'F' || hex[0] == 'f') && (hex[1] == 'E' || hex[1] == 'e')) id &= 0xFFF;  // FE001812: a light plugin's form
		return ItemKey(plugin, id);
	}

	std::size_t EditSet::Read(std::string_view a_text, std::size_t* a_bad)
	{
		if (a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB && static_cast<unsigned char>(a_text[2]) == 0xBF) {
			a_text.remove_prefix(3);  // byte order mark
		}
		std::size_t read = 0, bad = 0;
		for (const auto& raw : SplitOn(a_text, '\n')) {
			if (raw.empty() || raw[0] == '#') continue;
			const auto cols = SplitOn(raw, '\t');
			const auto kind = Upper(cols[0]);
			const auto key = cols.size() > 1 ? NormalizeKey(cols[1]) : std::string();
			if (key.empty() || (kind != "ING" && kind != "DISH" && kind != "MISC" && kind != "NEEDS")) {
				++bad;
				continue;
			}
			if (kind == "NEEDS") {
				const auto c = cols.size() > 2 ? Upper(cols[2]) : std::string();
				// AUTO: Automatic chosen on purpose, so a patch file's choice for the recipe doesn't come back on the next
				// start (Codex review 2026-11-15, L2)
				if (c != "NOTFOOD" && c != "LIGHT" && c != "MEDIUM" && c != "HEAVY" && c != "DRINK" && c != "AUTO") {
					++bad;
					continue;
				}
				needs[key] = c;
			} else if (kind == "MISC") {
				const auto c = cols.size() > 2 ? Upper(cols[2]) : std::string();
				if (c != "HIDE" && c != "SHOW") {
					++bad;
					continue;
				}
				misc[key] = c == "HIDE";
			} else if (kind == "ING") {
				IngredientEdit e;
				if (cols.size() > 2 && !IsAuto(cols[2])) e.group = Upper(cols[2]) == "OUT" ? "OUT" : cols[2];
				if (cols.size() > 3 && !IsAuto(cols[3])) {
					const int s = std::atoi(cols[3].c_str());
					if (s >= 1 && s <= 3) e.stars = s;
				}
				if (cols.size() > 4 && !IsAuto(cols[4])) e.flavour = Upper(cols[4]) == "NONE" ? "None" : cols[4];
				ingredients[key] = e;
			} else {
				DishEdit e;
				if (cols.size() > 2) {
					const auto c = Upper(cols[2]);
					e.choice = c == "NEW" ? DishChoice::New : c == "NOT" ? DishChoice::Not : c == "MISC" ? DishChoice::Misc : DishChoice::Auto;
				}
				if (cols.size() > 3) e.tier = TierName(cols[3]);
				if (cols.size() > 4 && !IsAuto(cols[4])) {
					e.slotsSet = true;
					for (const auto& s : SplitOn(cols[4], ';')) {
						if (e.slots.size() >= 3) break;
						if (s.size() > 5 && Upper(s.substr(0, 5)) == "ITEM:") {
							if (auto k = NormalizeKey(s.substr(5)); !k.empty()) e.slots.push_back({ true, k });
						} else if (s.size() > 6 && Upper(s.substr(0, 6)) == "GROUP:") {
							e.slots.push_back({ false, Trim(s.substr(6)) });
						}
					}
					if (e.slots.empty()) e.slotsSet = false;  // nothing readable: automatic
				}
				dishes[key] = e;
			}
			++read;
		}
		if (a_bad) *a_bad += bad;
		return read;
	}

	std::string EditSet::Write(std::string_view a_header) const
	{
		std::string out(a_header);
		for (const auto& [key, e] : ingredients) {
			out += std::format("ING\t{}\t{}\t{}{}\n", key, e.group.empty() ? "AUTO" : e.group, e.stars ? std::to_string(e.stars) : "AUTO",
				e.flavour.empty() ? std::string() : "\t" + e.flavour);
		}
		for (const auto& [key, e] : dishes) {
			std::string slots = "AUTO";
			if (e.slotsSet) {
				slots.clear();
				for (const auto& s : e.slots) slots += (slots.empty() ? "" : ";") + std::string(s.item ? "item:" : "group:") + s.text;
			}
			out += std::format("DISH\t{}\t{}\t{}\t{}\n", key, ChoiceName(e.choice), e.tier.empty() ? "AUTO" : e.tier, slots);
		}
		for (const auto& [key, hidden] : misc) {
			out += std::format("MISC\t{}\t{}\n", key, hidden ? "HIDE" : "SHOW");
		}
		for (const auto& [key, choice] : needs) {
			out += std::format("NEEDS\t{}\t{}\n", key, choice);
		}
		return out;
	}

	void EditSet::Layer(const EditSet& a_top)
	{
		for (const auto& [key, e] : a_top.ingredients) {
			auto& mine = ingredients[key];
			if (!e.group.empty()) mine.group = e.group;
			if (e.stars) mine.stars = e.stars;
			if (!e.flavour.empty()) mine.flavour = e.flavour;
		}
		for (const auto& [key, e] : a_top.dishes) {
			auto& mine = dishes[key];
			if (e.choice != DishChoice::Auto) mine.choice = e.choice;
			if (!e.tier.empty()) mine.tier = e.tier;
			if (e.slotsSet) {
				mine.slotsSet = true;
				mine.slots = e.slots;
			}
		}
		for (const auto& [key, hidden] : a_top.misc) misc[key] = hidden;
		for (const auto& [key, choice] : a_top.needs) needs[key] = choice;
	}

	void EditSet::Replace(const EditSet& a_top)
	{
		for (const auto& [key, e] : a_top.ingredients) ingredients[key] = e;
		for (const auto& [key, e] : a_top.dishes) dishes[key] = e;
		for (const auto& [key, hidden] : a_top.misc) misc[key] = hidden;
		for (const auto& [key, choice] : a_top.needs) needs[key] = choice;
	}

	void EditSet::Clean(const std::vector<std::string>* a_keep)
	{
		const auto keep = [&](const std::string& a_key) { return a_keep && std::ranges::find(*a_keep, a_key) != a_keep->end(); };
		std::erase_if(ingredients, [&](const auto& p) { return p.second.Empty() && !keep(p.first); });
		std::erase_if(dishes, [&](const auto& p) { return p.second.Empty() && !keep(p.first); });
	}
}
