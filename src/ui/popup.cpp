#include "ui/popup.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/keynav.hpp"

namespace {

constexpr int kShadowMargin = 18;   // hueco alrededor del marco para la sombra
constexpr int kRowWidth = 226;

// Fila del menú: icono + título + subtítulo opcional. Se pinta a mano en vez
// de usar QToolButton para poder tener dos líneas de texto y un realce redondo.
class PopupRow : public QWidget {
public:
    PopupRow(const QIcon &icon, const QString &title, const QString &subtitle,
             std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent), m_icon(icon), m_title(title), m_sub(subtitle),
          m_click(std::move(onClick)) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        keynav::activatable(this, [this] { if (m_click) m_click(); });
    }

    QSize sizeHint() const override {
        return QSize(kRowWidth, m_sub.isEmpty() ? 30 : 40);
    }

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true;  update(); }
    void leaveEvent(QEvent *) override      { m_hover = false; update(); }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_click)
            m_click();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // El foco del teclado se ve como el paso del ratón, más un borde: así
        // las flechas se siguen con la vista sin que haya dos realces distintos.
        if (m_hover || keynav::showsFocus(this)) {
            p.setPen(keynav::showsFocus(this) ? QPen(QColor(255, 255, 255, 70), 1) : QPen(Qt::NoPen));
            p.setBrush(QColor(255, 255, 255, 20));
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        }

        const int iconX = 9;
        m_icon.paint(&p, QRect(iconX, (height() - 16) / 2, 16, 16));

        const int textX = iconX + 16 + 10;
        QFont f = font();
        if (m_sub.isEmpty()) {
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(QColor(Theme::fg()));
            p.drawText(QRect(textX, 0, width() - textX - 10, height()),
                       Qt::AlignVCenter | Qt::AlignLeft, m_title);
        } else {
            f.setPixelSize(12);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(Theme::fg()));
            p.drawText(QRect(textX, 5, width() - textX - 10, 15),
                       Qt::AlignVCenter | Qt::AlignLeft, m_title);

            f.setPixelSize(10);
            f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(QColor(Theme::muted()));
            p.drawText(QRect(textX, 20, width() - textX - 10, 14),
                       Qt::AlignVCenter | Qt::AlignLeft, m_sub);
        }
    }

private:
    QIcon m_icon;
    QString m_title;
    QString m_sub;
    std::function<void()> m_click;
    bool m_hover = false;
};

} // namespace

// ---------------------------------------------------------------------------

Popup::Popup(const Theme &theme, QWidget *anchor, Surface surface)
    : QWidget(anchor, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_theme(theme) {
    if (surface == Opaque) m_theme.opacity = 100;
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);

    m_shell = new QFrame;
    m_shell->setObjectName("popupShell");
    outer->addWidget(m_shell);

    auto *shadow = new QGraphicsDropShadowEffect(m_shell);
    shadow->setBlurRadius(44);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 170));
    m_shell->setGraphicsEffect(shadow);

    m_col = new QVBoxLayout(m_shell);
    m_col->setContentsMargins(6, 6, 6, 6);
    m_col->setSpacing(1);

    // El popup es una ventana propia: necesita la hoja de estilos por su
    // cuenta para que #popupShell tome el fondo con la opacidad del tema.
    setStyleSheet(m_theme.sheet());
}

void Popup::addHeader(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("popupHeader");
    m_col->addWidget(l);
}

void Popup::addItem(const QString &iconKind, const QString &title,
                    const QString &subtitle, std::function<void()> action) {
    const QColor tint = iconKind == "trash" ? QColor("#ff7a6b") : QColor(Theme::muted());
    auto *row = new PopupRow(paintIcon(iconKind, tint), title, subtitle,
                             [this, action] { run(action); });
    m_col->addWidget(row);
}

void Popup::addEditor(const QString &placeholder, const QString &text,
                      std::function<void(const QString &)> commit) {
    auto *host = new QWidget;
    auto *l = new QHBoxLayout(host);
    l->setContentsMargins(7, 4, 7, 5);

    auto *edit = new QLineEdit(text);
    edit->setObjectName("popupEdit");
    edit->setPlaceholderText(placeholder);
    edit->setFixedWidth(kRowWidth - 14);
    edit->selectAll();

    QObject::connect(edit, &QLineEdit::returnPressed, this, [this, edit, commit] {
        const QString value = edit->text().trimmed();
        run([value, commit] { if (commit) commit(value); });
    });

    l->addWidget(edit);
    m_col->addWidget(host);
    QTimer::singleShot(0, edit, qOverload<>(&QWidget::setFocus));
}

void Popup::addFields(const QStringList &placeholders, const QStringList &values,
                      std::function<void(const QStringList &)> commit) {
    auto *host = new QWidget;
    auto *l = new QVBoxLayout(host);
    l->setContentsMargins(7, 4, 7, 5);
    l->setSpacing(4);

    QList<QLineEdit *> edits;
    for (int i = 0; i < placeholders.size(); ++i) {
        auto *edit = new QLineEdit(values.value(i));
        edit->setObjectName("popupEdit");
        edit->setPlaceholderText(placeholders.at(i));
        edit->setFixedWidth(kRowWidth - 14);
        edits.append(edit);
        l->addWidget(edit);
    }

    // Se conectan cuando ya existen todos: Enter en cualquiera confirma el
    // conjunto, así el orden en que se rellenen da igual.
    for (QLineEdit *edit : edits) {
        QObject::connect(edit, &QLineEdit::returnPressed, this, [this, edits, commit] {
            QStringList out;
            for (QLineEdit *e : edits) out << e->text().trimmed();
            run([out, commit] { if (commit) commit(out); });
        });
    }

    m_col->addWidget(host);
    if (!edits.isEmpty()) {
        edits.first()->selectAll();
        QTimer::singleShot(0, edits.first(), qOverload<>(&QWidget::setFocus));
    }
}

