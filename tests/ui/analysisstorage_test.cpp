#include <catch2/catch.hpp>
#include <QFile>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include "analysisstorage.h"
#include "scratchpad.h"
#include "tabbedscratchpad.h"
#include "test_utils.h"

TEST_CASE( "Named queries retain all options and merge changes from other windows", "[ui][queries][storage]" )
{
    QTemporaryDir dir;
    const auto path = dir.filePath( "queries.json" );
    AnalysisStorage::QueryStore first( path ), second( path );
    AnalysisStorage::Query query{ "Camera", "\"camera\" and not(\"timeout\")", true, true, true, true, true };
    QString error;
    REQUIRE( first.update( query, false, &error ) );
    REQUIRE( second.update( { "Other", "error", false, false, false, false, false }, false, &error ) );
    QList<AnalysisStorage::Query> loaded;
    REQUIRE( first.load( &loaded, &error ) );
    REQUIRE( loaded.size() == 2 );
    REQUIRE( loaded.first().toJson() == query.toJson() );
    query.regex = false;
    REQUIRE( first.update( query, false, &error ) );
    REQUIRE( second.load( &loaded, &error ) );
    REQUIRE( loaded.size() == 2 );
    REQUIRE_FALSE( loaded.last().regex );
    REQUIRE( first.update( query, true, &error ) );
    REQUIRE( second.load( &loaded, &error ) );
    REQUIRE( loaded.size() == 1 );
    QFile corrupt( path );
    REQUIRE( corrupt.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    corrupt.write( "{}" ); corrupt.close();
    REQUIRE_FALSE( first.update( query, false, &error ) );
    REQUIRE( corrupt.open( QIODevice::ReadOnly ) );
    REQUIRE( corrupt.readAll() == "{}" );
}

TEST_CASE( "Scratchpad files survive restart and concurrent windows have separate ownership", "[ui][scratchpad][storage]" )
{
    QTemporaryDir dir;
    QString path, error;
    AnalysisStorage::ScratchDocument original{ { { "Request 87", QString::fromUtf8( "中文日志\nrequestId=87\n" ) } }, 0 };
    {
        AnalysisStorage::ScratchStore first, second;
        AnalysisStorage::ScratchDocument empty;
        REQUIRE( first.open( dir.path(), "window-a", &empty, &error ) );
        path = first.path();
        REQUIRE( first.save( original, &error ) );
        REQUIRE( second.open( dir.path(), "window-a", &empty, &error ) );
        REQUIRE( second.path() != first.path() );
        REQUIRE( second.save( { { { "Other", "independent" } }, 0 }, &error ) );
        QJsonObject root;
        REQUIRE( AnalysisStorage::readJson( first.path(), &root, &error ) );
        REQUIRE( root == original.toJson() );
    }
    {
        AnalysisStorage::ScratchStore restored;
        AnalysisStorage::ScratchDocument value;
        REQUIRE( restored.open( dir.path(), "window-a", &value, &error ) );
        REQUIRE( value.toJson() == original.toJson() );
    }
    QFile corrupt( path );
    REQUIRE( corrupt.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    corrupt.write( "not-json" ); corrupt.close();
    AnalysisStorage::ScratchStore blocked;
    AnalysisStorage::ScratchDocument value;
    REQUIRE_FALSE( blocked.open( dir.path(), "window-a", &value, &error ) );
    REQUIRE_FALSE( blocked.save( original, &error ) );
    REQUIRE( corrupt.open( QIODevice::ReadOnly ) );
    REQUIRE( corrupt.readAll() == "not-json" );
}

TEST_CASE( "Scratchpad checkpoints before closing and restores tabs and Unicode text", "[ui][scratchpad][recovery]" )
{
    QTemporaryDir dir;
    QString path;
    const auto unicode = QString::fromUtf8( "相机请求\nframeNumber=1234" );
    {
        TabbedScratchPad window;
        REQUIRE( window.configureStorage( dir.path(), "ui-window" ) );
        path = window.storagePath();
        window.addData( unicode );
        QJsonObject saved;
        QString error;
        REQUIRE( waitUiState( [ & ] {
            AnalysisStorage::readJson( path, &saved, &error );
            const auto pages = saved.value( "pages" ).toArray();
            return !pages.isEmpty() && pages.first().toObject().value( "text" ).toString() == unicode;
        } ) );
        QTest::keyClick( &window, Qt::Key_N, Qt::ControlModifier );
        auto* tabs = window.findChild<QTabWidget*>();
        REQUIRE( tabs->count() == 2 );
        tabs->setTabText( 1, "Request 87" );
        window.addData( "requestId=87" );
        tabs->setCurrentIndex( 0 );
        REQUIRE( window.flush() );
    }
    {
        TabbedScratchPad restored;
        REQUIRE( restored.configureStorage( dir.path(), "ui-window" ) );
        auto* tabs = restored.findChild<QTabWidget*>();
        REQUIRE( tabs->count() == 2 );
        REQUIRE( tabs->currentIndex() == 0 );
        REQUIRE( tabs->tabText( 1 ) == "Request 87" );
        auto* page = qobject_cast<ScratchPad*>( tabs->widget( 0 ) );
        REQUIRE( page->plainText() == unicode );
        const auto exportPath = dir.filePath( "excerpt.txt" );
        QString error;
        REQUIRE( page->saveTextToFile( exportPath, &error ) );
        QFile exported( exportPath );
        REQUIRE( exported.open( QIODevice::ReadOnly ) );
        REQUIRE( exported.readAll() == unicode.toUtf8() );
        REQUIRE_FALSE( page->saveTextToFile( dir.path(), &error ) );
        REQUIRE( page->plainText() == unicode );
        // Force an automatic-write failure without depending on OS permissions.
        REQUIRE( QFile::remove( path ) );
        REQUIRE( QDir().mkdir( path ) );
        restored.addData( "still in memory" );
        REQUIRE_FALSE( restored.flush() );
        REQUIRE( page->plainText().contains( "still in memory" ) );
        REQUIRE( QDir().rmdir( path ) );
        REQUIRE( restored.flush() );
    }
}
