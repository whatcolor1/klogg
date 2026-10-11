#include "predefinedfiltersbuttonpanel.h"
#include "readablecolors.h"

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
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
    : QWidget( parent ), more_( new QToolButton( this ) ), groupButton_( new QToolButton( this ) ), menu_( new QMenu( this ) )
{
    setObjectName( "predefinedFiltersButtonPanel" );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    more_->setText( tr( "More" ) );
    more_->setObjectName( "moreFiltersButton" );
    more_->setPopupMode( QToolButton::InstantPopup );
    more_->setMenu( menu_ );
    more_->hide();
    groupButton_->setObjectName( "filterGroupButton" );
    groupButton_->setPopupMode( QToolButton::InstantPopup );
    groupButton_->setMenu( new QMenu( groupButton_ ) );
    groupButton_->hide();
    setContextMenuPolicy( Qt::CustomContextMenu );
    connect( this, &QWidget::customContextMenuRequested, this, [this]( const QPoint& pos ) {
        QMenu menu;
        menu.addAction( tr( "Edit filters and colors…" ), this,
                        &PredefinedFiltersButtonPanel::editFiltersRequested );
        menu.addSeparator();
        auto* save = menu.addAction( tr( "Save enabled filters as combination…" ), this, [this] {
            bool ok = false;
            const auto name = QInputDialog::getText( this, tr( "Save combination" ), tr( "Name:" ),
                QLineEdit::Normal, {}, &ok ).trimmed();
            if ( !ok || name.isEmpty() ) return;
            const auto combinations = PredefinedFiltersCollection::getSynced().combinations();
            const bool exists = std::any_of( combinations.cbegin(), combinations.cend(),
                [&name]( const auto& c ) { return c.name == name; } );
            if ( exists && QMessageBox::question( this, tr( "Replace combination" ),
                tr( "Replace the saved combination with this name?" ) ) != QMessageBox::Yes ) return;
            saveCombination( name );
        } );
        save->setEnabled( !active_.isEmpty() );
        const auto combinations = PredefinedFiltersCollection::getSynced().combinations();
        if ( !combinations.isEmpty() ) {
            auto* load = menu.addMenu( tr( "Load combination" ) );
            auto* remove = menu.addMenu( tr( "Delete combination" ) );
            for ( const auto& combination : combinations ) {
                const auto name = combination.name;
                load->addAction( name, this, [this, name] {
                    if ( !applyCombination( name ) ) QMessageBox::warning( this,
                        tr( "Combination unavailable" ), tr( "Some filters were changed or deleted. The current search was kept." ) );
                } );
                remove->addAction( name, this, [this, name] {
                    if ( QMessageBox::question( this, tr( "Delete combination" ), name ) != QMessageBox::Yes ) return;
                    auto& collection = PredefinedFiltersCollection::getSynced();
                    auto entries = collection.combinations();
                    entries.erase( std::remove_if( entries.begin(), entries.end(),
                        [&name]( const auto& c ) { return c.name == name; } ), entries.end() );
                    collection.setCombinations( entries );
                    collection.save();
                } );
            }
        }
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
    auto* groups = groupButton_->menu();
    groups->clear();
    groups->addAction( tr( "All groups" ), this, [this] { setGroup( {} ); } );
    QStringList names;
    bool ungrouped = false;
    for ( const auto& f : filters_ ) {
        if ( f.group.isEmpty() ) ungrouped = true;
        else if ( !names.contains( f.group ) ) names.append( f.group );
    }
    for ( const auto& name : names ) groups->addAction( name, this, [this, name] { setGroup( name ); } );
    if ( ungrouped && !names.isEmpty() ) groups->addAction( tr( "Ungrouped" ), this,
        [this] { setGroup( QStringLiteral( "\x01" ) ); } );
    if ( !group_.isEmpty() && group_ != QStringLiteral( "\x01" ) && !names.contains( group_ ) ) group_.clear();
    groupButton_->setVisible( !names.isEmpty() );
    groupButton_->setText( group_.isEmpty() ? tr( "All groups" ) : group_ == QStringLiteral( "\x01" ) ? tr( "Ungrouped" ) : group_ );
    for ( const auto& filter : filters_ ) {
        auto* button = new QToolButton( this );
        button->setToolButtonStyle( Qt::ToolButtonTextOnly );
        button->setText( filter.name );
        const auto color = filter.color.isValid() ? filter.color : automaticFilterColor( filter.name );
        const auto fore = readableForeground( color );
        const auto border = color.lightness() > 128 ? color.darker( 160 ) : color.lighter( 160 );
        button->setStyleSheet( QString(
            "QToolButton { background: %1; color: %2; border: 2px solid transparent; "
            "border-bottom-color: %3; border-radius: 3px; padding: 1px 6px; } "
            "QToolButton:hover { border-color: %3; } "
            "QToolButton:checked { border: 2px solid %2; } "
            "QToolButton:focus { border: 2px dashed %2; }" )
            .arg( color.name(), fore.name(), border.name() ) );
        button->setToolTip( filter.pattern + "\n" + tr( "Click to toggle; right-click to edit filters or save/load combinations." ) );
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

void PredefinedFiltersButtonPanel::setGroup( const QString& group )
{
    group_ = group;
    groupButton_->setText( group.isEmpty() ? tr( "All groups" ) : group == QStringLiteral( "\x01" ) ? tr( "Ungrouped" ) : group );
    arrangeButtons();
}

bool PredefinedFiltersButtonPanel::saveCombination( const QString& name )
{
    if ( name.trimmed().isEmpty() || active_.isEmpty() ) return false;
    auto& collection = PredefinedFiltersCollection::getSynced();
    auto entries = collection.combinations();
    entries.erase( std::remove_if( entries.begin(), entries.end(),
        [&name]( const auto& c ) { return c.name == name.trimmed(); } ), entries.end() );
    entries.append( { name.trimmed(), active_ } );
    collection.setCombinations( entries );
    collection.save();
    return true;
}

bool PredefinedFiltersButtonPanel::applyCombination( const QString& name )
{
    const auto entries = PredefinedFiltersCollection::getSynced().combinations();
    for ( const auto& combination : entries ) {
        if ( combination.name != name ) continue;
        if ( combination.filters.isEmpty() ) return false;
        for ( const auto& f : combination.filters ) {
            if ( std::none_of( filters_.cbegin(), filters_.cend(),
                [&f]( const auto& candidate ) { return sameFilter( f, candidate ); } ) ) return false;
        }
        setActiveFilters( combination.filters );
        const auto selected = active_;
        Q_EMIT filtersChanged( selected );
        return true;
    }
    return false;
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
    constexpr int gap = 3;
    const int groupWidth = groupButton_->isHidden() ? 0 : std::min( width(), groupButton_->sizeHint().width() + gap );
    const int available = std::max( 0, width() - 2 * gap - groupWidth );
    QVector<int> visible;
    for ( int i = 0; i < filters_.size(); ++i ) {
        if ( group_.isEmpty() || ( group_ == QStringLiteral( "\x01" ) ? filters_[i].group.isEmpty() : filters_[i].group == group_ ) ) visible.append( i );
    }
    int rowHeight = more_->sizeHint().height();
    for ( auto* b : buttons_ ) rowHeight = std::max( rowHeight, b->sizeHint().height() );
    // First try without overflow, then reserve the More button on row two.
    QVector<QRect> positions;
    const auto pack = [&]( bool reserveMore ) {
        positions.clear();
        if ( available <= 0 ) return;
        int x = 0, row = 0;
        for ( int index : visible ) {
            auto* b = buttons_[index];
            const int w = std::min( available, b->sizeHint().width() );
            int limit = available - ( reserveMore && row == 1 ? more_->sizeHint().width() + gap : 0 );
            if ( x + w > limit && row == 0 ) { row = 1; x = 0; }
            limit = available - ( reserveMore && row == 1 ? more_->sizeHint().width() + gap : 0 );
            if ( x + w > limit ) break;
            positions.append( QRect( gap + groupWidth + x, gap + row * ( rowHeight + gap ), w, rowHeight ) );
            x += w + gap;
        }
    };
    pack( false );
    const bool overflow = positions.size() < visible.size();
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
    for ( auto* b : buttons_ ) b->hide();
    for ( int v = 0; v < visible.size(); ++v ) {
        const int i = visible[v];
        auto* b = buttons_[i];
        b->setVisible( v < positions.size() );
        if ( v < positions.size() ) b->setGeometry( positions[v] );
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
    groupButton_->setGeometry( gap, gap, std::max( 0, groupWidth - gap ), rowHeight );
    more_->setCheckable( true );
    more_->setChecked( hiddenActive );
    more_->setVisible( overflow );
    more_->setGeometry( gap + groupWidth + std::max( 0, available - more_->sizeHint().width() ),
                        gap + rowHeight + gap,
                        std::min( available, more_->sizeHint().width() ), rowHeight );
}
