#include "SettingsJson.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "PathUtil.hpp"


namespace gs {

namespace {

using nlohmann::json;
using Warnings = std::vector<std::string>;

// ---- writing --------------------------------------------------------------

template <typename T>
json to_value(const T& v)
{
    if constexpr (std::is_enum_v<T>)
        return std::string(enum_id(v));
    else if constexpr (std::is_same_v<T, Vec3>)
        return json::array({v.x, v.y, v.z});
    else if constexpr (std::is_same_v<T, Rgb>)
        return json::array({v.r, v.g, v.b});
    else
        return v;
}

// ---- reading --------------------------------------------------------------

std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

// Numbers, booleans (0/1) and strings holding exactly one number.
std::optional<double> to_number(const json& v)
{
    if (v.is_number())
        return v.get<double>();
    if (v.is_boolean())
        return v.get<bool>() ? 1.0 : 0.0;
    if (v.is_string()) {
        const std::string_view text = trim(v.get_ref<const std::string&>());
        double out = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        if (ec == std::errc() && end == text.data() + text.size())
            return out;
    }
    return std::nullopt;
}

template <typename T>
std::optional<T> parse(const json& v);

template <>
std::optional<double> parse(const json& v)
{
    return to_number(v);
}

template <>
std::optional<int> parse(const json& v)
{
    // Truncates toward zero; out-of-range values saturate.
    const auto n = to_number(v);
    if (!n || !std::isfinite(*n))
        return std::nullopt;
    return static_cast<int>(
        std::clamp(*n, double{std::numeric_limits<int>::min()}, double{std::numeric_limits<int>::max()}));
}

template <>
std::optional<bool> parse(const json& v)
{
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number())
        return v.get<double>() != 0.0;
    if (v.is_string()) {
        std::string text(trim(v.get_ref<const std::string&>()));
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "1")
            return true;
        if (text == "false" || text == "0")
            return false;
    }
    return std::nullopt;
}

template <>
std::optional<std::string> parse(const json& v)
{
    if (v.is_string())
        return v.get<std::string>();
    return std::nullopt;
}

// [x, y, z]
template <>
std::optional<Vec3> parse(const json& v)
{
    if (!v.is_array() || v.size() != 3)
        return std::nullopt;
    const auto x = to_number(v[0]), y = to_number(v[1]), z = to_number(v[2]);
    if (!x || !y || !z)
        return std::nullopt;
    return Vec3{*x, *y, *z};
}

// [r, g, b] in 0..255; extra entries (such as alpha) are ignored.
template <>
std::optional<Rgb> parse(const json& v)
{
    if (!v.is_array() || v.size() < 3)
        return std::nullopt;
    std::uint8_t c[3];
    for (int i = 0; i < 3; ++i) {
        const auto n = to_number(v[i]);
        if (!n || !std::isfinite(*n))
            return std::nullopt;
        c[i] = static_cast<std::uint8_t>(std::lround(std::clamp(*n, 0.0, 255.0)));
    }
    return Rgb{c[0], c[1], c[2]};
}

template <typename E>
    requires std::is_enum_v<E>
std::optional<E> parse_enum(const json& v)
{
    if (!v.is_string())
        return std::nullopt;
    return enum_from_id<E>(v.get_ref<const std::string&>());
}

template <typename T>
std::string describe()
{
    if constexpr (std::is_enum_v<T>) {
        std::string ids;
        for (const auto& n : enum_names<T>())
            ids += (ids.empty() ? "" : ", ") + std::string(n.id);
        return "one of " + ids;
    } else if constexpr (std::is_same_v<T, Vec3>) {
        return "an [x, y, z] array";
    } else if constexpr (std::is_same_v<T, Rgb>) {
        return "an [r, g, b] array";
    } else if constexpr (std::is_same_v<T, bool>) {
        return "true or false";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return "a string";
    } else {
        return "a number";
    }
}

void warn_bad_value(Warnings& w, std::string_view key, std::string_view expected, const json& v)
{
    std::string shown = v.dump();
    if (shown.size() > 40)
        shown = shown.substr(0, 37) + "...";
    w.push_back(std::string(key) + ": expected " + std::string(expected) + ", got " + shown + " (ignored)");
}

// Settings that earlier versions had and this one does not: a file that holds the value every
// export now has loads without a word, any other value with a warning that it is ignored.
struct RetiredKey {
    std::string_view key;
    json kept;           // the value this version always uses
    std::string_view why;
};

const std::vector<RetiredKey>& retired_keys()
{
    static const std::vector<RetiredKey> keys = {
        {"blend_mode", "additive", "ParticleMDL draws particles additively only"},
        {"fullbright", false, "ParticleMDL draws particles additively only, with the map's lighting"},
    };
    return keys;
}

bool is_known_key(std::string_view key)
{
    static const std::vector<std::string_view> keys = [] {
        std::vector<std::string_view> k;
        const Settings s;
        for_each_field(s, [&](std::string_view field, const auto&) { k.push_back(field); });
        return k;
    }();
    return std::ranges::find(keys, key) != keys.end();
}

} // namespace

json to_json(const Settings& s)
{
    json j = json::object();
    for_each_field(s, [&](std::string_view key, const auto& value) { j[std::string(key)] = to_value(value); });
    return j;
}

std::string settings_file_text(const Settings& s)
{
    std::string text = "{\n";
    bool first = true;
    for_each_field(s, [&](std::string_view key, const auto& value) {
        text += first ? "  \"" : ",\n  \"";
        text += key;
        text += "\": ";
        text += to_value(value).dump();
        first = false;
    });
    return text + "\n}\n";
}

std::vector<std::string> apply_json(Settings& s, const json& j)
{
    if (!j.is_object())
        throw std::runtime_error("a settings file must contain a JSON object");

    Warnings w;
    for (const auto& item : j.items()) {
        if (is_known_key(item.key()))
            continue;
        const auto retired = std::ranges::find(retired_keys(), item.key(), &RetiredKey::key);
        if (retired == retired_keys().end())
            w.push_back("unknown setting '" + item.key() + "' (ignored)");
        else if (item.value() != retired->kept)
            w.push_back(item.key() + " " + item.value().dump() + ": " + std::string(retired->why) + " (ignored)");
    }

    for_each_field(s, [&](std::string_view key, auto& field) {
        using T = std::remove_cvref_t<decltype(field)>;
        const auto it = j.find(key);
        if (it == j.end())
            return;
        std::optional<T> value;
        if constexpr (std::is_enum_v<T>)
            value = parse_enum<T>(*it);
        else
            value = parse<T>(*it);
        if (value)
            field = std::move(*value);
        else
            warn_bad_value(w, key, describe<T>(), *it);
    });
    // Files saved before depth existed had one width for X and Y: keep that look.
    if (!j.contains("volume_depth") && j.contains("volume_width"))
        s.volume_depth = s.volume_width;
    return w;
}

SettingsLoad settings_from_json(const json& j, Settings base)
{
    SettingsLoad result{std::move(base), {}};
    result.warnings = apply_json(result.settings, j);

    const json before = to_json(result.settings);
    if (validate(result.settings)) {
        const json after = to_json(result.settings);
        for (const auto& item : after.items())
            if (before.at(item.key()) != item.value())
                result.warnings.push_back(item.key() + ": " + before.at(item.key()).dump() +
                                          " is out of range, using " + item.value().dump());
    }
    return result;
}

SettingsLoad read_settings_file(const std::filesystem::path& path, Settings base)
{
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot read " + to_utf8(path));
    return settings_from_json(json::parse(f), std::move(base));
}

} // namespace gs
