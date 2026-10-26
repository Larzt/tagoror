#include "ui/overlay.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kMargin = 8;     ///< Around the dialog, inside the panel.
constexpr int kTopGap = 40;    ///< Below the panel's top: the header still shows.
}  // namespace

Overlay::Overlay(const Theme *, QWidget *host, const QString &title) : QWidget(host) {
    setObjectName("overlay");
    hide();

    m_dialog = new QWidget(this);
    m_dialog->setObjectName("paletteDialog");
    m_dialog->setAttribute(Qt::WA_StyledBackground, true);
    m_col = new QVBoxLayout(m_dialog);
    m_col->setContentsMargins(14, 12, 14, 14);
    m_col->setSpacing(10);

    auto *head = new QLabel(title);
    head->setObjectName("overlayTitle");
    m_col->addWidget(head);

    host->installEventFilter(this);
}

void Overlay::setContent(QWidget *content) {
    m_content = content;
    m_col->addWidget(content);
}

void Overlay::open(QWidget *focus) {
    m_back = QApplication::focusWidget();
    place();
    show();
    raise();
    if (focus) focus->setFocus(Qt::ShortcutFocusReason);
    else m_dialog->setFocus(Qt::OtherFocusReason);
}

void Overlay::close() {
    if (!isVisible()) return;
    hide();
    if (m_back) m_back->setFocus(Qt::OtherFocusReason);
    if (closed) closed();
    deleteLater();
}

void Overlay::place() {
    QWidget *host = parentWidget();
    setGeometry(host->rect());
    const int w = qMin(width() - 2 * kMargin, m_maxWidth);
    m_dialog->setFixedWidth(w);
    // Wrapped warnings make the height depend on the width.
    const int want = m_dialog->hasHeightForWidth() ? m_dialog->heightForWidth(w)
                                                   : m_dialog->sizeHint().height();
    const int maxH = height() - kTopGap - kMargin;
    const int h = qMin(want, maxH);
    // Centred in what is left under the header, or right under it when tall.
    const int y = qMax(kTopGap, (height() - h) / 2);
    m_dialog->setGeometry((width() - w) / 2, y, w, h);
}

void Overlay::paintEvent(QPaintEvent *) {
    // The panel shows through, dimmed: where you were is still where you are.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(9, 11, 14, 158));
    p.drawRoundedRect(QRectF(rect()), 14, 14);
}

void Overlay::mousePressEvent(QMouseEvent *e) {
    if (!m_dialog->geometry().contains(e->position().toPoint())) close();
}

void Overlay::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool Overlay::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        QTimer::singleShot(0, this, [this] { place(); });
    return QWidget::eventFilter(watched, event);
}
