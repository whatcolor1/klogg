#pragma once

#include <QColor>
#include <QString>
#include <array>
#include <cmath>

// Soft backgrounds with enough contrast for long log-reading sessions.
inline const std::array<const char*, 18>& readableColorPalette()
{
    static const std::array<const char*, 18> colors = {{
        "#dbeafe", "#fce7f3", "#dcfce7", "#fef3c7", "#ede9fe", "#ccfbf1",
        "#e0f2fe", "#e0e7ff", "#e5e7eb", "#ffedd5", "#ffe4e6", "#ecfccb",
        "#cffafe", "#f3e8ff", "#fae8ff", "#fef9c3", "#d1fae5", "#f1f5f9"
    }};
    return colors;
}

// Saturated alternatives for buttons and keyword emphasis; soft defaults stay unchanged.
inline const std::array<const char*, 18>& vividColorPalette()
{
    static const std::array<const char*, 18> colors = {{
        "#dc2626", "#ea580c", "#f59e0b", "#facc15", "#65a30d", "#16a34a",
        "#059669", "#0d9488", "#0891b2", "#0284c7", "#2563eb", "#4338ca",
        "#7c3aed", "#9333ea", "#c026d3", "#db2777", "#e11d48", "#334155"
    }};
    return colors;
}

inline QColor readableForeground( const QColor& background )
{
    const auto linear = []( double value ) {
        return value <= 0.04045 ? value / 12.92 : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
    };
    const double luminance = 0.2126 * linear( background.redF() )
        + 0.7152 * linear( background.greenF() ) + 0.0722 * linear( background.blueF() );
    const QColor dark( "#18212f" );
    const double darkLuminance = 0.2126 * linear( dark.redF() )
        + 0.7152 * linear( dark.greenF() ) + 0.0722 * linear( dark.blueF() );
    if ( ( luminance + 0.05 ) / ( darkLuminance + 0.05 ) >= 4.5 ) return dark;
    return 1.05 / ( luminance + 0.05 ) >= 4.5 ? QColor( Qt::white ) : QColor( Qt::black );
}

inline QColor automaticFilterColor( const QString& name )
{
    // Stable across runs and Qt versions; avoid a process-seeded string hash.
    quint32 hash = 2166136261u;
    for ( const auto ch : name ) hash = ( hash ^ ch.unicode() ) * 16777619u;
    const auto& colors = readableColorPalette();
    return QColor( colors[ hash % colors.size() ] );
}
