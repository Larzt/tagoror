#include "ui/navbar.hpp"

#include <QBoxLayout>
#include <QEnterEvent>
#include <QPainter>

#include "core/lang.hpp"
#include "ui/keynav.hpp"

namespace {

const QColor kRed("#ff7a6b");

QColor withAlpha(QColor c, qreal alpha) {
    c.setAlphaF(alpha);
    return c;
}

}  // namespace

/// One destination: an icon in a pill (accent-tinted when open) over a label.
/// A QAbstractButton so Tab, Space and Enter (keynav) work as on any button.
class NavBar::NavItem : public QAbstractButton {
public:
    NavItem(const Theme *theme, const Item &item, QWidget *parent)
        : QAbstractButton(parent), m_theme(theme), m_item(item) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
        retranslate();
    }

    const Item &item() const { return m_item; }
    bool rail = false;
    bool active = false;
    bool dot = false;
    QString countdown;

    void retranslate() {
        const QString name = L(m_item.name);
        setToolTip(m_item.shortcut.isEmpty() ? name : name + " · " + m_item.shortcut);
        setAccessibleName(name);
        update();
    }

    QSize sizeHint() const override {
        return rail ? QSize(60, 62) : QSize(70, NavBar::kBarHeight - 10);
    }
    QSize minimumSizeHint() const override {
        // Small on purpose: four labels must not widen the panel (see *Card widths*).
        return rail ? QSize(60, 62) : QSize(40, NavBar::kBarHeight - 10);
    }

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        if (m_hover && !active) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 15));
            p.drawRoundedRect(r, 8, 8);
        }
        if (keynav::showsFocus(this)) {
            p.setPen(QPen(m_theme->accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 8, 8);
        }

        // Icon pill and label, centred as a block.
        const int pillW = rail ? 40 : 44, pillH = 24, gap = 3;
        QFont f = font();
        f.setPixelSize(rail ? 10 : 11);
        const bool timer = !countdown.isEmpty();
        if (timer) {
            f.setFamily("IBM Plex Mono");
            f.setStyleHint(QFont::Monospace);
            f.setWeight(QFont::Medium);
        } else {
            f.setWeight(active ? QFont::DemiBold : QFont::Medium);
        }
        const QFontMetrics fm(f);
        const bool twoLines = rail && !timer &&
                              fm.horizontalAdvance(L(m_item.name)) > width() - 4;
        const int blockH = pillH + gap + fm.height() * (twoLines ? 2 : 1);
        const int top = (height() - blockH) / 2;
        const QRectF pill((width() - pillW) / 2.0, top, pillW, pillH);

        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(m_theme->accent, 0.20));
            p.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
        }
        const QColor ink = active ? m_theme->accent : QColor(Theme::muted());
        paintIcon(m_item.icon, ink, 16).paint(&p, QRect(int(pill.center().x()) - 8,
                                                        int(pill.center().y()) - 8, 16, 16));

        if (dot && !active) {
            // A ring in the surface colour around it, so it reads over the icon.
            p.setPen(QPen(QColor(19, 22, 27), 2));
            p.setBrush(kRed);
            p.drawEllipse(QPointF(pill.center().x() + (rail ? 10 : 12) + 3.5, pill.top() + 4.5), 3.5, 3.5);
        }

        p.setFont(f);
        p.setPen(timer ? m_theme->accent : active ? QColor(Theme::fg()) : QColor(Theme::muted()));
        const QString label = timer ? countdown : L(m_item.name);
        // The rail is 60px wide: a long name breaks onto a second line there, as in
        // the design, instead of losing its end. The bar has room to elide.
        if (rail && fm.horizontalAdvance(label) > width() - 4) {
            const QRect textRect(2, int(pill.bottom()) + gap, width() - 4, height() - int(pill.bottom()) - gap);
            p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWrapAnywhere, label);
            return;
        }
        const QRect textRect(2, int(pill.bottom()) + gap, width() - 4, fm.height());
        p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
                   fm.elidedText(label, Qt::ElideRight, textRect.width()));
    }

private:
    const Theme *m_theme;
    Item m_item;
    bool m_hover = false;
};

NavBar::NavBar(const Theme *theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setObjectName("navBar");
    setAttribute(Qt::WA_StyledBackground, true);
    setLayoutMode(Bar);
}

void NavBar::setItems(const QList<Item> &items) {
    for (NavItem *i : m_items) {
        i->hide();
        i->deleteLater();
    }
    m_items.clear();
    for (const Item &it : items) {
        auto *item = new NavItem(m_theme, it, this);
        item->rail = m_layout == Rail;
        QWidget *page = it.page;
        connect(item, &QAbstractButton::clicked, this, [this, page] {
            if (activated) activated(page);
        });
        m_items.append(item);
    }
    setLayoutMode(m_layout);
}

void NavBar::setLayoutMode(Layout layout) {
    m_layout = layout;
    delete m_box;
    if (layout == Bar) {
        m_box = new QHBoxLayout(this);
        m_box->setContentsMargins(6, 4, 6, 6);
        m_box->setSpacing(2);
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setFixedHeight(kBarHeight);
    } else {
        m_box = new QVBoxLayout(this);
        m_box->setContentsMargins(4, 8, 4, 8);
        m_box->setSpacing(4);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setFixedWidth(kRailWidth);
    }
    for (NavItem *i : m_items) {
        i->rail = layout == Rail;
        m_box->addWidget(i, layout == Bar ? 1 : 0, layout == Bar ? Qt::Alignment() : Qt::AlignHCenter);
        i->updateGeometry();
    }
    if (layout == Rail) m_box->addStretch();
    setProperty("rail", layout == Rail);
    style()->unpolish(this);
    style()->polish(this);
}

void NavBar::setActive(QWidget *page) {
    for (NavItem *i : m_items) {
        const bool on = i->item().page == page;
        if (i->active != on) {
            i->active = on;
            i->update();
        }
    }
}

void NavBar::setCountdown(QWidget *page, const QString &text) {
    for (NavItem *i : m_items)
        if (i->item().page == page && i->countdown != text) {
            i->countdown = text;
            i->update();
        }
}

void NavBar::setDot(QWidget *page, bool on) {
    for (NavItem *i : m_items)
        if (i->item().page == page && i->dot != on) {
            i->dot = on;
            i->update();
        }
}

void NavBar::retranslate() {
    for (NavItem *i : m_items) i->retranslate();
}

QWidget *NavBar::buttonFor(QWidget *page) const {
    for (NavItem *i : m_items)
        if (i->item().page == page) return i;
    return nullptr;
}
