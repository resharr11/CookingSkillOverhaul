#include "Classifier.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace FCPatch
{
	namespace
	{
		bool IsAsciiAlnum(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

		// a 3-byte UTF-8 character from the CJK / Hangul ranges (lead bytes E3..ED): these scripts have no spaces
		bool HasNoSpaceScript(std::string_view a_text)
		{
			for (unsigned char c : a_text) {
				if (c >= 0xE3 && c <= 0xED) {
					return true;
				}
			}
			return false;
		}

		// plural and possessive endings are ignored: "berries" = "berry", "salts" = "salt", "dragon's" = "dragon"
		std::string Stem(std::string a_word)
		{
			auto ends = [&](std::string_view s) { return a_word.size() > s.size() && a_word.compare(a_word.size() - s.size(), s.size(), s) == 0; };
			if (a_word.size() > 4 && ends("oes")) {  // "potatoes", "tomatoes"
				a_word.resize(a_word.size() - 2);
			} else if (a_word.size() > 4 && ends("ies")) {
				a_word.replace(a_word.size() - 3, 3, "y");
			} else if (a_word.size() > 4 && ends("ie")) {  // "cookie" and "cookies" both become "cooky"
				a_word.replace(a_word.size() - 2, 2, "y");
			} else if (a_word.size() > 4 && (ends("ches") || ends("shes") || ends("xes") || ends("sses"))) {
				a_word.resize(a_word.size() - 2);
			} else if (a_word.size() > 3 && ends("s") && !ends("ss") && !ends("us")) {
				a_word.pop_back();
			}
			return a_word;
		}

		std::string Trim(std::string_view a_s)
		{
			std::size_t b = 0, e = a_s.size();
			while (b < e && (a_s[b] == ' ' || a_s[b] == '\t' || a_s[b] == '\r')) ++b;
			while (e > b && (a_s[e - 1] == ' ' || a_s[e - 1] == '\t' || a_s[e - 1] == '\r')) --e;
			return std::string(a_s.substr(b, e - b));
		}

		std::vector<std::string> Split(std::string_view a_s, char a_sep)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			for (std::size_t i = 0; i <= a_s.size(); ++i) {
				if (i == a_s.size() || a_s[i] == a_sep) {
					out.push_back(std::string(a_s.substr(start, i - start)));
					start = i + 1;
				}
			}
			return out;
		}

		struct Text
		{
			std::vector<std::string> tokens;  // stemmed words
			std::string              lower;   // whole text, lower case (for scripts without spaces)
		};

		Text MakeText(std::string_view a_s, bool a_isId)
		{
			Text t;
			t.lower = Lower(a_s);
			for (auto& w : Tokens(a_s, a_isId)) {
				t.tokens.push_back(Stem(w));
			}
			return t;
		}
	}

	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		for (auto& c : out) {
			if (c >= 'A' && c <= 'Z') {
				c = static_cast<char>(c - 'A' + 'a');
			}
		}
		return out;
	}

	// words of a name: runs of letters and digits (any non-ASCII byte counts as a letter, so accented and Cyrillic words stay whole);
	// apostrophes are dropped ("Dragon's" -> "dragons"); editor IDs are also split at camelCase and letter/digit changes
	std::vector<std::string> Tokens(std::string_view a_text, bool a_splitCamel)
	{
		std::vector<std::string> out;
		std::string              cur;
		auto                     flush = [&] {
            if (!cur.empty()) {
                out.push_back(Lower(cur));
                cur.clear();
            }
		};
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const unsigned char c = static_cast<unsigned char>(a_text[i]);
			if (c == '\'' || (c == 0xE2 && i + 2 < a_text.size() && static_cast<unsigned char>(a_text[i + 1]) == 0x80 && static_cast<unsigned char>(a_text[i + 2]) == 0x99)) {
				if (c == 0xE2) i += 2;  // typographic apostrophe
				continue;
			}
			const bool letter = IsAsciiAlnum(c) || c >= 0x80;
			if (!letter) {
				flush();
				continue;
			}
			if (a_splitCamel && !cur.empty() && c < 0x80) {
				const unsigned char p = static_cast<unsigned char>(cur.back());
				const bool          lowerToUpper = (p >= 'a' && p <= 'z') && (c >= 'A' && c <= 'Z');
				const bool          digitChange = ((p >= '0' && p <= '9') != (c >= '0' && c <= '9')) && p < 0x80;
				if (lowerToUpper || digitChange) {
					flush();
				}
			}
			cur.push_back(static_cast<char>(c));
		}
		flush();
		return out;
	}

	// "ITMPotionUpSD", "Filler01": names that are really editor IDs (no space, camelCase or digits)
	bool LooksLikeId(std::string_view a_name)
	{
		if (a_name.find(' ') != std::string_view::npos || HasNoSpaceScript(a_name)) {
			return false;
		}
		for (std::size_t i = 1; i < a_name.size(); ++i) {
			const unsigned char p = static_cast<unsigned char>(a_name[i - 1]), c = static_cast<unsigned char>(a_name[i]);
			if ((p >= 'a' && p <= 'z' && c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
				return true;
			}
		}
		return false;
	}

	Restriction GroupRestriction(std::string_view a_group)
	{
		// measured on our own table (2026-11-12): the groups whose ingredients mostly need alcohol
		static const char* gross[] = { "Claws, teeth & horns", "Eyes, ears & tongues", "Fat & flesh", "Feathers & wings", "Hides, scales & shells",
			"Insects & eggs", "Magical remains", "Roots, bark & wild plants" };
		if (a_group == "Poisonous plants") return Restriction::Poisonous;
		if (a_group == "Human & mer remains") return Restriction::Namira;
		if (a_group == "Dragon parts") return Restriction::Dragon;
		for (auto* g : gross) {
			if (a_group == g) return Restriction::Gross;
		}
		return Restriction::None;
	}

	const char* RestrictionName(Restriction a_r)
	{
		switch (a_r) {
		case Restriction::Gross: return "gross";
		case Restriction::Poisonous: return "poisonous";
		case Restriction::Namira: return "Namira";
		case Restriction::Dragon: return "dragon";
		default: return "-";
		}
	}

	bool Words::LoadFile(const std::string& a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		if (!f) {
			return false;
		}
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadText(ss.str());
	}

	bool Words::LoadText(std::string_view a_text)
	{
		if (a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB && static_cast<unsigned char>(a_text[2]) == 0xBF) {
			a_text.remove_prefix(3);  // UTF-8 byte order mark
		}
		auto phrases = [](const std::string& a_list) {
			std::vector<Phrase> out;
			for (auto& w : Split(a_list, ',')) {
				std::string word = Trim(w);
				Phrase      p;
				if (!word.empty() && word[0] == '~') {
					p.generic = true;
					word = Trim(std::string_view(word).substr(1));
				}
				if (word.empty()) continue;
				p.raw = Lower(word);
				p.cjk = HasNoSpaceScript(word);
				for (auto& t : Tokens(word, false)) p.tokens.push_back(Stem(t));
				out.push_back(std::move(p));
			}
			return out;
		};
		for (auto& rawLine : Split(a_text, '\n')) {
			const std::string line = Trim(rawLine);
			if (line.empty() || line[0] == '#') continue;
			auto cols = Split(line, '\t');
			for (auto& c : cols) c = Trim(c);
			const std::string& kind = cols[0];
			auto add = [&](std::vector<Entry>& a_list, const std::string& a_target, const std::string& a_words) {
				for (auto& p : phrases(a_words)) a_list.push_back({ std::move(p), a_target });
			};
			if (kind == "GROUP" && cols.size() >= 3) add(_groups, cols[1], cols[2]);
			else if (kind == "DISH" && cols.size() >= 3) add(_dish, cols[1], cols[2]);
			else if (kind == "SKIP" && cols.size() >= 2) add(_skip, "", cols[1]);
			else if (kind == "MISC" && cols.size() >= 2) add(_misc, "", cols[1]);
			else if (kind == "CUT" && cols.size() >= 2) add(_cut, "", cols[1]);
			else if (kind == "UNCOOKED" && cols.size() >= 2) add(_uncooked, "", cols[1]);
			else if (kind == "BAKED" && cols.size() >= 2) add(_bakedEnd, "", cols[1]);
			else if (kind == "BAKING" && cols.size() >= 2) add(_bakedAny, "", cols[1]);
			else if (kind == "SERVING" && cols.size() >= 2) add(_serving, "", cols[1]);
			else if (kind == "NOTBAKED" && cols.size() >= 2) add(_notBaked, "", cols[1]);
			else if (kind == "DOUGH" && cols.size() >= 2) add(_doughEnd, "", cols[1]);
			else if (kind == "DOUGHANY" && cols.size() >= 2) add(_doughAny, "", cols[1]);
			else if (kind == "SHORTDROP" && cols.size() >= 2) add(_shortDrop, "", cols[1]);
			else if (kind == "PRESERVED" && cols.size() >= 2) add(_preserved, "", cols[1]);
			else if (kind == "PLAIN" && cols.size() >= 2) add(_plain, "", cols[1]);
			else if (kind == "VARIANT" && cols.size() >= 2) add(_variant, "", cols[1]);
			else if (kind == "NAMIRA" && cols.size() >= 2) add(_namira, "", cols[1]);
			else if (kind == "PEOPLE" && cols.size() >= 2) add(_people, "", cols[1]);
			else if (kind == "FLAVOUR" && cols.size() >= 2) add(_flavour, "", cols[1]);
			else if (kind == "SALT" && cols.size() >= 2) add(_salt, "", cols[1]);
			else if (kind == "JOIN" && cols.size() >= 2) add(_join, "", cols[1]);
			else if (kind == "COMBINE" && cols.size() >= 2) add(_combine, "", cols[1]);
			else if (kind == "NAMIRA_SAFE_END" && cols.size() >= 2) add(_safeEnd, "", cols[1]);
			else if (kind == "DRAGON_BODY" && cols.size() >= 2) add(_dragonBody, "", cols[1]);
			else if (kind == "POTION" && cols.size() >= 3) add(_potion, cols[1], cols[2]);  // POTION <flavour> <words>
			else if (kind == "KEYWORD" && cols.size() >= 3) _keywords.emplace_back(Lower(cols[1]), cols[2]);
			else if (kind == "CATEGORY" && cols.size() >= 3) {
				// CATEGORY <id> <groups> ; <words>
				auto& c = _categories[Lower(cols[1])];
				const auto semi = cols[2].find(';');
				for (auto& g : Split(cols[2].substr(0, semi), ',')) {
					if (auto t = Trim(g); !t.empty()) c.groups.push_back(t);
				}
				if (semi != std::string::npos) add(c.words, "", cols[2].substr(semi + 1));
			} else if (kind == "FLAVOURWORD" && cols.size() >= 4) {
				// FLAVOURWORD <group> <flavour> <words>
				for (auto& p : phrases(cols[3])) _flavourWords.push_back({ cols[1], Entry{ std::move(p), cols[2] }, true });
			} else if (kind == "SPECIES" && cols.size() >= 4) {
				// SPECIES <groups, split by commas> <flavour> <words>: one animal's own flavour in whichever of these groups
				// it lands (slaughterfish lives in rivers and the sea); not offered as a choice for the group's other items
				for (auto& g : Split(cols[1], ',')) {
					if (auto t = Trim(g); !t.empty()) {
						for (auto& p : phrases(cols[3])) _flavourWords.push_back({ t, Entry{ p, cols[2] }, false });  // a copy per group
					}
				}
			}
		}
		return true;
	}

	namespace
	{
		// where the phrase occurs in the text; returns false if it doesn't. a_end = position just after the match
		bool Find(const Words::Phrase& a_p, const Text& a_t, std::size_t& a_end)
		{
			if (a_p.cjk) {
				const auto at = a_t.lower.rfind(a_p.raw);
				if (at == std::string::npos) return false;
				a_end = at + a_p.raw.size();
				return true;
			}
			if (a_p.tokens.empty() || a_p.tokens.size() > a_t.tokens.size()) return false;
			for (std::size_t i = a_t.tokens.size() - a_p.tokens.size() + 1; i-- > 0;) {  // from the end: the last occurrence
				if (std::equal(a_p.tokens.begin(), a_p.tokens.end(), a_t.tokens.begin() + i)) {
					a_end = i + a_p.tokens.size();
					return true;
				}
			}
			return false;
		}
	}

	const Words::Entry* Words::Best(const std::vector<Entry>& a_list, const std::string& a_text, bool a_isId) const
	{
		const Text t = MakeText(a_text, a_isId);
		// "Eye of Fox", "Mead with Juniper Berry": the words before the first joining word name the thing
		std::size_t joinAt = t.tokens.size();
		for (std::size_t i = 1; i < t.tokens.size(); ++i) {
			for (auto& j : _join) {
				if (!j.phrase.cjk && j.phrase.tokens.size() == 1 && j.phrase.tokens[0] == t.tokens[i] && i < joinAt) {
					joinAt = i;
				}
			}
		}
		const Entry* best = nullptr;
		std::size_t  bestEnd = 0, bestLen = 0;
		bool         bestHead = false;
		for (auto& e : a_list) {
			std::size_t end = 0;
			if (!Find(e.phrase, t, end)) continue;
			const std::size_t len = e.phrase.cjk ? e.phrase.raw.size() : e.phrase.tokens.size();
			const bool        head = e.phrase.cjk || end <= joinAt;  // inside the naming part
			bool              better = false;
			if (!best) better = true;
			else if (best->phrase.generic != e.phrase.generic) better = best->phrase.generic;  // a specific word beats a generic one
			else if (head != bestHead) better = head;                                         // then words before a joining word
			else if (end != bestEnd) better = end > bestEnd;                                  // then the word nearest the end
			else better = len > bestLen;                                                      // then the longer phrase
			if (better) {
				best = &e;
				bestEnd = end;
				bestLen = len;
				bestHead = head;
			}
		}
		return best;
	}

	std::string Words::PotionWord(const std::string& a_name, const std::vector<std::string>& a_allowed) const
	{
		std::vector<Entry> list;
		for (const auto& e : _potion) {
			for (const auto& f : a_allowed) {
				if (Lower(e.target) == Lower(f)) {
					list.push_back(e);
					list.back().target = f;  // the flavour as the caller writes it
					break;
				}
			}
		}
		const auto* best = list.empty() ? nullptr : Best(list, a_name, false);
		return best ? best->target : std::string();
	}

	bool Words::Any(const std::vector<Entry>& a_list, const std::string& a_text, bool a_isId) const
	{
		return Best(a_list, a_text, a_isId) != nullptr;
	}

	Result Words::Classify(const Input& a_in) const
	{
		Result r;
		auto   skip = [&](const char* a_why) {
            r.kind = Kind::Skip;
            r.why = a_why;
            return r;
		};
		if (a_in.isPoison) return skip("poison");
		if (!a_in.playable) return skip("not playable");

		// 1. keywords other mods put on the item
		std::string kwGroup;
		for (auto& k : a_in.keywords) {
			const std::string lk = Lower(k);
			for (auto& [id, target] : _keywords) {
				if (id != lk) continue;
				if (target == "SKIP") return skip("keyword says not a food");
				if (target == "DISH") {
					if (!a_in.isIngredient) {
						r.kind = Kind::Dish;
						r.why = "keyword " + k + " (a finished dish)";
					}
				} else if (kwGroup.empty()) {
					kwGroup = target;
				}
			}
		}

		// 2. the text to read: the display name; the editor ID only when the name gives no word at all
		std::string text = (a_in.name.empty() || LooksLikeId(a_in.name)) ? std::string() : a_in.name;
		bool        isId = false;
		const bool  nameHasWord = !text.empty() && (Any(_groups, text, false) || Any(_dish, text, false) || Any(_skip, text, false));
		if (!nameHasWord && !a_in.editorId.empty()) {
			text = a_in.editorId;
			isId = true;
		}
		if (text.empty()) return skip("no name");

		if (Any(_skip, text, isId)) return skip(isId ? "editor ID says test or spoiled" : "spoiled, test or placeholder name");
		if (Any(_misc, text, isId)) {
			r.kind = Kind::Misc;
			r.why = "special case (a Misc word in the name)";
			return r;
		}

		// 3. finished dishes (food items only)
		if (!a_in.isIngredient) {
			if (const Entry* d = Best(_dish, text, isId)) {
				r.kind = Kind::Dish;
				r.dishTier = d->target;
				if (r.why.empty()) r.why = "dish word \"" + d->phrase.raw + "\"";
			}
			// two foods joined by a COMBINE word ("Goat and Potatoes", "Milk and Honey Tea", "Mead with Juniper Berry"): a
			// finished dish or drink, never a pot ingredient (his "all fix sound good", 2026-11-15). Languages with spaces
			// only; each side must name a food of its own.
			if (r.kind != Kind::Dish && !isId && !_combine.empty()) {
				const auto words = Split(text, ' ');
				for (std::size_t i = 1; i + 1 < words.size() && r.kind != Kind::Dish; ++i) {
					const auto w = Lower(Trim(words[i]));
					const bool joins = std::any_of(_combine.begin(), _combine.end(), [&](const Entry& e) { return e.phrase.raw == w; });
					if (!joins) continue;
					std::string left, right;
					for (std::size_t j = 0; j < words.size(); ++j) {
						if (j == i) continue;
						auto& side = j < i ? left : right;
						side += (side.empty() ? "" : " ") + words[j];
					}
					if (!GroupOf(left).empty() && !GroupOf(right).empty()) {
						r.kind = Kind::Dish;
						r.dishTier = "Prepared";
						if (r.why.empty()) r.why = "two foods joined by \"" + w + "\"";
					}
				}
			}
			if (r.kind == Kind::Dish) return r;
		}

		// 4. the group
		const Entry* g = Best(_groups, text, isId);
		if (!kwGroup.empty()) {
			r.group = kwGroup;
			r.why = "keyword";
		} else if (g) {
			r.group = g->target;
			r.why = std::string(isId ? "editor ID word \"" : "word \"") + g->phrase.raw + "\"";
		} else {
			r.kind = Kind::Unknown;
			r.why = "no known word";
			return r;
		}
		r.kind = Kind::Ingredient;

		// 5. restrictions: dragon body part, then Namira, then the group's own rule
		const Text t = MakeText(text, isId);
		auto       EndsSafe = [&](const Text& a_t) {
            if (a_t.tokens.empty()) return false;
            for (auto& e : _safeEnd) {
                if (!e.phrase.cjk && e.phrase.tokens.size() == 1 && e.phrase.tokens[0] == a_t.tokens.back()) return true;
            }
            return false;
		};
		const bool hasDragon = std::find(t.tokens.begin(), t.tokens.end(), "dragon") != t.tokens.end();
		if (hasDragon && Any(_dragonBody, text, isId)) {
			r.group = "Dragon parts";
			r.restriction = Restriction::Dragon;
			r.why += ", dragon + body part";
			return r;
		}
		bool namira = r.group == "Human & mer remains";
		static const char* bodyGroups[] = { "Fat & flesh", "Eyes, ears & tongues", "Claws, teeth & horns", "Hides, scales & shells", "Magical remains" };
		const bool         bodyPart = std::any_of(std::begin(bodyGroups), std::end(bodyGroups), [&](const char* g) { return r.group == g; });
		if (!namira && bodyPart && Any(_people, text, isId) && !EndsSafe(t)) {
			r.group = "Human & mer remains";  // "Blood of a Nord", "Elven Heart": a person's remains
			namira = true;
		}
		if (!namira && Any(_namira, text, isId)) {
			// "Imp Stool", "Void Salts", "Vampire Fish": a plant, mineral or fish named after a creature
			namira = !EndsSafe(t);
		}
		if (namira) {
			r.restriction = Restriction::Namira;
			r.why += ", Namira word";
		} else if (!a_in.isIngredient && GroupRestriction(r.group) == Restriction::Gross) {
			r.restriction = Restriction::None;  // food items are never gross (55/55 on our table)
		} else {
			r.restriction = GroupRestriction(r.group);
		}
		return r;
	}

	std::string Words::GroupOf(const std::string& a_name) const
	{
		const Entry* g = Best(_groups, a_name, false);
		return g ? g->target : std::string();
	}

	std::string Words::KeyPhrase(const std::string& a_name) const
	{
		const Entry* g = Best(_groups, a_name, false);
		return g ? g->phrase.raw : std::string();
	}

	bool Words::Mentions(const std::string& a_text, const std::string& a_phrase) const
	{
		if (a_phrase.empty()) return false;
		Phrase p;
		p.raw = Lower(a_phrase);
		p.cjk = HasNoSpaceScript(a_phrase);
		for (auto& t : Tokens(a_phrase, false)) p.tokens.push_back(Stem(t));
		std::size_t end = 0;
		return Find(p, MakeText(a_text, false), end);
	}

	std::string Words::DishTier(const std::string& a_name) const
	{
		const Entry* d = Best(_dish, a_name, false);
		return d ? d->target : std::string();
	}

	namespace
	{
		bool FirstTokenIn(const std::vector<std::string>& a_tokens, std::size_t a_at, const std::vector<Words::Phrase>& a_list)
		{
			for (auto& p : a_list) {
				if (!p.cjk && p.tokens.size() == 1 && a_at < a_tokens.size() && p.tokens[0] == a_tokens[a_at]) return true;
			}
			return false;
		}
	}

	std::string Words::BaseKey(const std::string& a_name) const
	{
		const Text          t = MakeText(a_name, false);
		std::vector<Phrase> lead;
		for (auto& e : _variant) lead.push_back(e.phrase);
		for (auto& e : _flavour) lead.push_back(e.phrase);
		std::size_t i = 0;
		while (i + 1 < t.tokens.size() && FirstTokenIn(t.tokens, i, lead)) ++i;
		std::string key;
		for (; i < t.tokens.size(); ++i) {
			if (!key.empty()) key += ' ';
			key += t.tokens[i];
		}
		return key.empty() ? t.lower : key;
	}

	std::string Words::WithoutFlavour(const std::string& a_name) const
	{
		const auto sp = a_name.find(' ');
		if (sp == std::string::npos) return a_name;
		const Text first = MakeText(a_name.substr(0, sp), false);
		std::vector<Phrase> fl;
		for (auto& e : _flavour) fl.push_back(e.phrase);
		if (first.tokens.size() == 1 && FirstTokenIn(first.tokens, 0, fl)) return a_name.substr(sp + 1);
		return a_name;
	}

	bool Words::NamesItem(const std::string& a_dishName, const std::string& a_itemName) const
	{
		if (a_itemName.empty()) return false;
		if (const auto key = KeyPhrase(a_itemName); !key.empty() && Mentions(a_dishName, key)) return true;
		if (HasNoSpaceScript(a_itemName)) return false;  // Chinese, Japanese: only the key phrase above
		const Text dish = MakeText(a_dishName, false);
		for (const auto& raw : Tokens(a_itemName, false)) {
			const std::string t = Stem(raw);
			if (t.size() < 3) continue;
			bool plain = false;
			for (const auto* list : { &_plain, &_join, &_salt, &_dish, &_variant, &_flavour }) {
				for (const auto& e : *list) plain = plain || (e.phrase.tokens.size() == 1 && e.phrase.tokens[0] == t);
			}
			for (const auto& e : _groups) plain = plain || (e.phrase.generic && e.phrase.tokens.size() == 1 && e.phrase.tokens[0] == t);
			if (plain) continue;
			for (const auto& d : dish.tokens) {
				if (d == t) return true;
			}
		}
		return false;
	}

	bool Words::IsMisc(const std::string& a_name) const
	{
		return Any(_misc, a_name, false);
	}

	std::vector<std::string> Words::DistinctWords(const std::string& a_name) const
	{
		std::vector<std::string> out;
		for (const auto& raw : Tokens(a_name, false)) {
			const std::string t = Stem(raw);
			if (t.size() < 3) continue;
			bool plain = false;
			for (const auto* list : { &_plain, &_join, &_salt, &_dish, &_variant, &_flavour }) {
				for (const auto& e : *list) plain = plain || (e.phrase.tokens.size() == 1 && e.phrase.tokens[0] == t);
			}
			for (const auto& e : _groups) plain = plain || (e.phrase.generic && e.phrase.tokens.size() == 1 && e.phrase.tokens[0] == t);
			if (!plain && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
		}
		return out;
	}

	bool Words::IsCookedCut(const std::string& a_name) const
	{
		return Any(_cut, a_name, false) && !Any(_uncooked, a_name, false);
	}

	// a dish name's last word is one of a_end ("Walnut Bread"), also at the end of a longer word ("cornbread": at least two
	// letters before it, so "troll" isn't a roll), not counting serving words after it ("Bread Roll Basket"); a_except
	// words never count ("Potato Pancake")
	bool Words::EndsIn(const std::vector<Entry>& a_end, const std::vector<Entry>* a_except, const std::string& a_name) const
	{
		Text t = MakeText(a_name, false);
		for (bool cut = true; cut && t.tokens.size() > 1;) {
			cut = false;
			for (auto& e : _serving) {
				if (!e.phrase.cjk && e.phrase.tokens.size() == 1 && t.tokens.back() == e.phrase.tokens.front()) {
					t.tokens.pop_back();
					cut = true;
					break;
				}
			}
		}
		if (t.tokens.empty()) return false;
		const auto& last = t.tokens.back();
		if (a_except) {
			for (auto& e : *a_except) {
				if (!e.phrase.cjk && e.phrase.tokens.size() == 1 && e.phrase.tokens.front() == last) return false;
			}
		}
		for (auto& e : a_end) {
			std::size_t end = 0;
			if (Find(e.phrase, t, end) && (e.phrase.cjk || end == t.tokens.size())) return true;  // "Bread Soup" is a soup
			if (!e.phrase.cjk && e.phrase.tokens.size() == 1) {
				const auto& w = e.phrase.tokens.front();
				if (last.size() >= w.size() + 2 && last.compare(last.size() - w.size(), w.size(), w) == 0) return true;
			}
		}
		return false;
	}

	std::string Words::ShortName(const std::string& a_name) const
	{
		// split on spaces, keep each word as written unless its stem is a SHORTDROP word
		std::string out, word;
		auto flush = [&] {
			if (word.empty()) return;
			std::string core;  // the word without punctuation, for the check
			for (char c : word) {
				if (std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80) core += c;
			}
			bool drop = core.empty();
			const auto toks = Tokens(core, false);
			if (toks.size() == 1) {
				const auto t = Stem(toks.front());
				for (auto& e : _shortDrop) drop = drop || (!e.phrase.cjk && e.phrase.tokens.size() == 1 && e.phrase.tokens.front() == t);
			}
			if (!drop) out += (out.empty() ? "" : " ") + word;
			word.clear();
		};
		for (char c : a_name) {
			if (c == ' ') flush();
			else word += c;
		}
		flush();
		// one of a kind in a dish name: "Snowberries" -> "Snowberry", "Grapes" -> "Grape" (plain ASCII words only)
		if (const auto sp = out.rfind(' ') + 1; out.size() - sp > 3 && std::ranges::all_of(out.substr(sp), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); })) {
			const auto last = out.substr(sp);
			if (last.size() > 4 && last.ends_with("ies")) out = out.substr(0, out.size() - 3) + "y";
			else if (last.ends_with("s") && !last.ends_with("ss") && !last.ends_with("us")) out.pop_back();
		}
		return out;
	}

	bool Words::IsBaked(const std::string& a_name) const
	{
		return Any(_bakedAny, a_name, false) || EndsIn(_bakedEnd, &_notBaked, a_name);
	}

	bool Words::IsDough(const std::string& a_name) const
	{
		return Any(_doughAny, a_name, false) || EndsIn(_doughEnd, nullptr, a_name);
	}

	bool Words::IsSalt(const std::string& a_name) const
	{
		const Entry* g = Best(_salt, a_name, false);
		if (!g) return false;
		// "Fire Salts", "Void Salts" are ingredients of their own groups, not plain salt
		const Entry* grp = Best(_groups, a_name, false);
		return !grp || grp->target == "Staples";
	}

	bool Words::InCategory(const std::string& a_category, const std::string& a_group, const std::string& a_name) const
	{
		const auto it = _categories.find(Lower(a_category));
		if (it == _categories.end()) return false;
		if (std::find(it->second.groups.begin(), it->second.groups.end(), a_group) != it->second.groups.end()) return true;
		const Text t = MakeText(a_name, false);
		for (auto& e : it->second.words) {
			std::size_t end = 0;
			if (Find(e.phrase, t, end)) return true;
		}
		return false;
	}

	std::vector<std::string> Words::FlavourWordsOf(const std::string& a_group) const
	{
		std::vector<std::string> out;
		for (auto& w : _flavourWords) {
			if (w.option && w.group == a_group && std::find(out.begin(), out.end(), w.entry.target) == out.end()) out.push_back(w.entry.target);
		}
		return out;
	}

	std::string Words::FlavourWord(const std::string& a_group, const std::string& a_name) const
	{
		const Text t = MakeText(a_name, false);
		for (auto& w : _flavourWords) {
			std::size_t end = 0;
			if (w.group == a_group && Find(w.entry.phrase, t, end)) return w.entry.target;
		}
		return {};
	}
}
