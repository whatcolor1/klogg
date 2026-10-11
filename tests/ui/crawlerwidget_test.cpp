/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <catch2/catch.hpp>
#include <algorithm>

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include "predefinedfiltersbuttonpanel.h"
#include "sessioninfo.h"
#include "configuration.h"
#include "readablecolors.h"
#include "loganalysishelpers.h"
#include "analysisstorage.h"
#include "colorlabelsmanager.h"
#include "ui_predefinedfiltersdialog.h"
#include "predefinedfiltersdialog.h"
#include "ui_highlightersdialog.h"
#include "ui_highlighteredit.h"
#include "ui_highlightersetedit.h"
#include "highlightersdialog.h"
#include <QPushButton>
#include <QScrollArea>
#include <QMenuBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QLineEdit>
#include <QSignalBlocker>
#include "styles.h"
#include <qglobal.h>
#include <qnamespace.h>
#include <qtestmouse.h>

#include "savedsearches.h"
#include "session.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

#include "crawlerwidget.h"

static const qint64 SL_NB_LINES = 100LL;

TEST_CASE( "Field tracking distinguishes IDs and elapsed time refuses ambiguous timestamps", "[ui][analysis]" )
{
    using namespace LogAnalysis;
    const auto pattern = fieldPattern( "frameNumber=1234 requestId=87", "1234" );
    REQUIRE_FALSE( pattern.isEmpty() );
    QRegularExpression expression( pattern );
    REQUIRE( expression.isValid() );
    REQUIRE( expression.match( "frameNumber: 1234 result" ).hasMatch() );
    REQUIRE_FALSE( expression.match( "frameNumber=12345" ).hasMatch() );
    REQUIRE_FALSE( expression.match( "requestId=1234" ).hasMatch() );
    REQUIRE_FALSE( expression.match( "frameNumber=1234.5" ).hasMatch() );
    REQUIRE( fieldPattern( "frameNumber=87 requestId=87", "87" ).isEmpty() );
    REQUIRE_FALSE( fieldPattern( "frameNumber=87 requestId=87", "requestId=87" ).isEmpty() );
    REQUIRE( fieldPattern( "exposure=1234", "1234" ).isEmpty() );
    REQUIRE_FALSE( fieldPattern( "requestId=ab.c", "ab.c" ).isEmpty() );
    REQUIRE( elapsedMicroseconds( "10-11 00:00:00.000001 A", "10-11 00:00:00.123456 B" ) == 123455 );
    REQUIRE( elapsedMicroseconds( "2026-10-11T23:59:59.900 A", "2026-10-12T00:00:00.100 B" ) == 200000 );
    REQUIRE( elapsedMicroseconds( "12:00:00 A", "12:00:01,25 B" ) == 1250000 );
    REQUIRE_FALSE( elapsedMicroseconds( "23:59:59 A", "00:00:01 B" ) );
    REQUIRE_FALSE( elapsedMicroseconds( "12:00:00 A", "2026-10-11 12:00:01 B" ) );
    REQUIRE_FALSE( timestamp( "2026-02-30 12:00:00 A" ) );
    REQUIRE_FALSE( timestamp( "25:00:00 A" ) );
    REQUIRE_FALSE( timestamp( "12:00:00.1234567 A" ) );
    REQUIRE_FALSE( timestamp( "2026-10-11T12:00:00Z A" ) );
    REQUIRE_FALSE( timestamp( "message contains 12:00:00" ) );
}

TEST_CASE( "Filter groups preserve active members and combinations restore atomically", "[ui][presets][groups]" )
{
    using Mode = PredefinedFilter::HighlightMode;
    const QList<PredefinedFilter> filters = {
        { "Streams", "configure_streams", false, QColor( "#dbeafe" ), Mode::Keywords, "Setup" },
        { "Requests", "request", false, QColor( "#ffe4e6" ), Mode::Keywords, "Capture" },
        { "Errors", "error", false }
    };
    auto& settings = PredefinedFiltersCollection::getSynced();
    const auto oldCombinations = settings.combinations();
    struct Restore {
        QList<FilterCombination> entries;
        ~Restore() { auto& c = PredefinedFiltersCollection::getSynced(); c.setCombinations( entries ); c.save(); }
    } restore{ oldCombinations };
    PredefinedFiltersButtonPanel panel;
    panel.resize( 1600, 100 );
    panel.setFilters( filters );
    panel.show();
    panel.setActiveFilters( { filters[0], filters[1] } );
    QSignalSpy changed( &panel, &PredefinedFiltersButtonPanel::filtersChanged );
    panel.setGroup( "Capture" );
    REQUIRE( panel.activeFilters().size() == 2 );
    REQUIRE( changed.count() == 0 );
    for ( auto* button : panel.findChildren<QToolButton*>() ) {
        if ( button->text() == "Streams" ) REQUIRE( button->isHidden() );
        if ( button->text() == "Requests" ) REQUIRE_FALSE( button->isHidden() );
    }
    REQUIRE( panel.saveCombination( "test capture combination" ) );
    panel.setActiveFilters( {} );
    REQUIRE( panel.applyCombination( "test capture combination" ) );
    REQUIRE( panel.activeFilters().size() == 2 );
    REQUIRE( changed.count() == 1 );
    panel.setFilters( { filters[1] } );
    REQUIRE_FALSE( panel.applyCombination( "test capture combination" ) );
    REQUIRE( panel.activeFilters().size() == 1 );
    REQUIRE( changed.count() == 1 );

    QTemporaryDir directory;
    QSettings disk( directory.filePath( "groups.ini" ), QSettings::IniFormat );
    PredefinedFiltersCollection collection;
    collection.setFilters( filters );
    collection.setCombinations( { { "Capture", { filters[0], filters[1] } } } );
    collection.saveToStorage( disk );
    PredefinedFiltersCollection recovered;
    recovered.retrieveFromStorage( disk );
    REQUIRE( recovered.getFilters()[0].group == "Setup" );
    REQUIRE( recovered.combinations().size() == 1 );
    REQUIRE( recovered.combinations()[0].filters.size() == 2 );
    REQUIRE( recovered.combinations()[0].filters[1].pattern == "request" );
}

