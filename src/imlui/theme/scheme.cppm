// Scheme: the Material 3 color roles, and their generation from a seed color.
export module imlui.theme.scheme;

import std;

import imlui.color;
import imlui.color.oklch;
import imlui.color.tonal_palette;

export namespace imlui {

struct Scheme {
    Color primary, onPrimary, primaryContainer, onPrimaryContainer;
    Color secondary, onSecondary, secondaryContainer, onSecondaryContainer;
    Color tertiary, onTertiary, tertiaryContainer, onTertiaryContainer;
    Color error, onError, errorContainer, onErrorContainer;
    Color surface, onSurface, surfaceVariant, onSurfaceVariant;
    Color surfaceDim, surfaceBright;
    Color surfaceContainerLowest, surfaceContainerLow, surfaceContainer;
    Color surfaceContainerHigh, surfaceContainerHighest;
    Color outline, outlineVariant;
    Color inverseSurface, inverseOnSurface, inversePrimary;
    Color scrim, shadow;
};

/// A color role's name and its Scheme member.
struct RoleEntry {
    std::string_view name;
    Color Scheme::* member;
};

/// All color roles, in declaration order.
[[nodiscard]] std::span<const RoleEntry> roles();

/// The role named `name` ("primary", "onSurface", ...), or nullptr.
Color* findRole(Scheme& s, std::string_view name);
const Color* findRole(const Scheme& s, std::string_view name);

/// Below this OKLCH chroma a seed is a grey (its hue is rounding noise).
inline constexpr float achromaticChroma = 1e-4f;

/// Generates a full Material 3 scheme from a seed color. A grey seed (chroma below achromaticChroma) gives a grey
/// scheme: every role but the error ones is neutral.
Scheme generateScheme(Color seed, bool dark);

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr std::array roleTable = {
    RoleEntry{"primary", &Scheme::primary},
    RoleEntry{"onPrimary", &Scheme::onPrimary},
    RoleEntry{"primaryContainer", &Scheme::primaryContainer},
    RoleEntry{"onPrimaryContainer", &Scheme::onPrimaryContainer},
    RoleEntry{"secondary", &Scheme::secondary},
    RoleEntry{"onSecondary", &Scheme::onSecondary},
    RoleEntry{"secondaryContainer", &Scheme::secondaryContainer},
    RoleEntry{"onSecondaryContainer", &Scheme::onSecondaryContainer},
    RoleEntry{"tertiary", &Scheme::tertiary},
    RoleEntry{"onTertiary", &Scheme::onTertiary},
    RoleEntry{"tertiaryContainer", &Scheme::tertiaryContainer},
    RoleEntry{"onTertiaryContainer", &Scheme::onTertiaryContainer},
    RoleEntry{"error", &Scheme::error},
    RoleEntry{"onError", &Scheme::onError},
    RoleEntry{"errorContainer", &Scheme::errorContainer},
    RoleEntry{"onErrorContainer", &Scheme::onErrorContainer},
    RoleEntry{"surface", &Scheme::surface},
    RoleEntry{"onSurface", &Scheme::onSurface},
    RoleEntry{"surfaceVariant", &Scheme::surfaceVariant},
    RoleEntry{"onSurfaceVariant", &Scheme::onSurfaceVariant},
    RoleEntry{"surfaceDim", &Scheme::surfaceDim},
    RoleEntry{"surfaceBright", &Scheme::surfaceBright},
    RoleEntry{"surfaceContainerLowest", &Scheme::surfaceContainerLowest},
    RoleEntry{"surfaceContainerLow", &Scheme::surfaceContainerLow},
    RoleEntry{"surfaceContainer", &Scheme::surfaceContainer},
    RoleEntry{"surfaceContainerHigh", &Scheme::surfaceContainerHigh},
    RoleEntry{"surfaceContainerHighest", &Scheme::surfaceContainerHighest},
    RoleEntry{"outline", &Scheme::outline},
    RoleEntry{"outlineVariant", &Scheme::outlineVariant},
    RoleEntry{"inverseSurface", &Scheme::inverseSurface},
    RoleEntry{"inverseOnSurface", &Scheme::inverseOnSurface},
    RoleEntry{"inversePrimary", &Scheme::inversePrimary},
    RoleEntry{"scrim", &Scheme::scrim},
    RoleEntry{"shadow", &Scheme::shadow},
};

} // namespace

