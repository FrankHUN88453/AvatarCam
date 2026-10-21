#include "import/UnityYaml.h"
#include <cstdlib>

namespace {

struct Line {
    int indent;
    std::string text;  // bal oldali szóközök nélkül, sorvégi \r nélkül
};

struct Parser {
    std::vector<Line> lines;
    size_t i = 0;

    explicit Parser(std::string_view src) {
        size_t start = 0;
        while (start <= src.size()) {
            size_t end = src.find('\n', start);
            if (end == std::string_view::npos) end = src.size();
            std::string_view l = src.substr(start, end - start);
            if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
            size_t ind = 0;
            while (ind < l.size() && l[ind] == ' ') ind++;
            std::string_view rest = l.substr(ind);
            if (!rest.empty() && rest[0] != '#') lines.push_back({(int)ind, std::string(rest)});
            if (end == src.size()) break;
            start = end + 1;
        }
    }

    static bool isSeqItem(const std::string& t) { return t == "-" || startsWith(t, "- "); }

    // "kulcs: érték" szétválasztása; -1 ha nem kulcs
    static long findKeySep(const std::string& t) {
        char q = 0;
        int depth = 0;
        for (size_t k = 0; k < t.size(); k++) {
            char c = t[k];
            if (q) {
                if (c == q) q = 0;
                continue;
            }
            if (k == 0 && (c == '\'' || c == '"')) {
                q = c;
                continue;
            }
            if (c == '{' || c == '[') {
                if (k == 0) return -1;
                depth++;
            } else if (c == '}' || c == ']')
                depth--;
            else if (c == ':' && depth == 0 && (k + 1 == t.size() || t[k + 1] == ' ')) return (long)k;
        }
        return -1;
    }

    static std::string unquoteKey(const std::string& k) {
        if (k.size() >= 2 && (k[0] == '\'' || k[0] == '"') && k.back() == k[0]) return k.substr(1, k.size() - 2);
        return k;
    }

    // --- flow ({...} / [...]) ---
    struct Flow {
        const std::string& s;
        size_t p = 0;
        void ws() {
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n')) p++;
        }
        json value() {
            ws();
            if (p >= s.size()) return "";
            if (s[p] == '{') return map();
            if (s[p] == '[') return seq();
            if (s[p] == '\'' || s[p] == '"') return quoted();
            size_t st = p;
            while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']') p++;
            return trim(std::string_view(s).substr(st, p - st));
        }
        std::string quoted() {
            char q = s[p++];
            std::string out;
            while (p < s.size()) {
                char c = s[p++];
                if (c == q) {
                    if (q == '\'' && p < s.size() && s[p] == '\'') {
                        out += '\'';
                        p++;
                        continue;
                    }
                    break;
                }
                if (q == '"' && c == '\\' && p < s.size()) {
                    char e = s[p++];
                    out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                    continue;
                }
                out += c;
            }
            return out;
        }
        json map() {
            json o = json::object();
            p++;  // {
            for (;;) {
                ws();
                if (p >= s.size()) break;
                if (s[p] == '}') {
                    p++;
                    break;
                }
                std::string key;
                if (s[p] == '\'' || s[p] == '"')
                    key = quoted();
                else {
                    size_t st = p;
                    while (p < s.size() && s[p] != ':' && s[p] != ',' && s[p] != '}') p++;
                    key = trim(std::string_view(s).substr(st, p - st));
                }
                ws();
                json v = "";
                if (p < s.size() && s[p] == ':') {
                    p++;
                    v = value();
                }
                o[key] = std::move(v);
                ws();
                if (p < s.size() && s[p] == ',') p++;
            }
            return o;
        }
        json seq() {
            json a = json::array();
            p++;  // [
            for (;;) {
                ws();
                if (p >= s.size()) break;
                if (s[p] == ']') {
                    p++;
                    break;
                }
                a.push_back(value());
                ws();
                if (p < s.size() && s[p] == ',') p++;
            }
            return a;
        }
    };