// Los presets de hora del calendario: cinco filas de menú son 150 px, más que
// la mitad del panel, y el popup terminaba encima del calendario. En chips
// caben tres por línea y el menú entero mide poco más que una tarjeta.
void Popup::addChoice(const QStringList &labels, int chosen,
                      std::function<void(int)> action) {
    addChips(labels, {}, QString(), std::move(action));

    // El último widget añadido es la rejilla de chips que acaba de montar
    // addChips: se marca el elegido sobre ella en vez de duplicar el montaje.
    QWidget *host = m_col->itemAt(m_col->count() - 1)->widget();
    if (!host) return;
    const QList<QToolButton *> chips = host->findChildren<QToolButton *>();
    for (int i = 0; i < chips.size(); ++i) chips.at(i)->setProperty("chosen", i == chosen);
}

void Popup::addChips(const QStringList &labels, const QList<bool> &muted,
                     const QString &mutedTip, std::function<void(int)> action) {
    constexpr int kPerRow = 3;

    auto *host = new QWidget;
    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(7, 3, 7, 4);
    grid->setSpacing(4);

    for (int i = 0; i < labels.size(); ++i) {
        auto *chip = new QToolButton;
        chip->setObjectName("popupChip");
        chip->setText(labels.at(i));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("past", muted.value(i, false));
        // Lo que antes era el subtítulo de la fila: en un chip no cabe, pero
        // la razón de que se vea apagado sigue estando a mano.
        if (muted.value(i, false)) chip->setToolTip(mutedTip);
        chip->setFixedHeight(26);
        QObject::connect(chip, &QToolButton::clicked, this, [this, i, action] {
            run([i, action] { if (action) action(i); });
        });
        grid->addWidget(chip, i / kPerRow, i % kPerRow);
    }
    // La última fila suele ir a medias: la columna sobrante se queda el hueco
    // en vez de repartirlo estirando los chips que hay.
    grid->setColumnStretch(kPerRow, 1);
    m_col->addWidget(host);
}

void Popup::addSeparator() {
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QString("background:%1; border:none;").arg(Theme::line()));
    m_col->addSpacing(3);
    m_col->addWidget(line);
    m_col->addSpacing(3);
}

// ---------------------------------------------------------------------------

void Popup::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up) {
        focusNextPrevChild(e->key() == Qt::Key_Down);
        return;
    }
    QWidget::keyPressEvent(e);   // Escape cierra: lo hace QWidget con los Qt::Popup
}

// El popup se queda el foco al abrirse (si no lo ha pedido ya un campo), para
// que las flechas y Tab funcionen desde el primer momento. No se enciende la
// primera fila: abierto con el ratón, un realce que nadie ha pedido parece un
// fallo; la primera pulsación de flecha ya lo lleva a ella.
void Popup::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    setFocusPolicy(Qt::StrongFocus);
    QTimer::singleShot(0, this, [this] {
        if (!focusWidget() || focusWidget() == this) setFocus(Qt::PopupFocusReason);
    });
}

void Popup::run(const std::function<void()> &action) {
    close();   // WA_DeleteOnClose: este objeto muere en cuanto vuelva al bucle
    if (action) QTimer::singleShot(0, qApp, action);
}

void Popup::place(const QPoint &globalTopLeft, int minY) {
    adjustSize();
    QPoint pos = globalTopLeft;

    if (QScreen *screen = QGuiApplication::screenAt(pos) ? QGuiApplication::screenAt(pos)
                                                         : QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        // El margen de sombra no cuenta como parte visible del menú.
        pos.setX(qBound(area.left() - kShadowMargin, pos.x(),
                        area.right() - width() + kShadowMargin));
        pos.setY(qBound(area.top() - kShadowMargin, pos.y(),
                        area.bottom() - height() + kShadowMargin));

        // Con suelo, la corrección contra la pantalla no puede subir el menú:
        // se queda por debajo de quien lo abrió aunque asome un poco. El suelo
        // se levanta solo si el marco visible ya no cabría entero, porque un
        // menú cortado por el borde de abajo es peor que uno mal puesto.
        if (minY != INT_MIN && minY + height() - kShadowMargin <= area.bottom())
            pos.setY(qMax(pos.y(), minY));
    } else if (minY != INT_MIN) {
        pos.setY(qMax(pos.y(), minY));
    }
    move(pos);
    show();
}

void Popup::showUnder(QWidget *anchor) {
    adjustSize();
    // Se descuenta el margen de sombra para que el marco quede pegado al botón.
    const QPoint below = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
    place(below - QPoint(kShadowMargin, 0) + QPoint(0, -kShadowMargin + 6));
}

void Popup::showBelow(QWidget *anchor) {
    adjustSize();
    const QPoint below = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
    const QPoint at = below - QPoint(kShadowMargin, 0) + QPoint(0, -kShadowMargin + 6);
    place(at, at.y());
}

void Popup::showAt(const QPoint &globalPos) {
    place(globalPos - QPoint(kShadowMargin, kShadowMargin) + QPoint(4, 4));
}
