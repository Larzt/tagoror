#pragma once

#include <QAbstractButton>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include "core/collection.hpp"
#include "ui/theme.hpp"

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class Store;

namespace coll {
class CountTab;
class Row;
}

/// The header's face on Colecciones: the collection's icon, its name elided and
/// a chevron that opens the menu to change collection or make a new one. A
/// painted button so the name never demands its width (see *Card widths*).
class CollectionSwitch : public QAbstractButton {
public:
    explicit CollectionSwitch(const Theme *theme, QWidget *parent = nullptr);
    void setCollection(const QString &name, const QString &icon);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    const Theme *m_theme;
    QString m_icon = "books";
    bool m_hover = false;
};

/// The Colecciones page (1.3.0): lists of things that move through states —
/// a library with Pendiente, Leyendo and Leído. One tab per state:
///  - the state "in progress" shows a card per item with its progress, quick
///    page chips and *Terminado*, and the next three of the queue;
///  - the other states are lists — the first one the queue, reordered by hand
///    with the grip while sorted by priority —; the last one, the finished,
///    with this year's figures on top and the rating.
/// From kWideFrom up (the app mode's width) it becomes three columns: the
/// collections, tags and the yearly goal on the left; *Todos* and the states
/// as a grid or a list in the middle; the selected item's detail on the right.
///
/// It reads and edits the Store's collections directly — quick changes such as
/// +10 pages or a drag in the queue are too many and too small to route each
/// through Panel — and says so with changed(). What needs a dialog, a note or
/// another page goes out as a signal, as in Seguimiento.
class CollectionsView : public QWidget {
    Q_OBJECT

public:
    /// The page's own width from which it shows three columns.
    static constexpr int kWideFrom = 760;

    explicit CollectionsView(const Theme &theme, QWidget *parent = nullptr);

    void setStore(Store *store);
    void setTheme(const Theme &theme);
    void refresh();
    void retranslate();

    /// The collection shown; the first one when the remembered one is gone.
    Collection *current() const;
    void setCurrent(const QString &id);
    /// Ctrl+F on this page.
    void focusSearch();
    /// Whether the wide detail offers the Planner and Seguimiento (parts of the
    /// app that may be left out).
    void setLinks(bool planner, bool tracking);

signals:
    /// Something was edited here: save, and repaint what shows it elsewhere.
    void changed();
    /// Another collection is shown (the header's name).
    void currentChanged();
    /// The header's "+" from inside the page (the empty state, the wide mode).
    void addRequested();
    void editRequested(CollectionItem *item);
    void newCollectionRequested();
    void editCollectionRequested(Collection *c);
    /// Open the linked Markdown note, creating it first if there is none.
    void noteRequested(CollectionItem *item);
    /// A reading block in the Planner, and the clock in Seguimiento.
    void planRequested(CollectionItem *item);
    void trackRequested(CollectionItem *item);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildTools();
    QWidget *buildEmpty();
    QWidget *buildSide();
    QWidget *buildDetail();
    void refreshTabs();
    void refreshBody();
    void refreshSide();
    void refreshDetail();
    void applyLayout();
    /// The wide mode's detail column, hidden or shown (remembered).
    void setDetailHidden(bool hidden);

    /// The tab's content, as the narrow panel shows it.
    void fillInProgress(QVBoxLayout *col, const Collection &c, const QString &stateId);
    void fillList(QVBoxLayout *col, const Collection &c, const QString &stateId);
    void fillFinished(QVBoxLayout *col, const Collection &c, const QString &stateId);
    /// The wide mode's centre: tiles or rows of the tab (or of all).
    void fillGrid(QVBoxLayout *col, const Collection &c, const QList<CollectionItem *> &items);

