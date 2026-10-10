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

#include "predefinedfiltersdialog.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QRegularExpression>
#include "highlighteredit.h"
#include "readablecolors.h"
#include <QFileDialog>
#include <QTimer>
#include <QToolButton>
#include <qboxlayout.h>
#include <qcheckbox.h>
#include <qglobal.h>
#include <qwidget.h>

#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"
#include "predefinedfilters.h"

class CenteredCheckbox : public QWidget {
  public:
    explicit CenteredCheckbox( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        auto layout = new QHBoxLayout;
        layout->setAlignment( Qt::AlignCenter );
        checkbox_ = new QCheckBox;
        layout->addWidget( checkbox_ );
        this->setLayout( layout );

        QPalette palette = this->palette();
        palette.setColor( QPalette::Base, palette.color( QPalette::Window ) );
        checkbox_->setPalette( palette );
    }

    bool isChecked() const
    {
        return checkbox_->isChecked();
    }

    void setChecked( bool isChecked )
    {
        checkbox_->setChecked( isChecked );
    }

  private:
    QCheckBox* checkbox_;
};

PredefinedFiltersDialog::PredefinedFiltersDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );
    resize( 1000, 560 );
    setMinimumSize( 700, 360 );
    filtersTableWidget->setAlternatingRowColors( true );
    filtersTableWidget->setSelectionMode( QAbstractItemView::SingleSelection );

    populateFiltersTable( PredefinedFiltersCollection::getSynced().getFilters() );

    connect( addFilterButton, &QToolButton::clicked, this, &PredefinedFiltersDialog::addFilter );
    connect( removeFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::removeFilter );
    connect( upButton, &QToolButton::clicked, this, &PredefinedFiltersDialog::moveFilterUp );
    connect( downButton, &QToolButton::clicked, this, &PredefinedFiltersDialog::moveFilterDown );
    connect( importFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::importFilters );
    connect( exportFilterButton, &QToolButton::clicked, this,
             &PredefinedFiltersDialog::exportFilters );

    connect( buttonBox, &QDialogButtonBox::clicked, this,
             &PredefinedFiltersDialog::resolveStandardButton );

    connect( filtersTableWidget, &QTableWidget::currentCellChanged, this,
             &PredefinedFiltersDialog::onCurrentCellChanged );

    dispatchToMainThread( [ this ] {
        IconLoader iconLoader( this );

        addFilterButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        removeFilterButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        upButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        downButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    } );
}

PredefinedFiltersDialog::PredefinedFiltersDialog( const QString& newFilter, QWidget* parent )
    : PredefinedFiltersDialog( parent )
{
    if ( !newFilter.isEmpty() ) {
        addFilterRow( newFilter );
    }
}

void PredefinedFiltersDialog::updateButtons()
{
    const auto filtersCount = filtersTableWidget->rowCount();
    removeFilterButton->setEnabled( filtersCount > 0 );

    updateUpDownButtons( filtersTableWidget->currentRow() );
}

void PredefinedFiltersDialog::onCurrentCellChanged( int currentRow, int currentColumn,
                                                    int previousRow, int previousColumn )
{
    Q_UNUSED( currentColumn )
    Q_UNUSED( previousRow )
    Q_UNUSED( previousColumn )

    updateUpDownButtons( currentRow );
}

void PredefinedFiltersDialog::updateUpDownButtons( int currentRow )
{
    upButton->setEnabled( currentRow > 0 );
    downButton->setEnabled( currentRow >= 0 && currentRow < filtersTableWidget->rowCount() - 1 );
}

void PredefinedFiltersDialog::populateFiltersTable(
    const PredefinedFiltersCollection::Collection& filters )
{
    filtersTableWidget->clear();

    filtersTableWidget->setRowCount( static_cast<int>( filters.size() ) );
    filtersTableWidget->setColumnCount( 5 );
    filtersTableWidget->setHorizontalHeaderLabels( {
        tr( "Name" ), tr( "Pattern" ), tr( "Regex" ), tr( "Color" ), tr( "Highlight" ) } );
    for ( int i = 0; i < filters.size(); ++i ) setFilterRow( i, filters[i] );
    auto* header = filtersTableWidget->horizontalHeader();
    header->setSectionResizeMode( 0, QHeaderView::Interactive );
    header->resizeSection( 0, 180 );
    header->setSectionResizeMode( 1, QHeaderView::Stretch );
    for ( int i = 2; i < 5; ++i ) header->setSectionResizeMode( i, QHeaderView::ResizeToContents );
    filtersTableWidget->verticalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    filtersTableWidget->setWordWrap( false );

    updateButtons();
}

