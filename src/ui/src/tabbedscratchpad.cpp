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

#include "tabbedscratchpad.h"
#include "scratchpad.h"

#include <QCloseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

TabbedScratchPad::TabbedScratchPad( QWidget* parent ) : QWidget( parent )
{
    hide();
    tabWidget_ = new QTabWidget( this );
    tabWidget_->setTabsClosable( true );
    tabWidget_->setMovable( false );
    auto* add = new QToolButton( tabWidget_ );
    add->setText( "+" );
    add->setToolTip( tr( "New scratchpad (Ctrl+N)" ) );
    add->setAutoRaise( true );
    tabWidget_->setCornerWidget( add, Qt::TopLeftCorner );
    connect( add, &QToolButton::clicked, this, &TabbedScratchPad::addTab );
    connect( tabWidget_, &QTabWidget::tabCloseRequested, this, &TabbedScratchPad::closeTab );
    connect( tabWidget_, &QTabWidget::currentChanged, this, [ this ] { scheduleSave(); } );
    connect( tabWidget_->tabBar(), &QTabBar::tabBarDoubleClicked, this, [ this ]( int index ) {
        if ( index < 0 ) return;
        bool ok = false;
        const auto name = QInputDialog::getText( this, tr( "Rename scratchpad" ), tr( "Name:" ),
            QLineEdit::Normal, tabWidget_->tabText( index ), &ok ).trimmed();
        if ( ok && !name.isEmpty() ) { tabWidget_->setTabText( index, name ); scheduleSave(); }
    } );
    status_ = new QLabel( this );
    status_->setWordWrap( true );
    status_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    auto* retry = new QPushButton( tr( "Save now" ), this );
    retry->setToolTip( tr( "Retry automatic saving" ) );
    connect( retry, &QPushButton::clicked, this, [ this ] { dirty_ = true; flush(); } );
    auto* row = new QHBoxLayout();
    row->addWidget( status_, 1 );
    row->addWidget( retry );
    auto* layout = new QVBoxLayout( this );
    layout->addWidget( tabWidget_ );
    layout->addLayout( row );
    saveTimer_.setSingleShot( true );
    saveTimer_.setInterval( 1000 );
    connect( &saveTimer_, &QTimer::timeout, this, [ this ] { flush(); } );
    addTab();
}

TabbedScratchPad::~TabbedScratchPad() { flush(); }

bool TabbedScratchPad::configureStorage( const QString& directory, const QString& owner )
{
    AnalysisStorage::ScratchDocument restored;
    QString error;
    configured_ = true;
    restoring_ = true;
    if ( !store_.open( directory, owner, &restored, &error ) ) {
        restoring_ = false;
        reportFailure( error );
        return false;
    }
    while ( tabWidget_->count() ) {
        auto* page = tabWidget_->widget( 0 );
        tabWidget_->removeTab( 0 );
        delete page;
    }
    for ( const auto& saved : restored.pages ) {
        addTab();
        const auto index = tabWidget_->count() - 1;
        tabWidget_->setTabText( index, saved.title );
        qobject_cast<ScratchPad*>( tabWidget_->widget( index ) )->setPlainText( saved.text );
    }
    if ( tabWidget_->count() == 0 ) addTab();
    tabWidget_->setCurrentIndex( restored.current );
    restoring_ = false;
    dirty_ = true;
    // Claim even a new empty window on disk before another window restores it.
    return flush();
}

AnalysisStorage::ScratchDocument TabbedScratchPad::document() const
{
    AnalysisStorage::ScratchDocument result;
    result.current = tabWidget_->currentIndex();
    for ( int i = 0; i < tabWidget_->count(); ++i ) {
        const auto* page = qobject_cast<ScratchPad*>( tabWidget_->widget( i ) );
        result.pages.append( { tabWidget_->tabText( i ), page->plainText() } );
    }
    return result;
}

void TabbedScratchPad::reportFailure( const QString& error )
{
    status_->setText( tr( "Automatic save failed. Keep this window open or use Save As.\n%1" ).arg( error ) );
    status_->setStyleSheet( "color: #b91c1c;" );
    status_->setToolTip( store_.path() );
}

