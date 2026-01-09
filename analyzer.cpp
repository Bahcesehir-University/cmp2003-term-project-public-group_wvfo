#include "analyzer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

static inline void trimInPlace(std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;

    std::size_t j = s.size();
    while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;

    if (i == 0 && j == s.size()) return;
    s = s.substr(i, j - i);
}

static std::vector<std::string> splitCSVLine(const std::string& line) {
    std::vector<std::string> out;
    out.reserve(6);

    std::string field;
    field.reserve(line.size());

    bool inQuotes = false;
    for (char ch : line) {
        if (ch == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (ch == ',' && !inQuotes) {
            out.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    out.push_back(std::move(field));
    return out;
}

static int parseHour(const std::string& dt) {
    std::size_t sp = dt.find(' ');
    if (sp == std::string::npos) return -1;
    if (sp + 3 > dt.size()) return -1;

    char c0 = dt[sp + 1];
    char c1 = dt[sp + 2];
    if (!std::isdigit(static_cast<unsigned char>(c0)) ||
        !std::isdigit(static_cast<unsigned char>(c1))) {
        return -1;
    }

    int hour = (c0 - '0') * 10 + (c1 - '0');
    if (hour < 0 || hour > 23) return -1;
    return hour;
}

struct SlotKey {
    std::string zone;
    int hour;

    bool operator==(const SlotKey& o) const noexcept {
        return hour == o.hour && zone == o.zone;
    }
};

struct SlotKeyHash {
    std::size_t operator()(const SlotKey& k) const noexcept {
        std::size_t h1 = std::hash<std::string>{}(k.zone);
        std::size_t h2 = std::hash<int>{}(k.hour);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
struct Data {
    std::unordered_map<std::string, long long> zoneCounts;
    std::unordered_map<SlotKey, long long, SlotKeyHash> slotCounts;
};

static std::unordered_map<const TripAnalyzer*, Data> g;

static Data& dataFor(const TripAnalyzer* self) {
    return g[self];
}
static const Data* dataIf(const TripAnalyzer* self) {
    auto it = g.find(self);
    if (it == g.end()) return nullptr;
    return &it->second;
}

}

void TripAnalyzer::ingestFile(const std::string& csvPath) {
    std::ifstream in(csvPath);
    if (!in.is_open()) return;

    Data& d = dataFor(this);
    d.zoneCounts.clear();
    d.slotCounts.clear();

    std::string line;

    
    if (!std::getline(in, line)) return;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        auto fields = splitCSVLine(line);
        if (fields.size() != 6) continue;

        std::string zone = std::move(fields[1]); // pickup_zone
        std::string dt   = std::move(fields[3]); // pickup_datetime

        trimInPlace(zone);
        trimInPlace(dt);

        if (zone.empty() || dt.empty()) continue;

        int hour = parseHour(dt);
        if (hour < 0) continue;

        ++d.zoneCounts[zone];
        ++d.slotCounts[SlotKey{zone, hour}];
    }
}

std::vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    std::vector<ZoneCount> res;
    if (k <= 0) return res;

    const Data* d = dataIf(this);
    if (!d) return res;

    res.reserve(d->zoneCounts.size());
    for (const auto& kv : d->zoneCounts) {
        res.push_back(ZoneCount{kv.first, kv.second});
    }

    std::sort(res.begin(), res.end(), [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;                            
    });

    if (static_cast<std::size_t>(k) < res.size()) res.resize(static_cast<std::size_t>(k));
    return res;
}

std::vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    std::vector<SlotCount> res;
    if (k <= 0) return res;

    const Data* d = dataIf(this);
    if (!d) return res;

    res.reserve(d->slotCounts.size());
    for (const auto& kv : d->slotCounts) {
        res.push_back(SlotCount{kv.first.zone, kv.first.hour, kv.second});
    }

    std::sort(res.begin(), res.end(), [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;                            
    });

    if (static_cast<std::size_t>(k) < res.size()) res.resize(static_cast<std::size_t>(k));
    return res;
}
