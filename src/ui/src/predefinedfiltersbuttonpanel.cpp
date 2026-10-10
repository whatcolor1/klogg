#include "predefinedfiltersbuttonpanel.h"
#include "readablecolors.h"

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>
#include <QPainter>
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
    setContextMenuPolicy( Qt::CustomContextMenu );
    connect( this, &QWidget::customContextMenuRequested, this, [this]( const QPoint& pos ) {
        QMenu menu;
        menu.addAction( tr( "Edit filters and colors…" ), this,
                        &PredefinedFiltersButtonPanel::editFiltersRequested );
        menu.exec( mapToGlobal( pos ) );
    } );
    more_->setToolTip( tr( "Additional filters; each filter can be switched on independently." ) );
}

void PredefinedFiltersButtonPanel::setFilters( const QList<PredefinedFilter>& filters )
{
    menu_->clear();
    qDeleteAll( buttons_ );
    buttons_.clear();
    filters_ = filters;
    for ( const auto& filter : filters_ ) {
        auto* button = new QToolButton( this );
        button->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        button->setText( filter.name );
        const auto color = filter.color.isValid() ? filter.color : automaticFilterColor( filter.name );
        const auto fore = readableForeground( color );
        const auto border = color.lightness() > 128 ? color.darker( 160 ) : color.lighter( 160 );
        button->setStyleSheet( QString(
            "QToolButton { background: %1; color: %2; border: 2px solid transparent; "
            "border-bottom-color: %3; border-radius: 4px; padding: 3px 8px; } "
            "QToolButton:hover { border-color: %3; } "
            "QToolButton:checked { border: 2px solid %2; } "
            "QToolButton:focus { border: 2px dashed %2; }" )
            .arg( color.name(), fore.name(), border.name() ) );
        button->setToolTip( filter.pattern + "\n" + tr( "Click to toggle; right-click to edit colors and highlighting." ) );
        button->setAccessibleName( filter.name );
        button->setCheckable( true );
        connect( button, &QToolButton::clicked, this, [this] {
            QList<PredefinedFilter> selected;
            for ( int i = 0; i < buttons_.size(); ++i ) {
                if ( buttons_[i]->isChecked() ) selected.append( filters_[i] );
            }
            setActiveFilters( selected );
            Q_EMIT filtersChanged( selected );
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
            const auto current = std::find_if( filters_.cbegin(), filters_.cend(),
                [&f]( const auto& candidate ) { return sameFilter( f, candidate ); } );
            active_.append( *current );
        }
    }
    for ( int i = 0; i < buttons_.size(); ++i ) {
        buttons_[i]->setChecked( std::any_of( active_.cbegin(), active_.cend(),
            [this, i]( const auto& f ) { return sameFilter( f, filters_[i] ); } ) );
        // Reserve the same icon width in both states so toggling never moves buttons.
        QPixmap check( 16, 16 );
        check.fill( Qt::transparent );
        if ( buttons_[i]->isChecked() ) {
            QPainter painter( &check );
            const auto color = filters_[i].color.isValid() ? filters_[i].color
                : automaticFilterColor( filters_[i].name );
            painter.setRenderHint( QPainter::Antialiasing );
            painter.setPen( QPen( readableForeground( color ), 2.2 ) );
            painter.drawLine( 3, 8, 6, 11 );
            painter.drawLine( 6, 11, 13, 4 );
        }
        buttons_[i]->setIcon( QIcon( check ) );
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
    return { 200, preferredHeight_ };
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
    // First try without overflow, then reserve the More button on row two.
    QVector<QRect> positions;
    const auto pack = [&]( bool reserveMore ) {
        positions.clear();
        if ( available <= 0 ) return;
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
    const int rows = filters_.isEmpty() ? 0
        : ( overflow || ( !positions.isEmpty() && positions.last().y() > gap ) ? 2 : 1 );
    const int height = rows == 0 ? 0 : rows * rowHeight + ( rows + 1 ) * gap;
    if ( preferredHeight_ != height ) {
        preferredHeight_ = height;
        setFixedHeight( height );
        updateGeometry();
    }
    menu_->clear();
    bool hiddenActive = false;
    for ( int i = 0; i < buttons_.size(); ++i ) {
        auto* b = buttons_[i];
        b->setVisible( i < positions.size() );
        if ( i < positions.size() ) b->setGeometry( positions[i] );
        else {
            auto* action = menu_->addAction( filters_[i].name );
            action->setToolTip( filters_[i].pattern );
            const auto color = filters_[i].color.isValid() ? filters_[i].color
                : automaticFilterColor( filters_[i].name );
            QPixmap swatch( 16, 16 );
            swatch.fill( color );
            action->setIcon( QIcon( swatch ) );
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
