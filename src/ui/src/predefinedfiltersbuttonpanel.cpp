#include "predefinedfiltersbuttonpanel.h"

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>
#include <algorithm>

namespace {
bool sameFilter( const PredefinedFilter& a, const PredefinedFilter& b )
{
    return a.name == b.name && a.pattern == b.pattern && a.useRegex == b.useRegex;
}
}

PredefinedFiltersButtonPanel::PredefinedFiltersButtonPanel( QWidget* parent )
    : QWidget( parent ), more_( new QToolButton( this ) ), menu_( new QMenu( this ) )
{
    setObjectName( "predefinedFiltersButtonPanel" );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    more_->setText( tr( "More" ) );
    more_->setObjectName( "moreFiltersButton" );
    more_->setPopupMode( QToolButton::InstantPopup );
    more_->setMenu( menu_ );
    more_->hide();
    setStyleSheet( "QToolButton:checked { background-color: palette(highlight); "
                   "color: palette(highlighted-text); border: 2px solid palette(highlight); "
                   "border-radius: 3px; font-weight: bold; }" );
}

void PredefinedFiltersButtonPanel::setFilters( const QList<PredefinedFilter>& filters )
{
    menu_->clear();
    qDeleteAll( buttons_ );
    buttons_.clear();
    filters_ = filters;
    for ( const auto& filter : filters_ ) {
        auto* button = new QToolButton( this );
        button->setText( filter.name );
        button->setToolTip( filter.pattern );
        button->setCheckable( true );
        connect( button, &QToolButton::clicked, this, [this, filter] {
            setActiveFilters( { filter } );
            Q_EMIT filterActivated( filter );
        } );
        buttons_.append( button );
    }
    setActiveFilters( active_ );
    arrangeButtons();
}

void PredefinedFiltersButtonPanel::setActiveFilters( const QList<PredefinedFilter>& filters )
{
    const auto requested = filters;
    active_.clear();
    for ( const auto& f : requested ) {
        if ( std::any_of( filters_.cbegin(), filters_.cend(),
             [&f]( const auto& candidate ) { return sameFilter( f, candidate ); } ) ) {
            active_.append( f );
        }
    }
    for ( int i = 0; i < buttons_.size(); ++i ) {
        buttons_[i]->setChecked( std::any_of( active_.cbegin(), active_.cend(),
            [this, i]( const auto& f ) { return sameFilter( f, filters_[i] ); } ) );
    }
    for ( auto* action : menu_->actions() ) {
        const auto i = action->data().toInt();
        action->setChecked( buttons_[i]->isChecked() );
    }
    const auto actions = menu_->actions();
    more_->setChecked( std::any_of( actions.cbegin(), actions.cend(),
        []( const auto* action ) { return action->isChecked(); } ) );
}

QSize PredefinedFiltersButtonPanel::minimumSizeHint() const
{
    return { 0, sizeHint().height() };
}

QSize PredefinedFiltersButtonPanel::sizeHint() const
{
    return { 200, filters_.isEmpty() ? 0 : 2 * ( more_->sizeHint().height() + 4 ) + 4 };
}

void PredefinedFiltersButtonPanel::resizeEvent( QResizeEvent* event )
{
    QWidget::resizeEvent( event );
    arrangeButtons();
}

void PredefinedFiltersButtonPanel::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );
    if ( event->type() == QEvent::LanguageChange ) more_->setText( tr( "More" ) );
    if ( event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
         || event->type() == QEvent::LanguageChange ) arrangeButtons();
}

void PredefinedFiltersButtonPanel::arrangeButtons()
{
    constexpr int gap = 4;
    const int available = std::max( 0, width() - 2 * gap );
    int rowHeight = more_->sizeHint().height();
    for ( auto* b : buttons_ ) rowHeight = std::max( rowHeight, b->sizeHint().height() );
    setFixedHeight( filters_.isEmpty() ? 0 : 2 * rowHeight + 3 * gap );
    // First try without overflow, then reserve the More button on row two.
    QVector<QRect> positions;
    const auto pack = [&]( bool reserveMore ) {
        positions.clear();
        int x = 0, row = 0;
        for ( auto* b : buttons_ ) {
            const int w = std::min( available, b->sizeHint().width() );
            int limit = available - ( reserveMore && row == 1 ? more_->sizeHint().width() + gap : 0 );
            if ( x + w > limit && row == 0 ) { row = 1; x = 0; }
            limit = available - ( reserveMore && row == 1 ? more_->sizeHint().width() + gap : 0 );
            if ( x + w > limit ) break;
            positions.append( QRect( gap + x, gap + row * ( rowHeight + gap ), w, rowHeight ) );
            x += w + gap;
        }
    };
    pack( false );
    const bool overflow = positions.size() < buttons_.size();
    if ( overflow ) pack( true );
    menu_->clear();
    bool hiddenActive = false;
    for ( int i = 0; i < buttons_.size(); ++i ) {
        auto* b = buttons_[i];
        b->setVisible( i < positions.size() );
        if ( i < positions.size() ) b->setGeometry( positions[i] );
        else {
            auto* action = menu_->addAction( filters_[i].name );
            action->setToolTip( filters_[i].pattern );
            action->setCheckable( true );
            action->setChecked( b->isChecked() );
            action->setData( i );
            hiddenActive = hiddenActive || b->isChecked();
            connect( action, &QAction::triggered, b, &QToolButton::click );
        }
    }
    more_->setCheckable( true );
    more_->setChecked( hiddenActive );
    more_->setVisible( overflow );
    more_->setGeometry( gap + std::max( 0, available - more_->sizeHint().width() ),
                        gap + rowHeight + gap,
                        std::min( available, more_->sizeHint().width() ), rowHeight );
}