    static int bracketBalance(const std::string& t) {
        int b = 0;
        char q = 0;
        for (char c : t) {
            if (q) {
                if (c == q) q = 0;
                continue;
            }
            if (c == '\'' || c == '"') q = c;
            else if (c == '{' || c == '[') b++;
            else if (c == '}' || c == ']') b--;
        }
        return b;
    }

    // Skalár vagy flow érték olvasása az aktuális sor "rest" részéből (folytatósorokkal)
    json scalarValue(std::string rest, int ownerIndent) {
        if (!rest.empty() && (rest[0] == '{' || rest[0] == '[')) {
            int bal = bracketBalance(rest);
            while (bal > 0 && i < lines.size()) {
                rest += ' ';
                rest += lines[i].text;
                bal += bracketBalance(lines[i].text);
                i++;
            }
            Flow f{rest};
            return f.value();
        }
        if (!rest.empty() && (rest[0] == '|' || rest[0] == '>')) {
            bool literal = rest[0] == '|';
            std::string out;
            while (i < lines.size() && lines[i].indent > ownerIndent) {
                if (!out.empty()) out += literal ? '\n' : ' ';
                out += lines[i].text;
                i++;
            }
            return out;
        }
        if (!rest.empty() && (rest[0] == '\'' || rest[0] == '"')) {
            char q = rest[0];
            // folytatás, amíg a nyitó idézőjel nincs lezárva
            auto closed = [q](const std::string& s) {
                size_t k = 1;
                while (k < s.size()) {
                    if (s[k] == q) {
                        if (q == '\'' && k + 1 < s.size() && s[k + 1] == '\'') {
                            k += 2;
                            continue;
                        }
                        return true;
                    }
                    if (q == '"' && s[k] == '\\') k++;
                    k++;
                }
                return false;
            };
            while (!closed(rest) && i < lines.size() && lines[i].indent > ownerIndent) {
                rest += ' ';
                rest += lines[i].text;
                i++;
            }
            Flow f{rest};
            return f.quoted();
        }
        // sima skalár, összevonva a beljebb kezdődő folytatósorokkal
        while (i < lines.size() && lines[i].indent > ownerIndent && !isSeqItem(lines[i].text) && findKeySep(lines[i].text) < 0) {
            rest += ' ';
            rest += lines[i].text;
            i++;
        }
        return trim(rest);
    }

    json parseNode(int indent) {
        if (i >= lines.size()) return "";
        if (isSeqItem(lines[i].text)) return parseSeq(lines[i].indent);
        return parseMap(lines[i].indent);
    }

    json parseMap(int indent) {
        json obj = json::object();
        while (i < lines.size() && lines[i].indent == indent && !isSeqItem(lines[i].text)) {
            const std::string t = lines[i].text;
            long sep = findKeySep(t);
            if (sep < 0) {
                i++;
                continue;
            }
            std::string key = unquoteKey(trim(std::string_view(t).substr(0, sep)));
            std::string rest = trim(std::string_view(t).substr(sep + 1));
            i++;
            if (rest.empty()) {
                if (i < lines.size() && lines[i].indent > indent)
                    obj[key] = parseNode(lines[i].indent);
                else if (i < lines.size() && lines[i].indent == indent && isSeqItem(lines[i].text))
                    obj[key] = parseSeq(indent);
                else
                    obj[key] = "";
            } else {
                obj[key] = scalarValue(rest, indent);
            }
        }
        return obj;
    }

    json parseSeq(int indent) {
        json arr = json::array();
        while (i < lines.size() && lines[i].indent == indent && isSeqItem(lines[i].text)) {
            std::string item = lines[i].text == "-" ? "" : lines[i].text.substr(2);
            size_t lead = 0;
            while (lead < item.size() && item[lead] == ' ') lead++;
            item = item.substr(lead);
            if (item.empty()) {
                i++;
                if (i < lines.size() && lines[i].indent > indent)
                    arr.push_back(parseNode(lines[i].indent));
                else
                    arr.push_back("");
                continue;
            }
            if (item[0] != '{' && item[0] != '[' && item[0] != '\'' && item[0] != '"' && findKeySep(item) >= 0) {
                // a sor egy leképezés első kulcsa: a sort átírjuk úgy, mintha beljebb kezdődne
                int ni = indent + 2 + (int)lead;
                lines[i].indent = ni;
                lines[i].text = item;
                arr.push_back(parseMap(ni));
                continue;
            }
            i++;
            arr.push_back(scalarValue(item, indent));
        }
        return arr;
    }
};

}  // namespace

