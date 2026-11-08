#include "ui/collections.hpp"

#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/coverimage.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QFontMetrics>
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
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHash>
#include <QVariant>
#include <algorithm>
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

/// @p text wrapped to @p width in at most @p maxLines, the last one elided.
/// A plain drawText() with a word wrap clips instead: aligned to the bottom,
/// a long title lost its beginning ("joven Werther").
QStringList wrapLines(const QString &text, const QFont &font, qreal width, int maxLines) {
    QStringList lines;
    if (text.isEmpty() || maxLines < 1 || width < 1) return lines;
    const QFontMetricsF fm(font);
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(width);
        if (lines.size() == maxLines - 1) {
            lines << fm.elidedText(text.mid(line.textStart()).simplified(), Qt::ElideRight, width);
            break;
        }
        lines << text.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    return lines;
}

}  // namespace

namespace coll {

// ---------------------------------------------------------------------------
// Painted pieces

/// A cover: the item's picture or, without one, its colour, a darker band down
/// the spine and, if big enough, the title's first words at the bottom.
class Cover : public QWidget {
public:
    Cover(const QColor &color, const QString &text, QSize size, QWidget *parent = nullptr)
        : QWidget(parent), m_color(color), m_text(text) {
        setFixedSize(size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    /// The picture's file (CollectionItem::coverPath()); empty: the colour.
    Cover *withImage(const QString &path) {
        m_image = path;
        return this;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect());
        const qreal radius = width() >= 40 ? 4 : 3;
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        // The picture says it all: no spine, no words over it.
        if (coverimage::draw(p, path, r, m_image)) return;
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
    QString m_image;
};

/// Priority (3) or rating (5) as stones of slightly uneven size, as in the
/// design: drawn by hand, they read as stones rather than as a progress bar.
class Stones : public QWidget {
public:
    Stones(int total, int filled, const QColor &on, QWidget *parent = nullptr)
        : QWidget(parent), m_total(total), m_filled(filled), m_on(on) {
        setFixedSize(total == 3 ? 26 : 40, 8);   // the stones' widths and gaps, rounded up
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
        // Elided: three tabs, the sort and the search share 300px.
        m_name = new ElidedLabel(QString(), QColor(Theme::muted()));
        m_name->setObjectName("collTabName");
        m_count = new QLabel;
        m_count->setObjectName("collTabCount");
        for (QLabel *label : {static_cast<QLabel *>(m_name), m_count}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            l->addWidget(label);
        }
    }

    void set(const QString &name, int count, bool chosen) {
        m_name->setText(name);
        m_name->setColor(QColor(chosen ? Theme::fg() : Theme::muted()));
        m_count->setText(QString::number(count));
        setToolTip(name);
        setProperty("chosen", chosen);
        m_name->setProperty("chosen", chosen);
        repolish(this);
        repolish(m_name);
        updateGeometry();
    }

    QSize sizeHint() const override {
        const int name = QFontMetrics(m_name->font()).horizontalAdvance(m_name->text());
        return QSize(name + m_count->sizeHint().width() + 6 + 18 + 2, 26);
    }
    /// Narrow, the name gives way first: the count is what tells tabs apart.
    QSize minimumSizeHint() const override { return QSize(m_count->sizeHint().width() + 30, 26); }

private:
    ElidedLabel *m_name;
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

/// A tile of the wide grid: the cover at 2:3 with the year, the title and the
/// author on it, a progress strip while in progress, and under it the state
/// and the priority.
class Tile : public QWidget {
public:
    CollectionItem *item;
    QString state;
    QColor stateColor;
    int percent = -1;
    bool priority = true;
    bool showYear = true;
    bool showAuthor = true;
    QString image;   ///< The cover picture's path, resolved once; empty: the colour.
    bool selected = false;
    std::function<void()> select;
    std::function<void()> open;
    std::function<void(const QPoint &)> menu;
    const Theme *theme;

    Tile(CollectionItem *item, const Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent), item(item), theme(theme) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setMinimumWidth(90);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        keynav::activatable(this, [this] { if (open) open(); });
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return (w - 12) * 3 / 2 + 12 + 22; }
    QSize sizeHint() const override { return QSize(130, heightForWidth(130)); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (selected || underMouse() || keynav::showsFocus(this)) {
            QColor fill = selected ? theme->accent : QColor(255, 255, 255);
            fill.setAlpha(selected ? 26 : 12);
            QColor edge = selected || keynav::showsFocus(this) ? theme->accent : QColor(255, 255, 255, 30);
            if (selected) edge.setAlpha(115);
            p.setPen(QPen(edge, 1));
            p.setBrush(fill);
            p.drawRoundedRect(frame, 10, 10);
        }
        // 2:3, unless the tile was given less height than that: then the cover
        // gives way, never the foot under it.
        const qreal coverH = qMax<qreal>(40, qMin<qreal>((width() - 12) * 1.5, height() - 6 - 28));
        const QRectF cover(6, 6, width() - 12, coverH);
        QPainterPath path;
        path.addRoundedRect(cover, 4, 4);
        const bool picture = coverimage::draw(p, path, cover, image);
        if (!picture) p.fillPath(path, CollectionItem::coverColor(item->color));
        p.save();
        p.setClipPath(path);
        if (picture) {
            // Shade at the top and the bottom, so the year, the title and the
            // author read over any picture.
            QLinearGradient top(cover.topLeft(), QPointF(cover.left(), cover.top() + cover.height() * 0.25));
            top.setColorAt(0, QColor(0, 0, 0, 150));
            top.setColorAt(1, QColor(0, 0, 0, 0));
            p.fillRect(cover, top);
            QLinearGradient bottom(QPointF(cover.left(), cover.top() + cover.height() * 0.4), cover.bottomLeft());
            bottom.setColorAt(0, QColor(0, 0, 0, 0));
            bottom.setColorAt(1, QColor(0, 0, 0, 215));
            p.fillRect(cover, bottom);
        } else {
            p.fillRect(QRectF(cover.left(), cover.top(), 5, cover.height()), QColor(0, 0, 0, 72));
        }
        if (percent >= 0) {
            const QRectF strip(cover.left(), cover.bottom() - 3, cover.width(), 3);
            p.fillRect(strip, QColor(0, 0, 0, 90));
            p.fillRect(QRectF(strip.left(), strip.top(), strip.width() * percent / 100.0, 3), theme->accent);
        }
        p.restore();

        const QRectF text = cover.adjusted(13, 10, -8, -10);
        QFont mono = font();
        mono.setFamily("IBM Plex Mono");
        mono.setPixelSize(9);
        p.setFont(mono);
        p.setPen(QColor(255, 255, 255, 178));
        qreal top = text.top();
        if (showYear && item->year > 0) {
            p.drawText(text, Qt::AlignLeft | Qt::AlignTop, QString::number(item->year));
            top += QFontMetricsF(mono).height() + 4;
        }
        QFont small = font();
        small.setPixelSize(10);
        const QFontMetricsF sm(small);
        qreal bottom = text.bottom();
        if (showAuthor && !item->author.isEmpty()) {
            const QRectF authorBox(text.left(), text.bottom() - sm.height(), text.width(), sm.height());
            p.setFont(small);
            p.drawText(authorBox, Qt::AlignLeft | Qt::AlignVCenter,
                       sm.elidedText(item->author, Qt::ElideRight, text.width()));
            bottom = authorBox.top() - 3;
        }
        // As many lines of title as the cover leaves room for, from the
        // bottom up, the last one with "…". At least one, over the year if
        // it must: the title is what tells tiles apart.
        QFont big = font();
        big.setPixelSize(12);
        big.setWeight(QFont::DemiBold);
        const qreal lineH = QFontMetricsF(big).height();
        const int maxLines = qBound(1, int((bottom - top) / lineH), 4);
        const QStringList lines = wrapLines(item->title.isEmpty() ? L("Sin título") : item->title, big,
                                            text.width(), maxLines);
        p.setFont(big);
        p.setPen(QColor(255, 255, 255, 235));
        qreal y = bottom - lines.size() * lineH;
        for (const QString &line : lines) {
            p.drawText(QRectF(text.left(), y, text.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter, line);
            y += lineH;
        }

        const QRectF foot(8, cover.bottom() + 4, width() - 16, 16);
        mono.setPixelSize(10);
        p.setFont(mono);
        p.setPen(stateColor);
        // The state, and how far along while in progress; elided before it
        // reaches the stones ("por escuchar" on a narrow tile).
        const qreal stones = priority ? 3 * 5 + 2 * 2 + 6 : 0;
        const QString footText = percent >= 0 ? QString("%1 · %2%").arg(state).arg(percent) : state;
        p.drawText(foot.adjusted(0, 0, -stones, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(mono).elidedText(footText, Qt::ElideRight, foot.width() - stones));
        if (priority) {
            static const qreal sizes[3] = {5, 5, 5};
            qreal x = foot.right() - 3 * 5 - 2 * 2;
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 3; ++i) {
                p.setBrush(i < item->priority ? QColor(Theme::fg()) : kStoneOff);
                p.drawEllipse(QRectF(x, foot.center().y() - sizes[i] / 2, sizes[i], sizes[i]));
                x += sizes[i] + 2;
            }
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && select) select();
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && open) open();
    }
    void contextMenuEvent(QContextMenuEvent *e) override {
        if (menu) menu(e->globalPos());
    }
};

/// A row of the wide sidebar: icon or dot, name, count.
class SideRow : public QToolButton {
public:
    SideRow(const QString &text, const QString &count, QWidget *parent = nullptr) : QToolButton(parent) {
        setObjectName("collSide");
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(30);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(8, 0, 8, 0);
        l->setSpacing(8);
        m_mark = new QLabel;
        m_mark->setFixedSize(16, 16);
        m_mark->setAlignment(Qt::AlignCenter);
        m_mark->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->addWidget(m_mark);
        m_name = new ElidedLabel(text, QColor(Theme::fg()));
        m_name->setObjectName("setRowText");
        m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->addWidget(m_name, 1);
        auto *n = new QLabel(count);
        n->setObjectName("collTabCount");
        n->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->addWidget(n);
        setToolTip(text);
    }
    void setMark(const QPixmap &pm) { m_mark->setPixmap(pm); }
    void setChosen(bool chosen) {
        setProperty("chosen", chosen);
        QFont f = m_name->font();
        f.setWeight(chosen ? QFont::DemiBold : QFont::Normal);
        m_name->setFont(f);
        repolish(this);
    }
    void setMuted() { m_name->setColor(QColor(Theme::muted())); }

private:
    QLabel *m_mark;
    ElidedLabel *m_name;
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
    m_scroll->installEventFilter(this);   // the grid's columns follow its width

    // Wide: the collections on the left and the detail on the right, each
    // behind a hairline.
    const auto divider = [] {
        auto *d = new QFrame;
        d->setObjectName("collDivider");
        d->setFixedWidth(1);
        return d;
    };
    pageRow->addWidget(buildSide());
    auto *leftRule = divider();
    pageRow->addWidget(leftRule);
    pageRow->addWidget(m_scroll, 1);
    auto *rightRule = divider();
    pageRow->addWidget(rightRule);
    pageRow->addWidget(buildDetail());
    m_side->setProperty("rule", QVariant::fromValue<QObject *>(leftRule));
    m_detailScroll->setProperty("rule", QVariant::fromValue<QObject *>(rightRule));

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

    // Narrow only: the tags the wide mode lists on its left, as a menu. Without
    // it a filter chosen wide stayed on, unseen and impossible to clear.
    m_tagBtn = iconButton("tag", L("Filtrar"));
    connect(m_tagBtn, &QToolButton::clicked, this, [this] { openTagMenu(m_tagBtn); });
    m_tagBtn->hide();
    l->addWidget(m_tagBtn);

    m_sortBtn = textButton("quickChip", QString());
    m_sortBtn->setIcon(paintIcon("sort", QColor(Theme::muted()), 13));
    m_sortBtn->setIconSize(QSize(13, 13));
    m_sortBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sortBtn->setToolTip(L("Ordenar"));
    connect(m_sortBtn, &QToolButton::clicked, this, [this] { openSortMenu(m_sortBtn); });
    l->addWidget(m_sortBtn);

    // Wide only: tiles or rows, and "+" right here (the header is far away).
    m_viewToggle = new QFrame;
    m_viewToggle->setObjectName("segTrack");
    auto *vt = new QHBoxLayout(m_viewToggle);
    vt->setContentsMargins(2, 2, 2, 2);
    vt->setSpacing(2);
    m_gridBtn = textButton("segOption", QString());
    m_gridBtn->setIcon(paintIcon("grid", QColor(Theme::muted()), 14));
    m_gridBtn->setToolTip(L("Cuadrícula"));
    m_listBtn = textButton("segOption", QString());
    m_listBtn->setIcon(paintIcon("list", QColor(Theme::muted()), 14));
    m_listBtn->setToolTip(L("Lista"));
    for (QToolButton *b : {m_gridBtn, m_listBtn}) {
        b->setFixedSize(28, 22);
        vt->addWidget(b);
    }
    connect(m_gridBtn, &QToolButton::clicked, this, [this] {
        m_grid = true;
        refreshTabs();
        refreshBody();
    });
    connect(m_listBtn, &QToolButton::clicked, this, [this] {
        m_grid = false;
        refreshTabs();
        refreshBody();
    });
    m_viewToggle->hide();
    l->addWidget(m_viewToggle);

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

    // Filled with the accent, as the notes' "+": the icon is dark.
    m_wideAdd = textButton("addNote", QString());
    m_wideAdd->setIcon(paintIcon("plus", QColor("#0c1222")));
    m_wideAdd->setIconSize(QSize(16, 16));
    m_wideAdd->setFixedSize(28, 28);
    connect(m_wideAdd, &QToolButton::clicked, this, [this] { emit addRequested(); });
    m_wideAdd->hide();
    l->addWidget(m_wideAdd);
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
    m_length = Coll::AnyLength;
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
    // Wider or narrower by a column: the tiles are rebuilt for the new count.
    // Their height follows their width, so leaving them to the layout would
    // stretch them past it and cut the state and stones under the cover.
    // Within the same count, each tile just takes the height of its new width.
    if (watched == m_scroll && event->type() == QEvent::Resize && m_wide && m_grid && m_gridColumns > 0) {
        const int avail = gridWidth();
        const int columns = gridColumns(avail);
        if (columns != m_gridColumns) {
            m_gridColumns = 0;   // one rebuild per change, however many resizes arrive
            QTimer::singleShot(0, this, [this] { if (m_wide) refreshBody(); });
        } else {
            const int tileW = (avail - 8 * (columns - 1)) / columns;
            for (QWidget *w : m_listHost->findChildren<QWidget *>())   // Tile has no Q_OBJECT
                if (auto *t = dynamic_cast<coll::Tile *>(w)) t->setFixedHeight(t->heightForWidth(tileW));
        }
    }
    // A reading card or a tile: double click opens the item, as on a row.
    const QString itemId = watched->property("itemId").toString();
    if (!itemId.isEmpty() && m_store) {
        CollectionItem *item = m_store->collectionItem(itemId);
        if (item && event->type() == QEvent::MouseButtonDblClick) {
            emit editRequested(item);
            return true;
        }
        if (item && event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            select(item);
            return false;
        }
    }
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
    if (m_wide) {
        refreshSide();
        refreshDetail();
    }
}

void CollectionsView::setLinks(bool planner, bool tracking) {
    m_planner = planner;
    m_tracking = tracking;
    if (m_wide) refreshDetail();
}

void CollectionsView::applyLayout() {
    m_side->setVisible(m_wide);
    m_detailScroll->setVisible(m_wide);
    if (auto *rule = qobject_cast<QWidget *>(m_side->property("rule").value<QObject *>()))
        rule->setVisible(m_wide);
    if (auto *rule = qobject_cast<QWidget *>(m_detailScroll->property("rule").value<QObject *>()))
        rule->setVisible(m_wide);
    m_viewToggle->setVisible(m_wide);
    m_wideAdd->setVisible(m_wide);
    m_list->setContentsMargins(m_wide ? 12 : 8, m_wide ? 12 : 8, m_wide ? 12 : 8, 12);
}

void CollectionsView::refreshTabs() {
    const Collection *c = current();
    if (!c) return;
    // Rebuilt only when the states change; otherwise relabelled, so the focus
    // stays on the tab it was on.
    // The state in progress first, as the design has it (Leyendo, Pendientes,
    // Leídos): it is what the page opens on. The rest in their order.
    QStringList ids;
    if (m_wide) ids << QString();   // Todos
    const QString inProgress = c->inProgressState();
    if (!inProgress.isEmpty()) ids << inProgress;
    for (const CollectionState &s : c->states)
        if (s.id != inProgress) ids << s.id;
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
        t->set(s ? L(s->name) : L("Todos"), int(visible(*c, t->stateId).size()), t->stateId == m_tab);
    }
    // Wide, the sidebar has the filters; narrow, this button, in the accent
    // while one is on.
    m_tagBtn->setVisible(!m_wide && (filtering() || Coll::filtersByLength(*c) || !tagCounts(*c).isEmpty()));
    m_tagBtn->setIcon(paintIcon("tag", filtering() ? m_theme.accent : QColor(Theme::muted())));
    m_tagBtn->setToolTip(filtering() ? filterText(*c) : L("Filtrar"));
    m_gridBtn->setProperty("chosen", m_grid);
    m_listBtn->setProperty("chosen", !m_grid);
    repolish(m_gridBtn);
    repolish(m_listBtn);

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
    if (m_length != Coll::AnyLength && Coll::lengthOf(item) != m_length) return false;
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
    // Narrow there is no sidebar to show what filters: it goes on top, with
    // the way out.
    if (!m_wide && filtering()) {
        auto *clear = textButton("noticeAction", L("Quitar el filtro"));
        connect(clear, &QToolButton::clicked, this, [this] {
            m_length = Coll::AnyLength;
            setTag(QString());
        });
        m_list->addWidget(sectionRow(filterText(*c), clear));
    }
    if (m_wide) fillGrid(m_list, *c, visible(*c, m_tab));
    else if (c->isInProgress(m_tab)) fillInProgress(m_list, *c, m_tab);
    else if (c->isFinal(m_tab)) fillFinished(m_list, *c, m_tab);
    else fillList(m_list, *c, m_tab);
    m_list->addStretch();
}

/// What a row says under its stones, after the sort: the year, when it was
/// added, or year and length.
QString CollectionsView::metaFor(const Collection &c, const CollectionItem *item) const {
    QStringList parts;
    // In progress, how far along says more than any sort.
    const QString progress = progressText(c, item);
    if (!progress.isEmpty()) return progress;
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
    l->addWidget((new coll::Cover(CollectionItem::coverColor(item->color), QString(), QSize(28, 40)))
                     ->withImage(item->coverPath()));

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    texts->addWidget(elided(item->title.isEmpty() ? L("Sin título") : item->title, "collTitle",
                            QColor(Theme::fg())));
    QString sub = c.has(Collection::kAuthor) ? item->author : QString();
    if (rating && c.has(Collection::kYear) && item->year > 0)
        sub = sub.isEmpty() ? QString::number(item->year) : sub + " · " + QString::number(item->year);
    if (!sub.isEmpty()) texts->addWidget(elided(sub, "collSub", QColor(Theme::muted())));
    // In progress, its bar, as on the reading card: the wide list's rows are
    // the only place the Todos and Leyendo tabs show it.
    if (c.isInProgress(item->state) && c.has(Collection::kProgress) && item->total > 0) {
        texts->addSpacing(2);
        texts->addWidget(new coll::Bar(item->percent(), m_theme.accent));
    }
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
    // The first state is the queue, and says how it is ordered.
    const bool queue = stateId == c.firstState();
    QString caption;
    if (!queue) {
        const CollectionState *s = c.state(stateId);
        caption = s ? L(s->name) : QString();
    } else if (c.sortKey == Collection::kByPriority) {
        caption = L("Cola · por prioridad");
    } else if (c.sortKey == Collection::kByYear) {
        caption = L("Por año de publicación");
    } else if (c.sortKey == Collection::kByAuthor) {
        caption = L("Por %1").arg(L(c.authorLabel()).toLower());
    } else if (c.sortKey == Collection::kByTitle) {
        caption = L("Por título");
    } else {
        caption = L("Por fecha añadido");
    }
    col->addWidget(sectionRow(caption, total));
    if (items.isEmpty()) {
        auto *none = new QLabel(m_search.isEmpty() && !filtering() ? L("Nada por aquí todavía.")
                                                                       : L("Nada coincide con la búsqueda."));
        none->setObjectName("popupText");
        none->setContentsMargins(6, 4, 6, 4);
        col->addWidget(none);
        return;
    }
    // By hand only where the order shown is the queue's own: by priority, high
    // first, and with nothing filtered out.
    const bool handOrder = queue && c.sortKey == Collection::kByPriority && !c.sortAscending &&
                           m_search.isEmpty() && !filtering();
    for (int i = 0; i < items.size(); ++i) {
        Row *r = row(c, items.at(i), i + 1, metaFor(c, items.at(i)), queue, false);
        if (r->grip) {
            r->grip->active = handOrder;
            r->grip->setToolTip(handOrder ? L("Arrastra para ordenar la cola")
                                          : L("Ordena por prioridad para mover a mano"));
            r->grip->moved = [this, r](const QPoint &at) { dragRowTo(r, at); };
            r->grip->finished = [this, r] { endRowDrag(r); };
            m_queueRows << r;
        }
        col->addWidget(r);
    }
}

/// The in-progress tab: a card per item, then what comes next.
void CollectionsView::fillInProgress(QVBoxLayout *col, const Collection &c, const QString &stateId) {
    const QList<CollectionItem *> items = visible(c, stateId);
    for (CollectionItem *i : items) col->addWidget(readingCard(c, i));
    if (items.isEmpty()) {
        auto *box = new QWidget;
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(16, 24, 16, 18);
        v->setSpacing(6);
        const bool filtered = !m_search.isEmpty() || filtering();
        auto *title = new QLabel(!filtered ? L("Nada en curso") : L("Nada coincide con la búsqueda."));
        title->setObjectName("trackEmptyTitle");
        title->setAlignment(Qt::AlignCenter);
        v->addWidget(title);
        if (!filtered) {
            auto *text = new QLabel(L("Empieza el primero de la cola."));
            text->setObjectName("popupText");
            text->setAlignment(Qt::AlignCenter);
            text->setWordWrap(true);
            text->setMinimumWidth(24);
            v->addWidget(text);
        }
        col->addWidget(box);
    }

    // A CONTINUACIÓN: the first three of the queue, each one click from starting.
    // Not while filtering: the queue would not be.
    if (!m_search.isEmpty() || filtering()) return;
    const QList<CollectionItem *> next = Coll::queue(m_store->collectionItems(), c).mid(0, 3);
    if (next.isEmpty() || c.firstState() == stateId) return;
    auto *seeQueue = textButton("noticeAction", L("Ver cola"));
    connect(seeQueue, &QToolButton::clicked, this, [this] {
        Collection *cur = current();
        if (!cur) return;
        m_tab = cur->firstState();
        cur->sortKey = Collection::kByPriority;
        cur->sortAscending = false;
        edited();
    });
    auto *head = sectionRow(L("A continuación"), seeQueue);
    head->setContentsMargins(0, 6, 0, 0);
    col->addWidget(head);
    for (int i = 0; i < next.size(); ++i) {
        CollectionItem *item = next.at(i);
        Row *r = row(c, item, i + 1, QString(), false, false);
        // "Empezar" instead of the stones: what this list is for.
        auto *l = static_cast<QHBoxLayout *>(r->layout());
        QLayoutItem *last = l->takeAt(l->count() - 1);
        if (last) {
            if (last->layout()) clearLayout(last->layout());
            delete last;
        }
        auto *start = textButton("quickChip", L("Empezar"));
        const QString target = stateId;
        connect(start, &QToolButton::clicked, this, [this, item, target] { moveTo(item, target); });
        l->addWidget(start, 0, Qt::AlignVCenter);
        col->addWidget(r);
    }
}

QWidget *CollectionsView::readingCard(const Collection &c, CollectionItem *item) {
    auto *card = new QFrame;
    card->setObjectName("trackClock");
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(10);

    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    top->addWidget((new coll::Cover(CollectionItem::coverColor(item->color), coverText(item->title),
                                    QSize(46, 66)))->withImage(item->coverPath()),
                   0, Qt::AlignTop);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(3);
    const CollectionState *s = c.state(item->state);
    QString kicker = s ? L(s->name).toUpper() : QString();
    if (item->startedMs > 0) kicker += " · " + L("DESDE %1").arg(Coll::shortDate(item->startedMs).toUpper());
    auto *k = new ElidedLabel(kicker, m_theme.accent);
    k->setObjectName("collKicker");
    texts->addWidget(k);
    auto *title = new QLabel(item->title.isEmpty() ? L("Sin título") : item->title);
    title->setObjectName("trackTask");
    title->setWordWrap(true);
    title->setMinimumWidth(24);   // see *Card widths*
    texts->addWidget(title);
    QStringList sub;
    if (c.has(Collection::kAuthor) && !item->author.isEmpty()) sub << item->author;
    if (c.has(Collection::kYear) && item->year > 0) sub << QString::number(item->year);
    if (!sub.isEmpty()) texts->addWidget(elided(sub.join(" · "), "collSub", QColor(Theme::muted())));
    texts->addStretch();
    top->addLayout(texts, 1);
    v->addLayout(top);

    auto *done = textButton("pickSave", L("Terminado"));
    done->setToolTip(L("Pasa a %1 con la fecha de hoy").arg(L(c.states.last().name)));
    const QString final = c.finalState();
    connect(done, &QToolButton::clicked, this, [this, item, final] { moveTo(item, final); });
    v->addLayout(progressBlock(c, item, done));

    // The card opens the item too, as a row does.
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this,
            [this, item, card](const QPoint &p) { openItemMenu(item, card->mapToGlobal(p)); });
    card->installEventFilter(this);
    card->setProperty("itemId", item->id);
    return card;
}

/// Where something in progress is and the ways to move it on: the bar, "p. 212
/// / 496" and what is left, +10, +25 and the page typed. The reading card's,
/// and the wide detail's, so the page changes without opening Editar.
/// @p trailing, if any, closes the row of buttons (the card's Terminado).
QVBoxLayout *CollectionsView::progressBlock(const Collection &c, CollectionItem *item, QWidget *trailing) {
    auto *v = new QVBoxLayout;
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    const QString unit = L(c.unitShort());
    if (c.has(Collection::kProgress)) {
        if (item->total > 0) {
            const int pct = item->percent();
            v->addWidget(new coll::Bar(pct, m_theme.accent));
            auto *nums = new QHBoxLayout;
            auto *at = new QLabel(QString("%1 <span style='color:%2'>%3</span> / %4")
                                      .arg(unit, Theme::fg(), Coll::number(item->progress),
                                           Coll::number(item->total)));
            at->setObjectName("timerSub");
            at->setTextFormat(Qt::RichText);
            nums->addWidget(at);
            nums->addStretch();
            auto *left = new QLabel(L("%1% · faltan %2 %3")
                                        .arg(pct)
                                        .arg(Coll::number(qMax(0, item->total - item->progress)), unit));
            left->setObjectName("timerSub");
            nums->addWidget(left);
            v->addLayout(nums);
        } else if (item->progress > 0) {
            auto *at = new QLabel(QString("%1 %2").arg(unit, Coll::number(item->progress)));
            at->setObjectName("timerSub");
            v->addWidget(at);
        }
    }

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    if (c.has(Collection::kProgress)) {
        for (int step : {10, 25}) {
            auto *chip = textButton("quickChip", QString("+%1 %2").arg(step).arg(unit));
            connect(chip, &QToolButton::clicked, this, [this, item, step] { addPages(item, step); });
            actions->addWidget(chip);
        }
        auto *write = textButton("quickChip", unit + " …");
        write->setToolTip(L("Escribir la página"));
        connect(write, &QToolButton::clicked, this, [this, item, write] { openPagePrompt(item, write); });
        actions->addWidget(write);
    }
    actions->addStretch();
    if (trailing) actions->addWidget(trailing);
    v->addLayout(actions);
    return v;
}


/// The finished tab: this year's figures, then the list with the rating.
void CollectionsView::fillFinished(QVBoxLayout *col, const Collection &c, const QString &stateId) {
    const QList<CollectionItem *> items = visible(c, stateId);
    const QDate today = QDate::currentDate();
    int thisYear = 0, units = 0;
    for (const CollectionItem *i : items) {
        if (i->finishedMs <= 0 || QDateTime::fromMSecsSinceEpoch(i->finishedMs).date().year() != today.year())
            continue;
        ++thisYear;
        units += i->total;
    }
    auto *tiles = new QWidget;
    auto *grid = new QGridLayout(tiles);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);
    const auto tile = [](const QString &label, const QString &value) {
        auto *f = new QFrame;
        f->setObjectName("timerHero");
        auto *v = new QVBoxLayout(f);
        v->setContentsMargins(10, 8, 10, 8);
        v->setSpacing(2);
        auto *l = new ElidedLabel(label, QColor(Theme::muted()));
        l->setObjectName("meta");
        v->addWidget(l);
        auto *n = new QLabel(value);
        n->setObjectName("trackTileValue");
        v->addWidget(n);
        return f;
    };
    const double perMonth = thisYear / double(today.month());
    grid->addWidget(tile(L("EN %1").arg(today.year()), QString::number(thisYear)), 0, 0);
    if (c.has(Collection::kProgress))
        grid->addWidget(tile(L(c.unitName()).toUpper(), Coll::number(units)), 0, 1);
    grid->addWidget(tile(L("MEDIA/MES"), Lang::locale().toString(perMonth, 'f', 1)), 0, 2);
    for (int i = 0; i < 3; ++i) grid->setColumnStretch(i, 1);
    col->addWidget(tiles);

