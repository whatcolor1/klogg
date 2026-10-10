#pragma once

#include <QWidget>
#include <QVector>
#include "predefinedfilters.h"

class QToolButton;
class QMenu;

// A bounded, two-row flow panel. Overflow remains keyboard-accessible in More.
class PredefinedFiltersButtonPanel : public QWidget {
    Q_OBJECT
  public:
    explicit PredefinedFiltersButtonPanel( QWidget* parent = nullptr );
    void setFilters( const QList<PredefinedFilter>& filters );
    void setActiveFilters( const QList<PredefinedFilter>& filters );
    QList<PredefinedFilter> activeFilters() const { return active_; }
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

  Q_SIGNALS:
    void filtersChanged( const QList<PredefinedFilter>& filters );

  protected:
    void resizeEvent( QResizeEvent* event ) override;
    void changeEvent( QEvent* event ) override;

  private:
    void arrangeButtons();
    QList<PredefinedFilter> filters_;
    QList<PredefinedFilter> active_;
    QVector<QToolButton*> buttons_;
    QToolButton* more_;
    QMenu* menu_;
};
