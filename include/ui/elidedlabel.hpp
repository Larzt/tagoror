#pragma once

#include <QColor>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>

/// A label that elides its text instead of demanding its full width. The note
/// list has no horizontal scrollbar, so one child's minimum becomes the whole
/// list's (see *Card widths* in CLAUDE.md).
class ElidedLabel : public QLabel {
public:
    ElidedLabel(const QString &text, const QColor &color, QWidget *parent = nullptr)
        : QLabel(text, parent), m_color(color) {
        setAttribute(Qt::WA_Hover);
    }

    void setColor(const QColor &color) {
        m_color = color;
        update();
    }

    /// Underlines on hover, for clickable labels.
    void setUnderlineOnHover(bool on) { m_underline = on; }

    QSize minimumSizeHint() const override { return QSize(24, sizeHint().height()); }

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true;  update(); }
    void leaveEvent(QEvent *) override      { m_hover = false; update(); }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QFont f = font();
        f.setUnderline(m_underline && m_hover);
        p.setFont(f);
        p.setPen(m_color);
        p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(text(), Qt::ElideRight, width()));
    }

private:
    QColor m_color;
    bool m_hover = false;
    bool m_underline = false;
};
