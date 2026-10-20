#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace gs {

enum class Geometry { SingleQuad, CrossedQuads, TriPlanarQuads, VolumetricBox };
enum class EndBehavior { DropThroughFloor, HideUnderground, HideInCore, StopAndStay, EndlessLoop };
enum class TexPattern { RadialOrb, HollowRing, SharpStar, SoftCloud, FlowerPetals, Snowflake, RainDrop };
enum class ColorMode { SolidColor, Gradient };
enum class ScaleMode { Static, VertexBone };

// An enum value with its id in settings files and its label in the editor.
template <typename E>
struct EnumName {
    E value;
    std::string_view id;
    const char* label;
};

// One table per enum, in declaration order (the combo index is the value).
template <typename E>
struct EnumTable;

template <>
struct EnumTable<Geometry> {
    static constexpr std::array names{
        EnumName<Geometry>{Geometry::SingleQuad, "single_quad", "Single Quad"},
        EnumName<Geometry>{Geometry::CrossedQuads, "crossed_quads", "Crossed-Quads"},
        EnumName<Geometry>{Geometry::TriPlanarQuads, "tri_planar_quads", "Tri-Planar Quads"},
        EnumName<Geometry>{Geometry::VolumetricBox, "volumetric_box", "Volumetric Box"},
    };
};

template <>
struct EnumTable<EndBehavior> {
    static constexpr std::array names{
        EnumName<EndBehavior>{EndBehavior::DropThroughFloor, "drop_through_floor", "Drop Through Floor"},
        EnumName<EndBehavior>{EndBehavior::HideUnderground, "hide_underground", "Hide Underground"},
        EnumName<EndBehavior>{EndBehavior::HideInCore, "hide_in_core", "Hide in Core"},
        EnumName<EndBehavior>{EndBehavior::StopAndStay, "stop_and_stay", "Stop & Stay"},
        EnumName<EndBehavior>{EndBehavior::EndlessLoop, "endless_loop", "Endless Loop"},
    };
};

template <>
struct EnumTable<TexPattern> {
    static constexpr std::array names{
        EnumName<TexPattern>{TexPattern::RadialOrb, "radial_orb", "Radial Orb"},
        EnumName<TexPattern>{TexPattern::HollowRing, "hollow_ring", "Hollow Ring"},
        EnumName<TexPattern>{TexPattern::SharpStar, "sharp_star", "Sharp Star"},
        EnumName<TexPattern>{TexPattern::SoftCloud, "soft_cloud", "Soft Cloud"},
        EnumName<TexPattern>{TexPattern::FlowerPetals, "flower_petals", "Flower Petals"},
        EnumName<TexPattern>{TexPattern::Snowflake, "snowflake", "Snowflake"},
        EnumName<TexPattern>{TexPattern::RainDrop, "rain_drop", "Rain Drop"},
    };
};

template <>
struct EnumTable<ColorMode> {
    static constexpr std::array names{
        EnumName<ColorMode>{ColorMode::SolidColor, "solid", "Solid Color"},
        EnumName<ColorMode>{ColorMode::Gradient, "gradient", "Gradient"},
    };
};

template <>
struct EnumTable<ScaleMode> {
    static constexpr std::array names{
        EnumName<ScaleMode>{ScaleMode::Static, "static", "Static"},
        EnumName<ScaleMode>{ScaleMode::VertexBone, "vertex_bone", "Vertex Bone"},
    };
};

template <typename E>
constexpr std::span<const EnumName<E>> enum_names() noexcept
{
    return EnumTable<E>::names;
}

template <typename E>
constexpr bool enum_valid(E value) noexcept
{
    return static_cast<std::size_t>(value) < enum_names<E>().size();
}

template <typename E>
constexpr std::string_view enum_id(E value) noexcept
{
    return enum_names<E>()[static_cast<std::size_t>(value)].id;
}

template <typename E>
constexpr const char* enum_label(E value) noexcept
{
    return enum_names<E>()[static_cast<std::size_t>(value)].label;
}

template <typename E>
constexpr std::optional<E> enum_from_id(std::string_view id) noexcept
{
    for (const auto& n : enum_names<E>())
        if (n.id == id)
            return n.value;
    return std::nullopt;
}

template <typename E>
constexpr bool table_in_declaration_order() noexcept
{
    for (std::size_t i = 0; i < enum_names<E>().size(); ++i)
        if (static_cast<std::size_t>(enum_names<E>()[i].value) != i)
            return false;
    return true;
}

static_assert(table_in_declaration_order<Geometry>());
static_assert(table_in_declaration_order<EndBehavior>());
static_assert(table_in_declaration_order<TexPattern>());
static_assert(table_in_declaration_order<ColorMode>());
static_assert(table_in_declaration_order<ScaleMode>());

} // namespace gs