    QString caption;
    if (c.sortKey == Collection::kByPriority) caption = L("Por valoración");
    else if (c.sortKey == Collection::kByYear) caption = L("Por año de publicación");
    else if (c.sortKey == Collection::kByAuthor) caption = L("Por %1").arg(L(c.authorLabel()).toLower());
    else if (c.sortKey == Collection::kByTitle) caption = L("Por título");
    else caption = L("Por fecha de fin");
    col->addWidget(sectionRow(caption, nullptr));
    if (items.isEmpty()) {
        auto *none = new QLabel(m_search.isEmpty() && !filtering() ? L("Nada terminado todavía.")
                                                                       : L("Nada coincide con la búsqueda."));
        none->setObjectName("popupText");
        none->setContentsMargins(6, 4, 6, 4);
        col->addWidget(none);
        return;
    }
    for (CollectionItem *i : items) col->addWidget(row(c, i, 0, metaFor(c, i), false, true));
}

void CollectionsView::addPages(CollectionItem *item, int pages) {
    int to = item->progress + pages;
    if (item->total > 0) to = qMin(to, item->total);
    item->progress = qMax(0, to);
    edited();
}

/// "p. …": the page typed, as the timers' editor takes a time.
void CollectionsView::openPagePrompt(CollectionItem *item, QWidget *anchor, const QPoint &at) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(item->total > 0 ? L("Página (de %1)").arg(item->total) : L("Página"));
    QPointer<CollectionsView> self(this);
    const QString id = item->id;
    menu->addEditor(L("Página"), item->progress > 0 ? QString::number(item->progress) : QString(),
                    [self, id](const QString &text) {
        if (!self || !self->m_store) return;
        CollectionItem *i = self->m_store->collectionItem(id);
        bool ok = false;
        const int page = text.trimmed().toInt(&ok);
        if (!i || !ok || page < 0) return;
        i->progress = i->total > 0 ? qMin(page, i->total) : page;
        self->edited();
    });
    if (anchor) menu->showUnder(anchor);
    else menu->showAt(at);
}

