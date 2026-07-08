#include "datcore/labels.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace datcore {
namespace {

// --- minimal, tolerant JSON value + recursive-descent parser --------------
struct JVal {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;
    const JVal* find(const std::string& k) const {
        for (auto& p : obj) if (p.first == k) return &p.second;
        return nullptr;
    }
};

struct Parser {
    const char* p;
    const char* end;
    std::string err;
    bool fail(const char* m) { if (err.empty()) err = m; return false; }
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }

    bool parse_string(std::string& out) {
        if (p >= end || *p != '"') return fail("expected string");
        ++p;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { out += c; continue; }
            if (p >= end) return fail("bad escape");
            char e = *p++;
            switch (e) {
                case '"': out += '"'; break;   case '\\': out += '\\'; break;
                case '/': out += '/'; break;    case 'n': out += '\n'; break;
                case 't': out += '\t'; break;   case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;   case 'f': out += '\f'; break;
                case 'u': {
                    if (end - p < 4) return fail("bad \\u");
                    int cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++; cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else return fail("bad hex");
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                    else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: return fail("bad escape char");
            }
        }
        if (p >= end) return fail("unterminated string");
        ++p;
        return true;
    }

    bool parse_array(JVal& v) {
        v.t = JVal::Arr; ++p; ws();
        if (p < end && *p == ']') { ++p; return true; }
        for (;;) {
            JVal e; if (!parse_value(e)) return false; v.arr.push_back(std::move(e)); ws();
            if (p >= end) return fail("unterminated array");
            if (*p == ',') { ++p; ws(); continue; }
            if (*p == ']') { ++p; return true; }
            return fail("expected , or ]");
        }
    }

    bool parse_object(JVal& v) {
        v.t = JVal::Obj; ++p; ws();
        if (p < end && *p == '}') { ++p; return true; }
        for (;;) {
            ws(); std::string key; if (!parse_string(key)) return false; ws();
            if (p >= end || *p != ':') return fail("expected :"); ++p;
            JVal val; if (!parse_value(val)) return false;
            v.obj.emplace_back(std::move(key), std::move(val)); ws();
            if (p >= end) return fail("unterminated object");
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return fail("expected , or }");
        }
    }

    bool parse_value(JVal& v) {
        ws();
        if (p >= end) return fail("unexpected end");
        char c = *p;
        if (c == '"') { v.t = JVal::Str; return parse_string(v.str); }
        if (c == '{') return parse_object(v);
        if (c == '[') return parse_array(v);
        if (c == 't' && end - p >= 4 && !strncmp(p, "true", 4)) { p += 4; v.t = JVal::Bool; v.b = true; return true; }
        if (c == 'f' && end - p >= 5 && !strncmp(p, "false", 5)) { p += 5; v.t = JVal::Bool; v.b = false; return true; }
        if (c == 'n' && end - p >= 4 && !strncmp(p, "null", 4)) { p += 4; v.t = JVal::Null; return true; }
        char* e2 = nullptr; double d = strtod(p, &e2);
        if (e2 == p) return fail("bad value");
        v.t = JVal::Num; v.num = d; p = e2; return true;
    }
};

std::string json_escape(const std::string& s) {
    std::string o; o.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;  case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;  case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;  default: o += c;
        }
    }
    return o;
}

std::string to_lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

} // namespace

std::string Labels::hash_key(int32_t hash) { return "hash:" + std::to_string(hash); }
std::string Labels::murmur_key(uint32_t m) { char b[24]; snprintf(b, sizeof(b), "murmur:%08x", m); return b; }

const Label* Labels::get(const std::string& key) const {
    auto it = map_.find(key);
    return it == map_.end() ? nullptr : &it->second;
}

const Label* Labels::resolve(int32_t hash, uint32_t murmur) const {
    if (hash) { if (const Label* l = get(hash_key(hash))) return l; }
    return get(murmur_key(murmur));
}

void Labels::set(const std::string& key, Label label) { map_[key] = std::move(label); }

std::vector<std::string> Labels::search(const std::string& query) const {
    std::string q = to_lower(query);
    std::vector<std::string> out;
    for (auto& kv : map_) {
        const Label& l = kv.second;
        bool hit = to_lower(l.name).find(q) != std::string::npos ||
                   to_lower(l.category).find(q) != std::string::npos;
        for (auto& t : l.tags) if (to_lower(t).find(q) != std::string::npos) hit = true;
        if (hit) out.push_back(kv.first);
    }
    return out;
}

bool Labels::load(const std::string& path, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return true; // missing file = empty store, not an error
    std::string buf;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        buf.resize(static_cast<size_t>(sz));
        if (fread(&buf[0], 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
            fclose(f); if (err) *err = "read error"; return false;
        }
    }
    fclose(f);
    if (buf.empty()) return true;

    Parser ps{buf.data(), buf.data() + buf.size(), ""};
    JVal root;
    if (!ps.parse_value(root) || root.t != JVal::Obj) { if (err) *err = ps.err.empty() ? "not a JSON object" : ps.err; return false; }
    const JVal* labels = root.find("labels");
    if (!labels || labels->t != JVal::Obj) { if (err) *err = "missing 'labels' object"; return false; }

    std::unordered_map<std::string, Label> loaded;
    for (auto& kv : labels->obj) {
        if (kv.second.t != JVal::Obj) continue;
        Label l;
        if (auto* v = kv.second.find("name");     v && v->t == JVal::Str) l.name = v->str;
        if (auto* v = kv.second.find("category"); v && v->t == JVal::Str) l.category = v->str;
        if (auto* v = kv.second.find("source");   v && v->t == JVal::Str) l.source = v->str;
        if (auto* v = kv.second.find("notes");    v && v->t == JVal::Str) l.notes = v->str;
        if (auto* v = kv.second.find("tags");     v && v->t == JVal::Arr)
            for (auto& t : v->arr) if (t.t == JVal::Str) l.tags.push_back(t.str);
        loaded[kv.first] = std::move(l);
    }
    map_.swap(loaded);
    return true;
}

bool Labels::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    std::vector<const std::pair<const std::string, Label>*> items;
    items.reserve(map_.size());
    for (auto& kv : map_) items.push_back(&kv);
    std::sort(items.begin(), items.end(), [](auto a, auto b) { return a->first < b->first; });

    fprintf(f, "{\n  \"version\": 1,\n  \"labels\": {\n");
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string& key = items[i]->first;
        const Label& l = items[i]->second;
        fprintf(f, "    \"%s\": { \"name\": \"%s\", \"category\": \"%s\", \"tags\": [",
                json_escape(key).c_str(), json_escape(l.name).c_str(), json_escape(l.category).c_str());
        for (size_t j = 0; j < l.tags.size(); ++j)
            fprintf(f, "%s\"%s\"", j ? ", " : "", json_escape(l.tags[j]).c_str());
        fprintf(f, "], \"source\": \"%s\", \"notes\": \"%s\" }%s\n",
                json_escape(l.source).c_str(), json_escape(l.notes).c_str(), (i + 1 < items.size()) ? "," : "");
    }
    fprintf(f, "  }\n}\n");
    fclose(f);
    return true;
}

} // namespace datcore
