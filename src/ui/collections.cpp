#include "ui/collections.hpp"

#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace {

const QColor kAmber("#f2b757");
/// The dots of a priority or a rating not reached.
const QColor kStoneOff(255, 255, 255, 36);

QToolButton *iconButton(const QString &kind, const QString &tip, int size = 28) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, QColor(Theme::muted())));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(size, size);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    b->setToolTip(tip);
    return b;
}

QToolButton *textButton(const char *objectName, const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName(objectName);
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

QLabel *section(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("pageSection");
    return l;
}

/// A caption on the left and something on the right, both section-styled.
QWidget *sectionRow(const QString &left, QWidget *right) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    l->addWidget(section(left), 1);
    if (right) l->addWidget(right, 0, Qt::AlignBottom);
    return w;
}

ElidedLabel *elided(const QString &text, const char *objectName, const QColor &color) {
    auto *l = new ElidedLabel(text, color);
    l->setObjectName(objectName);
    l->setToolTip(text);
    return l;
}

void repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void clearLayout(QLayout *l) {
    while (QLayoutItem *item = l->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();   // taking it out of the layout does not delete it
            w->deleteLater();
        } else if (QLayout *child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

/// "Proyecto Hail Mary" → the first three words, for the text on a cover.
QString coverText(const QString &title) {
    return title.simplified().split(' ', Qt::SkipEmptyParts).mid(0, 3).join(' ');
}

}  // namespace

namespace coll {

// ---------------------------------------------------------------------------
// Painted pieces

/// A cover: its colour, a darker band down the spine and, if big enough, the
/// title's first words at the bottom.
class Cover : public QWidget {
public:
    Cover(const QColor &color, const QString &text, QSize size, QWidget *parent = nullptr)
        : QWidget(parent), m_color(color), m_text(text) {
        setFixedSize(size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect());
        const qreal radius = width() >= 40 ? 4 : 3;
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        p.fillPath(path, m_color);
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(0, 0, width() >= 40 ? 4 : 3, height()), QColor(0, 0, 0, 72));
        p.restore();
        if (m_text.isEmpty() || width() < 40) return;
        QFont f = font();
        f.setPixelSize(width() >= 56 ? 9 : 8);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 214));
        const QRectF box = r.adjusted(width() >= 56 ? 10 : 9, 6, -5, -6);
        p.drawText(box, Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap, m_text);
    }

private:
    QColor m_color;
    QString m_text;
};

/// Priority (3) or rating (5) as stones of slightly uneven size, as in the
/// design: drawn by hand, they read as stones rather than as a progress bar.
class Stones : public QWidget {
public:
    Stones(int total, int filled, const QColor &on, QWidget *parent = nullptr)
        : QWidget(parent), m_total(total), m_filled(filled), m_on(on) {
        setFixedSize(total == 3 ? 24 : 36, 8);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        static const QSizeF three[3] = {{6, 5.4}, {6.6, 6}, {5.6, 5}};
        static const QSizeF five[5] = {{5.5, 5}, {6, 5.5}, {5, 4.6}, {6, 5.4}, {5.4, 5}};
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const qreal gap = m_total == 3 ? 3 : 2.5;
        qreal x = 0;
        for (int i = 0; i < m_total; ++i) {
            const QSizeF s = m_total == 3 ? three[i] : five[i];
            p.setBrush(i < m_filled ? m_on : kStoneOff);
            p.drawEllipse(QRectF(x, (height() - s.height()) / 2, s.width(), s.height()));
            x += s.width() + gap;
        }
    }

private:
    int m_total;
    int m_filled;
    QColor m_on;
};

/// A 4px bar in the accent.
class Bar : public QWidget {
public:
    Bar(int percent, const QColor &color, QWidget *parent = nullptr)
        : QWidget(parent), m_percent(percent), m_color(color) {
        setFixedHeight(4);
        setMinimumWidth(24);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 18));
        p.drawRoundedRect(QRectF(rect()), 2, 2);
        if (m_percent <= 0) return;
        p.setBrush(m_color);
        p.drawRoundedRect(QRectF(0, 0, width() * qMin(m_percent, 100) / 100.0, height()), 2, 2);
    }