/// The current collection's tags, most used first, with their counts.
QList<QPair<QString, int>> CollectionsView::tagCounts(const Collection &c) const {
    QHash<QString, int> counts;
    if (m_store)
        for (const CollectionItem *i : m_store->collectionItems())
            if (i->collection == c.id)
                for (const QString &t : i->tags) ++counts[t];
    QList<QPair<QString, int>> out;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) out.append({it.key(), it.value()});
    std::sort(out.begin(), out.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    return out;
}

/// Filters by @p tag (empty: none), in both modes.
void CollectionsView::setTag(const QString &tag) {
    m_tag = tag;
    refreshTabs();
    refreshBody();
    if (m_wide) refreshSide();
}

/// Filters by length (1.3.3), in both modes.
void CollectionsView::setLength(Coll::Length length) {
    m_length = length;
    refreshTabs();
    refreshBody();
    if (m_wide) refreshSide();
}

bool CollectionsView::filtering() const { return !m_tag.isEmpty() || m_length != Coll::AnyLength; }

/// "Hasta 250 p.", "251–500 p.", "Más de 500 p.".
QString CollectionsView::lengthText(const Collection &c, Coll::Length length) {
    const QString unit = L(c.unitShort());
    switch (length) {
        case Coll::Short:  return L("Hasta %1 %2").arg(Coll::kShortUpTo).arg(unit);
        case Coll::Medium: return QString("%1–%2 %3").arg(Coll::kShortUpTo + 1).arg(Coll::kMediumUpTo).arg(unit);
        case Coll::Long:   return L("Más de %1 %2").arg(Coll::kMediumUpTo).arg(unit);
        default:           return L("Cualquiera");
    }
}