namespace {
bool generateDataFiles( QTemporaryFile& file )
{
    char newLine[ 90 ];

    if ( file.open() ) {
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            snprintf( newLine, 89,
                      "LOGDATA \t is a part of glogg, we are going to test it thoroughly, this is "
                      "line %06d",
                      i );
            file.write( newLine, static_cast<qint64>( qstrlen( newLine ) ) );
#ifdef Q_OS_WIN
            file.write( "\r\n", 2 );
#else
            file.write( "\n", 1 );
#endif
        }
        file.flush();
    }

    return true;
}

} // namespace

struct CrawlerWidgetPrivate {
};

template <>
struct CrawlerWidget::access_by<CrawlerWidgetPrivate> {
    std::unique_ptr<CrawlerWidget> crawler;

    bool isLoadingFinished()
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount getLogNbLines()
    {
        return crawler->logData_->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return crawler->logFilteredData_->getNbLine();
    }

    void selectAllInMainView()
    {
        crawler->logMainView_->selectAll();
    }

    void selectAllInFilteredView()
    {
        crawler->filteredView_->selectAll();
    }

    QString mainViewSelectedText()
    {
        return crawler->logMainView_->getSelectedText();
    }

    QString filteredViewSelectedText()
    {
        return crawler->filteredView_->getSelectedText();
    }

    QString firstFilteredLine()
    {
        return crawler->logFilteredData_->getNbLine().get() > 0
            ? crawler->logFilteredData_->getLineString( 0_lnum ) : QString{};
    }

    bool filteredContains( const QString& pattern )
    {
        const auto lines = crawler->logFilteredData_->getLines(
            0_lnum, crawler->logFilteredData_->getNbLine() );
        return std::any_of( lines.cbegin(), lines.cend(),
            [&pattern]( const auto& line ) { return line.contains( pattern ); } );
    }

    bool bothViewsHaveHighlight( const QColor& color )
    {
        const auto containsColor = [&color]( QWidget* widget ) {
            const auto image = widget->grab().toImage();
            for ( int y = 0; y < image.height(); ++y ) {
                for ( int x = 0; x < image.width(); ++x ) {
                    if ( image.pixelColor( x, y ) == color ) return true;
                }
            }
            return false;
        };
        return containsColor( crawler->logMainView_->viewport() )
            && containsColor( crawler->filteredView_->viewport() );
    }

    void saveHighlightDiagnostics()
    {
        crawler->logMainView_->viewport()->grab().save( "highlight-main.png" );
        crawler->filteredView_->viewport()->grab().save( "highlight-filtered.png" );
    }

    qsizetype filteredColorPixels( const QColor& color )
    {
        const auto image = crawler->filteredView_->viewport()->grab().toImage();
        qsizetype pixels = 0;
        for ( int y = 0; y < image.height(); ++y )
            for ( int x = 0; x < image.width(); ++x )
                if ( image.pixelColor( x, y ) == color ) ++pixels;
        return pixels;
    }

    void toggleMark()
    {
        crawler->logMainView_->selectAndDisplayLine( 10_lnum );
        crawler->activateWindow();
        crawler->logMainView_->setFocus();
        QTest::qWait( 20 );
        QTest::keyClick( crawler->logMainView_, Qt::Key_M );
    }

    LinesCount markCount() { return crawler->logFilteredData_->getNbMarks(); }

    void trackField( const QString& pattern ) { crawler->trackField( pattern ); }

    void setHistoryEntries( const QStringList& entries )
    {
        const QSignalBlocker blocker( crawler->searchLineEdit_ );
        crawler->searchLineEdit_->clear();
        crawler->searchLineEdit_->addItems( entries );
        crawler->searchLineEdit_->setCurrentIndex( 0 );
    }
    void browseHistory( Qt::Key key ) { QTest::keyClick( crawler->searchLineEdit_, key ); }
    QString draftText() { return crawler->searchLineEdit_->currentText(); }
    void editDraft( const QString& text )
    {
        auto* edit = crawler->searchLineEdit_->lineEdit();
        edit->selectAll();
        QTest::keyClick( edit, Qt::Key_Backspace );
        QTest::keyClicks( edit, text );
    }

    bool markersStayInGutter()
    {
        for ( auto* view : { static_cast<AbstractLogView*>( crawler->logMainView_ ),
                            static_cast<AbstractLogView*>( crawler->filteredView_ ) } ) {
            const auto image = view->viewport()->grab().toImage();
            for ( int y = 0; y < image.height(); ++y ) {
                for ( int x = image.width() / 2; x < image.width(); ++x ) {
                    if ( image.pixelColor( x, y ) == QColor( "#c2410c" ) ) return false;
                }
            }
        }
        return true;
    }