json parseYamlBlock(const std::string& text) {
    Parser p(text);
    // %YAML, %TAG és --- sorok kihagyása
    std::vector<Line> filtered;
    for (auto& l : p.lines)
        if (!startsWith(l.text, "%") && !startsWith(l.text, "---")) filtered.push_back(l);
    p.lines = std::move(filtered);
    if (p.lines.empty()) return json::object();
    return p.parseNode(p.lines[0].indent);
}

std::vector<UnityDoc> parseUnityYaml(const std::string& text) {
    std::vector<UnityDoc> docs;
    // dokumentum fejlécek: "--- !u!<cls> &<id>[ stripped]"
    size_t pos = 0;
    struct Head {
        size_t start, bodyStart;
        int cls;
        std::string id;
        bool stripped;
    };
    std::vector<Head> heads;
    while (true) {
        size_t f = text.find("--- !u!", pos);
        if (f == std::string::npos) break;
        if (f != 0 && text[f - 1] != '\n') {
            pos = f + 1;
            continue;
        }
        size_t eol = text.find('\n', f);
        if (eol == std::string::npos) eol = text.size();
        std::string hl = text.substr(f, eol - f);
        Head h{f, eol, 0, "", false};
        h.cls = std::atoi(hl.c_str() + 7);
        size_t amp = hl.find('&');
        if (amp != std::string::npos) {
            size_t e = amp + 1;
            while (e < hl.size() && (hl[e] == '-' || (hl[e] >= '0' && hl[e] <= '9'))) e++;
            h.id = hl.substr(amp + 1, e - amp - 1);
        }
        h.stripped = hl.find("stripped") != std::string::npos;
        heads.push_back(h);
        pos = eol;
    }
    docs.reserve(heads.size());
    for (size_t k = 0; k < heads.size(); k++) {
        size_t end = k + 1 < heads.size() ? heads[k + 1].start : text.size();
        std::string body = text.substr(heads[k].bodyStart, end - heads[k].bodyStart);
        UnityDoc d;
        d.cls = heads[k].cls;
        d.id = heads[k].id;
        d.stripped = heads[k].stripped;
        try {
            Parser p(body);
            json root = p.lines.empty() ? json::object() : p.parseNode(p.lines[0].indent);
            if (root.is_object() && !root.empty()) {
                auto it = root.begin();
                d.type = it.key();
                d.d = it.value().is_object() ? it.value() : json::object();
            } else
                d.d = json::object();
        } catch (...) {
            d.d = json::object();
        }
        docs.push_back(std::move(d));
    }
    return docs;
}

static const json kNull = nullptr;
static const json kEmptyArr = json::array();