    QWidget *readingCard(const Collection &c, CollectionItem *item);
    QWidget *goalCard(const Collection &c);
    void openGoalPrompt(QWidget *anchor);
    QVBoxLayout *progressBlock(const Collection &c, CollectionItem *item, QWidget *trailing);
    coll::Row *row(const Collection &c, CollectionItem *item, int number, const QString &meta,
                   bool grip, bool rating);
    QString metaFor(const Collection &c, const CollectionItem *item) const;
    /// "p. 212 / 496 · 43%" while in progress; empty otherwise.
    QString progressText(const Collection &c, const CollectionItem *item) const;
    /// The wide grid's width for tiles, and the columns that fit in it.
    int gridWidth() const;
    static int gridColumns(int width);
    /// Items of the tab after the search.
    QList<CollectionItem *> visible(const Collection &c, const QString &stateId) const;
    bool matches(const CollectionItem *item) const;

    void openSortMenu(QWidget *anchor);
    void openTagMenu(QWidget *anchor);
    void setTag(const QString &tag);
    void setLength(Coll::Length length);
    /// A tag or a length filters.
    bool filtering() const;
    QString filterText(const Collection &c) const;
    static QString lengthText(const Collection &c, Coll::Length length);
    QHash<int, int> lengthCounts(const Collection &c) const;
    QList<QPair<QString, int>> tagCounts(const Collection &c) const;
    void openItemMenu(CollectionItem *item, const QPoint &globalPos);
    /// Under @p anchor, or at @p at without one (the item's menu).
    void openPagePrompt(CollectionItem *item, QWidget *anchor, const QPoint &at = QPoint());
    void select(CollectionItem *item);
    void addPages(CollectionItem *item, int pages);
    void moveTo(CollectionItem *item, const QString &stateId);
    void edited();

    /// Queue reordering by hand: the row follows the pointer inside the list,
    /// and on release the order is written.
    void dragRowTo(coll::Row *row, const QPoint &globalPos);
    void endRowDrag(coll::Row *row);

    Theme m_theme;
    Store *m_store = nullptr;
    QString m_current;          ///< Collection id.
    QString m_tab;              ///< State id; empty: Todos (wide only).
    QString m_search;
    QString m_tag;              ///< Wide mode's tag filter; empty: none.
    QString m_selected;         ///< Item id shown in the wide detail.
    bool m_wide = false;
    bool m_grid = true;         ///< Wide centre: tiles or rows.
    Coll::Length m_length = Coll::AnyLength;   ///< The page filter (books).
    int m_gridColumns = 0;      ///< The tiles' columns as built; 0: no grid.
    bool m_planner = true;
    bool m_tracking = true;

    QWidget *m_tools = nullptr;
    QHBoxLayout *m_tabsLayout = nullptr;
    QList<coll::CountTab *> m_tabs;
    QToolButton *m_sortBtn = nullptr;
    QToolButton *m_tagBtn = nullptr;   ///< Narrow: the tags, as a menu.
    QToolButton *m_searchBtn = nullptr;
    QLineEdit *m_searchField = nullptr;
    QWidget *m_viewToggle = nullptr;
    QToolButton *m_gridBtn = nullptr;
    QToolButton *m_listBtn = nullptr;
    QToolButton *m_wideAdd = nullptr;

    QStackedWidget *m_stack = nullptr;   ///< Empty state / the page.
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyText = nullptr;
    QToolButton *m_emptyCreate = nullptr;

    QWidget *m_page = nullptr;
    QWidget *m_side = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_list = nullptr;
    QScrollArea *m_detailScroll = nullptr;
    QPointer<QWidget> m_detailCover;   ///< The detail's cover, sized to the column.
    QSize detailCoverSize() const;
    /// Wide: sidebar | centre | detail, with edges that can be dragged.
    QSplitter *m_split = nullptr;
    QToolButton *m_detailBtn = nullptr;
    static constexpr int kSideMin = 160, kSideMax = 360;
    static constexpr int kDetailMin = 240, kDetailMax = 520;
    int m_sideWidth = 210;
    int m_detailWidth = 300;
    bool m_detailHidden = false;
    QWidget *m_detailHost = nullptr;
    QVBoxLayout *m_detail = nullptr;

    QList<coll::Row *> m_queueRows;      ///< The draggable rows, in their current order.
    bool m_rowMoved = false;
};
