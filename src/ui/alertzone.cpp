#include "ui/alertzone.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"

namespace {

const QColor kRed("#ff7a6b");
const QColor kWarn("#f2b757");

QColor colorOf(const AlertZone::Notice &n, const QColor &accent) {
    switch (n.kind) {
        case AlertZone::Notice::Alarm: return kRed;
        case AlertZone::Notice::Warn: return kWarn;
        default: return accent;
    }
}

/// The ringing bell in its red disc, with a halo that beats: the one moving
/// thing in the zone, so an alarm is told apart from a notice at a glance.
class Pulse : public QWidget {
public:
    explicit Pulse(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(38, 38);
        m_anim = new QVariantAnimation(this);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setDuration(1400);
        m_anim->setLoopCount(-1);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this] { update(); });
        m_anim->start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal t = m_anim->currentValue().toReal();
        // Grows and fades out, then starts again: opacity and scale only.
        QColor halo = kRed;
        halo.setAlphaF(0.55 * (1.0 - t));
        p.setPen(QPen(halo, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 15 + 3.5 * t, 15 + 3.5 * t);
        QColor disc = kRed;
        disc.setAlphaF(0.16);
        p.setPen(Qt::NoPen);
        p.setBrush(disc);
        p.drawEllipse(c, 15, 15);
        paintIcon("bell", kRed, 16).paint(&p, QRect(int(c.x()) - 8, int(c.y()) - 8, 16, 16));
    }

private:
    QVariantAnimation *m_anim;
};

QToolButton *button(const QString &object, const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName(object);
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

/// Title over a mono subtitle, both elided: notice text is user text (a note's
/// title) and must not widen the panel.
QWidget *texts(const AlertZone::Notice &n, const QColor &subColor) {
    auto *box = new QWidget;
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(1);
    auto *title = new ElidedLabel(n.title, QColor(Theme::fg()));
    title->setObjectName("noticeTitle");
    title->setToolTip(n.title);
    col->addWidget(title);
    if (!n.sub.isEmpty()) {
        auto *sub = new ElidedLabel(n.sub, subColor);
        sub->setObjectName("noticeSub");
        sub->setToolTip(n.sub);
        col->addWidget(sub);
    }
    return box;
}

/// Runs after the click has been delivered: the action usually changes the
/// notices, and the rebuild destroys the button that was clicked.
void later(QObject *context, const std::function<void()> &fn) {
    if (fn) QTimer::singleShot(0, context, fn);
}

}  // namespace

AlertZone::AlertZone(const Theme *theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setObjectName("alertZone");
    m_col = new QVBoxLayout(this);
    m_col->setContentsMargins(0, 0, 0, 0);
    m_col->setSpacing(6);
    hide();
}

void AlertZone::setNotices(const QList<Notice> &notices) {
    QString sig;
    for (const Notice &n : notices)
        sig += QString::number(n.kind) + n.icon + n.title + '\x1f' + n.sub + n.action + n.second + '\x1e';
    m_notices = notices;
    if (sig == m_signature) return;
    m_signature = sig;
    if (m_notices.size() < 2) m_expanded = false;
    rebuild();
}

int AlertZone::heightFor(int width) const {
    if (m_notices.isEmpty()) return 0;
    // Just rebuilt, the layout still answers with the empty one it cached.
    m_col->invalidate();
    return m_col->hasHeightForWidth() ? m_col->totalHeightForWidth(width)
                                      : m_col->totalSizeHint().height();
}

void AlertZone::rebuild() {
    while (QLayoutItem *it = m_col->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    if (m_notices.isEmpty()) {
        hide();
        if (changed) changed();
        return;
    }
    if (m_expanded) m_col->addWidget(buildList());
    m_col->addWidget(buildTop(m_notices.first()));
    show();
    raise();
    if (changed) changed();
    // Once more after the new rows are polished: their stylesheet padding is not
    // in the first measurement.
    QTimer::singleShot(0, this, [this] { if (changed) changed(); });
}

QWidget *AlertZone::buildTop(const Notice &n) {
    const bool alarm = n.kind == Notice::Alarm;
    auto *card = new QFrame;
    card->setObjectName(alarm ? "noticeAlarm" : "notice");
    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(alarm ? 9 : 11, alarm ? 8 : 7, alarm ? 10 : 6, alarm ? 10 : 7);
    col->setSpacing(9);

    auto *row = new QHBoxLayout;
    row->setSpacing(alarm ? 8 : 10);
    if (alarm) {
        row->addWidget(new Pulse);
    } else {
        auto *icon = new QLabel;
        icon->setPixmap(paintIcon(n.icon, colorOf(n, m_theme->accent), 16).pixmap(16, 16));
        row->addWidget(icon);
    }
    row->addWidget(texts(n, alarm ? QColor("#f0a99f") : QColor(Theme::muted())), 1);

    if (m_notices.size() > 1) {
        auto *count = button("noticeCount", L("1 de %1").arg(m_notices.size()));
        count->setToolTip(m_expanded ? L("Ocultar los demás avisos") : L("Ver todos los avisos"));
        connect(count, &QToolButton::clicked, this, [this] {
            m_expanded = !m_expanded;
            QTimer::singleShot(0, this, [this] { rebuild(); });
        });
        row->addWidget(count, 0, Qt::AlignVCenter);
    }
    if (!alarm && !n.action.isEmpty()) {
        auto *act = button("noticeAction", n.action);
        connect(act, &QToolButton::clicked, this, [this, fn = n.act] { later(this, fn); });
        row->addWidget(act, 0, Qt::AlignVCenter);
    }
    col->addLayout(row);

    if (alarm) {
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(6);
        if (!n.second.isEmpty()) {
            auto *snooze = button("noticeGhost", n.second);
            snooze->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            snooze->setMinimumWidth(40);
            connect(snooze, &QToolButton::clicked, this, [this, fn = n.secondAct] { later(this, fn); });
            buttons->addWidget(snooze, 1);
        }
        auto *stop = button("noticeStop", n.action);
        stop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        stop->setMinimumWidth(40);
        connect(stop, &QToolButton::clicked, this, [this, fn = n.act] { later(this, fn); });
        buttons->addWidget(stop, 1);
        col->addLayout(buttons);
    }
    return card;
}

/// The rest, most urgent first, each with its own action.
QWidget *AlertZone::buildList() {
    auto *list = new QFrame;
    list->setObjectName("noticeList");
    auto *col = new QVBoxLayout(list);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(0);
    for (int i = 1; i < m_notices.size(); ++i) {
        const Notice &n = m_notices.at(i);
        auto *row = new QWidget;
        row->setObjectName("noticeRow");
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 4, 4, 4);
        rl->setSpacing(9);
        auto *icon = new QLabel;
        icon->setPixmap(paintIcon(n.icon, colorOf(n, m_theme->accent), 15).pixmap(15, 15));
        rl->addWidget(icon);
        rl->addWidget(texts(n, QColor(Theme::muted())), 1);
        if (!n.action.isEmpty()) {
            auto *act = button("noticeAction", n.action);
            connect(act, &QToolButton::clicked, this, [this, fn = n.act] { later(this, fn); });
            rl->addWidget(act, 0, Qt::AlignVCenter);
        }
        col->addWidget(row);
    }
    return list;
}
