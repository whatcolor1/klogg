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

#ifndef KLOGG_TABBEDSCRATCHPAD_H
#define KLOGG_TABBEDSCRATCHPAD_H

#include <QKeyEvent>
#include <QTabWidget>
#include <QWidget>
#include <QTimer>
#include "analysisstorage.h"

class QLabel;
class QCloseEvent;
class QShowEvent;

class TabbedScratchPad : public QWidget {
    Q_OBJECT
  public:
    explicit TabbedScratchPad( QWidget* parent = nullptr );

    ~TabbedScratchPad() override;
    bool configureStorage( const QString& directory, const QString& owner );
    bool flush();
    QString storagePath() const { return store_.path(); }
    AnalysisStorage::ScratchDocument document() const;
    TabbedScratchPad( const TabbedScratchPad& ) = delete;
    TabbedScratchPad& operator=( const TabbedScratchPad& ) = delete;

  public Q_SLOTS:
    void addData( QString newData );
    void replaceData( QString newData );

  protected:
    void keyPressEvent( QKeyEvent* event ) override;
    void closeEvent( QCloseEvent* event ) override;
    void showEvent( QShowEvent* event ) override;

  private:
    void addTab();
    void appendPage( const QString& title, const QString& text );
    void closeTab( int index );
    void scheduleSave();
    void reportFailure( const QString& error );

  private:
    QTabWidget* tabWidget_{ nullptr };
    AnalysisStorage::ScratchStore store_;
    QTimer saveTimer_;
    QLabel* status_ = nullptr;
    bool configured_ = false;
    bool restoring_ = false;
    bool dirty_ = false;
};

#endif // KLOGG_TABBEDSCRATCHPAD_H
