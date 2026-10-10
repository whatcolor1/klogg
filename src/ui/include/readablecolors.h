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

inline QColor readableForeground( const QColor& background )
{
    const auto linear = []( double value ) {
        return value <= 0.04045 ? value / 12.92 : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
    };
    const double luminance = 0.2126 * linear( background.redF() )
        + 0.7152 * linear( background.greenF() ) + 0.0722 * linear( background.blueF() );
    return luminance > 0.179 ? QColor( "#18212f" ) : QColor( Qt::white );
}

inline QColor automaticFilterColor( const QString& name )
{
    // Stable across runs and Qt versions; avoid a process-seeded string hash.
    quint32 hash = 2166136261u;
    for ( const auto ch : name ) hash = ( hash ^ ch.unicode() ) * 16777619u;
    const auto& colors = readableColorPalette();
    return QColor( colors[ hash % colors.size() ] );
}
