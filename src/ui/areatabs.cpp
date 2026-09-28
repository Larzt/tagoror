#include "ui/areatabs.hpp"

#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include "core/lang.hpp"
#include "ui/keynav.hpp"

namespace {

constexpr int kHeight = 34;
constexpr int kLeft = 8;           // margen izquierdo de la tira
constexpr int kRight = 6;
constexpr int kPad = 7;            // a cada lado del texto de una pestaña
constexpr int kGap = 2;            // entre pestañas
constexpr int kGlyph = 12;         // lo que ocupa el glifo, con su aire
constexpr int kMaxName = 116;      // un nombre largo se recorta, no empuja a las demás
constexpr int kAddSize = 26;
const QColor kRed("#ff7a6b");

QFont tabFont(const QFont &base, bool active) {
    QFont f = base;
    f.setPixelSize(12);
    f.setWeight(active ? QFont::DemiBold : QFont::Medium);
    return f;
}

}  // namespace

AreaTabs::AreaTabs(const Theme *theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setObjectName("areaTabs");
    setMouseTracking(true);
    // Una sola parada de Tab para toda la tira; dentro se va con las flechas.
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kHeight);
}

QSize AreaTabs::sizeHint() const { return {240, kHeight}; }
QSize AreaTabs::minimumSizeHint() const { return {60, kHeight}; }

void AreaTabs::setTabs(const QList<Tab> &tabs, const QString &active) {
    m_tabs = tabs;
    m_active = active;
    // La pestaña que se estaba renombrando puede haber desaparecido (borrada
    // en otro equipo): entonces el editor sobra.
    if (m_editor && indexOf(m_editing) < 0) finishRename(false);
    relayout();
    update();
}

int AreaTabs::indexOf(const QString &id) const {
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs.at(i).id == id) return i;
    return -1;
}

int AreaTabs::tabWidth(const Tab &t, QString *elided) const {
    // Se mide con la letra de la activa, la más ancha: así una pestaña no
    // cambia de tamaño al elegirla y la tira no baila.
    const QFontMetrics fm(tabFont(font(), true));
    const QString text = fm.elidedText(t.name.isEmpty() ? L("Sin nombre") : t.name,
                                       Qt::ElideRight, kMaxName);
    if (elided) *elided = text;
    return kPad * 2 + (t.glyph.isEmpty() ? 0 : kGlyph + 5) + fm.horizontalAdvance(text);
}

void AreaTabs::relayout() {
    m_slots.clear();
    m_hidden = 0;
    m_hiddenRinging = false;
    m_overflowRect = QRect();

    m_addRect = QRect(width() - kRight - kAddSize, (kHeight - kAddSize) / 2, kAddSize, kAddSize);
    const int limit = m_addRect.left() - 4;

    QList<int> widths;
    QStringList texts;
    int total = 0;
    for (const Tab &t : m_tabs) {
        QString text;
        widths << tabWidth(t, &text);
        texts << text;
        total += widths.last() + kGap;
    }

    // ¿Caben todas? Entonces no hay «+N».
    QList<int> shown;
    if (kLeft + total <= limit) {
        for (int i = 0; i < m_tabs.size(); ++i) shown << i;
    } else {
        const QFontMetrics fm(tabFont(font(), false));
        const int chipW = fm.horizontalAdvance(QString("+%1").arg(m_tabs.size())) + 26;
        const int room = limit - chipW - 4 - kLeft;
        int used = 0;
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (used + widths.at(i) > room) break;
            shown << i;
            used += widths.at(i) + kGap;
        }
        // La activa siempre se ve: si ha quedado fuera, ocupa el sitio de la
        // última, y de las de antes se quitan las que hagan falta para que quepa.
        const int act = indexOf(m_active);
        if (act >= 0 && !shown.contains(act)) {
            while (!shown.isEmpty()) {
                int sum = widths.at(act);
                for (int i : shown) sum += widths.at(i) + kGap;
                if (sum <= room) break;
                shown.removeLast();
            }
            shown << act;
        }
        // Ni la activa cabe entera: va sola, recortada al hueco que haya.
        if (shown.isEmpty() && act >= 0) shown << act;
    }

    int x = kLeft;
    for (int i : shown) {
        int w = widths.at(i);
        QString text = texts.at(i);
        const int room = limit - x - (shown.size() < m_tabs.size() ? 60 : 0);
        if (w > room && room > kPad * 2 + 20) {
            // Solo pasa en ventanas muy estrechas: se recorta más.
            const QFontMetrics fm(tabFont(font(), true));
            const int textRoom = room - kPad * 2 - (m_tabs.at(i).glyph.isEmpty() ? 0 : kGlyph + 5);
            text = fm.elidedText(m_tabs.at(i).name, Qt::ElideRight, textRoom);
            w = room;
        }
        m_slots.append({i, QRect(x, 0, w, kHeight), text});
        x += w + kGap;
    }

    m_hidden = int(m_tabs.size() - m_slots.size());
    if (m_hidden > 0) {
        for (int i = 0; i < m_tabs.size(); ++i)
            if (!shown.contains(i) && m_tabs.at(i).ringing) m_hiddenRinging = true;
        const QFontMetrics fm(tabFont(font(), false));
        const int chipW = fm.horizontalAdvance(QString("+%1").arg(m_hidden)) + 26;
        m_overflowRect = QRect(m_addRect.left() - 4 - chipW, (kHeight - 24) / 2, chipW, 24);
    }
}