bool TabbedScratchPad::flush()
{
    saveTimer_.stop();
    if ( !configured_ || !dirty_ ) return true;
    QString error;
    if ( !store_.save( document(), &error ) ) { reportFailure( error ); return false; }
    dirty_ = false;
    status_->setStyleSheet( {} );
    status_->setText( tr( "Automatically saved" ) );
    status_->setToolTip( store_.path() );
    return true;
}

void TabbedScratchPad::scheduleSave()
{
    if ( restoring_ ) return;
    dirty_ = true;
    // Throttle instead of debounce: continuous typing still checkpoints each second.
    if ( configured_ && !saveTimer_.isActive() ) saveTimer_.start();
}

void TabbedScratchPad::addTab()
{
    auto* page = new ScratchPad( tabWidget_ );
    connect( page, &ScratchPad::contentsChanged, this, &TabbedScratchPad::scheduleSave );
    const auto index = tabWidget_->addTab( page, tr( "Scratchpad %1" ).arg( ++tabCounter_ ) );
    tabWidget_->setCurrentIndex( index );
    scheduleSave();
}

void TabbedScratchPad::closeTab( int index )
{
    if ( index < 0 || index >= tabWidget_->count() ) return;
    auto* page = qobject_cast<ScratchPad*>( tabWidget_->widget( index ) );
    if ( !page->plainText().isEmpty() && QMessageBox::question( this, tr( "Close scratchpad" ),
        tr( "Close and delete this scratchpad? Use Save As first to keep a text copy." ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes ) return;
    auto next = document();
    next.pages.removeAt( index );
    const auto previousIndex = tabWidget_->currentIndex();
    next.current = qBound( 0, previousIndex - ( index < previousIndex ? 1 : 0 ),
        qMax( 0, static_cast<int>( next.pages.size() ) - 1 ) );
    QString error;
    // Commit deletion before changing the UI; failed writes keep the page intact.
    if ( configured_ && !store_.save( next, &error ) ) { reportFailure( error ); return; }
    tabWidget_->removeTab( index );
    page->deleteLater();
    if ( tabWidget_->count() == 0 ) addTab();
    scheduleSave();
}

void TabbedScratchPad::closeEvent( QCloseEvent* event )
{
    if ( !flush() && QMessageBox::warning( this, tr( "Scratchpad save failed" ),
        tr( "Changes could not be saved. Cancel to use Save As, or close this window anyway." ),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel ) != QMessageBox::Discard ) {
        event->ignore();
        return;
    }
    QWidget::closeEvent( event );
}

void TabbedScratchPad::keyPressEvent( QKeyEvent* event )
{
    const auto mod = event->modifiers();
    const auto key = event->key();
    if ( mod == Qt::ControlModifier && key == Qt::Key_N ) addTab();
    else if ( mod == Qt::ControlModifier && ( key == Qt::Key_W || key == Qt::Key_Q ) ) closeTab( tabWidget_->currentIndex() );
    else if ( ( mod == Qt::ControlModifier && ( key == Qt::Key_Tab || key == Qt::Key_PageDown ) ) )
        tabWidget_->setCurrentIndex( ( tabWidget_->currentIndex() + 1 ) % tabWidget_->count() );
    else if ( ( mod == ( Qt::ControlModifier | Qt::ShiftModifier ) && key == Qt::Key_Tab )
        || ( mod == Qt::ControlModifier && key == Qt::Key_PageUp ) )
        tabWidget_->setCurrentIndex( ( tabWidget_->currentIndex() + tabWidget_->count() - 1 ) % tabWidget_->count() );
    else { QWidget::keyPressEvent( event ); return; }
    event->accept();
}

void TabbedScratchPad::addData( QString newData )
{
    if ( auto* page = qobject_cast<ScratchPad*>( tabWidget_->currentWidget() ) ) page->addData( std::move( newData ) );
}

void TabbedScratchPad::replaceData( QString newData )
{
    if ( auto* page = qobject_cast<ScratchPad*>( tabWidget_->currentWidget() ) ) page->replaceData( std::move( newData ) );
}