const json& yget(const json& obj, const char* key) {
    if (!obj.is_object()) return kNull;
    auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

const json& yarr(const json& obj, const char* key) {
    const json& v = yget(obj, key);
    return v.is_array() ? v : kEmptyArr;
}

float ynum(const json& j, float def) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if (s.empty()) return def;
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        return end == s.c_str() ? def : (float)v;
    }
    if (j.is_number()) return j.get<float>();
    return def;
}
int yint(const json& j, int def) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if (s.empty()) return def;
        char* end = nullptr;
        long long v = std::strtoll(s.c_str(), &end, 10);
        return end == s.c_str() ? def : (int)v;
    }
    if (j.is_number()) return j.get<int>();
    return def;
}
float ynum(const json& obj, const char* key, float def) { return ynum(yget(obj, key), def); }
int yint(const json& obj, const char* key, int def) { return yint(yget(obj, key), def); }
std::string ystr(const json& obj, const char* key, const std::string& def) {
    const json& v = yget(obj, key);
    return v.is_string() ? v.get<std::string>() : def;
}
Vec3 yvec3(const json& j, Vec3 def) {
    if (!j.is_object()) return def;
    return {ynum(yget(j, "x"), def.x), ynum(yget(j, "y"), def.y), ynum(yget(j, "z"), def.z)};
}
Quat yquat(const json& j) {
    if (!j.is_object()) return {};
    return Quat(ynum(yget(j, "x"), 0.0f), ynum(yget(j, "y"), 0.0f), ynum(yget(j, "z"), 0.0f), ynum(yget(j, "w"), 1.0f));
}
Vec4 ycolor(const json& j, Vec4 def) {
    if (!j.is_object()) return def;
    return {ynum(yget(j, "r"), def.x), ynum(yget(j, "g"), def.y), ynum(yget(j, "b"), def.z), ynum(yget(j, "a"), def.w)};
}
URef yref(const json& j) {
    URef r;
    if (!j.is_object()) return r;
    const json& f = yget(j, "fileID");
    if (f.is_string()) r.fileID = f.get<std::string>();
    const json& g = yget(j, "guid");
    if (g.is_string()) r.guid = g.get<std::string>();
    return r;
}

std::string xorId(const std::string& a, const std::string& b) {
    uint64_t x = (uint64_t)std::strtoll(a.c_str(), nullptr, 10);
    uint64_t y = (uint64_t)std::strtoll(b.c_str(), nullptr, 10);
    uint64_t r = (x ^ y) & 0x7fffffffffffffffull;
    return std::to_string((long long)r);
}

void setPropertyPath(json& obj, const std::string& path, const json& value) {
    std::vector<std::string> parts;
    for (auto& seg : split(path, '.')) {
        if (seg == "Array") continue;
        parts.push_back(seg);
    }
    if (parts.empty()) return;
    json* cur = &obj;
    auto isIndex = [](const std::string& s) { return startsWith(s, "data[") && endsWith(s, "]"); };
    auto indexOf = [](const std::string& s) { return std::atoi(s.c_str() + 5); };
    for (size_t k = 0; k + 1 < parts.size(); k++) {
        const std::string& p = parts[k];
        bool nextIsArr = isIndex(parts[k + 1]) || parts[k + 1] == "size";
        if (isIndex(p)) {
            if (!cur->is_array()) *cur = json::array();
            size_t idx = (size_t)indexOf(p);
            while (cur->size() <= idx) cur->push_back(nextIsArr ? json::array() : json::object());
            cur = &(*cur)[idx];
        } else {
            if (!cur->is_object()) *cur = json::object();
            json& next = (*cur)[p];
            if (!next.is_object() && !next.is_array()) next = nextIsArr ? json::array() : json::object();
            cur = &next;
        }
    }
    const std::string& last = parts.back();
    if (last == "size") {
        if (!cur->is_array()) *cur = json::array();
        size_t n = (size_t)yint(value, 0);
        while (cur->size() > n) cur->erase(cur->size() - 1);
        while (cur->size() < n) cur->push_back(cur->empty() ? json("0") : cur->back());
        return;
    }
    if (isIndex(last)) {
        if (!cur->is_array()) *cur = json::array();
        size_t idx = (size_t)indexOf(last);
        while (cur->size() <= idx) cur->push_back("0");
        (*cur)[idx] = value;
    } else {
        if (!cur->is_object()) *cur = json::object();
        (*cur)[last] = value;
    }
}

std::vector<int> hexInt32Array(const std::string& s) {
    std::vector<int> out;
    for (size_t k = 0; k + 8 <= s.size(); k += 8) {
        std::string b = s.substr(k + 6, 2) + s.substr(k + 4, 2) + s.substr(k + 2, 2) + s.substr(k, 2);
        out.push_back((int)(uint32_t)std::strtoul(b.c_str(), nullptr, 16));
    }
    return out;
}