void AreaTabs::resizeEvent(QResizeEvent *) {
    relayout();
    if (m_editor) {
        // El editor sigue a su pestaña, que puede haberse movido.
        for (const Slot &s : m_slots)
            if (m_tabs.at(s.index).id == m_editing)
                m_editor->setGeometry(s.rect.adjusted(2, 6, -2, -6));
    }
}

void AreaTabs::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // La raya de abajo, como la de la cabecera.
    p.setPen(QColor(255, 255, 255, 23));
    p.drawLine(QPointF(0, kHeight - 0.5), QPointF(width(), kHeight - 0.5));

    const QColor fg(Theme::fg());
    const QColor muted(Theme::muted());
    const QColor accent = m_theme->accent;

    for (const Slot &s : m_slots) {
        const Tab &t = m_tabs.at(s.index);
        const bool isActive = t.id == m_active;
        const bool hovered = m_hover == s.index;
        const QRect r = s.rect;

        // Soltar aquí: la pestaña destino se tiñe con el acento.
        if (t.id == m_drop) {
            QColor fill = accent;
            fill.setAlphaF(0.20);
            QColor edge = accent;
            edge.setAlphaF(0.65);
            p.setPen(QPen(edge, 1.5));
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(r).adjusted(1, 5, -1, -5), 7, 7);
        } else if (hovered && !isActive) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 14));
            p.drawRoundedRect(QRectF(r).adjusted(1, 5, -1, -5), 7, 7);
        }

        int x = r.left() + kPad;
        const QColor ink = isActive || hovered || t.id == m_drop ? fg : muted;
        if (!t.glyph.isEmpty()) {
            const QColor gc = isActive ? accent : muted;
            p.drawPixmap(x, (kHeight - kGlyph) / 2,
                         paintIcon("glyph-" + t.glyph, gc, kGlyph).pixmap(kGlyph, kGlyph));
            x += kGlyph + 5;
        }
        if (!(m_editor && m_editing == t.id)) {
            p.setFont(tabFont(font(), isActive));
            p.setPen(ink);
            p.drawText(QRect(x, 0, r.right() - kPad - x + 1, kHeight),
                       Qt::AlignVCenter | Qt::AlignLeft, s.text);
        }

        // El punto rojo: esa área tiene algo sonando, aunque no esté abierta.
        if (t.ringing) {
            p.setPen(Qt::NoPen);
            p.setBrush(kRed);
            p.drawEllipse(QPointF(r.right() - 3.5, 9.5), 3.2, 3.2);
        }

        if (isActive) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(r.left() + 4, kHeight - 2.5, r.width() - 8, 2.5), 1.2, 1.2);
            if (keynav::showsFocus(this)) {
                p.setPen(QPen(accent, 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(r).adjusted(1, 5, -1, -5), 7, 7);
            }
        }
    }

    // «+N»: las que no caben, con el punto rojo de las escondidas.
    if (m_hidden > 0) {
        const QRectF c = QRectF(m_overflowRect).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor fill = accent;
        fill.setAlphaF(m_hover == -3 ? 0.24 : 0.14);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(c, 7, 7);
        p.setFont(tabFont(font(), true));
        p.setPen(accent);
        const QString label = QString("+%1").arg(m_hidden);
        const QRect textRect(m_overflowRect.left() + 8, m_overflowRect.top(),
                             m_overflowRect.width() - 24, m_overflowRect.height());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
        p.drawPixmap(m_overflowRect.right() - 15, m_overflowRect.center().y() - 6,
                     paintIcon("chevronDown", accent, 12).pixmap(12, 12));
        if (m_hiddenRinging) {
            p.setBrush(kRed);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(m_overflowRect.right() - 1, m_overflowRect.top() + 1), 3.2, 3.2);
        }
    }

    // «+»: área nueva.
    if (m_hover == -2) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 18));
        p.drawRoundedRect(QRectF(m_addRect), 8, 8);
    }
    p.drawPixmap(m_addRect.center().x() - 7, m_addRect.center().y() - 7,
                 paintIcon("plus", m_hover == -2 ? fg : muted, 14).pixmap(14, 14));
}

