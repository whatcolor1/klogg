/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "predefinedfilters.h"

#include "log.h"

void PredefinedFiltersCollection::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "PredefinedFiltersCollection::retrieveFromStorage";

    if ( settings.contains( "PredefinedFiltersCollection/version" ) ) {
        settings.beginGroup( "PredefinedFiltersCollection" );
        if ( settings.value( "version" ).toInt() <= PredefinedFiltersCollection_VERSION ) {
            filters_.clear();

            int size = settings.beginReadArray( "filters" );

            filters_.reserve( size );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );

                filters_.push_back( { settings.value( "name" ).toString(),
                                      settings.value( "filter" ).toString(),
                                      settings.value( "regex", true ).toBool(),
                                      QColor( settings.value( "color" ).toString() ),
                                      static_cast<PredefinedFilter::HighlightMode>(
                                          qBound( 0, settings.value( "highlight_mode", 0 ).toInt(), 2 ) ),
                                      settings.value( "group" ).toString() } );
            }
            settings.endArray();
            combinations_.clear();
            const int count = settings.beginReadArray( "combinations" );
            for ( int i = 0; i < count; ++i ) {
                settings.setArrayIndex( i );
                FilterCombination combination;
                combination.name = settings.value( "name" ).toString();
                const int members = settings.beginReadArray( "members" );
                for ( int j = 0; j < members; ++j ) {
                    settings.setArrayIndex( j );
                    combination.filters.append( { settings.value( "name" ).toString(),
                        settings.value( "pattern" ).toString(), settings.value( "regex", true ).toBool() } );
                }
                settings.endArray();
                if ( !combination.name.isEmpty() ) combinations_.append( combination );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of PredefinedFiltersCollection, ignoring it...";
        }
        settings.endGroup();
    }
}

void PredefinedFiltersCollection::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "PredefinedFiltersCollection::saveToStorage";

    settings.beginGroup( "PredefinedFiltersCollection" );
    settings.setValue( "version", PredefinedFiltersCollection_VERSION );

    settings.remove( "filters" );

    settings.beginWriteArray( "filters" );
    int arrayIndex = 0;
    for ( const auto& filter : filters_ ) {
        settings.setArrayIndex( arrayIndex );
        settings.setValue( "name", filter.name );
        settings.setValue( "filter", filter.pattern );
        settings.setValue( "regex", filter.useRegex );
        settings.setValue( "group", filter.group );
        settings.setValue( "color", filter.color.isValid() ? filter.color.name( QColor::HexArgb ) : QString{} );
        settings.setValue( "highlight_mode", static_cast<int>( filter.highlightMode ) );

        arrayIndex++;
    }
    settings.endArray();
    settings.remove( "combinations" );
    settings.beginWriteArray( "combinations", combinations_.size() );
    for ( int i = 0; i < combinations_.size(); ++i ) {
        settings.setArrayIndex( i );
        settings.setValue( "name", combinations_[i].name );
        settings.beginWriteArray( "members", combinations_[i].filters.size() );
        for ( int j = 0; j < combinations_[i].filters.size(); ++j ) {
            settings.setArrayIndex( j );
            const auto& f = combinations_[i].filters[j];
            settings.setValue( "name", f.name );
            settings.setValue( "pattern", f.pattern );
            settings.setValue( "regex", f.useRegex );
        }
        settings.endArray();
    }
    settings.endArray();
    settings.endGroup();
    settings.sync();
}

void PredefinedFiltersCollection::saveToStorage(
    const PredefinedFiltersCollection::Collection& filters )
{
    filters_ = filters;
    this->save();
}

PredefinedFiltersCollection::Collection PredefinedFiltersCollection::getFilters() const
{
    return filters_;
}

PredefinedFiltersCollection::Collection PredefinedFiltersCollection::getSyncedFilters()
{
    const auto& synced = this->getSynced();
    filters_ = synced.getFilters();
    combinations_ = synced.combinations();
    return filters_;
}

void PredefinedFiltersCollection::setFilters( const Collection& filters )
{
    filters_ = filters;
}