std::span<const RoleEntry> roles() { return roleTable; }

Color* findRole(Scheme& s, std::string_view name) {
    for (const auto& r : roleTable)
        if (r.name == name) return &(s.*r.member);
    return nullptr;
}

const Color* findRole(const Scheme& s, std::string_view name) {
    return findRole(const_cast<Scheme&>(s), name);
}

Scheme generateScheme(Color seed, bool dark) {
    Oklch o = toOklch(seed);
    // A grey has no hue: its chroma is rounding noise (about 1e-7; an 8-bit color one step off grey has 1e-3), and so
    // is the angle atan2 makes of it. A grey seed gives a grey scheme (only the error roles keep their color).
    const float tint = o.c < achromaticChroma ? 0.0f : 1.0f;
    if (tint == 0.0f) o.h = 0.0f;
    float c = o.c < 0.12f ? 0.12f : (o.c > 0.22f ? 0.22f : o.c);
    TonalPalette primary{o.h, c * tint};
    TonalPalette secondary{o.h, 0.045f * tint};
    TonalPalette tertiary{o.h + 60.0f >= 360.0f ? o.h - 300.0f : o.h + 60.0f, 0.07f * tint};
    TonalPalette neutral{o.h, 0.008f * tint};
    TonalPalette neutralVariant{o.h, 0.018f * tint};
    TonalPalette error{25.0f, 0.19f};

    Scheme s{};
    auto assign = [&](TonalPalette p, Color& base, Color& on, Color& cont, Color& onCont) {
        base = p.tone(dark ? 80 : 40);
        on = p.tone(dark ? 20 : 100);
        cont = p.tone(dark ? 30 : 90);
        onCont = p.tone(dark ? 90 : 10);
    };
    assign(primary, s.primary, s.onPrimary, s.primaryContainer, s.onPrimaryContainer);
    assign(secondary, s.secondary, s.onSecondary, s.secondaryContainer, s.onSecondaryContainer);
    assign(tertiary, s.tertiary, s.onTertiary, s.tertiaryContainer, s.onTertiaryContainer);
    assign(error, s.error, s.onError, s.errorContainer, s.onErrorContainer);

    const TonalPalette& n = neutral;
    const TonalPalette& nv = neutralVariant;
    if (dark) {
        s.surface = n.tone(6);            s.onSurface = n.tone(90);
        s.surfaceVariant = nv.tone(30);   s.onSurfaceVariant = nv.tone(80);
        s.surfaceDim = n.tone(6);         s.surfaceBright = n.tone(24);
        s.surfaceContainerLowest = n.tone(4);
        s.surfaceContainerLow = n.tone(10);
        s.surfaceContainer = n.tone(12);
        s.surfaceContainerHigh = n.tone(17);
        s.surfaceContainerHighest = n.tone(22);
        s.outline = nv.tone(60);          s.outlineVariant = nv.tone(30);
        s.inverseSurface = n.tone(90);    s.inverseOnSurface = n.tone(20);
        s.inversePrimary = primary.tone(40);
    } else {
        s.surface = n.tone(98);           s.onSurface = n.tone(10);
        s.surfaceVariant = nv.tone(90);   s.onSurfaceVariant = nv.tone(30);
        s.surfaceDim = n.tone(87);        s.surfaceBright = n.tone(98);
        s.surfaceContainerLowest = n.tone(100);
        s.surfaceContainerLow = n.tone(96);
        s.surfaceContainer = n.tone(94);
        s.surfaceContainerHigh = n.tone(92);
        s.surfaceContainerHighest = n.tone(90);
        s.outline = nv.tone(50);          s.outlineVariant = nv.tone(80);
        s.inverseSurface = n.tone(20);    s.inverseOnSurface = n.tone(95);
        s.inversePrimary = primary.tone(80);
    }
    s.scrim = {0, 0, 0, 1};
    s.shadow = {0, 0, 0, 1};
    return s;
}

} // namespace imlui