/// What filters, in words: "Solo «Fantasía» · Hasta 250 p.".
QString CollectionsView::filterText(const Collection &c) const {
    QStringList parts;
    if (!m_tag.isEmpty()) parts << L("Solo «%1»").arg(m_tag);
    if (m_length != Coll::AnyLength) parts << lengthText(c, m_length);
    return parts.join(" · ");
}

/// How many of @p c's items are in each length band.
QHash<int, int> CollectionsView::lengthCounts(const Collection &c) const {
    QHash<int, int> counts;
    if (m_store)
        for (const CollectionItem *i : m_store->collectionItems())
            if (i->collection == c.id) ++counts[Coll::lengthOf(i)];
    return counts;
}

/// Narrow: the filters as a menu — length for books, then every tag (the
/// sidebar shows eight) — each with the way back to all.
void CollectionsView::openTagMenu(QWidget *anchor) {
    const Collection *c = current();
    if (!c) return;
    auto *menu = new Popup(m_theme, this);
    QPointer<CollectionsView> self(this);
    if (Coll::filtersByLength(*c)) {
        menu->addHeader(L(c->unitName()).toUpper());
        const QHash<int, int> counts = lengthCounts(*c);
        for (Coll::Length band : {Coll::AnyLength, Coll::Short, Coll::Medium, Coll::Long}) {
            const QString hint = band == Coll::AnyLength ? QString() : QString::number(counts.value(band));
            menu->addShortcutItem(m_length == band ? "check" : "", lengthText(*c, band), hint, [self, band] {
                if (self) self->setLength(band);
            });
        }
        menu->addSeparator();
    }
    const QList<QPair<QString, int>> tags = tagCounts(*c);
    if (tags.isEmpty() && m_tag.isEmpty()) {
        menu->showUnder(anchor);
        return;
    }
    menu->addHeader(L("ETIQUETAS"));
    menu->addShortcutItem(m_tag.isEmpty() ? "check" : "", L("Todas"), QString(), [self] {
        if (self) self->setTag(QString());
    });
    for (const auto &[tag, count] : tags) {
        const QString t = tag;
        menu->addShortcutItem(t == m_tag ? "check" : "", t, QString::number(count), [self, t] {
            if (self) self->setTag(t);
        });
    }
    menu->showUnder(anchor);
}