    void verifyInterfaceFontIsolation()
    {
        const auto uiFont = QApplication::font();
        const auto originalLogFont = crawler->logMainView_->font();
        QFont largeLogFont( "Courier New", 28, QFont::Bold );
        crawler->logMainView_->updateFont( largeLogFont );
        REQUIRE( crawler->logMainView_->font().pointSize() == 28 );
        auto* panel = crawler->findChild<PredefinedFiltersButtonPanel*>();
        REQUIRE( panel->font() == uiFont );
        const auto menus = crawler->logMainView_->findChildren<QMenu*>();
        REQUIRE_FALSE( menus.isEmpty() );
        for ( auto* menu : menus ) {
            REQUIRE( menu->font().pointSizeF() == uiFont.pointSizeF() );
            REQUIRE_FALSE( menu->font().bold() );
        }
        crawler->logMainView_->updateFont( originalLogFont );
    }

    void setSearchPattern( const QString& pattern )
    {
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
    }

    void enableCaseSensitiveSearch()
    {
        if ( !crawler->matchCaseButton_->isChecked() ) {
            QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableInverseMatch()
    {
        if ( !crawler->inverseButton_->isChecked() ) {
            QTest::mouseClick( crawler->inverseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableBooleanCombinationMode()
    {
        if ( !crawler->booleanButton_->isChecked() ) {
            QTest::mouseClick( crawler->booleanButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void runSearch()
    {
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        QTest::qWait( 100 );

        waitUiState( [ & ]() { return crawler->stopButton_->isHidden(); } );
    }

    void render()
    {
        crawler->grab();
    }
};

using CrawlerWidgetVisitor = CrawlerWidget::access_by<CrawlerWidgetPrivate>;

SCENARIO( "Crawler widget search", "[ui]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    session.savedSearches().clear();

    REQUIRE( session.savedSearches().recentSearches().empty() );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();

    REQUIRE( crawlerVisitor.getLogNbLines().get() == SL_NB_LINES );

    GIVEN( "loaded log data" )
    {
        THEN( "Has no lines in log view" )
        {
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }

        WHEN( "search for lines" )
        {
            crawlerVisitor.setSearchPattern( "this is line" );
            crawlerVisitor.runSearch();

            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "all lines are matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }

            AND_WHEN( "copy all from main view" )
            {
                crawlerVisitor.selectAllInMainView();
                auto text = crawlerVisitor.mainViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }

            AND_WHEN( "copy all from filtered view" )
            {
                crawlerVisitor.selectAllInFilteredView();
                auto text = crawlerVisitor.filteredViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }
        }

        WHEN( "search for 10" )
        {
            crawlerVisitor.setSearchPattern( "10" );

            crawlerVisitor.runSearch();

            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } );

            THEN( "single line match" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 1 );
            }
        }

        WHEN( "case sensitive search" )
        {
            crawlerVisitor.setSearchPattern( "THIS" );
            crawlerVisitor.enableCaseSensitiveSearch();
            crawlerVisitor.runSearch();

            THEN( "no lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
            }
        }

        WHEN( "inverse match search" )
        {
            crawlerVisitor.setSearchPattern( "not match" );
            crawlerVisitor.enableInverseMatch();
            crawlerVisitor.runSearch();

            THEN( "all lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }
        }

        WHEN( "boolean search" )
        {
            crawlerVisitor.setSearchPattern( "\"glogg\" or \"klogg\"" );
            crawlerVisitor.enableBooleanCombinationMode();
            crawlerVisitor.runSearch();

            THEN( "has lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() >= 2 );
            }
        }
    }
}

TEST_CASE( "Preset panel bounds overflow and keeps menu selection visible", "[ui][presets]" )
{
    PredefinedFiltersButtonPanel panel;
    QList<PredefinedFilter> filters;
    for ( int i = 0; i < 20; ++i ) {
        filters.append( { QString( "Filter %1" ).arg( i ), QString::number( i ), false } );
    }
    panel.setFilters( filters );
    panel.resize( 260, panel.sizeHint().height() );
    panel.show();
    QTest::qWait( 20 );
    auto* more = panel.findChild<QToolButton*>( "moreFiltersButton" );
    REQUIRE( more );
    REQUIRE_FALSE( more->isHidden() );
    REQUIRE_FALSE( more->menu()->actions().isEmpty() );
    QSet<int> rows;
    for ( auto* button : panel.findChildren<QToolButton*>() ) {
        if ( button->isHidden() ) continue;
        rows.insert( button->y() );
        REQUIRE( panel.rect().contains( button->geometry() ) );
    }
    REQUIRE( rows.size() <= 2 );
    const int twoRowHeight = panel.height();
    QSignalSpy activated( &panel, &PredefinedFiltersButtonPanel::filtersChanged );
    auto* overflowAction = more->menu()->actions().last();
    overflowAction->trigger();
    REQUIRE( activated.count() == 1 );
    REQUIRE( overflowAction->isChecked() );
    REQUIRE( more->isChecked() );
    panel.resize( 10000, panel.height() );
    QTest::qWait( 20 );
    REQUIRE( more->isHidden() );
    REQUIRE( panel.height() < twoRowHeight );
    const int oneRowHeight = panel.height();
    panel.resize( 1, panel.height() );
    QTest::qWait( 20 );
    REQUIRE_FALSE( more->isHidden() );
    REQUIRE( more->menu()->actions().size() == filters.size() );
    REQUIRE( panel.height() > oneRowHeight );
    panel.setFilters( {} );
    REQUIRE( panel.height() == 0 );
}

TEST_CASE( "Preset clicks run search and recover the selected preset", "[ui][presets][session]" )
{
    auto& config = Configuration::get();
    struct RestoreHighlight {
        Configuration& config;
        bool enabled;
        bool variate;
        QColor color;
        ~RestoreHighlight() {
            config.setEnableMainSearchHighlight( enabled );
            config.setVariateMainSearchHighlight( variate );
            config.setMainSearchBackColor( color );
        }
    } restoreHighlight{ config, config.mainSearchHighlight(), config.variateMainSearchHighlight(),
                        config.mainSearchBackColor() };
    const QColor highlightColor( 17, 231, 197 );
    config.setEnableMainSearchHighlight( true );
    config.setVariateMainSearchHighlight( false );
    config.setMainSearchBackColor( highlightColor );
    QTemporaryFile file{ "preset_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );
    Session session;
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [] { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [&] { return visitor.isLoadingFinished(); } ) );
    visitor.crawler->resize( 1600, 1000 );
    visitor.crawler->show();
    auto* panel = visitor.crawler->findChild<PredefinedFiltersButtonPanel*>();
    REQUIRE( panel );
    const QList<PredefinedFilter> filters = {
        { "Line ten", "000010", false }, { "Line twenty", "000020", false } };
    panel->setFilters( filters );
    const auto buttons = panel->findChildren<QToolButton*>();
    int enabled = 0;
    for ( const auto& filter : filters ) {
        auto button = std::find_if( buttons.cbegin(), buttons.cend(),
            [&filter]( const auto* b ) { return b->text() == filter.name; } );
        REQUIRE( button != buttons.cend() );
        (*button)->click();
        ++enabled;
        REQUIRE( waitUiState( [&] {
            return visitor.getLogFilteredNbLines().get() == enabled
                && visitor.filteredContains( filter.pattern );
        } ) );
        REQUIRE( visitor.getLogFilteredNbLines().get() == enabled );
        REQUIRE( (*button)->isChecked() );
        visitor.render();
        // Data arrives on a worker thread; the queued UI refresh must finish before sampling pixels.
        const bool highlighted = waitUiState( [&] {
            return visitor.bothViewsHaveHighlight( highlightColor );
        } );
        if ( !highlighted ) visitor.saveHighlightDiagnostics();
        INFO( "highlight enabled=" << config.mainSearchHighlight()
              << "; color=" << config.mainSearchBackColor().name().toStdString()
              << "; main top line=" << visitor.crawler->getTopLine().get() );
        REQUIRE( highlighted );
    }
    const auto context = visitor.crawler->context()->toString();
    const auto json = QJsonDocument::fromJson( context.toUtf8() ).object();
    REQUIRE( json.value( "ST" ).toString() == "000010|000020" );
    REQUIRE( json.value( "PF" ).toArray().size() == 2 );
    visitor.crawler->setViewContext( context );
    REQUIRE( panel->activeFilters().size() == 2 );
    CrawlerWidgetVisitor recovered;
    recovered.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [] { return new CrawlerWidget(); } ) ) );
    recovered.crawler->findChild<PredefinedFiltersButtonPanel*>()->setFilters( filters );
    recovered.crawler->setViewContext( context );
    REQUIRE( waitUiState( [&] { return recovered.isLoadingFinished(); } ) );
    REQUIRE( waitUiState( [&] {
        return recovered.getLogFilteredNbLines().get() == 2
            && recovered.filteredContains( "000020" ) && recovered.filteredContains( "000010" );
    } ) );
    auto* recoveredPanel = recovered.crawler->findChild<PredefinedFiltersButtonPanel*>();
    REQUIRE( recoveredPanel->activeFilters().size() == 2 );
    for ( auto* button : panel->findChildren<QToolButton*>() ) {
        if ( button->text() == "Line ten" ) button->click();
    }
    REQUIRE( waitUiState( [&] {
        return visitor.getLogFilteredNbLines().get() == 1 && visitor.filteredContains( "000020" );
    } ) );
    REQUIRE( panel->activeFilters().size() == 1 );
    for ( auto* button : panel->findChildren<QToolButton*>() ) {
        if ( button->text() == "Line twenty" ) button->click();
    }
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines().get() == 0; } ) );
    REQUIRE( panel->activeFilters().isEmpty() );
    auto unicodeContext = json;
    const auto unicode = QString::fromUtf8( "相机错误" );
    unicodeContext.insert( "ST", unicode );
    recovered.crawler->setViewContext(
        QString::fromUtf8( QJsonDocument( unicodeContext ).toJson() ) );
    const auto recoveredJson = QJsonDocument::fromJson(
        recovered.crawler->context()->toString().toUtf8() ).object();
    REQUIRE( recoveredJson.value( "ST" ).toString() == unicode );
}