private:
    int m_percent;
    QColor m_color;
};

/// A tab: the state's name and its count in mono, as two labels inside the
/// button so each keeps its own font.
class CountTab : public QToolButton {
public:
    QString stateId;

    CountTab(const QString &stateId, QWidget *parent = nullptr) : QToolButton(parent), stateId(stateId) {
        setObjectName("setTab");
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(9, 0, 9, 0);
        l->setSpacing(6);
        m_name = new QLabel;
        m_name->setObjectName("collTabName");
        m_count = new QLabel;
        m_count->setObjectName("collTabCount");
        for (QLabel *label : {m_name, m_count}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            l->addWidget(label);
        }
    }

    void set(const QString &name, int count, bool chosen) {
        m_name->setText(name);
        m_count->setText(QString::number(count));
        setToolTip(name);
        setProperty("chosen", chosen);
        m_name->setProperty("chosen", chosen);
        repolish(this);
        repolish(m_name);
        updateGeometry();
    }

    QSize sizeHint() const override {
        return QSize(m_name->sizeHint().width() + m_count->sizeHint().width() + 6 + 18, 26);
    }
    /// Narrow, the name gives way first: the count is what tells tabs apart.
    QSize minimumSizeHint() const override { return QSize(m_count->sizeHint().width() + 30, 26); }

private:
    QLabel *m_name;
    QLabel *m_count;
};

/// The six dots that reorder a row of the queue.
class Grip : public QWidget {
public:
    std::function<void()> started;
    std::function<void(const QPoint &)> moved;
    std::function<void()> finished;
    bool active = true;   ///< Only while sorted by priority; dimmed otherwise.

    Grip(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(12, 20);
        setCursor(Qt::OpenHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        QColor c(Theme::muted());
        c.setAlphaF(active ? 1.0 : 0.25);
        p.setBrush(c);
        for (int col = 0; col < 2; ++col)
            for (int r = 0; r < 3; ++r)
                p.drawEllipse(QPointF(3.5 + col * 5, 5 + r * 5), 1.5, 1.5);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (!active || e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
        m_press = e->globalPosition().toPoint();
        m_dragging = false;
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!active || !(e->buttons() & Qt::LeftButton)) return;
        const QPoint at = e->globalPosition().toPoint();
        if (!m_dragging) {
            if ((at - m_press).manhattanLength() < QApplication::startDragDistance()) return;
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
            if (started) started();
        }
        if (moved) moved(at);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (!m_dragging) return QWidget::mouseReleaseEvent(e);
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        if (finished) finished();
    }

private:
    QPoint m_press;
    bool m_dragging = false;
};

/// A list row: hover background, Enter or a double click opens the item, the
/// right button its menu, a click selects it (the wide mode's detail).
class Row : public QWidget {
public:
    CollectionItem *item = nullptr;
    Grip *grip = nullptr;
    std::function<void()> open;
    std::function<void()> select;
    std::function<void(const QPoint &)> menu;

    Row(CollectionItem *item, QWidget *parent = nullptr) : QWidget(parent), item(item) {
        setObjectName("bdayRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { if (open) open(); });
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && select)
            select();
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && open) open();
    }
    void contextMenuEvent(QContextMenuEvent *e) override {
        if (menu) menu(e->globalPos());
    }
};

}  // namespace coll

using coll::CountTab;
using coll::Row;

// ---------------------------------------------------------------------------
// CollectionSwitch

CollectionSwitch::CollectionSwitch(const Theme *theme, QWidget *parent)
    : QAbstractButton(parent), m_theme(theme) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setToolTip(L("Cambiar de colección"));
}

void CollectionSwitch::setCollection(const QString &name, const QString &icon) {
    setText(name);
    m_icon = icon.isEmpty() ? QStringLiteral("books") : icon;
    updateGeometry();
    update();
}

QSize CollectionSwitch::sizeHint() const {
    QFont f = font();
    f.setPixelSize(13);
    f.setWeight(QFont::DemiBold);
    return QSize(6 + 16 + 6 + QFontMetrics(f).horizontalAdvance(text()) + 6 + 12 + 6, 28);
}

