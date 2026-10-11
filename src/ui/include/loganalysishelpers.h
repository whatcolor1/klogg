#pragma once

#include <QDate>
#include <QRegularExpression>
#include <QString>
#include <optional>

namespace LogAnalysis {
// A field and its value must be selected together, or the selected value must
// identify exactly one ID field on the line. Never fall back to a bare number.
inline QString fieldPattern( const QString& line, const QString& selection )
{
    const QRegularExpression fields( QStringLiteral(
        R"re(\b([A-Za-z_][A-Za-z0-9_]*)\s*[=:]\s*["']?([A-Za-z0-9_.-]+))re" ) );
    auto matches = fields.globalMatch( line );
    QString result;
    int count = 0;
    while ( matches.hasNext() ) {
        const auto match = matches.next();
        const auto field = match.captured( 1 );
        const auto value = match.captured( 2 );
        const QRegularExpression idName( QStringLiteral( "(?:id|number)$|^(?:frame|request|session|thread)$" ),
            QRegularExpression::CaseInsensitiveOption );
        if ( !idName.match( field ).hasMatch() ) continue;
        auto selected = selection.trimmed();
        if ( selected.endsWith( '"' ) || selected.endsWith( '\'' ) ) selected.chop( 1 );
        if ( selected != value && selected != match.captured().trimmed() ) continue;
        ++count;
        result = QStringLiteral( R"re(\b%1\s*[=:]\s*["']?%2(?![A-Za-z0-9_.-]))re" )
            .arg( QRegularExpression::escape( field ), QRegularExpression::escape( value ) );
    }
    return count == 1 ? result : QString{};
}

struct Timestamp {
    qint64 microseconds;
    bool dated;
    bool yearKnown;
};

inline std::optional<Timestamp> timestamp( const QString& line )
{
    // Only a timestamp at the start of the entry is accepted. Fractional seconds
    // retain microsecond precision; timezone suffixes are deliberately unsupported.
    const QRegularExpression expression( QStringLiteral(
        R"re(^\s*\[?(?:(?:(\d{4})-)?(\d{2})-(\d{2})[T ])?(\d{2}):(\d{2}):(\d{2})(?:[.,](\d{1,6}))?(?![\d.,Zz+\-]))re" ) );
    const auto m = expression.match( line );
    if ( !m.hasMatch() ) return std::nullopt;
    const int hours = m.captured( 4 ).toInt();
    const int minutes = m.captured( 5 ).toInt();
    const int seconds = m.captured( 6 ).toInt();
    if ( hours > 23 || minutes > 59 || seconds > 59 ) return std::nullopt;
    const bool dated = !m.captured( 2 ).isEmpty();
    qint64 days = 0;
    if ( dated ) {
        const QDate date( m.captured( 1 ).isEmpty() ? 2000 : m.captured( 1 ).toInt(),
            m.captured( 2 ).toInt(), m.captured( 3 ).toInt() );
        if ( !date.isValid() ) return std::nullopt;
        days = date.toJulianDay();
    }
    const auto fraction = m.captured( 7 ).leftJustified( 6, '0' ).toLongLong();
    return Timestamp{ ( ( days * 24 + hours ) * 3600 + minutes * 60 + seconds ) * 1000000 + fraction,
        dated, !m.captured( 1 ).isEmpty() };
}

inline std::optional<qint64> elapsedMicroseconds( const QString& start, const QString& end )
{
    const auto a = timestamp( start ), b = timestamp( end );
    if ( !a || !b || a->dated != b->dated || a->yearKnown != b->yearKnown ) return std::nullopt;
    const auto delta = b->microseconds - a->microseconds;
    // Negative time-only differences could mean midnight or reordered logs;
    // guessing would hide a potentially important timing problem.
    if ( delta < 0 ) return std::nullopt;
    return delta;
}
}