TEST_CASE( "Session checkpoints round trip current tab and Unicode context", "[ui][session]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings( directory.filePath( "session.ini" ), QSettings::IniFormat );
    SessionInfo session;
    session.add( "window" );
    const auto unicode = QString::fromUtf8( "{\"ST\":\"相机错误\"}" );
    session.setOpenFiles( "window", { { "a.log", 0, unicode }, { "b.log", 0, "{}" } } );
    session.setCurrentIndex( "window", 0 );
    session.saveToStorage( settings );
    QSettings disk( directory.filePath( "session.ini" ), QSettings::IniFormat );
    SessionInfo restored;
    restored.retrieveFromStorage( disk );
    REQUIRE( restored.currentIndex( "window" ) == 0 );
    REQUIRE( restored.openFiles( "window" ).size() == 2 );
    REQUIRE( restored.openFiles( "window" ).front().viewContext == unicode );
}

TEST_CASE( "Preset colors persist, preserve legacy settings, and move with editor rows", "[ui][presets][colors]" )
{
    using Mode = PredefinedFilter::HighlightMode;
    const QList<PredefinedFilter> filters = {
        { "Error", "error", false, QColor( "#ffe4e6" ), Mode::WholeLine },
        { "Streams", "configure_streams", false, QColor( "#dbeafe" ), Mode::Keywords }
    };
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings( directory.filePath( "filters.conf" ), QSettings::IniFormat );
    PredefinedFiltersCollection collection;
    collection.setFilters( filters );
    collection.saveToStorage( settings );
    PredefinedFiltersCollection restored;
    restored.retrieveFromStorage( settings );
    REQUIRE( restored.getFilters().size() == 2 );
    REQUIRE( restored.getFilters()[0].color == filters[0].color );
    REQUIRE( restored.getFilters()[0].highlightMode == Mode::WholeLine );
    REQUIRE( restored.getFilters()[1].highlightMode == Mode::Keywords );
    settings.remove( "PredefinedFiltersCollection/filters/1/color" );
    settings.remove( "PredefinedFiltersCollection/filters/1/highlight_mode" );
    restored.retrieveFromStorage( settings );
    REQUIRE_FALSE( restored.getFilters()[0].color.isValid() );
    REQUIRE( restored.getFilters()[0].highlightMode == Mode::Search );

    auto& presets = PredefinedFiltersCollection::getSynced();
    struct RestorePresets {
        QList<PredefinedFilter> filters;
        ~RestorePresets() { PredefinedFiltersCollection::get().saveToStorage( filters ); }
    } restore{ presets.getFilters() };
    presets.saveToStorage( filters );
    PredefinedFiltersDialog dialog;
    dialog.show();
    QTest::qWait( 50 );
    REQUIRE( dialog.filtersTableWidget->columnCount() == 6 );
    dialog.filtersTableWidget->setCurrentCell( 0, 0 );
    dialog.downButton->click();
    REQUIRE( dialog.filtersTableWidget->item( 1, 0 )->text() == "Error" );
    const auto* color = dialog.filtersTableWidget->cellWidget( 1, 3 );
    REQUIRE( color->property( "filterColor" ).value<QColor>() == filters[0].color );
    const auto* mode = qobject_cast<QComboBox*>( dialog.filtersTableWidget->cellWidget( 1, 4 ) );
    REQUIRE( mode );
    REQUIRE( mode->currentIndex() == 2 );
    dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
    const auto saved = PredefinedFiltersCollection::get().getFilters();
    REQUIRE( saved[1].name == "Error" );
    REQUIRE( saved[1].color == filters[0].color );
    REQUIRE( saved[1].highlightMode == Mode::WholeLine );
    dialog.grab().save( "ui-predefined-filter-editor.png" );
}