void CollectionsView::openSortMenu(QWidget *anchor) {
    Collection *c = current();
    if (!c) return;
    const bool finished = c->isFinal(m_tab);
    struct Option {
        QString key, label, hint;
        bool shown;
    };
    const QList<Option> options{
        {Collection::kByPriority, finished ? L("Valoración") : L("Prioridad"),
         finished ? L("valoración") : L("cola"),
         finished ? c->has(Collection::kRating) : c->has(Collection::kPriority)},
        {Collection::kByYear, L("Publicación"), L("año"), c->has(Collection::kYear)},
        {Collection::kByAuthor, L(c->authorLabel()), L("apellido"), c->has(Collection::kAuthor)},
        {Collection::kByTitle, L("Título"), QStringLiteral("A–Z"), true},
        {Collection::kByAdded, finished ? L("Terminado") : L("Añadido"), L("fecha"), true},
    };
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("ORDENAR POR"));
    QPointer<CollectionsView> self(this);
    const QString id = c->id;
    for (const Option &o : options) {
        if (!o.shown) continue;
        const QString key = o.key;
        menu->addShortcutItem(c->sortKey == key ? "check" : "", o.label, o.hint, [self, id, key] {
            if (!self || !self->m_store) return;
            Collection *col = self->m_store->collection(id);
            if (!col) return;
            col->sortKey = key;
            col->sortAscending = Collection::defaultAscending(key);
            self->edited();
        });
    }
    menu->addSeparator();
    // Which way it goes now, in words: "Alta primero", "Más antiguos primero",
    // "A → Z". The row turns it round.
    const QString k = c->sortKey;
    const bool asc = c->sortAscending;
    QString words;
    if (k == Collection::kByPriority)
        words = finished ? (asc ? L("Peor valorados primero") : L("Mejor valorados primero"))
                         : (asc ? L("Baja primero") : L("Alta primero"));
    else if (k == Collection::kByAuthor || k == Collection::kByTitle)
        words = asc ? QStringLiteral("A → Z") : QStringLiteral("Z → A");
    else
        words = asc ? L("Más antiguos primero") : L("Más recientes primero");
    menu->addShortcutItem(asc ? "chevronUp" : "chevronDown", words, QString(), [self, id] {
        if (!self || !self->m_store) return;
        if (Collection *col = self->m_store->collection(id)) {
            col->sortAscending = !col->sortAscending;
            self->edited();
        }
    });
    menu->showUnder(anchor);
}