QSize CollectionSwitch::minimumSizeHint() const { return QSize(6 + 16 + 6 + 24 + 6 + 12 + 6, 28); }

void CollectionSwitch::enterEvent(QEnterEvent *) {
    m_hover = true;
    update();
}
void CollectionSwitch::leaveEvent(QEvent *) {
    m_hover = false;
    update();
}

void CollectionSwitch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setPixelSize(13);
    f.setWeight(QFont::DemiBold);
    const int textW = qMin(QFontMetrics(f).horizontalAdvance(text()), width() - (6 + 16 + 6 + 6 + 12 + 6));
    const QRect content(0, 0, qMin(width(), 6 + 16 + 6 + textW + 6 + 12 + 6), height());
    if (m_hover || isDown() || keynav::showsFocus(this)) {
        p.setPen(keynav::showsFocus(this) ? QPen(m_theme->accent, 1) : QPen(Qt::NoPen));
        p.setBrush(QColor(255, 255, 255, 18));
        p.drawRoundedRect(QRectF(content).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }
    p.drawPixmap(6, (height() - 16) / 2, paintIcon(m_icon, m_theme->accent, 16).pixmap(16, 16));
    p.setFont(f);
    p.setPen(QColor(Theme::fg()));
    p.drawText(QRect(28, 0, textW, height()), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(f).elidedText(text(), Qt::ElideRight, textW));
    p.drawPixmap(28 + textW + 6, (height() - 12) / 2,
                 paintIcon("chevronDown", QColor(Theme::muted()), 12).pixmap(12, 12));
}

// ---------------------------------------------------------------------------
// CollectionsView

CollectionsView::CollectionsView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("collections");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTools());

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildEmpty());

    m_page = new QWidget;
    auto *pageRow = new QHBoxLayout(m_page);
    pageRow->setContentsMargins(0, 0, 0, 0);
    pageRow->setSpacing(0);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName("collScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_listHost = new QWidget;
    m_listHost->setObjectName("listHost");
    m_list = new QVBoxLayout(m_listHost);
    m_list->setContentsMargins(8, 8, 8, 12);
    m_list->setSpacing(7);
    m_scroll->setWidget(m_listHost);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->setObjectName("scrollViewport");
    pageRow->addWidget(m_scroll, 1);

    m_stack->addWidget(m_page);
    root->addWidget(m_stack, 1);
}

QWidget *CollectionsView::buildTools() {
    auto *row = new QFrame;
    row->setObjectName("toolsRow");
    row->setFixedHeight(36);
    m_tools = row;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(9, 0, 6, 0);
    l->setSpacing(2);

    auto *tabs = new QWidget;
    m_tabsLayout = new QHBoxLayout(tabs);
    m_tabsLayout->setContentsMargins(0, 0, 0, 0);
    m_tabsLayout->setSpacing(2);
    l->addWidget(tabs, 0);

    // The search unfolds over the tabs, as the notes' does.
    m_searchField = new QLineEdit;
    m_searchField->setObjectName("searchField");
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setMinimumWidth(24);   // see *Card widths*
    m_searchField->setContextMenuPolicy(Qt::NoContextMenu);
    m_searchField->setPlaceholderText(L("Título, autor o etiqueta"));
    m_searchField->installEventFilter(this);   // Escape closes it
    m_searchField->hide();
    connect(m_searchField, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_search = text.trimmed();
        refreshTabs();
        refreshBody();
    });
    l->addWidget(m_searchField, 1);
    l->addStretch();

    m_sortBtn = textButton("quickChip", QString());
    m_sortBtn->setIcon(paintIcon("sort", QColor(Theme::muted()), 13));
    m_sortBtn->setIconSize(QSize(13, 13));
    m_sortBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sortBtn->setToolTip(L("Ordenar"));
    connect(m_sortBtn, &QToolButton::clicked, this, [this] { openSortMenu(m_sortBtn); });
    l->addWidget(m_sortBtn);

    m_searchBtn = iconButton("search", L("Buscar · Ctrl+F"));
    connect(m_searchBtn, &QToolButton::clicked, this, [this] {
        if (m_searchField->isVisible()) {
            m_searchField->clear();
            m_searchField->hide();
            for (CountTab *t : m_tabs) t->show();
        } else {
            focusSearch();
        }
    });
    l->addWidget(m_searchBtn);
    return row;
}