TEST_CASE( "Colored keyword and whole-line presets compose and M marks stand out", "[ui][presets][colors]" )
{
    using Mode = PredefinedFilter::HighlightMode;
    QTemporaryFile file{ "colored_preset_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );
    Session session;
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [] { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [&] { return visitor.isLoadingFinished(); } ) );
    visitor.crawler->resize( 1400, 900 );
    visitor.crawler->show();
    auto* panel = visitor.crawler->findChild<PredefinedFiltersButtonPanel*>();
    REQUIRE( panel );
    const QColor blue( "#dbeafe" ), pink( "#ffe4e6" );
    visitor.verifyInterfaceFontIsolation();
    panel->setFilters( {
        { "Keyword", "000010", false, blue, Mode::Keywords },
        { "Whole line", "000010|000020", true, pink, Mode::WholeLine }
    } );
    const auto click = [panel]( const QString& name ) {
        for ( auto* button : panel->findChildren<QToolButton*>() )
            if ( button->text() == name ) { button->click(); return; }
        FAIL( "Preset button missing" );
    };
    click( "Keyword" );
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines() == 1_lcount; } ) );
    REQUIRE( waitUiState( [&] { return visitor.bothViewsHaveHighlight( blue ); } ) );
    const auto keywordPixels = visitor.filteredColorPixels( blue );
    REQUIRE( keywordPixels > 0 );
    click( "Whole line" );
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines() == 2_lcount; } ) );
    REQUIRE( waitUiState( [&] {
        return visitor.bothViewsHaveHighlight( blue ) && visitor.bothViewsHaveHighlight( pink );
    } ) );
    REQUIRE( visitor.filteredColorPixels( pink ) > 4 * keywordPixels );
    visitor.crawler->grab().save( "ui-colored-filter-panel.png" );
    click( "Whole line" );
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines() == 1_lcount; } ) );
    REQUIRE( waitUiState( [&] { return visitor.filteredColorPixels( pink ) == 0; } ) );
    visitor.toggleMark();
    REQUIRE( waitUiState( [&] { return visitor.markCount() == 1_lcount; } ) );
    REQUIRE( waitUiState( [&] { return visitor.bothViewsHaveHighlight( QColor( "#c2410c" ) ); } ) );
    REQUIRE( visitor.markersStayInGutter() );
    visitor.crawler->grab().save( "ui-prominent-m-mark.png" );
    visitor.toggleMark();
    REQUIRE( waitUiState( [&] { return visitor.markCount() == 0_lcount; } ) );
}

