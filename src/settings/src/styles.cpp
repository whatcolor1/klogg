/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include <QApplication>
#include <QPalette>
#include <QFontDatabase>
#include <QStyleFactory>
#include <qcolor.h>

#include "configuration.h"
#include "log.h"
#include "styles.h"

namespace {
void applyInterfaceAppearance()
{
    // UI typography is independent of the configurable fixed-pitch log font.
    QFont font = QFontDatabase::systemFont( QFontDatabase::GeneralFont );
#ifdef Q_OS_WIN
    const auto installed = QFontDatabase::families();
    QStringList families;
    for ( const auto& family : { QStringLiteral( "Microsoft YaHei UI" ),
                                QStringLiteral( "Microsoft YaHei" ),
                                QStringLiteral( "Segoe UI" ) } ) {
        if ( installed.contains( family, Qt::CaseInsensitive ) ) families.append( family );
    }
    if ( !families.isEmpty() ) font.setFamilies( families );
#endif
    if ( font.pointSizeF() <= 0 ) font.setPointSizeF( 9.0 );
    font.setStyleHint( QFont::SansSerif );
    font.setStyleStrategy( QFont::PreferAntialias );
    font.setWeight( QFont::Normal );
    font.setItalic( false );
    font.setFixedPitch( false );
    QApplication::setFont( font );
    // Override platform-specific menu fonts too, including mixed-script menus.
    QApplication::setFont( font, "QMenu" );
    QApplication::setFont( font, "QMenuBar" );
    QApplication::setFont( font, "QToolTip" );

    // Palette roles preserve the selected light/dark theme and system contrast.
    // No QWidget/global font selector: custom log painting keeps its own font.
    qApp->setStyleSheet( QStringLiteral( R"(
        QMenu { background: palette(window); color: palette(window-text);
                border: 1px solid palette(mid); padding: 3px 0px; }
        QMenu::item { padding: 4px 24px; }
        QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QMenu::item:disabled { color: palette(mid); }
        QMenu::separator { height: 1px; background: palette(mid); margin: 3px 8px; }
        QMenuBar::item { padding: 4px 8px; }
        QMenuBar::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QToolBar { spacing: 3px; padding: 2px; border: 0px;
                   border-bottom: 1px solid palette(mid); }
        QPushButton { padding: 3px 9px; border: 1px solid palette(mid); border-radius: 4px;
                      background: palette(button); color: palette(button-text); }
        QPushButton:hover { border-color: palette(highlight); }
        QPushButton:pressed, QPushButton:checked { background: palette(highlight);
                                                 color: palette(highlighted-text); }
        QPushButton:default, QPushButton:focus { border-color: palette(highlight); }
        QPushButton:disabled { color: palette(mid); }
        QToolButton { padding: 2px 4px; border: 1px solid transparent; border-radius: 3px; }
        QToolButton:hover { border-color: palette(mid); background: palette(midlight); }
        QToolButton:checked, QToolButton:pressed { background: palette(highlight);
                                                 color: palette(highlighted-text); }
        QToolButton:focus { border-color: palette(highlight); }
        QLineEdit { padding: 3px 5px; border: 1px solid palette(mid); border-radius: 3px;
                    background: palette(base); color: palette(text); }
        QLineEdit:focus { border-color: palette(highlight); }
        QLineEdit:disabled { color: palette(mid); }
    )" ) );
}
}

QStringList StyleManager::availableStyles()
{
    QStringList styles;
#ifdef Q_OS_WIN
    styles << VistaKey;
    styles << WindowsKey;
    styles << FusionKey;
#else
    styles << QStyleFactory::keys();
#endif

    auto removedStyles = std::remove_if( styles.begin(), styles.end(), []( const QString& style ) {
        return style.startsWith( Gtk2Key, Qt::CaseInsensitive )
               || style.startsWith( Bb10Key, Qt::CaseInsensitive );
    } );

    styles.erase( removedStyles, styles.end() );

    styles << DarkStyleKey;

#ifndef Q_OS_MACOS
    styles << DarkWindowsStyleKey;
#endif

    std::sort( styles.begin(), styles.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );

    return styles;
}

QString StyleManager::defaultPlatformStyle()
{
#if defined( Q_OS_WIN )
    return VistaKey;
#elif defined( Q_OS_MACOS )
    return MacintoshKey;
#else
    return FusionKey;
#endif
}

void StyleManager::applyStyle( const QString& style )
{
    LOG_INFO << "Setting style to " << style;

    if ( style == DarkStyleKey || style == DarkWindowsStyleKey ) {
        const auto palette = Configuration::get().darkPalette();

        QPalette darkPalette;
        darkPalette.setColor( QPalette::Window, QColor( palette.at( "Window" ) ) );
        darkPalette.setColor( QPalette::WindowText, QColor( palette.at( "WindowText" ) ) );
        darkPalette.setColor( QPalette::Base, QColor( palette.at( "Base" ) ) );
        darkPalette.setColor( QPalette::AlternateBase, QColor( palette.at( "AlternateBase" ) ) );
        darkPalette.setColor( QPalette::ToolTipBase, QColor( palette.at( "ToolTipBase" ) ) );
        darkPalette.setColor( QPalette::ToolTipText, QColor( palette.at( "ToolTipText" ) ) );
        darkPalette.setColor( QPalette::Text, QColor( palette.at( "Text" ) ) );
        darkPalette.setColor( QPalette::Button, QColor( palette.at( "Button" ) ) );
        darkPalette.setColor( QPalette::ButtonText, QColor( palette.at( "ButtonText" ) ) );
        darkPalette.setColor( QPalette::Link, QColor( palette.at( "Link" ) ) );
        darkPalette.setColor( QPalette::Highlight, QColor( palette.at( "Highlight" ) ) );
        darkPalette.setColor( QPalette::HighlightedText,
                              QColor( palette.at( "HighlightedText" ) ) );

        darkPalette.setColor( QPalette::Active, QPalette::Button,
                              QColor( palette.at( "ActiveButton" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::ButtonText,
                              QColor( palette.at( "DisabledButtonText" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::WindowText,
                              QColor( palette.at( "DisabledWindowText" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::Text,
                              QColor( palette.at( "DisabledText" ) ) );
        darkPalette.setColor( QPalette::Disabled, QPalette::Light,
                              QColor( palette.at( "DisabledLight" ) ) );

        if ( style == DarkWindowsStyleKey ) {
            qApp->setStyle( QStyleFactory::create( WindowsKey ) );
        }
        else {
            qApp->setStyle( QStyleFactory::create( FusionKey ) );
        }

        qApp->setPalette( darkPalette );
    }
    else {
        qApp->setStyle( style );
        qApp->setStyleSheet( "" );
    }
    applyInterfaceAppearance();
}