/// As Seguimiento's: the icon, what this page is and the way to start.
QWidget *CollectionsView::buildEmpty() {
    auto *box = new QWidget;
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(16, 28, 16, 16);
    col->setSpacing(8);
    auto *icon = new QLabel;
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(paintIcon("books", QColor(Theme::muted()), 30).pixmap(30, 30));
    col->addWidget(icon);
    m_emptyTitle = new QLabel;
    m_emptyTitle->setObjectName("trackEmptyTitle");
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setWordWrap(true);
    col->addWidget(m_emptyTitle);
    m_emptyText = new QLabel;
    m_emptyText->setObjectName("popupText");
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    m_emptyText->setMinimumWidth(24);
    col->addWidget(m_emptyText);
    col->addSpacing(6);
    m_emptyCreate = textButton("bdayGhost", QString());
    connect(m_emptyCreate, &QToolButton::clicked, this, [this] { emit newCollectionRequested(); });
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(m_emptyCreate);
    row->addStretch();
    col->addLayout(row);
    col->addStretch();
    return box;
}

void CollectionsView::setStore(Store *store) {
    m_store = store;
    refresh();
}

void CollectionsView::setTheme(const Theme &theme) {
    m_theme = theme;
    refresh();
}

void CollectionsView::retranslate() {
    m_searchField->setPlaceholderText(L("Título, autor o etiqueta"));
    m_sortBtn->setToolTip(L("Ordenar"));
    m_searchBtn->setToolTip(L("Buscar · Ctrl+F"));
    refresh();
}

Collection *CollectionsView::current() const {
    if (!m_store || m_store->collections().isEmpty()) return nullptr;
    if (Collection *c = m_store->collection(m_current)) return c;
    return m_store->collections().first();
}

void CollectionsView::setCurrent(const QString &id) {
    if (id == m_current) return;
    m_current = id;
    m_tab.clear();
    m_selected.clear();
    m_tag.clear();
    refresh();
    emit currentChanged();
}

void CollectionsView::focusSearch() {
    if (!current()) return;
    for (CountTab *t : m_tabs) t->hide();
    m_searchField->show();
    m_searchField->setFocus(Qt::ShortcutFocusReason);
    m_searchField->selectAll();
}

bool CollectionsView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_searchField && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_searchField->clear();
        m_searchField->hide();
        for (CountTab *t : m_tabs) t->show();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void CollectionsView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    const bool wide = width() >= kWideFrom;
    if (wide == m_wide) return;
    m_wide = wide;
    // Not from inside the resize: rebuilding moves widgets the layout is placing.
    QTimer::singleShot(0, this, [this] { refresh(); });
}

void CollectionsView::refresh() {
    Collection *c = current();
    m_emptyTitle->setText(L("Todavía no hay colecciones"));
    m_emptyText->setText(L("Una lista de libros por leer, leyendo y leídos, con su progreso y "
                           "tu cola por prioridad."));
    m_emptyCreate->setText(L("Crear colección"));
    m_stack->setCurrentIndex(c ? 1 : 0);
    m_tools->setVisible(c != nullptr);
    if (!c) return;
    m_current = c->id;

    // The tab shown must still exist; narrow there is no Todos.
    if ((!m_tab.isEmpty() && c->stateIndex(m_tab) < 0) || (m_tab.isEmpty() && !m_wide)) {
        const QString inProgress = c->inProgressState();
        m_tab = inProgress.isEmpty() ? c->firstState() : inProgress;
    }
    applyLayout();
    refreshTabs();
    refreshBody();
}

void CollectionsView::applyLayout() {}