void CollectionsView::dragRowTo(Row *dragged, const QPoint &globalPos) {
    if (!m_queueRows.contains(dragged)) return;
    // Near the edges the list scrolls, as the notes do while dragging a card.
    const QPoint inView = m_scroll->viewport()->mapFromGlobal(globalPos);
    QScrollBar *bar = m_scroll->verticalScrollBar();
    if (inView.y() < 24) bar->setValue(bar->value() - 12);
    else if (inView.y() > m_scroll->viewport()->height() - 24) bar->setValue(bar->value() + 12);

    const int y = m_listHost->mapFromGlobal(globalPos).y();
    int target = 0;
    for (Row *r : std::as_const(m_queueRows)) {
        if (r == dragged) continue;
        if (r->geometry().center().y() < y) ++target;
    }
    const int from = int(m_queueRows.indexOf(dragged));
    if (target == from) return;
    const int base = m_list->indexOf(m_queueRows.first());
    m_queueRows.move(from, target);
    m_list->removeWidget(dragged);
    m_list->insertWidget(base + target, dragged);
    m_rowMoved = true;
}

void CollectionsView::endRowDrag(Row *dragged) {
    if (!m_rowMoved || !current()) return;
    m_rowMoved = false;
    QList<CollectionItem *> order;
    for (Row *r : std::as_const(m_queueRows)) order << r->item;
    Coll::moveInQueue(order, dragged->item, int(order.indexOf(dragged->item)));
    edited();
}

void CollectionsView::select(CollectionItem *item) {
    const QString id = item ? item->id : QString();
    if (id == m_selected || !m_wide) {
        m_selected = id;
        return;
    }
    m_selected = id;
    // The tiles show which one; repainting them is enough.
    // No Q_OBJECT on these, so findChildren<Tile *>() cannot tell them apart.
    for (QWidget *w : m_listHost->findChildren<QWidget *>()) {
        if (auto *t = dynamic_cast<coll::Tile *>(w)) {
            t->selected = t->item->id == m_selected;
            t->update();
        } else if (auto *r = dynamic_cast<Row *>(w)) {
            r->setProperty("chosen", r->item->id == m_selected);
            repolish(r);
        }
    }
    refreshDetail();
}

void CollectionsView::edited() {
    emit changed();
    refreshTabs();
    refreshBody();
    if (m_wide) {
        refreshSide();
        refreshDetail();
    }
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
    // The page, from wherever it is shown (tile, row), without Editar.
    if (c->isInProgress(item->state) && c->has(Collection::kProgress)) {
        const QString unit = L(c->unitShort());
        for (int step : {10, 25})
            menu->addItem("plus", QString("+%1 %2").arg(step).arg(unit), QString(),
                          [this, item, step] { addPages(item, step); });
        menu->addItem("pencil", L("Escribir la página"), QString(),
                      [this, item, globalPos] { openPagePrompt(item, nullptr, globalPos); });
    }
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

// ---------------------------------------------------------------------------
// Wide mode

QWidget *CollectionsView::buildSide() {
    m_side = new QWidget;
    m_side->setObjectName("collPane");
    m_side->setFixedWidth(210);
    m_sideLayout = new QVBoxLayout(m_side);
    m_sideLayout->setContentsMargins(8, 10, 8, 10);
    m_sideLayout->setSpacing(2);
    m_side->hide();
    return m_side;
}

QWidget *CollectionsView::buildDetail() {
    m_detailScroll = new QScrollArea;
    m_detailScroll->setFixedWidth(300);
    m_detailScroll->setWidgetResizable(true);
    m_detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailScroll->setFrameShape(QFrame::NoFrame);
    m_detailHost = new QWidget;
    m_detailHost->setObjectName("listHost");
    m_detail = new QVBoxLayout(m_detailHost);
    m_detail->setContentsMargins(16, 16, 16, 16);
    m_detail->setSpacing(14);
    m_detailScroll->setWidget(m_detailHost);
    m_detailScroll->viewport()->setAutoFillBackground(false);
    m_detailScroll->viewport()->setObjectName("scrollViewport");
    m_detailScroll->hide();
    return m_detailScroll;
}

/// The collections with their counts, the current one's tags (a click filters
/// by one) and, if it has one, the yearly goal.
void CollectionsView::refreshSide() {
    clearLayout(m_sideLayout);
    const Collection *shown = current();
    if (!m_store || !shown) return;
    m_sideLayout->addWidget(section(L("Colecciones")));
    for (Collection *c : m_store->collections()) {
        auto *r = new coll::SideRow(c->name, QString::number(Coll::countIn(m_store->collectionItems(), c->id)));
        r->setMark(paintIcon(c->icon(), c == shown ? m_theme.accent : QColor(Theme::muted()), 16).pixmap(16, 16));
        r->setChosen(c == shown);
        const QString id = c->id;
        connect(r, &QToolButton::clicked, this, [this, id] { setCurrent(id); });
        m_sideLayout->addWidget(r);
    }
    auto *add = new coll::SideRow(L("Nueva colección"), QString());
    add->setMark(paintIcon("plus", QColor(Theme::muted()), 14).pixmap(14, 14));
    add->setMuted();
    connect(add, &QToolButton::clicked, this, [this] { emit newCollectionRequested(); });
    m_sideLayout->addWidget(add);

    // Tags, by how many items carry them.
    QHash<QString, int> counts;
    for (const CollectionItem *i : m_store->collectionItems())
        if (i->collection == shown->id)
            for (const QString &t : i->tags) ++counts[t];
    if (!counts.isEmpty()) {
        QStringList tags = counts.keys();
        std::sort(tags.begin(), tags.end(), [&counts](const QString &a, const QString &b) {
            return counts.value(a) != counts.value(b) ? counts.value(a) > counts.value(b) : a < b;
        });
        auto *head = section(L("Etiquetas"));
        head->setContentsMargins(0, 12, 0, 0);
        m_sideLayout->addWidget(head);
        static const QColor dots[] = {QColor(), kAmber, QColor(Theme::muted()), QColor("#4fc99a"),
                                      QColor("#b98cff"), QColor("#ff7a6b")};
        for (int i = 0; i < tags.size() && i < 8; ++i) {
            const QString tag = tags.at(i);
            auto *r = new coll::SideRow(tag, QString::number(counts.value(tag)));
            QPixmap dot(16, 16);
            dot.fill(Qt::transparent);
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(i == 0 ? m_theme.accent : dots[i % 6]);
            p.drawEllipse(QRectF(4, 4, 8, 8));
            p.end();
            r->setMark(dot);
            r->setChosen(tag == m_tag);
            r->setToolTip(tag == m_tag ? L("Quitar el filtro") : L("Ver solo «%1»").arg(tag));
            connect(r, &QToolButton::clicked, this, [this, tag] { setTag(m_tag == tag ? QString() : tag); });
            m_sideLayout->addWidget(r);
        }
    }
    // PÁGINAS: the length bands, for books; a click filters, again unfilters.
    if (Coll::filtersByLength(*shown)) {
        const QHash<int, int> counts = lengthCounts(*shown);
        auto *head = section(L(shown->unitName()).left(1).toUpper() + L(shown->unitName()).mid(1));
        head->setContentsMargins(0, 12, 0, 0);
        m_sideLayout->addWidget(head);
        for (Coll::Length band : {Coll::Short, Coll::Medium, Coll::Long}) {
            auto *r = new coll::SideRow(lengthText(*shown, band), QString::number(counts.value(band)));
            r->setChosen(m_length == band);
            r->setToolTip(m_length == band ? L("Quitar el filtro") : L("Ver solo «%1»").arg(lengthText(*shown, band)));
            connect(r, &QToolButton::clicked, this, [this, band] {
                setLength(m_length == band ? Coll::AnyLength : band);
            });
            m_sideLayout->addWidget(r);
        }
    }
    m_sideLayout->addStretch();

    // RETO: finished this year against the goal.
    if (shown->goal > 0) {
        const int year = QDate::currentDate().year();
        int done = 0;
        for (const CollectionItem *i : m_store->collectionItems())
            if (i->collection == shown->id && shown->isFinal(i->state) && i->finishedMs > 0 &&
                QDateTime::fromMSecsSinceEpoch(i->finishedMs).date().year() == year)
                ++done;
        auto *card = new QFrame;
        card->setObjectName("timerHero");
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(6);
        auto *k = new QLabel(L("RETO %1").arg(year));
        k->setObjectName("meta");
        v->addWidget(k);
        auto *nums = new QHBoxLayout;
        nums->setSpacing(4);
        auto *n = new QLabel(QString::number(done));
        n->setObjectName("trackTileValue");
        nums->addWidget(n, 0, Qt::AlignBottom);
        auto *of = new ElidedLabel(QString("/ %1").arg(shown->goal), QColor(Theme::muted()));
        of->setObjectName("timerSub");
        nums->addWidget(of, 1, Qt::AlignBottom);
        v->addLayout(nums);
        v->addWidget(new coll::Bar(qMin(100, done * 100 / shown->goal), m_theme.accent));
        m_sideLayout->addWidget(card);
    }
}

/// The width the grid has: the centre's, less its margins and a scroll bar's
/// room, always, so the bar appearing does not change the columns and make
/// it disappear again.
int CollectionsView::gridWidth() const {
    const int bar = m_scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_scroll);
    return qMax(110, m_scroll->width() - 24 - bar);
}