TEST_CASE( "Filter palettes persist, preserve custom colors until applied, and retain highlight modes", "[ui][presets][palettes]" )
{
    using Mode = PredefinedFilter::HighlightMode;
    auto& presets = PredefinedFiltersCollection::getSynced();
    struct RestorePalette {
        QList<PredefinedFilter> filters;
        QString scheme;
        ~RestorePalette() {
            auto& collection = PredefinedFiltersCollection::getSynced();
            collection.setColorScheme( scheme );
            collection.saveToStorage( filters );
        }
    } restore{ presets.getFilters(), presets.colorScheme() };
    const QList<PredefinedFilter> original = {
        { "Error", "error", false, QColor( "#123456" ), Mode::WholeLine },
        { "Warning", "warning", false, QColor( "#654321" ), Mode::Search }
    };
    presets.setColorScheme( "latte" );
    presets.saveToStorage( original );
    {
        PredefinedFiltersDialog cancelled;
        auto* scheme = cancelled.findChild<QComboBox*>( "filterColorScheme" );
        REQUIRE( scheme );
        scheme->setCurrentIndex( scheme->findData( "nord" ) );
        cancelled.findChild<QPushButton*>( "applyFilterPalette" )->click();
        cancelled.reject();
        REQUIRE( PredefinedFiltersCollection::getSynced().getFilters()[0].color == original[0].color );
        REQUIRE( PredefinedFiltersCollection::getSynced().colorScheme() == "latte" );
    }
    PredefinedFiltersDialog dialog;
    auto* scheme = dialog.findChild<QComboBox*>( "filterColorScheme" );
    REQUIRE( scheme );
    REQUIRE( dialog.filtersTableWidget->cellWidget( 0, 3 )->property( "filterColor" ).value<QColor>() == original[0].color );
    scheme->setCurrentIndex( scheme->findData( "vivid" ) );
    dialog.addFilterButton->click();
    REQUIRE( dialog.filtersTableWidget->rowCount() == 3 );
    REQUIRE( dialog.filtersTableWidget->cellWidget( 2, 3 )->property( "filterColor" ).value<QColor>() == filterSchemeColor( "vivid", 2 ) );
    // Applying the palette must not remove an unfinished row.
    dialog.findChild<QPushButton*>( "applyFilterPalette" )->click();
    REQUIRE( dialog.filtersTableWidget->rowCount() == 3 );
    dialog.removeFilterButton->click();
    dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
    const auto& saved = PredefinedFiltersCollection::getSynced();
    REQUIRE( saved.colorScheme() == "vivid" );
    REQUIRE( saved.getFilters()[0].color == filterSchemeColor( "vivid", 0 ) );
    REQUIRE( saved.getFilters()[0].highlightMode == Mode::WholeLine );
    REQUIRE( saved.getFilters()[1].highlightMode == Mode::Keywords );
    REQUIRE( saved.getFilters()[1].pattern == original[1].pattern );
    QTemporaryDir dir;
    QSettings settings( dir.filePath( "palette.conf" ), QSettings::IniFormat );
    saved.saveToStorage( settings );
    PredefinedFiltersCollection recovered;
    recovered.retrieveFromStorage( settings );
    REQUIRE( recovered.colorScheme() == "vivid" );
    REQUIRE( recovered.getFilters()[0].color == saved.getFilters()[0].color );
    recovered.setColorScheme( "unknown-future-palette" );
    REQUIRE( recovered.colorScheme() == "latte" );
    PredefinedFiltersDialog reopened;
    REQUIRE( reopened.findChild<QComboBox*>( "filterColorScheme" )->currentData().toString() == "vivid" );
    for ( const auto& name : { "latte", "nord", "vivid" } ) {
        for ( size_t index = 0; index < 18; ++index ) {
            const auto background = filterSchemeColor( name, index );
            const auto foreground = readableForeground( background );
            const auto luminance = []( const QColor& color ) {
                const auto linear = []( double channel ) { return channel <= 0.04045 ? channel / 12.92 : std::pow( ( channel + 0.055 ) / 1.055, 2.4 ); };
                return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() ) + 0.0722 * linear( color.blueF() );
            };
            const auto light = luminance( background ), dark = luminance( foreground );
            REQUIRE( ( std::max( light, dark ) + 0.05 ) / ( std::min( light, dark ) + 0.05 ) >= 4.5 );
        }
    }
}

