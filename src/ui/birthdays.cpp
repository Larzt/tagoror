#include "ui/birthdays.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

#include "ui/keynav.hpp"
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

constexpr int kAvatarRow = 28;     // avatar of a card in the grid
constexpr int kCardMin = 150;      // a grid card's narrowest; two need 2 × this + gap
constexpr int kSoonDays = 7;       // up to here the countdown is in the accent

/// A stable tint per person, derived from the name (not the position) so it
/// does not change when others are added. Hand-rolled rather than qHash,
/// which Qt 6 seeds randomly per process.
QColor nameTint(const QString &name) {
    unsigned h = 2166136261u;
    for (const QChar &c : name) {
        h ^= unsigned(c.unicode());
        h *= 16777619u;
    }
    // Fixed saturation and value: only the hue varies, so nobody's colour reads
    // worse than the others' on the dark background.
    return QColor::fromHsv(int(h % 360u), 95, 205);
}

/// Month name in the interface language, upper case. Never
/// QLocale::system(): the language is the setting.
QString monthName(int month) {
    return Lang::locale().toString(QDate(2000, month, 1), "MMMM").toUpper();
}

/// Circle with the initials. Painted rather than a QLabel, which would demand
/// the width of its text (see *Card widths* in CLAUDE.md).
class Avatar : public QWidget {
public:
    Avatar(const QString &initials, const QColor &tint, int px, QWidget *parent = nullptr)
        : QWidget(parent), m_text(initials), m_tint(tint), m_px(px) {
        setFixedSize(px, px);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor fill = m_tint;
        fill.setAlpha(46);
        p.setPen(QPen(QColor(m_tint.red(), m_tint.green(), m_tint.blue(), 110), 1.0));
        p.setBrush(fill);
        p.drawEllipse(QRectF(0.5, 0.5, m_px - 1.0, m_px - 1.0));

        QFont f = font();
        f.setPixelSize(m_px <= 30 ? 11 : 14);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(m_tint);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    QColor m_tint;
    int m_px;
};

/// A list row: a plain QWidget with WA_StyledBackground so the stylesheet
/// paints its highlight.
class BirthdayRow : public QWidget {
public:
    explicit BirthdayRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("bdayRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { if (click) click(); });
    }

    /// Assigned after construction: the popup anchors to the row itself.
    std::function<void()> click;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && click)
            click();
    }
};

/// The highlighted card's backdrop: two soft blobs of colour (the accent and
/// a warm amber) under a dark veil. Painted once into a pixmap per size and
/// accent — radial gradients, which read as blur at a fraction of the cost of
/// blurring anything — and only drawn afterwards.
class HeroCard : public BirthdayRow {
public:
    explicit HeroCard(const QColor &accent) : m_accent(accent) {
        setObjectName("bdayHero");
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const qreal dpr = devicePixelRatioF();
        if (m_cache.size() != size() * dpr || m_cacheAccent != m_accent) {
            m_cache = QPixmap(size() * dpr);
            m_cache.setDevicePixelRatio(dpr);
            m_cache.fill(Qt::transparent);
            QPainter c(&m_cache);
            c.setRenderHint(QPainter::Antialiasing);
            QPainterPath clip;
            clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
            c.setClipPath(clip);
            c.fillRect(rect(), QColor(28, 31, 38));
            const auto blob = [&c](QPointF centre, qreal rx, qreal ry, QColor color, qreal alpha) {
                QRadialGradient g(centre, qMax(rx, ry));
                color.setAlphaF(alpha);
                g.setColorAt(0, color);
                color.setAlphaF(0);
                g.setColorAt(1, color);
                c.save();
                c.translate(centre);
                c.scale(1.0, ry / rx);
                c.translate(-centre);
                c.fillRect(QRectF(centre.x() - rx, centre.y() - rx, rx * 2, rx * 2), g);
                c.restore();
            };
            blob({width() * 0.18, -10}, width() * 0.62, 150, m_accent, 0.55);
            blob({width() * 0.95, height() + 10.0}, width() * 0.6, 140, QColor("#f2b757"), 0.32);
            c.fillRect(rect(), QColor(20, 23, 29, 97));
            c.setClipping(false);
            c.setPen(QPen(QColor(255, 255, 255, 36), 1));
            c.setBrush(Qt::NoBrush);
            c.drawPath(clip);
            m_cacheAccent = m_accent;
        }
        QPainter p(this);
        p.drawPixmap(0, 0, m_cache);
        if (keynav::showsFocus(this)) {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(m_accent, 1.5));
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
        }
    }

