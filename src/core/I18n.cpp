#include "core/I18n.h"
#include <atomic>
#include <string_view>
#include <unordered_map>

namespace {
std::atomic<int> gLang{kLangEnglish};

struct Entry {
    const char* en;
    const char* hu;
};
// a magyar fordítások (angol -> magyar)
const Entry kHungarian[] = {
#include "core/Lang_hu.inc"
};

const std::unordered_map<std::string_view, const char*>& hungarian() {
    static const std::unordered_map<std::string_view, const char*> m = [] {
        std::unordered_map<std::string_view, const char*> r;
        r.reserve(std::size(kHungarian));
        for (const Entry& e : kHungarian) r.emplace(e.en, e.hu);
        return r;
    }();
    return m;
}
}  // namespace

void setLanguage(int lang) { gLang = (lang >= 0 && lang < kLangCount) ? lang : kLangEnglish; }
int language() { return gLang; }

const char* T(const char* english) {
    if (gLang == kLangHungarian && english) {
        const auto& m = hungarian();
        auto it = m.find(english);
        if (it != m.end()) return it->second;
    }
    return english;
}

const char* languageName(int lang) {
    switch (lang) {
        case kLangHungarian: return "Magyar";
        default: return "English";
    }
}