/// Tiles of 110 px or more, up to eight.
int CollectionsView::gridColumns(int width) {
    return qBound(1, (width + 8) / (110 + 8), 8);
}

/// "p. 212 / 496 · 43%", "ep. 3", or nothing: how far along something in
/// progress is, for the rows and tiles of the wide mode.
QString CollectionsView::progressText(const Collection &c, const CollectionItem *item) const {
    if (!c.isInProgress(item->state) || !c.has(Collection::kProgress)) return QString();
    const QString unit = L(c.unitShort());
    if (item->total > 0)
        return QString("%1 %2 / %3 · %4%")
            .arg(unit, Coll::number(item->progress), Coll::number(item->total))
            .arg(item->percent());
    if (item->progress > 0) return QString("%1 %2").arg(unit, Coll::number(item->progress));
    return QString();
}

/// The centre: tiles in as many columns as fit, or the rows.
void CollectionsView::fillGrid(QVBoxLayout *col, const Collection &c, const QList<CollectionItem *> &items) {
    if (items.isEmpty()) {
        auto *none = new QLabel(m_search.isEmpty() && !filtering() ? L("Nada por aquí todavía.")
                                                                       : L("Nada coincide con la búsqueda."));
        none->setObjectName("popupText");
        none->setContentsMargins(6, 4, 6, 4);
        col->addWidget(none);
        return;
    }
    if (m_selected.isEmpty() || !std::any_of(items.begin(), items.end(),
                                             [this](const CollectionItem *i) { return i->id == m_selected; }))
        m_selected = items.first()->id;

    if (!m_grid) {
        for (int i = 0; i < items.size(); ++i) {
            CollectionItem *item = items.at(i);
            Row *r = row(c, item, 0, metaFor(c, item), false, c.isFinal(item->state));
            r->setProperty("chosen", item->id == m_selected);
            col->addWidget(r);
        }
        return;
    }
    auto *host = new QWidget;
    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    // The columns that fit now, each tile as tall as its width asks: the grid
    // is rebuilt when the window changes how many fit (eventFilter), so a tile
    // is never stretched past the height it was made for.
    const int avail = gridWidth();
    const int columns = gridColumns(avail);
    m_gridColumns = columns;
    const int tileW = (avail - 8 * (columns - 1)) / columns;
    for (int i = 0; i < items.size(); ++i) {
        CollectionItem *item = items.at(i);
        auto *t = new coll::Tile(item, &m_theme);
        const CollectionState *s = c.state(item->state);
        t->state = s ? L(s->name).toLower() : QString();
        t->stateColor = c.isInProgress(item->state) ? m_theme.accent
                        : c.isFinal(item->state)     ? QColor(Theme::muted())
                                                     : kAmber;
        t->percent = c.isInProgress(item->state) && c.has(Collection::kProgress) ? item->percent() : -1;
        t->priority = c.has(Collection::kPriority);
        t->showYear = c.has(Collection::kYear);
        t->showAuthor = c.has(Collection::kAuthor);
        t->image = item->coverPath();
        t->selected = item->id == m_selected;
        t->setFixedHeight(t->heightForWidth(tileW));
        // Whole, whatever the tile had room for.
        QStringList tip{item->title.isEmpty() ? L("Sin título") : item->title};
        if (t->showAuthor && !item->author.isEmpty()) tip << item->author;
        const QString figures = progressText(c, item);
        if (!figures.isEmpty()) tip << figures;
        t->setToolTip(tip.join('\n'));
        t->select = [this, item] { select(item); };
        t->open = [this, item] { emit editRequested(item); };
        t->menu = [this, item](const QPoint &at) { openItemMenu(item, at); };
        grid->addWidget(t, i / columns, i % columns);
    }
    for (int i = 0; i < columns; ++i) grid->setColumnStretch(i, 1);
    col->addWidget(host);
}

