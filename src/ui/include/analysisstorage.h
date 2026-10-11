#pragma once

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QUuid>
#include <memory>
#include <utility>

#include "persistentinfo.h"

namespace AnalysisStorage {

inline QString directory()
{
    return QFileInfo( PersistentInfo::getSettings( app_settings{} ).fileName() )
        .absoluteDir().filePath( "analysis" );
}

inline bool writeJson( const QString& path, const QJsonObject& object, QString* error )
{
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) ) {
        if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Cannot create data directory: %1" ).arg( QFileInfo( path ).absolutePath() );
        return false;
    }
    QSaveFile file( path );
    file.setDirectWriteFallback( false );
    const auto bytes = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() ) {
        if ( error ) *error = file.errorString();
        return false;
    }
    return true;
}

inline bool readJson( const QString& path, QJsonObject* object, QString* error )
{
    if ( !QFileInfo::exists( path ) ) { *object = {}; return true; }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        if ( error ) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() ) {
        if ( error ) *error = parseError.errorString();
        return false;
    }
    *object = doc.object();
    return true;
}

struct Query {
    QString name;
    QString expression;
    bool matchCase = false;
    bool regex = false;
    bool inverse = false;
    bool logical = false;
    bool autoRefresh = false;
    QJsonObject toJson() const
    {
        return { { "name", name }, { "expression", expression }, { "case", matchCase },
                 { "regex", regex }, { "inverse", inverse }, { "logical", logical },
                 { "refresh", autoRefresh } };
    }
    static bool fromJson( const QJsonObject& object, Query* query )
    {
        if ( !object.value( "name" ).isString() || object.value( "name" ).toString().trimmed().isEmpty()
             || !object.value( "expression" ).isString() || object.value( "expression" ).toString().isEmpty() ) return false;
        for ( const auto* key : { "case", "regex", "inverse", "logical", "refresh" } ) {
            if ( !object.value( key ).isBool() ) return false;
        }
        *query = { object.value( "name" ).toString(), object.value( "expression" ).toString(),
                   object.value( "case" ).toBool(), object.value( "regex" ).toBool(),
                   object.value( "inverse" ).toBool(), object.value( "logical" ).toBool(),
                   object.value( "refresh" ).toBool() };
        return true;
    }
};

// Read-modify-write under a lock keeps changes from other windows/processes.
class QueryStore {
  public:
    explicit QueryStore( QString path ) : path_( std::move( path ) ) {}
    bool load( QList<Query>* queries, QString* error ) const
    {
        QJsonObject root;
        if ( !readJson( path_, &root, error ) ) return false;
        QList<Query> result;
        if ( QFileInfo::exists( path_ ) ) {
            if ( root.value( "version" ).toInt() != 1 || !root.value( "queries" ).isArray() ) {
                if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Unsupported query data" );
                return false;
            }
            for ( const auto& value : root.value( "queries" ).toArray() ) {
                Query query;
                if ( !value.isObject() || !Query::fromJson( value.toObject(), &query ) ) {
                    if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Invalid query data" );
                    return false;
                }
                for ( const auto& other : result ) if ( other.name == query.name ) {
                    if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Duplicate query name" );
                    return false;
                }
                result.append( query );
            }
        }
        *queries = result;
        return true;
    }
    bool update( const Query& query, bool remove, QString* error ) const
    {
        if ( !QDir().mkpath( QFileInfo( path_ ).absolutePath() ) ) return false;
        QLockFile lock( path_ + ".lock" );
        if ( !lock.tryLock( 0 ) ) {
            if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Query data is busy; please retry" );
            return false;
        }
        QList<Query> queries;
        if ( !load( &queries, error ) ) return false;
        for ( int i = queries.size() - 1; i >= 0; --i ) if ( queries[ i ].name == query.name ) queries.removeAt( i );
        if ( !remove ) {
            Query checked;
            if ( !Query::fromJson( query.toJson(), &checked ) ) return false;
            queries.append( query );
        }
        QJsonArray array;
        for ( const auto& value : queries ) array.append( value.toJson() );
        return writeJson( path_, { { "version", 1 }, { "queries", array } }, error );
    }
  private:
    QString path_;
};

struct ScratchPage { QString title; QString text; };
struct ScratchDocument {
    QList<ScratchPage> pages;
    int current = 0;
    QJsonObject toJson() const
    {
        QJsonArray array;
        for ( const auto& page : pages ) array.append( QJsonObject{ { "title", page.title }, { "text", page.text } } );
        return { { "version", 1 }, { "current", current }, { "pages", array } };
    }
    static bool fromJson( const QJsonObject& root, ScratchDocument* document )
    {
        if ( root.value( "version" ).toInt() != 1 || !root.value( "pages" ).isArray() ) return false;
        ScratchDocument result;
        for ( const auto& value : root.value( "pages" ).toArray() ) {
            const auto object = value.toObject();
            if ( !object.value( "title" ).isString() || !object.value( "text" ).isString() ) return false;
            result.pages.append( { object.value( "title" ).toString(), object.value( "text" ).toString() } );
        }
        result.current = qBound( 0, root.value( "current" ).toInt(), qMax( 0, static_cast<int>( result.pages.size() ) - 1 ) );
        *document = result;
        return true;
    }
};

// One locked file per scratchpad window. Recover unclaimed windows after restart.
class ScratchStore {
  public:
    bool open( const QString& directory, const QString& owner, ScratchDocument* document, QString* error )
    {
        if ( !QDir().mkpath( directory ) ) {
            if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Cannot create scratchpad directory" );
            return false;
        }
        QDir dir( directory );
        const auto key = QString::fromLatin1( QCryptographicHash::hash( owner.toUtf8(), QCryptographicHash::Sha256 ).toHex() );
        QStringList candidates{ dir.filePath( key + ".json" ) };
        if ( !QFileInfo::exists( candidates.first() ) ) {
            candidates = QStringList{};
            for ( const auto& info : dir.entryInfoList( { "*.json" }, QDir::Files, QDir::Time ) ) candidates.append( info.absoluteFilePath() );
            candidates.append( dir.filePath( key + ".json" ) );
        }
        candidates.append( dir.filePath( QUuid::createUuid().toString( QUuid::WithoutBraces ) + ".json" ) );
        for ( const auto& candidate : candidates ) {
            auto lock = std::make_unique<QLockFile>( candidate + ".lock" );
            lock->setStaleLockTime( 0 );
            if ( !lock->tryLock( 0 ) ) continue;
            path_ = candidate;
            lock_ = std::move( lock );
            QJsonObject root;
            if ( !readJson( path_, &root, error ) || ( QFileInfo::exists( path_ ) && !ScratchDocument::fromJson( root, document ) ) ) {
                if ( error && error->isEmpty() ) *error = QCoreApplication::translate( "AnalysisStorage", "Invalid scratchpad data; original file preserved" );
                writable_ = false;
                return false;
            }
            writable_ = true;
            return true;
        }
        if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Scratchpad data is busy" );
        return false;
    }
    bool save( const ScratchDocument& document, QString* error ) const
    {
        if ( !writable_ || !lock_ || !lock_->isLocked() ) {
            if ( error ) *error = QCoreApplication::translate( "AnalysisStorage", "Automatic saving is unavailable; use Save As" );
            return false;
        }
        return writeJson( path_, document.toJson(), error );
    }
    QString path() const { return path_; }
  private:
    QString path_;
    std::unique_ptr<QLockFile> lock_;
    bool writable_ = false;
};

} // namespace AnalysisStorage