private:
    QColor m_accent;
    QPixmap m_cache;
    QColor m_cacheAccent;
};

}  // namespace

BirthdayView::BirthdayView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("birthdays");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_host = host;
    host->installEventFilter(this);
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(6);
    m_layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(150);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    col->addWidget(scroll, 1);

    refresh();
}

void BirthdayView::setSource(const QList<Birthday *> *list) {
    m_list = list;
    refresh();
}

void BirthdayView::setTheme(const Theme &theme) {
    m_theme = theme;
    refresh();
}

void BirthdayView::setByMonth(bool on) {
    if (on == m_byMonth) return;
    m_byMonth = on;
    refresh();
}

/// Sorted by time left: in December the next one is in January. This is also
/// what decides who is highlighted. Ties are broken by name so two people on
/// the same day do not swap places between repaints.
QList<Birthday *> BirthdayView::sorted() const {
    QList<Birthday *> found;
    if (!m_list) return found;

    for (Birthday *b : *m_list)
        if (b->isValid()) found.append(b);

    std::sort(found.begin(), found.end(), [](Birthday *a, Birthday *b) {
        const int da = a->daysUntil(), db = b->daysUntil();
        return da != db ? da < db : a->name.localeAwareCompare(b->name) < 0;
    });
    return found;
}

/// The other order: January to December, the whole year's agenda.
QList<Birthday *> BirthdayView::byCalendar() const {
    QList<Birthday *> found;
    if (!m_list) return found;

    for (Birthday *b : *m_list)
        if (b->isValid()) found.append(b);

    std::sort(found.begin(), found.end(), [](Birthday *a, Birthday *b) {
        if (a->month != b->month) return a->month < b->month;
        if (a->day != b->day) return a->day < b->day;
        return a->name.localeAwareCompare(b->name) < 0;
    });
    return found;
}

int BirthdayView::columnsFor(int width) const { return width >= 2 * kCardMin + 6 ? 2 : 1; }

bool BirthdayView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_host && event->type() == QEvent::Resize &&
        columnsFor(m_host->width()) != m_columns)
        refresh();
    return QWidget::eventFilter(watched, event);
}

void BirthdayView::refresh() {
    // Clear everything but the trailing stretch. Hidden as well as deleted: out
    // of the layout they stay painted and event-handling until deleteLater() runs.
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    m_columns = columnsFor(m_host ? m_host->width() : 0);

    // The highlighted one always comes from the closeness order: "who is next"
    // does not depend on how the list is viewed.
    const QList<Birthday *> all = sorted();
    int at = 0;

    if (all.isEmpty()) {
        auto *empty = new QLabel(L("Todavía no hay cumpleaños"));
        empty->setObjectName("meta");
        empty->setWordWrap(true);   // see *Card widths*: it must not demand its width
        empty->setContentsMargins(7, 8, 7, 4);
        m_layout->insertWidget(at++, empty);
        m_layout->insertWidget(at++, buildAddRow());
        return;
    }

    // Highlighted: those tied for soonest. Usually one; several when people share
    // the day, and picking one would be choosing for the user.
    const int soonest = all.first()->daysUntil();
    int highlighted = 0;
    while (highlighted < all.size() && all.at(highlighted)->daysUntil() == soonest) {
        m_layout->insertWidget(at++, buildHero(all.at(highlighted)));
        ++highlighted;
    }

    // By month the list is the whole year, highlighted ones included: someone
    // missing from their month reads as a bug. By closeness it continues the card
    // above, so repeating them is redundant.
    const QList<Birthday *> list = m_byMonth ? byCalendar() : all.mid(highlighted);
    m_layout->insertWidget(at++, buildHeader(int(list.size())));

    int lastMonth = 0;
    QGridLayout *grid = nullptr;
    int cell = 0;
    for (Birthday *b : list) {
        // By closeness the month is that of the next turn (so next January follows
        // December); by calendar it is simply theirs.
        const int month = m_byMonth ? b->month : b->nextDate().month();
        if (month != lastMonth) {
            lastMonth = month;
            auto *head = new QLabel(monthName(month));
            head->setObjectName("pageSection");
            m_layout->insertWidget(at++, head);
            auto *box = new QWidget;
            grid = new QGridLayout(box);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setSpacing(6);
            for (int c = 0; c < m_columns; ++c) grid->setColumnStretch(c, 1);
            m_layout->insertWidget(at++, box);
            cell = 0;
        }
        grid->addWidget(buildCard(b), cell / m_columns, cell % m_columns);
        ++cell;
    }

    if (list.isEmpty()) {
        auto *empty = new QLabel(L("Ninguno más por ahora"));
        empty->setObjectName("meta");
        empty->setWordWrap(true);
        empty->setContentsMargins(7, 8, 7, 4);
        m_layout->insertWidget(at++, empty);
    }
}