int AreaTabs::hitTest(const QPoint &pos) const {
    if (m_addRect.contains(pos)) return -2;
    if (m_hidden > 0 && m_overflowRect.contains(pos)) return -3;
    for (const Slot &s : m_slots)
        if (s.rect.contains(pos)) return s.index;
    return -1;
}

void AreaTabs::mouseMoveEvent(QMouseEvent *e) {
    const int h = hitTest(e->position().toPoint());
    if (h != m_hover) {
        m_hover = h;
        setCursor(h == -1 ? Qt::ArrowCursor : Qt::PointingHandCursor);
        if (h >= 0) setToolTip(m_tabs.at(h).name);
        else if (h == -2) setToolTip(L("Nueva área · Ctrl+T"));
        else if (h == -3) setToolTip(L("Todas las áreas"));
        else setToolTip(QString());
        update();
    }
}

void AreaTabs::leaveEvent(QEvent *) {
    m_hover = -1;
    update();
}

void AreaTabs::activate(int index) {
    if (index < 0 || index >= m_tabs.size()) return;
    const QString id = m_tabs.at(index).id;
    if (id == m_active) return;
    // El panel contesta con setTabs(); hasta entonces ya se ve elegida.
    m_active = id;
    relayout();
    update();
    if (activated) activated(id);
}

void AreaTabs::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const int h = hitTest(e->position().toPoint());
    if (h == -2) {
        if (addRequested) addRequested();
    } else if (h == -3) {
        if (overflowRequested) overflowRequested(mapToGlobal(m_overflowRect.bottomLeft()));
    } else if (h >= 0) {
        activate(h);
    }
}

void AreaTabs::mouseDoubleClickEvent(QMouseEvent *e) {
    const int h = hitTest(e->position().toPoint());
    if (h >= 0) beginRename(m_tabs.at(h).id);
}

void AreaTabs::contextMenuEvent(QContextMenuEvent *e) {
    QString id = m_active;
    if (e->reason() == QContextMenuEvent::Mouse) {
        const int h = hitTest(e->pos());
        if (h < 0) return;
        id = m_tabs.at(h).id;
    }
    if (menuRequested) menuRequested(id, menuPoint(id));
    e->accept();
}

QPoint AreaTabs::menuPoint(const QString &id) const {
    for (const Slot &s : m_slots)
        if (m_tabs.at(s.index).id == id) return mapToGlobal(s.rect.bottomLeft());
    return mapToGlobal(QPoint(kLeft, kHeight));
}