void PredefinedFiltersDialog::saveSettings() const
{
    PredefinedFiltersCollection::getSynced().saveToStorage( readFiltersTable() );
}

void PredefinedFiltersDialog::setFilterRow( int row, const PredefinedFilter& filter )
{
    filtersTableWidget->setItem( row, 0, new QTableWidgetItem( filter.name ) );
    filtersTableWidget->setItem( row, 1, new QTableWidgetItem( filter.pattern ) );
    auto* regex = new CenteredCheckbox;
    regex->setChecked( filter.useRegex );
    filtersTableWidget->setCellWidget( row, 2, regex );
    auto* color = new QPushButton( "Aa" );
    color->setObjectName( "filterColorButton" );
    color->setMinimumWidth( 64 );
    const auto updateColor = [color]( const QColor& value ) {
        color->setProperty( "filterColor", value );
        color->setStyleSheet( QString( "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid %2; border-radius: 3px; padding: 4px 12px; }" )
            .arg( value.name(), readableForeground( value ).name() ) );
        color->setToolTip( value.name() );
    };
    updateColor( filter.color.isValid() ? filter.color : automaticFilterColor( filter.name ) );
    auto* mode = new QComboBox;
    mode->setObjectName( "filterHighlightMode" );
    mode->addItems( { tr( "Search highlight" ), tr( "Keywords" ), tr( "Whole line" ) } );
    mode->setCurrentIndex( static_cast<int>( filter.highlightMode ) );
    mode->setToolTip( tr( "Applied to enabled filters in both log views. When whole-line rules overlap, the first filter wins; keyword colors remain visible." ) );
    connect( color, &QPushButton::clicked, color, [color, mode, updateColor] {
        QColor chosen;
        if ( HighlighterEdit::showColorPicker( color->property( "filterColor" ).value<QColor>(), chosen ) ) {
            chosen.setAlpha( 255 );
            updateColor( chosen );
            if ( mode->currentIndex() == 0 ) mode->setCurrentIndex( 1 );
        }
    } );
    filtersTableWidget->setCellWidget( row, 3, color );
    filtersTableWidget->setCellWidget( row, 4, mode );
}

PredefinedFilter PredefinedFiltersDialog::readFilterRow( int row ) const
{
    PredefinedFilter filter;
    if ( auto* item = filtersTableWidget->item( row, 0 ) ) filter.name = item->text();
    if ( auto* item = filtersTableWidget->item( row, 1 ) ) filter.pattern = item->text();
    auto* regex = static_cast<CenteredCheckbox*>( filtersTableWidget->cellWidget( row, 2 ) );
    filter.useRegex = regex && regex->isChecked();
    auto* color = filtersTableWidget->cellWidget( row, 3 );
    if ( color ) filter.color = color->property( "filterColor" ).value<QColor>();
    auto* mode = qobject_cast<QComboBox*>( filtersTableWidget->cellWidget( row, 4 ) );
    if ( mode ) filter.highlightMode = static_cast<PredefinedFilter::HighlightMode>( mode->currentIndex() );
    return filter;
}

PredefinedFiltersCollection::Collection PredefinedFiltersDialog::readFiltersTable() const
{
    PredefinedFiltersCollection::Collection filters;
    for ( int row = 0; row < filtersTableWidget->rowCount(); ++row ) {
        const auto filter = readFilterRow( row );
        if ( !filter.name.isEmpty() && !filter.pattern.isEmpty() ) filters.append( filter );
    }
    return filters;
}

void PredefinedFiltersDialog::addFilter()
{
    addFilterRow( {} );
}

void PredefinedFiltersDialog::addFilterRow( const QString& newFilter )
{
    const int row = filtersTableWidget->rowCount();
    filtersTableWidget->insertRow( row );
    PredefinedFilter filter{ {}, newFilter, false };
    filter.color = QColor( readableColorPalette()[ static_cast<size_t>( row ) % readableColorPalette().size() ] );
    filter.highlightMode = PredefinedFilter::HighlightMode::Keywords;
    setFilterRow( row, filter );
    filtersTableWidget->setCurrentCell( row, 0 );
    filtersTableWidget->scrollToItem( filtersTableWidget->item( row, 0 ) );
    filtersTableWidget->editItem( filtersTableWidget->item( row, 0 ) );
    filtersTableWidget->resizeRowToContents( row );
    updateButtons();
}

