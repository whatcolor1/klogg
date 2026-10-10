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
    QSignalSpy activated( &panel, &PredefinedFiltersButtonPanel::filtersChanged );
    auto* overflowAction = more->menu()->actions().last();
    overflowAction->trigger();
    REQUIRE( activated.count() == 1 );
    REQUIRE( overflowAction->isChecked() );
    REQUIRE( more->isChecked() );
    panel.resize( 10000, panel.height() );
    QTest::qWait( 20 );
    REQUIRE( more->isHidden() );
    panel.resize( 1, panel.height() );
    QTest::qWait( 20 );
    REQUIRE_FALSE( more->isHidden() );
    REQUIRE( more->menu()->actions().size() == filters.size() );
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