void AreaTabs::keyPressEvent(QKeyEvent *e) {
    const int act = indexOf(m_active);
    const bool ctrlShift = (e->modifiers() & Qt::ControlModifier) &&
                           (e->modifiers() & Qt::ShiftModifier);
    switch (e->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right: {
            const int step = e->key() == Qt::Key_Left ? -1 : 1;
            if (ctrlShift) {
                if (moveRequested) moveRequested(m_active, step);
            } else {
                activate(act + step);
            }
            return;
        }
        case Qt::Key_Home:
            activate(0);
            return;
        case Qt::Key_End:
            activate(int(m_tabs.size()) - 1);
            return;
        case Qt::Key_F2:
            beginRename(m_active);
            return;
        case Qt::Key_Delete:
            if (deleteRequested) deleteRequested(m_active);
            return;
        case Qt::Key_Down:
            focusNextChild();   // de la tira a las notas
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (menuRequested) menuRequested(m_active, menuPoint(m_active));
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(e);
}

void AreaTabs::focusInEvent(QFocusEvent *e) {
    QWidget::focusInEvent(e);
    update();
}

void AreaTabs::focusOutEvent(QFocusEvent *e) {
    QWidget::focusOutEvent(e);
    update();
}

// --- áreas soltadas y renombradas --------------------------------------------

QString AreaTabs::areaAt(const QPoint &globalPos) const {
    const QPoint pos = mapFromGlobal(globalPos);
    if (!rect().contains(pos)) return {};
    for (const Slot &s : m_slots)
        if (s.rect.contains(pos)) return m_tabs.at(s.index).id;
    return {};
}

void AreaTabs::setDropTarget(const QString &id) {
    if (id == m_drop) return;
    m_drop = id;
    update();
}

void AreaTabs::beginRename(const QString &id) {
    if (indexOf(id) < 0) return;
    // Si está escondida en «+N», se abre primero: no se puede renombrar a
    // ciegas una pestaña que no se ve.
    if (id != m_active) activate(indexOf(id));
    if (m_editor) finishRename(true);

    m_editing = id;
    m_editor = new QLineEdit(this);
    m_editor->setObjectName("areaRename");
    m_editor->setText(m_tabs.at(indexOf(id)).name);
    m_editor->selectAll();
    m_editor->installEventFilter(this);
    // Enter y perder el foco llegan los dos: el segundo encuentra el editor ya
    // cerrado y no hace nada.
    connect(m_editor, &QLineEdit::editingFinished, this, [this] { finishRename(true); });
    for (const Slot &s : m_slots)
        if (m_tabs.at(s.index).id == id) {
            // Un poco más ancho que el nombre, para que se pueda escribir.
            QRect r = s.rect.adjusted(2, 6, -2, -6);
            r.setWidth(qMax(r.width(), 120));
            r.setRight(qMin(r.right(), (m_hidden > 0 ? m_overflowRect.left()
                                                     : m_addRect.left()) - 4));
            m_editor->setGeometry(r);
        }
    m_editor->show();
    m_editor->setFocus(Qt::OtherFocusReason);
    update();
}

void AreaTabs::finishRename(bool commit) {
    if (!m_editor) return;
    QLineEdit *editor = m_editor;
    const QString id = m_editing;
    const QString text = editor->text().trimmed();
    // Con Intro o Escape el foco vuelve a la tira; si el editor se cerró porque
    // se hizo clic en otro sitio, el foco ya es de ese otro sitio.
    const bool hadFocus = editor->hasFocus();
    m_editor = nullptr;
    m_editing.clear();
    editor->hide();
    // Diferido: quien pide cerrar es una señal del propio editor.
    editor->deleteLater();
    update();
    // Un nombre vacío no deja un área sin nombre: se queda el que tenía.
    if (commit && !text.isEmpty() && renamed) renamed(id, text);
    if (hadFocus && isVisible()) setFocus(Qt::OtherFocusReason);
}

bool AreaTabs::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_editor && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        finishRename(false);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