void CollectionsView::refreshTabs() {
    const Collection *c = current();
    if (!c) return;
    // Rebuilt only when the states change; otherwise relabelled, so the focus
    // stays on the tab it was on.
    QStringList ids;
    for (const CollectionState &s : c->states) ids << s.id;
    QStringList have;
    for (const CountTab *t : m_tabs) have << t->stateId;
    if (ids != have) {
        for (CountTab *t : m_tabs) {
            t->hide();
            t->deleteLater();
        }
        m_tabs.clear();
        for (const QString &id : ids) {
            auto *t = new CountTab(id);
            connect(t, &QToolButton::clicked, this, [this, id] {
                m_tab = id;
                refreshTabs();
                refreshBody();
            });
            m_tabsLayout->addWidget(t);
            m_tabs << t;
            if (m_searchField->isVisible()) t->hide();
        }
    }
    for (CountTab *t : m_tabs) {
        const CollectionState *s = c->state(t->stateId);
        t->set(L(s->name.toUtf8().constData()), int(visible(*c, s->id).size()), t->stateId == m_tab);
    }

    // The sort button says what and which way: "Prioridad ↓".
    const bool finished = c->isFinal(m_tab);
    QString name;
    if (c->sortKey == Collection::kByPriority) name = finished ? L("Valoración") : L("Prioridad");
    else if (c->sortKey == Collection::kByYear) name = L("Publicación");
    else if (c->sortKey == Collection::kByAuthor) name = L(c->authorLabel());
    else if (c->sortKey == Collection::kByTitle) name = L("Título");
    else name = finished ? L("Terminado") : L("Añadido");
    m_sortBtn->setText(name + (c->sortAscending ? " ↑" : " ↓"));
}

QList<CollectionItem *> CollectionsView::visible(const Collection &c, const QString &stateId) const {
    QList<CollectionItem *> out;
    if (!m_store) return out;
    for (CollectionItem *i : Coll::sorted(m_store->collectionItems(), c, stateId))
        if (matches(i)) out << i;
    return out;
}

bool CollectionsView::matches(const CollectionItem *item) const {
    if (!m_tag.isEmpty() && !item->tags.contains(m_tag)) return false;
    if (m_search.isEmpty()) return true;
    if (item->title.contains(m_search, Qt::CaseInsensitive)) return true;
    if (item->author.contains(m_search, Qt::CaseInsensitive)) return true;
    for (const QString &t : item->tags)
        if (t.contains(m_search, Qt::CaseInsensitive)) return true;
    return false;
}

void CollectionsView::refreshBody() {
    const Collection *c = current();
    m_queueRows.clear();
    clearLayout(m_list);
    if (!c) return;
    fillList(m_list, *c, m_tab);
    m_list->addStretch();
}

/// What a row says under its stones, after the sort: the year, when it was
/// added, or year and length.
QString CollectionsView::metaFor(const Collection &c, const CollectionItem *item) const {
    QStringList parts;
    if (c.sortKey == Collection::kByAdded)
        return c.isFinal(item->state) ? L("terminado %1").arg(Coll::shortDate(item->finishedMs))
                                      : L("añadido %1").arg(Coll::shortDate(item->addedMs));
    if (c.isFinal(item->state) && item->finishedMs > 0)
        return L("terminado %1").arg(Coll::shortDate(item->finishedMs));
    if (c.has(Collection::kYear) && item->year > 0) parts << QString::number(item->year);
    if (c.sortKey != Collection::kByYear && c.has(Collection::kProgress) && item->total > 0)
        parts << QString("%1 %2").arg(Coll::number(item->total), L(c.unitShort()));
    return parts.join(" · ");
}