TEST_CASE( "Readable labels provide eighteen choices and remain scrollable", "[ui][colors]" )
{
    const auto labels = HighlighterSetCollection::getSynced().quickHighlighters();
    REQUIRE( labels.size() == 18 );
    REQUIRE( readableForeground( QColor( Qt::black ) ) == QColor( Qt::white ) );
    REQUIRE( readableForeground( QColor( Qt::red ) ) == QColor( Qt::black ) );
    for ( const auto& background : readableColorPalette() ) {
        REQUIRE( readableForeground( QColor( background ) ) == QColor( "#18212f" ) );
    }
    ColorLabelsManager manager;
    manager.setColorLabel( 17, "last color" );
    REQUIRE( manager.colorLabels().size() == 18 );
    REQUIRE( manager.colorLabels()[17].contains( "last color" ) );
    HighlightersDialog dialog;
    dialog.tabWidget->setCurrentIndex( 1 );
    dialog.resize( 900, 560 );
    dialog.show();
    QTest::qWait( 50 );
    REQUIRE( dialog.findChild<QScrollArea*>() );
    dialog.grab().save( "ui-readable-color-labels.png" );
}

TEST_CASE( "Interface typography and compact colored controls", "[ui][appearance]" )
{
    QWidget window;
    window.setWindowTitle( QString::fromUtf8( "Klogg 界面字体与交互预览" ) );
    auto* layout = new QVBoxLayout( &window );
    layout->setContentsMargins( 8, 6, 8, 6 );
    layout->setSpacing( 6 );
    auto* bar = new QMenuBar;
    auto* menu = bar->addMenu( QString::fromUtf8( "编辑(&E)" ) );
    menu->addAction( QString::fromUtf8( "拷贝(&C)" ) )->setShortcut( QKeySequence::Copy );
    menu->addAction( QString::fromUtf8( "选择全部(&A)" ) )->setShortcut( QKeySequence::SelectAll );
    menu->addSeparator();
    menu->addAction( QString::fromUtf8( "查找(&F)…" ) )->setShortcut( QKeySequence::Find );
    menu->addAction( QString::fromUtf8( "打开包含此文件的文件夹" ) );
    bar->addMenu( QString::fromUtf8( "视图(&V)" ) );
    bar->addMenu( QString::fromUtf8( "工具(&T)" ) );
    layout->setMenuBar( bar );
    layout->addWidget( new QLabel( QString::fromUtf8( "界面字体 · 中文与 English 0123456789" ) ) );
    layout->addWidget( new QLineEdit( QString::fromUtf8( "搜索 error / configure_streams" ) ) );
    auto* controls = new QHBoxLayout;
    controls->addWidget( new QPushButton( QString::fromUtf8( "应用" ) ) );
    auto* disabled = new QPushButton( QString::fromUtf8( "不可用" ) );
    disabled->setEnabled( false );
    controls->addWidget( disabled );
    controls->addStretch();
    layout->addLayout( controls );
    auto* panel = new PredefinedFiltersButtonPanel;
    const QList<PredefinedFilter> filters = {
        { "configure_streams", "configure_streams", false, QColor( "#dbeafe" ) },
        { "ProcessRequest() FRO-URO", "ProcessRequest", false, QColor( "#fce7f3" ) },
        { "error", "error", false, QColor( "#dc2626" ) },
        { "warning", "warning", false, QColor( "#facc15" ) },
        { "capture", "capture", false, QColor( "#2563eb" ) }
    };
    panel->setFilters( filters );
    panel->setActiveFilters( { filters[0], filters[2] } );
    layout->addWidget( panel );
    window.resize( 1000, 210 );
    window.show();
    QTest::qWait( 50 );
    const auto buttons = panel->findChildren<QToolButton*>();
    const auto found = std::find_if( buttons.cbegin(), buttons.cend(),
        []( const auto* button ) { return button->text() == "configure_streams"; } );
    REQUIRE( found != buttons.cend() );
    auto* first = *found;
    REQUIRE( first->icon().isNull() );
    REQUIRE( first->toolButtonStyle() == Qt::ToolButtonTextOnly );
    const auto geometry = first->geometry();
    first->click();
    REQUIRE( first->geometry() == geometry );
    REQUIRE_FALSE( first->isChecked() );
    REQUIRE_FALSE( QApplication::font().bold() );
    REQUIRE_FALSE( QApplication::font().fixedPitch() );
    REQUIRE( menu->font().pointSizeF() == QApplication::font().pointSizeF() );
    for ( const auto* color : vividColorPalette() ) {
        const QColor background( color );
        REQUIRE( background.isValid() );
        const auto fore = readableForeground( background );
        REQUIRE( ( fore == QColor( Qt::white ) || fore == QColor( Qt::black )
                   || fore == QColor( "#18212f" ) ) );
    }
    if ( QGuiApplication::platformName() == "windows" ) {
        REQUIRE( QFontMetrics( QApplication::font() ).inFont( QChar( 'A' ) ) );
        window.grab().save( "ui-native-typography.png" );
        menu->popup( window.mapToGlobal( QPoint( 10, 30 ) ) );
        QTest::qWait( 50 );
        menu->grab().save( "ui-native-menu.png" );
        menu->hide();
    }
    else window.grab().save( "ui-compact-controls.png" );
}

