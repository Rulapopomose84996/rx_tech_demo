#include "rxtech/replay_manifest.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace rxtech {

namespace {

struct JsonCursor {
    const std::string& text;
    std::size_t pos = 0;
};

void skip_ws(JsonCursor& cursor) {
    while (cursor.pos < cursor.text.size() &&
           std::isspace(static_cast<unsigned char>(cursor.text[cursor.pos])) != 0) {
        ++cursor.pos;
    }
}

void expect(JsonCursor& cursor, char ch) {
    skip_ws(cursor);
    if (cursor.pos >= cursor.text.size() || cursor.text[cursor.pos] != ch) {
        throw std::runtime_error(std::string("expected character: ") + ch);
    }
    ++cursor.pos;
}

bool consume(JsonCursor& cursor, char ch) {
    skip_ws(cursor);
    if (cursor.pos < cursor.text.size() && cursor.text[cursor.pos] == ch) {
        ++cursor.pos;
        return true;
    }
    return false;
}

std::string parse_string(JsonCursor& cursor) {
    skip_ws(cursor);
    expect(cursor, '"');
    std::string result;
    while (cursor.pos < cursor.text.size()) {
        const char ch = cursor.text[cursor.pos++];
        if (ch == '"') {
            return result;
        }
        if (ch == '\\') {
            if (cursor.pos >= cursor.text.size()) {
                throw std::runtime_error("invalid escape");
            }
            const char escaped = cursor.text[cursor.pos++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: throw std::runtime_error("unsupported escape");
            }
            continue;
        }
        result.push_back(ch);
    }
    throw std::runtime_error("unterminated string");
}

std::uint64_t parse_uint(JsonCursor& cursor) {
    skip_ws(cursor);
    std::uint64_t value = 0;
    bool has_digit = false;
    while (cursor.pos < cursor.text.size()) {
        const char ch = cursor.text[cursor.pos];
        if (ch < '0' || ch > '9') {
            break;
        }
        has_digit = true;
        value = value * 10U + static_cast<std::uint64_t>(ch - '0');
        ++cursor.pos;
    }
    if (!has_digit) {
        throw std::runtime_error("expected unsigned integer");
    }
    return value;
}

void skip_value(JsonCursor& cursor);

void skip_object(JsonCursor& cursor) {
    expect(cursor, '{');
    if (consume(cursor, '}')) {
        return;
    }
    while (true) {
        (void)parse_string(cursor);
        expect(cursor, ':');
        skip_value(cursor);
        if (consume(cursor, '}')) {
            return;
        }
        expect(cursor, ',');
    }
}

void skip_array(JsonCursor& cursor) {
    expect(cursor, '[');
    if (consume(cursor, ']')) {
        return;
    }
    while (true) {
        skip_value(cursor);
        if (consume(cursor, ']')) {
            return;
        }
        expect(cursor, ',');
    }
}

void skip_value(JsonCursor& cursor) {
    skip_ws(cursor);
    if (cursor.pos >= cursor.text.size()) {
        throw std::runtime_error("unexpected end of json");
    }
    const char ch = cursor.text[cursor.pos];
    if (ch == '{') {
        skip_object(cursor);
    } else if (ch == '[') {
        skip_array(cursor);
    } else if (ch == '"') {
        (void)parse_string(cursor);
    } else if (ch >= '0' && ch <= '9') {
        (void)parse_uint(cursor);
    } else if (cursor.text.compare(cursor.pos, 4, "null") == 0) {
        cursor.pos += 4;
    } else {
        throw std::runtime_error("unsupported json value");
    }
}

ReplayEntry parse_entry(JsonCursor& cursor) {
    ReplayEntry entry;
    expect(cursor, '{');
    if (consume(cursor, '}')) {
        return entry;
    }
    while (true) {
        const std::string key = parse_string(cursor);
        expect(cursor, ':');
        if (key == "sequence") {
            entry.sequence = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "kind") {
            entry.kind = parse_string(cursor);
        } else if (key == "file") {
            entry.file = parse_string(cursor);
        } else if (key == "offset_bytes") {
            entry.offset_bytes = parse_uint(cursor);
        } else if (key == "length_bytes") {
            entry.length_bytes = parse_uint(cursor);
        } else if (key == "cpi") {
            entry.cpi = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "prt") {
            entry.prt = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "channel") {
            entry.channel = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "channel_name") {
            entry.channel_name = parse_string(cursor);
        } else if (key == "packet_index") {
            entry.packet_index = static_cast<std::uint32_t>(parse_uint(cursor));
        } else {
            skip_value(cursor);
        }
        if (consume(cursor, '}')) {
            return entry;
        }
        expect(cursor, ',');
    }
}

std::vector<ReplayEntry> parse_entries(JsonCursor& cursor) {
    std::vector<ReplayEntry> entries;
    expect(cursor, '[');
    if (consume(cursor, ']')) {
        return entries;
    }
    while (true) {
        entries.push_back(parse_entry(cursor));
        if (consume(cursor, ']')) {
            return entries;
        }
        expect(cursor, ',');
    }
}

std::string read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open replay manifest: " + path);
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

}  // namespace

ReplayManifest load_replay_manifest(const std::string& path) {
    const std::string text = read_all(path);
    JsonCursor cursor{text};
    ReplayManifest manifest;

    expect(cursor, '{');
    if (consume(cursor, '}')) {
        return manifest;
    }
    while (true) {
        const std::string key = parse_string(cursor);
        expect(cursor, ':');
        if (key == "format_version") {
            manifest.format_version = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "sample_type") {
            manifest.sample_type = parse_string(cursor);
        } else if (key == "cpi") {
            manifest.cpi = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "total_udp_units") {
            manifest.total_udp_units = static_cast<std::uint32_t>(parse_uint(cursor));
        } else if (key == "replay_sequence") {
            manifest.entries = parse_entries(cursor);
        } else {
            skip_value(cursor);
        }

        if (consume(cursor, '}')) {
            break;
        }
        expect(cursor, ',');
    }

    return manifest;
}

}  // namespace rxtech