QWidget *BirthdayView::buildHeader(int listed) {
    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(2, 6, 2, 0);
    l->setSpacing(4);

    // The two orders as two small tabs; the lit one doubles as the section title.
    auto tab = [this, l](const QString &text, bool chosen, bool value) {
        auto *b = new QToolButton;
        b->setObjectName("bdayTab");
        b->setText(text);
        b->setProperty("chosen", chosen);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, value] {
            if (value == m_byMonth) return;
            m_byMonth = value;
            refresh();
            emit orderChanged(m_byMonth);
        });
        l->addWidget(b);
    };
    tab(L("PRÓXIMOS"), !m_byMonth, false);
    tab(L("MESES"), m_byMonth, true);
    l->addStretch();

    // The count differs between orders: by closeness, those after the highlighted
    // card; by month, all of them. Two different labels make that visible.
    auto *count = new QLabel(listed == 0 ? QString()
                                         : (m_byMonth ? L("%1 EN TOTAL").arg(listed)
                                                      : L("%1 MÁS").arg(listed)));
    count->setObjectName("meta");
    count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->addWidget(count);
    return row;
}

/// The next one: date, who and how many days, then what to do about it.
QWidget *BirthdayView::buildHero(Birthday *b) {
    const bool isToday = b->isToday();
    auto *card = new HeroCard(m_theme.accent);
    // Clicking the card opens the same editor as a grid card; the buttons inside
    // keep their own clicks.
    card->click = [this, b, card] { emit editRequested(b, card); };

    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(14, 14, 14, 12);
    col->setSpacing(14);

    auto *top = new QHBoxLayout;
    auto *kicker = new QLabel(isToday ? L("HOY") : L("PRÓXIMO"));
    kicker->setObjectName("heroKicker");
    top->addWidget(kicker);
    top->addStretch();
    QString date = Lang::locale().toString(b->nextDate(), "ddd d MMM");
    if (!date.isEmpty()) date[0] = date[0].toUpper();
    auto *dateLabel = new QLabel(date);
    dateLabel->setObjectName("heroDate");
    top->addWidget(dateLabel);
    col->addLayout(top);

    auto *mid = new QHBoxLayout;
    mid->setSpacing(12);
    mid->addWidget(new Avatar(b->initials(), QColor(Theme::fg()), 46), 0, Qt::AlignVCenter);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *name = new ElidedLabel(b->name.isEmpty() ? L("Sin nombre") : b->name, QColor(Theme::fg()));
    name->setObjectName("heroName");
    name->setToolTip(b->name);
    texts->addWidget(name);
    QString sub = b->turns() > 0 ? L("Cumple %1 años").arg(b->turns()) : b->relation.trimmed();
    if (b->greeted()) sub = sub.isEmpty() ? L("Felicitado") : sub + " · " + L("felicitado");
    auto *subLabel = new ElidedLabel(sub, QColor("#e3e6ec"));
    subLabel->setObjectName("heroSub");
    texts->addWidget(subLabel);
    mid->addLayout(texts, 1);

    auto *count = new QVBoxLayout;
    count->setSpacing(0);
    const int days = b->daysUntil();
    // Today there are no days to count: the cake instead, painted (an emoji would
    // depend on the system having a colour font).
    auto *big = new QLabel(isToday ? QString() : QString::number(days));
    big->setObjectName("heroDays");
    if (isToday) big->setPixmap(paintIcon("cake", QColor(Theme::fg()), 28).pixmap(28, 28));
    big->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    count->addWidget(big);
    auto *unit = new QLabel(isToday ? L("hoy") : days == 1 ? L("día") : L("días"));
    unit->setObjectName("heroUnit");
    unit->setAlignment(Qt::AlignRight | Qt::AlignTop);
    count->addWidget(unit);
    mid->addLayout(count);
    col->addLayout(mid);

    // The buttons get a row of their own: they carry text, and a row of fixed
    // widths is the *Card widths* trap.
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    const auto chip = [](const QString &icon, const QString &text) {
        auto *c = new QToolButton;
        c->setObjectName("heroChip");
        c->setText(text);
        if (!icon.isEmpty()) {
            c->setIcon(paintIcon(icon, QColor(Theme::fg()), 13));
            c->setIconSize(QSize(13, 13));
            c->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        c->setCursor(Qt::PointingHandCursor);
        c->setFocusPolicy(Qt::TabFocus);
        return c;
    };
    // While ringing, stopping it comes first.
    if (b->ringing) {
        auto *stop = chip("stop", L("Detener aviso"));
        stop->setObjectName("heroStop");
        connect(stop, &QToolButton::clicked, this, [this, b] { emit dismissRequested(b); });
        actions->addWidget(stop);
    } else if (isToday) {
        // Only on the day itself: greeting months early means nothing, and the mark
        // is stored per year.
        auto *greet = chip(b->greeted() ? "check" : QString(),
                           b->greeted() ? L("Felicitado") : L("Felicitar"));
        greet->setToolTip(L("Marca que ya le has felicitado este año"));
        connect(greet, &QToolButton::clicked, this, [this, b] { emit greetToggled(b); });
        actions->addWidget(greet);
    }
    auto *remind = chip("bell", b->remindAt.isValid()
                                    ? L("Aviso %1").arg(b->remindAt.toString("HH:mm"))
                                    : L("Avisarme…"));
    connect(remind, &QToolButton::clicked, this,
            [this, b, remind] { emit remindRequested(b, remind); });
    actions->addWidget(remind);
    actions->addStretch();
    col->addLayout(actions);
    return card;
}

/// One person in the grid: avatar and name, then the date with the age they
/// turn, and how long is left.
QWidget *BirthdayView::buildCard(Birthday *b) {
    auto *card = new BirthdayRow;
    card->setObjectName("bdayCard");
    card->click = [this, b, card] { emit editRequested(b, card); };
    card->setMinimumWidth(60);   // see *Card widths*: the grid shares out the width

    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(10, 10, 10, 10);
    col->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(new Avatar(b->initials(), nameTint(b->name), kAvatarRow), 0, Qt::AlignVCenter);
    auto *name = new ElidedLabel(b->name.isEmpty() ? L("Sin nombre") : b->name, QColor(Theme::fg()));
    name->setObjectName("bdayRowName");
    name->setToolTip(b->relation.isEmpty() ? b->name : b->name + " · " + b->relation);
    top->addWidget(name, 1);
    if (b->remindAt.isValid()) {
        auto *bell = new QLabel;
        bell->setFixedSize(11, 11);
        bell->setPixmap(paintIcon("bell", QColor(Theme::muted()), 11).pixmap(11, 11));
        bell->setToolTip(L("Aviso a las %1").arg(b->remindAt.toString("HH:mm")));
        top->addWidget(bell, 0, Qt::AlignVCenter);
    }
    col->addLayout(top);

    auto *bottom = new QHBoxLayout;
    QString left = b->dateLabel();
    if (b->turns() > 0) left += QStringLiteral(" · ") + QString::number(b->turns());
    auto *date = new ElidedLabel(left, QColor(Theme::muted()));
    date->setObjectName("bdayDate");
    bottom->addWidget(date, 1);
    const int days = b->daysUntil();
    auto *when = new QLabel(days == 0 ? L("hoy") : L("%1 d").arg(days));
    when->setObjectName("bdayWhen");
    // This week's in the accent, the rest dimmed: a page full of accent colour
    // highlights nothing.
    when->setProperty("soon", days <= kSoonDays);
    bottom->addWidget(when);
    col->addLayout(bottom);
    return card;
}

/// The add row, styled like a card so it reads as "the next one". Only on an
/// empty page now: the header's "+" adds otherwise.
QWidget *BirthdayView::buildAddRow() {
    auto *add = new BirthdayRow;
    add->setObjectName("bdayCard");
    auto *l = new QHBoxLayout(add);
    l->setContentsMargins(10, 10, 10, 10);
    l->setSpacing(9);

    auto *slot = new QLabel;
    slot->setFixedSize(kAvatarRow, kAvatarRow);
    slot->setPixmap(paintIcon("checkdots", QColor(Theme::muted()), kAvatarRow)
                        .pixmap(kAvatarRow, kAvatarRow));
    l->addWidget(slot);

    auto *text = new QLabel(L("Añadir un cumpleaños"));
    text->setObjectName("bdayRowName");
    l->addWidget(text, 1);

    add->click = [this, add] { emit addRequested(add); };
    return add;
}