Row *CollectionsView::row(const Collection &c, CollectionItem *item, int number,
                          const QString &meta, bool grip, bool rating) {
    auto *r = new Row(item);
    auto *l = new QHBoxLayout(r);
    l->setContentsMargins(grip ? 2 : 6, 7, 6, 7);
    l->setSpacing(9);
    if (grip) {
        r->grip = new coll::Grip;
        r->grip->setToolTip(L("Arrastra para ordenar la cola"));
        l->addWidget(r->grip, 0, Qt::AlignVCenter);
    }
    if (number > 0) {
        auto *n = new QLabel(QString("%1").arg(number, 2, 10, QChar('0')));
        n->setObjectName("timerSub");
        n->setFixedWidth(18);
        l->addWidget(n, 0, Qt::AlignVCenter);
    }
    l->addWidget(new coll::Cover(CollectionItem::coverColor(item->color), QString(), QSize(28, 40)));

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    texts->addWidget(elided(item->title.isEmpty() ? L("Sin título") : item->title, "collTitle",
                            QColor(Theme::fg())));
    QString sub = c.has(Collection::kAuthor) ? item->author : QString();
    if (rating && c.has(Collection::kYear) && item->year > 0)
        sub = sub.isEmpty() ? QString::number(item->year) : sub + " · " + QString::number(item->year);
    if (!sub.isEmpty()) texts->addWidget(elided(sub, "collSub", QColor(Theme::muted())));
    l->addLayout(texts, 1);

    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(5);
    if (rating && c.has(Collection::kRating)) {
        auto *s = new coll::Stones(5, item->rating, m_theme.accent);
        s->setToolTip(L("Valoración %1 de 5").arg(item->rating));
        right->addWidget(s, 0, Qt::AlignRight);
    } else if (!rating && c.has(Collection::kPriority)) {
        auto *s = new coll::Stones(3, item->priority, QColor(Theme::fg()));
        s->setToolTip(item->priority == 3   ? L("Prioridad alta")
                      : item->priority == 2 ? L("Prioridad media")
                                            : L("Prioridad baja"));
        right->addWidget(s, 0, Qt::AlignRight);
    }
    if (!meta.isEmpty()) {
        auto *m = new QLabel(meta);
        m->setObjectName("meta");
        right->addWidget(m, 0, Qt::AlignRight);
    }
    l->addLayout(right);

    r->open = [this, item] { emit editRequested(item); };
    r->select = [this, item] { select(item); };
    r->menu = [this, item](const QPoint &at) { openItemMenu(item, at); };
    return r;
}

/// The plain list of a state: what the queue and any other state show.
void CollectionsView::fillList(QVBoxLayout *col, const Collection &c, const QString &stateId) {
    const QList<CollectionItem *> items = visible(c, stateId);
    int pages = 0;
    for (const CollectionItem *i : items) pages += i->total;
    QLabel *total = nullptr;
    if (c.has(Collection::kProgress) && pages > 0) {
        total = new QLabel(QString("%1 %2").arg(Coll::number(pages), L(c.unitShort())));
        total->setObjectName("meta");
    }
    const CollectionState *s = c.state(stateId);
    col->addWidget(sectionRow(s ? L(s->name.toUtf8().constData()) : QString(), total));
    if (items.isEmpty()) {
        auto *none = new QLabel(m_search.isEmpty() ? L("Nada por aquí todavía.") : L("Nada coincide con la búsqueda."));
        none->setObjectName("popupText");
        none->setContentsMargins(6, 4, 6, 4);
        col->addWidget(none);
        return;
    }
    for (int i = 0; i < items.size(); ++i)
        col->addWidget(row(c, items.at(i), i + 1, metaFor(c, items.at(i)), false, false));
}

void CollectionsView::select(CollectionItem *item) {
    m_selected = item ? item->id : QString();
}

void CollectionsView::edited() {
    emit changed();
    refreshTabs();
    refreshBody();
}

void CollectionsView::moveTo(CollectionItem *item, const QString &stateId) {
    const Collection *c = current();
    if (!c || !item) return;
    Coll::setState(item, *c, stateId);
    edited();
}

void CollectionsView::openItemMenu(CollectionItem *item, const QPoint &globalPos) {
    const Collection *c = current();
    if (!c) return;
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(item->title.isEmpty() ? L("Sin título") : item->title);
    menu->addItem("pencil", L("Editar…"), QString(), [this, item] { emit editRequested(item); });
    for (const CollectionState &s : c->states) {
        if (s.id == item->state) continue;
        const QString id = s.id;
        menu->addItem("chevronRight", L("Mover a %1").arg(L(s.name.toUtf8().constData())), QString(),
                      [this, item, id] { moveTo(item, id); });
    }
    menu->addSeparator();
    menu->addItem("trash", L("Eliminar"), QString(), [this, item] {
        if (!m_store || !m_store->collectionItems().contains(item)) return;
        if (m_selected == item->id) m_selected.clear();
        m_store->removeCollectionItem(item);
        edited();
    });
    menu->showAt(globalPos);
}
