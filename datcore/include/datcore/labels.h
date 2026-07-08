#pragma once
// Human/derived labels for Gw.dat assets, keyed by a STABLE id ("hash:<n>" for
// the ANet file number, or "murmur:<hex>" for the content hash) so labels survive
// dat rebuilds and MFT reordering. This is the shared sink for every label source:
// manual CLI edits, community seed data, auto-derived map associations, and — the
// plan — in-game browse+label sessions whose labels.json is scp'd back to macOS.
//
// Dependency-free JSON (hand-rolled, tolerant) so both the macOS CLI and the
// Windows in-game tool (which links datcore) read/write the exact same file.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace datcore {

struct Label {
    std::string name;
    std::string category;            // free text: npc, armor, weapon, prop, texture, map, ...
    std::vector<std::string> tags;
    std::string source;              // manual, ingame, community, map-derived, ...
    std::string notes;
};

class Labels {
public:
    // Load labels.json. A missing file is not an error (starts empty). On a parse
    // error, returns false and (if err) a message; the store is left unchanged.
    bool load(const std::string& path, std::string* err = nullptr);
    bool save(const std::string& path) const;

    const Label* get(const std::string& key) const;
    const Label* resolve(int32_t hash, uint32_t murmur) const; // hash key first, else murmur
    void set(const std::string& key, Label label);

    // Keys whose name / category / tags contain `query` (case-insensitive).
    std::vector<std::string> search(const std::string& query) const;

    size_t size() const { return map_.size(); }
    const std::unordered_map<std::string, Label>& all() const { return map_; }

    static std::string hash_key(int32_t hash);
    static std::string murmur_key(uint32_t murmur);

private:
    std::unordered_map<std::string, Label> map_;
};

} // namespace datcore
