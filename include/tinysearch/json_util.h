#pragma once

#include <optional>
#include <sstream>
#include <string>

namespace tinysearch::json {

// TinySearch intentionally avoids pulling in a third-party JSON library
// (network access to fetch dependencies is not assumed to be available,
// and the API's JSON needs are tiny) — see docs/architecture.md, "Why no
// JSON library?". This is a deliberately minimal writer/reader:
//   - escape()/quote() are enough to safely emit any string as JSON.
//   - extract_string_field() / extract_number_field() do a simple scan for
//     `"key"` followed by a string/number value in a flat JSON object; they
//     do not implement a general JSON parser and are only used to read the
//     small, fixed-shape request bodies this API accepts (e.g. {"path": "..."}).

inline std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

inline std::string quote(const std::string& s) { return "\"" + escape(s) + "\""; }

// Finds `"key"` in `body`, then the following quoted string value. Returns
// std::nullopt if the key isn't found or isn't followed by a string.
inline std::optional<std::string> extract_string_field(const std::string& body,
                                                         const std::string& key) {
    std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::nullopt;
    pos = body.find('"', pos);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    std::string out;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            ++pos;
            switch (body[pos]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                default: out += body[pos];
            }
        } else {
            out += body[pos];
        }
        ++pos;
    }
    return out;
}

inline std::optional<long> extract_number_field(const std::string& body, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    std::size_t start = pos;
    while (pos < body.size() && (std::isdigit(static_cast<unsigned char>(body[pos])) || body[pos] == '-')) {
        ++pos;
    }
    if (pos == start) return std::nullopt;
    try {
        return std::stol(body.substr(start, pos - start));
    } catch (...) {
        return std::nullopt;
    }
}

// Extracts `"key": ["a", "b", "c"]` as a vector of strings. Only used to
// read the evaluation dataset's fixed-shape `"relevant": [...]` arrays —
// see benchmark/ranking_evaluation.cpp.
inline std::vector<std::string> extract_string_array_field(const std::string& body,
                                                             const std::string& key) {
    std::vector<std::string> out;
    std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) return out;
    pos = body.find('[', pos + needle.size());
    if (pos == std::string::npos) return out;
    std::size_t end = body.find(']', pos);
    if (end == std::string::npos) return out;
    std::size_t i = pos + 1;
    while (i < end) {
        std::size_t q1 = body.find('"', i);
        if (q1 == std::string::npos || q1 > end) break;
        std::size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 > end) break;
        out.push_back(body.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

// Splits a top-level JSON array of objects, e.g. `[{...}, {...}]`, into the
// raw text of each `{...}` object. Only used for reading the evaluation
// dataset (see benchmark/ranking_evaluation.cpp) — not a general parser.
inline std::vector<std::string> split_top_level_objects(const std::string& array_json) {
    std::vector<std::string> objects;
    int depth = 0;
    std::size_t start = std::string::npos;
    bool in_string = false;
    for (std::size_t i = 0; i < array_json.size(); ++i) {
        char c = array_json[i];
        if (in_string) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(array_json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

}  // namespace tinysearch::json