TEST_CASE( "Field tracking searches immediately without matching other numeric fields", "[ui][analysis]" )
{
    QTemporaryFile file;
    REQUIRE( file.open() );
    file.write( "10-11 01:00:00.000 frameNumber=1234 start\n"
                "10-11 01:00:00.001 requestId=1234 unrelated\n"
                "10-11 01:00:00.002 frameNumber=12345 unrelated\n"
                "10-11 01:00:00.003 frameNumber: 1234 end\n" );
    file.flush();
    file.close();
    Session session;
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [] { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [&] { return visitor.isLoadingFinished(); } ) );
    visitor.trackField( LogAnalysis::fieldPattern( "frameNumber=1234", "1234" ) );
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines() == 2_lcount; } ) );
    REQUIRE( visitor.filteredContains( "frameNumber=1234" ) );
    REQUIRE( visitor.filteredContains( "frameNumber: 1234" ) );
    REQUIRE_FALSE( visitor.filteredContains( "requestId=1234" ) );
    REQUIRE_FALSE( visitor.filteredContains( "frameNumber=12345" ) );
}

TEST_CASE( "Browsing and editing draft searches preserve applied preset buttons and colors", "[ui][presets][history]" )
{
    QTemporaryFile file{ "history_preset_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );
    Session session;
    session.savedSearches().clear();
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [] { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [&] { return visitor.isLoadingFinished(); } ) );
    visitor.crawler->resize( 1400, 900 );
    visitor.crawler->show();
    auto* panel = visitor.crawler->findChild<PredefinedFiltersButtonPanel*>();
    REQUIRE( panel );
    const QColor presetColor( "#dbeafe" );
    panel->setFilters( { { "Line ten", "000010", false, presetColor,
        PredefinedFilter::HighlightMode::Keywords } } );
    QToolButton* preset = nullptr;
    for ( auto* button : panel->findChildren<QToolButton*>() ) {
        if ( button->text() == "Line ten" ) preset = button;
    }
    REQUIRE( preset );
    preset->click();
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines() == 1_lcount; } ) );
    REQUIRE( visitor.filteredContains( "000010" ) );
    REQUIRE( waitUiState( [&] { return visitor.bothViewsHaveHighlight( presetColor ); } ) );
    visitor.setHistoryEntries( { "000010", "000020" } );
    visitor.browseHistory( Qt::Key_Down );
    REQUIRE( visitor.draftText() == "000020" );
    REQUIRE( preset->isChecked() );
    REQUIRE( panel->activeFilters().size() == 1 );
    REQUIRE( visitor.filteredContains( "000010" ) );
    REQUIRE_FALSE( visitor.filteredContains( "000020" ) );
    REQUIRE( visitor.bothViewsHaveHighlight( presetColor ) );
    visitor.browseHistory( Qt::Key_Up );
    REQUIRE( visitor.draftText() == "000010" );
    REQUIRE( preset->isChecked() );
    visitor.editDraft( "000030" );
    REQUIRE( preset->isChecked() );
    REQUIRE( visitor.filteredContains( "000010" ) );
    REQUIRE( visitor.bothViewsHaveHighlight( presetColor ) );
    // A checkpoint must not execute an unsubmitted history/draft query on restart.
    const auto checkpoint = QJsonDocument::fromJson( visitor.crawler->context()->toString().toUtf8() ).object();
    REQUIRE( checkpoint.value( "ST" ).toString() == "000010" );
    REQUIRE( checkpoint.value( "PF" ).toArray().size() == 1 );
    visitor.runSearch();
    REQUIRE( waitUiState( [&] { return visitor.filteredContains( "000030" ); } ) );
    REQUIRE_FALSE( preset->isChecked() );
    REQUIRE( panel->activeFilters().isEmpty() );
    REQUIRE( waitUiState( [&] { return visitor.filteredColorPixels( presetColor ) == 0; } ) );
    preset->click();
    REQUIRE( waitUiState( [&] { return visitor.filteredContains( "000010" ); } ) );
    REQUIRE( preset->isChecked() );
    visitor.editDraft( {} );
    REQUIRE( preset->isChecked() );
    visitor.runSearch();
    REQUIRE( waitUiState( [&] { return visitor.getLogFilteredNbLines() == 0_lcount; } ) );
    REQUIRE( panel->activeFilters().isEmpty() );
}


TEST_CASE( "Complete queries restore modes before immediate execution and reject invalid plans", "[ui][queries]" )
{
    QTemporaryFile file{ "query_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );
    Session session;
    CrawlerWidgetVisitor visitor;
    visitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [] { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ] { return visitor.isLoadingFinished(); } ) );
    AnalysisStorage::Query query{ "Two lines", "\"000010\" or \"000020\"", true, false, false, true, true };
    QString error;
    REQUIRE( visitor.crawler->applyQuery( query, &error ) );
    REQUIRE( waitUiState( [ & ] { return visitor.getLogFilteredNbLines().get() == 2; } ) );
    REQUIRE( visitor.crawler->currentQuery( query.name ).toJson() == query.toJson() );
    query = { "Regex", "0000(10|20)", false, true, true, false, false };
    REQUIRE( visitor.crawler->applyQuery( query, &error ) );
    REQUIRE( waitUiState( [ & ] { return visitor.getLogFilteredNbLines().get() == 98; } ) );
    const auto before = visitor.crawler->currentQuery( query.name ).toJson();
    query.expression = "[";
    REQUIRE_FALSE( visitor.crawler->applyQuery( query, &error ) );
    REQUIRE( visitor.crawler->currentQuery( "Regex" ).toJson() == before );
    REQUIRE( visitor.getLogFilteredNbLines().get() == 98 );
    query.expression.clear();
    REQUIRE_FALSE( visitor.crawler->applyQuery( query, &error ) );
}