/// The selected item: cover and kicker, its figures, priority and tags, the
/// links to the Planner and Seguimiento, Editar and the next step.
void CollectionsView::refreshDetail() {
    clearLayout(m_detail);
    Collection *c = current();
    CollectionItem *item = m_store ? m_store->collectionItem(m_selected) : nullptr;
    if (!c || !item || item->collection != c->id) {
        auto *none = new QLabel(L("Elige uno para ver su ficha."));
        none->setObjectName("popupText");
        none->setWordWrap(true);
        none->setMinimumWidth(24);
        m_detail->addWidget(none);
        m_detail->addStretch();
        return;
    }

    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    top->addWidget((new coll::Cover(CollectionItem::coverColor(item->color), QString(), QSize(70, 104)))
                       ->withImage(item->coverPath()),
                   0, Qt::AlignTop);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(4);
    const CollectionState *s = c->state(item->state);
    QString kicker = s ? L(s->name).toUpper() : QString();
    QString tone;
    if (c->isInProgress(item->state)) {
        if (item->startedMs > 0) kicker += " · " + L("DESDE %1").arg(Coll::shortDate(item->startedMs).toUpper());
    } else if (c->isFinal(item->state)) {
        tone = "muted";
        if (item->finishedMs > 0) kicker += " · " + Coll::shortDate(item->finishedMs).toUpper();
    } else {
        tone = "amber";
        if (item->state == c->firstState()) {
            const int at = int(Coll::queue(m_store->collectionItems(), *c).indexOf(item)) + 1;
            kicker += " · " + L("%1 EN LA COLA").arg(at, 2, 10, QChar('0'));
        }
    }
    auto *k = new QLabel(kicker);
    k->setObjectName("collKicker");
    k->setProperty("tone", tone);
    k->setWordWrap(true);
    k->setMinimumWidth(24);
    texts->addWidget(k);
    auto *title = new QLabel(item->title);
    title->setObjectName("heroName");
    title->setWordWrap(true);
    title->setMinimumWidth(24);
    texts->addWidget(title);
    if (c->has(Collection::kAuthor) && !item->author.isEmpty())
        texts->addWidget(elided(item->author, "collSub", QColor(Theme::muted())));
    texts->addStretch();
    top->addLayout(texts, 1);
    m_detail->addLayout(top);

    // AÑO · PÁGINAS · AÑADIDO
    auto *tiles = new QGridLayout;
    tiles->setSpacing(6);
    int column = 0;
    const auto tile = [&](const QString &label, const QString &value) {
        auto *f = new QFrame;
        f->setObjectName("timerHero");
        auto *v = new QVBoxLayout(f);
        v->setContentsMargins(8, 7, 8, 7);
        v->setSpacing(2);
        auto *l = new ElidedLabel(label, QColor(Theme::muted()));
        l->setObjectName("meta");
        v->addWidget(l);
        auto *n = new QLabel(value);
        n->setObjectName("timerSub");
        v->addWidget(n);
        tiles->addWidget(f, 0, column, 1, 1);
        tiles->setColumnStretch(column++, 1);
    };
    if (c->has(Collection::kYear) && item->year > 0) tile(L("AÑO"), QString::number(item->year));
    // The length; the page reached is in the progress block below.
    if (c->has(Collection::kProgress) && item->total > 0)
        tile(L(c->unitName()).toUpper(), Coll::number(item->total));
    tile(L("AÑADIDO"), Coll::shortDate(item->addedMs));
    m_detail->addLayout(tiles);

    // In progress: the page, moved on right here, as on the reading card.
    if (c->isInProgress(item->state) && c->has(Collection::kProgress))
        m_detail->addLayout(progressBlock(*c, item, nullptr));

    // Priority, changed right here.
    if (c->has(Collection::kPriority) && !c->isFinal(item->state)) {
        auto *pb = new QVBoxLayout;
        pb->setSpacing(6);
        auto *label = new QLabel(L("Prioridad"));
        label->setObjectName("setFieldLabel");
        pb->addWidget(label);
        auto *track = new QFrame;
        track->setObjectName("segTrack");
        auto *tl = new QHBoxLayout(track);
        tl->setContentsMargins(3, 3, 3, 3);
        tl->setSpacing(2);
        const QStringList names{L("Baja"), L("Media"), L("Alta")};
        for (int p = 1; p <= 3; ++p) {
            auto *b = textButton("segOption", names.at(p - 1));
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setProperty("chosen", item->priority == p);
            connect(b, &QToolButton::clicked, this, [this, item, p] {
                item->priority = p;
                edited();
            });
            tl->addWidget(b, 1);
        }
        pb->addWidget(track);
        m_detail->addLayout(pb);
    } else if (c->has(Collection::kRating) && c->isFinal(item->state)) {
        auto *s5 = new coll::Stones(5, item->rating, m_theme.accent);
        s5->setToolTip(L("Valoración %1 de 5").arg(item->rating));
        m_detail->addWidget(s5);
    }

    if (!item->tags.isEmpty()) {
        auto *tagBox = new QWidget;
        auto *tg = new QGridLayout(tagBox);
        tg->setContentsMargins(0, 0, 0, 0);
        tg->setSpacing(5);
        for (int i = 0; i < item->tags.size(); ++i) {
            const QString tag = item->tags.at(i);
            auto *chip = textButton("trackChip", tag);
            chip->setToolTip(L("Ver solo «%1»").arg(tag));
            connect(chip, &QToolButton::clicked, this, [this, tag] { setTag(tag); });
            tg->addWidget(chip, i / 2, i % 2, Qt::AlignLeft);
        }
        tg->setColumnStretch(2, 1);
        m_detail->addWidget(tagBox);
    }

    if (c->has(Collection::kSynopsis) && !item->synopsis.isEmpty()) {
        auto *label = new QLabel(L("Sinopsis"));
        label->setObjectName("setFieldLabel");
        m_detail->addWidget(label);
        auto *text = new QLabel(item->synopsis);
        text->setObjectName("popupText");
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        text->setMinimumWidth(24);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_detail->addWidget(text);
    }

    // Planificar: a reading block in the Planner, the clock in Seguimiento.
    if ((m_planner || m_tracking) && !c->isFinal(item->state)) {
        auto *pv = new QVBoxLayout;
        pv->setSpacing(6);
        auto *label = new QLabel(L("Planificar"));
        label->setObjectName("setFieldLabel");
        pv->addWidget(label);
        const auto link = [&](const QString &icon, const QString &text, std::function<void()> fn) {
            auto *b = textButton("trackRecent", text);
            b->setIcon(paintIcon(icon, QColor(Theme::muted()), 14));
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setMinimumWidth(24);
            connect(b, &QToolButton::clicked, this, std::move(fn));
            pv->addWidget(b);
        };
        if (m_planner)
            link("calendar", L("Bloque de lectura en el Planner"), [this, item] { emit planRequested(item); });
        if (m_tracking)
            link("bars", L("Medir tiempo con Seguimiento"), [this, item] { emit trackRequested(item); });
        m_detail->addLayout(pv);
    }
    m_detail->addStretch();

    // Editar, and the next step for its state.
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    auto *editBtn = textButton("pickCancel", L("Editar"));
    connect(editBtn, &QToolButton::clicked, this, [this, item] { emit editRequested(item); });
    actions->addWidget(editBtn);
    QString nextState, nextText;
    if (c->isInProgress(item->state)) {
        nextState = c->finalState();
        nextText = L("Terminado");
    } else if (!c->isFinal(item->state)) {
        const QString inProgress = c->inProgressState();
        nextState = inProgress.isEmpty() ? c->finalState() : inProgress;
        nextText = inProgress.isEmpty() ? L("Terminado") : L("Empezar");
    }
    if (!nextState.isEmpty()) {
        auto *next = textButton("pickSave", nextText);
        next->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(next, &QToolButton::clicked, this, [this, item, nextState] { moveTo(item, nextState); });
        actions->addWidget(next, 1);
    } else {
        actions->addStretch();
    }
    m_detail->addLayout(actions);
}