void PredefinedFiltersDialog::removeFilter()
{
    filtersTableWidget->removeRow( filtersTableWidget->currentRow() );

    updateButtons();
}

void PredefinedFiltersDialog::moveFilterUp()
{
    const auto currentRow = filtersTableWidget->currentRow();
    const auto selectedColumn = filtersTableWidget->currentColumn();

    if ( currentRow >= 0 ) {
        swapFilters( currentRow, currentRow - 1, selectedColumn );
    }
}

void PredefinedFiltersDialog::moveFilterDown()
{
    const auto currentRow = filtersTableWidget->currentRow();
    const auto selectedColumn = filtersTableWidget->currentColumn();

    if ( currentRow >= 0 ) {
        swapFilters( currentRow, currentRow + 1, selectedColumn );
    }
}

void PredefinedFiltersDialog::swapFilters( int currentRow, int newRow, int selectedColumn )
{
    if ( newRow < 0 || newRow >= filtersTableWidget->rowCount() ) return;
    PredefinedFiltersCollection::Collection rows;
    for ( int row = 0; row < filtersTableWidget->rowCount(); ++row ) rows.append( readFilterRow( row ) );
    rows.swapItemsAt( currentRow, newRow );
    populateFiltersTable( rows );
    filtersTableWidget->setCurrentCell( newRow, selectedColumn );
}

void PredefinedFiltersDialog::importFilters()
{
    const auto file
        = QFileDialog::getOpenFileName( this, tr( "Select file to import" ), "",
                                        tr( "Predefined filters (*.conf);;All files (*)" ) );

    if ( file.isEmpty() ) {
        return;
    }

    LOG_DEBUG << "Loading predefined filters from " << file;
    QSettings settings{ file, QSettings::IniFormat };

    PredefinedFiltersCollection collection;
    collection.retrieveFromStorage( settings );
    populateFiltersTable( collection.getFilters() );
}

void PredefinedFiltersDialog::exportFilters()
{
    auto file = QFileDialog::getSaveFileName( this, tr( "Export predefined filters" ), "",
                                              tr( "Predefined filters (*.conf)" ) );

    if ( file.isEmpty() ) {
        return;
    }

    if ( !file.endsWith( ".conf" ) ) {
        file += ".conf";
    }

    QSettings settings{ file, QSettings::IniFormat };

    PredefinedFiltersCollection collection;
    collection.setFilters( readFiltersTable() );
    collection.saveToStorage( settings );
}

void PredefinedFiltersDialog::resolveStandardButton( QAbstractButton* button )
{
    LOG_DEBUG << "PredefinedFiltersDialog::resolveStandardButton";

    const auto role = buttonBox->buttonRole( button );
    if ( role == QDialogButtonBox::ApplyRole || role == QDialogButtonBox::AcceptRole ) {
        for ( int row = 0; row < filtersTableWidget->rowCount(); ++row ) {
            const auto filter = readFilterRow( row );
            if ( filter.name.isEmpty() && filter.pattern.isEmpty() ) continue;
            if ( filter.name.trimmed().isEmpty() || filter.pattern.isEmpty() ) {
                filtersTableWidget->setCurrentCell( row, filter.name.trimmed().isEmpty() ? 0 : 1 );
                QMessageBox::warning( this, tr( "Incomplete filter" ), tr( "Enter both a name and a pattern." ) );
                return;
            }
            const QRegularExpression expression( filter.pattern );
            if ( filter.useRegex && !expression.isValid() ) {
                filtersTableWidget->setCurrentCell( row, 1 );
                QMessageBox::warning( this, tr( "Invalid regular expression" ), expression.errorString() );
                return;
            }
        }
    }

    switch ( role ) {
    case QDialogButtonBox::RejectRole:
        reject();
        return;

    case QDialogButtonBox::ApplyRole:
        saveSettings();
        break;

    case QDialogButtonBox::AcceptRole:
        saveSettings();
        accept();
        break;
    default:
        LOG_ERROR << "PredefinedFiltersDialog::resolveStandardButton unhandled role: " << role;
        return;
    }

    Q_EMIT optionsChanged();
}
