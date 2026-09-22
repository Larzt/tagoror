#include "ui/notecard.hpp"
#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/imagethumb.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"
#include "audio/recorder.hpp"
#include "ui/theme.hpp"
#include "audio/wave.hpp"
#include "ui/waveform.hpp"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QStyle>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QProgressBar>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QPainter>
#include <QVBoxLayout>

namespace {

// --- la fecha escrita a mano -----------------------------------------------
//
// El campo se precarga con lo que ya tiene el recordatorio, y tiene que
// hacerlo en el mismo formato que sabe leer. Cargarlo con 'due' -- la etiqueta
// que enseña el chip, "mar 25 ago 09:00" -- convertía el gesto natural
// (abrirlo, retocar la hora, aceptar) en texto que el lector no entiende: el
// aviso se quedaba sin instante, y como el calendario solo coloca los que lo
// tienen (Note::isScheduled), desaparecía de la rejilla y de la cuenta del pie
// aunque el chip siguiera enseñando una fecha. De ahí sale el formato, no de
// la etiqueta.
QString dueFieldText(const Note *n) {
    return n->dueAtMs > 0 ? n->dueAt().toString("dd/MM HH:mm") : n->due;
}

// dd/MM HH:mm, con el año opcional detrás del mes. La fecha se arma por piezas
// en vez de dejársela a QDateTime::fromString(): sin año esa función ancla en
// 1900, que no es bisiesto, así que un 29 de febrero no se podía escribir de
// ninguna manera. El año que falta es el actual, y así el 29 solo vale en los
// años que de verdad lo tienen -- para los demás está 29/02/2028.
QDateTime parseDue(const QString &text) {
    static const QRegularExpression re(
        R"(^\s*(\d{1,2})\s*/\s*(\d{1,2})(?:\s*/\s*(\d{4}))?[\s,]+(\d{1,2})\s*:\s*(\d{2})\s*$)");
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return QDateTime();

    const int year = m.captured(3).isEmpty() ? QDate::currentDate().year()
                                             : m.captured(3).toInt();
    const QDate date(year, m.captured(2).toInt(), m.captured(1).toInt());
    const QTime time(m.captured(4).toInt(), m.captured(5).toInt());
    return date.isValid() && time.isValid() ? QDateTime(date, time) : QDateTime();
}

// Editor de cuerpo que crece con su contenido: sin barras de scroll propias,
// la altura sigue al documento entre un mínimo y un máximo.
void autoGrow(QTextEdit *e, const QString &placeholder) {
    // Sin el menú nativo de cortar/pegar: el clic derecho es para el menú de
    // la nota. Los atajos de teclado siguen funcionando igual.
    e->setContextMenuPolicy(Qt::NoContextMenu);
    e->setPlaceholderText(placeholder);
    // Tab sale del editor en vez de escribir un tabulador: si no, navegar con
    // el teclado se quedaba atrapado en la primera nota de texto.
    e->setTabChangesFocus(true);
    e->setAcceptRichText(false);
    e->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    e->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    e->setFixedHeight(46);
    // Fijar la altura no basta: la política de un QTextEdit sigue siendo
    // Expanding, y el layout de la tarjeta la propaga hacia arriba. La lista
    // le daba entonces el hueco sobrante de una ventana alta, y como el editor
    // no puede crecer, se lo quedaba la etiqueta de abajo en forma de hueco.
    e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QObject::connect(e->document()->documentLayout(),
                     &QAbstractTextDocumentLayout::documentSizeChanged, e,
                     [e](const QSizeF &s) {
                         e->setFixedHeight(qBound(46, int(s.height()) + 14, 170));
                     });
}

// --- Markdown ---------------------------------------------------------------
//
// En CommonMark un salto de línea suelto no parte el párrafo: "leche\npan" se
// lee "leche pan". Para Markdown escrito como documento es lo correcto, pero en
// una nota rápida cada línea es una línea, y así las venía viendo todo el que
// ya tenía notas. Se marcan como saltos duros (dos espacios al final) salvo
// cuando la línea siguiente ya empieza un bloque propio, o dentro de un bloque
// de código, donde las líneas se respetan solas.
QString hardBreaks(const QString &source) {
    static const QRegularExpression blockStart(
        R"(^\s*([-*+]\s|\d+[.)]\s|#{1,6}\s|>|```|~~~))");
    const QStringList lines = source.split('\n');
    QStringList out;
    bool fenced = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith("```") || trimmed.startsWith("~~~")) fenced = !fenced;
        const bool hasNext = i + 1 < lines.size();
        const bool breakHere = !fenced && hasNext && !trimmed.isEmpty() &&
                               !lines.at(i + 1).trimmed().isEmpty() &&
                               !blockStart.match(lines.at(i + 1)).hasMatch();
        out << (breakHere ? line + "  " : line);
    }
    return out.join('\n');
}

// Cuerpo de una nota con Markdown. En reposo enseña el texto ya formateado;
// al entrar (con el ratón o con Tab) pasa al texto tal cual para editarlo, y al
// salir vuelve a formatearse. Es un solo editor y no dos widgets que se
// turnan, así el alto, el foco y el orden de tabulación no cambian de dueño.
//
// Lo que se guarda es siempre el texto fuente: 'm_source' manda, y lo que haya
// en el documento mientras se ve formateado no se escribe nunca en la nota.
class MarkdownEdit : public QTextEdit {
public:
    std::function<void(const QString &)> edited;   // el texto fuente cambió

    MarkdownEdit(const QString &source, const QColor &accent) : m_source(source) {
        // Los enlaces en el acento, como los adjuntos de la tarjeta. La hoja de
        // estilos no toca este rol, así que la paleta del propio widget manda.
        QPalette pal = palette();
        pal.setColor(QPalette::Link, accent);
        setPalette(pal);
        setAcceptRichText(false);
        viewport()->setMouseTracking(true);

        connect(this, &QTextEdit::textChanged, this, [this] {
            if (m_internal || !m_editing) return;
            m_source = toPlainText();
            if (edited) edited(m_source);
        });
        render();
    }

    const QString &source() const { return m_source; }
    bool editing() const { return m_editing; }

    void beginEdit() {
        if (m_editing) return;
        m_editing = true;
        m_internal = true;
        setPlainText(m_source);
        m_internal = false;
        moveCursor(QTextCursor::End);
    }

    void endEdit() {
        if (!m_editing) return;
        m_editing = false;
        render();
    }

protected:
    // Los títulos se calculan sobre la letra del editor, y esa letra la pone la
    // hoja de estilos al pulir el widget -- después del constructor -- y otra
    // vez al cambiar el tamaño de texto en ajustes. Se reformatea entonces.
    void changeEvent(QEvent *e) override {
        QTextEdit::changeEvent(e);
        if (e->type() == QEvent::FontChange && !m_editing) render();
    }

    void focusInEvent(QFocusEvent *e) override {
        QTextEdit::focusInEvent(e);
        // Con el ratón se espera a soltar: el clic puede ser sobre un enlace, y
        // entonces lo que se quiere es abrirlo, no editar la nota.
        if (e->reason() == Qt::MouseFocusReason && !m_editing) m_clickPending = true;
        else beginEdit();
    }

    void focusOutEvent(QFocusEvent *e) override {
        QTextEdit::focusOutEvent(e);
        m_clickPending = false;
        // Cambiar de ventana o abrir un menú no es terminar de escribir.
        if (e->reason() == Qt::ActiveWindowFocusReason || e->reason() == Qt::PopupFocusReason)
            return;
        endEdit();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape && m_editing) {
            clearFocus();   // focusOut formatea
            return;
        }
        if (!m_editing) beginEdit();   // llegó sin foco de ratón ni de Tab
        QTextEdit::keyPressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_editing)
            viewport()->setCursor(anchorAt(e->position().toPoint()).isEmpty()
                                      ? Qt::IBeamCursor : Qt::PointingHandCursor);
        QTextEdit::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (m_clickPending && e->button() == Qt::LeftButton) {
            m_clickPending = false;
            const QPoint at = e->position().toPoint();
            const QString href = anchorAt(at);
            if (!href.isEmpty()) {
                QDesktopServices::openUrl(QUrl(href));
                clearFocus();
                return;
            }
            // El sitio del clic en el texto formateado no es el mismo en el
            // fuente, pero cae cerca: mejor que mandar el cursor al final.
            beginEdit();
            setTextCursor(cursorForPosition(at));
            viewport()->setCursor(Qt::IBeamCursor);
            return;
        }
        QTextEdit::mouseReleaseEvent(e);
    }

private:
    void render() {
        m_internal = true;
        if (m_source.trimmed().isEmpty()) {
            clear();   // así se ve el texto de ayuda
        } else {
            setMarkdown(hardBreaks(m_source));
            tidy();
        }
        m_internal = false;
    }

    // El importador formatea para un documento, no para una tarjeta de 300 px:
    // un # salía al doble del texto, una lista sangraba 40 px y los enlaces
    // iban en el azul de la paleta de la aplicación en vez del acento.
    void tidy() {
        QTextDocument *doc = document();
        doc->setIndentWidth(14);
        const int base = QFontInfo(font()).pixelSize();
        const QColor accent = palette().color(QPalette::Link);
        QColor tint = accent;
        tint.setAlphaF(0.16);

        QTextCursor c(doc);
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            QTextBlockFormat f = b.blockFormat();
            const int level = f.headingLevel();
            // 12 px entre cada línea hacían de una lista de la compra una
            // columna el doble de alta que en texto plano.
            f.setTopMargin(level > 0 && b != doc->begin() ? 6 : 0);
            f.setBottomMargin(level > 0 ? 2 : 1);
            // Una cita sangraba 40 px por cada lado; en una tarjeta estrecha eso
            // es media línea.
            const bool quote = f.hasProperty(QTextFormat::BlockQuoteLevel);
            if (quote) {
                f.setLeftMargin(10);
                f.setRightMargin(0);
            }
            c.setPosition(b.position());
            c.setBlockFormat(f);

            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                QTextCharFormat cf = frag.charFormat();
                bool touched = false;
                if (level > 0) {
                    // El ajuste relativo del importador hay que quitarlo, no
                    // ponerlo a cero: mientras la propiedad exista, manda sobre
                    // el tamaño en píxeles y el título sale del tamaño del texto.
                    cf.clearProperty(QTextFormat::FontSizeAdjustment);
                    cf.clearProperty(QTextFormat::FontPointSize);
                    cf.setProperty(QTextFormat::FontPixelSize,
                                   qRound(base * (level == 1 ? 1.3 : level == 2 ? 1.15 : 1.05)));
                    cf.setFontWeight(QFont::Bold);
                    touched = true;
                }
                if (quote) {
                    cf.setFontItalic(true);
                    cf.setForeground(QColor(Theme::muted()));
                    touched = true;
                }
                if (cf.isAnchor()) {
                    cf.setForeground(accent);
                    touched = true;
                }
                if (cf.fontFixedPitch()) {
                    cf.setForeground(accent);
                    cf.setBackground(tint);
                    touched = true;
                }
                if (!touched) continue;
                c.setPosition(frag.position());
                c.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
                c.setCharFormat(cf);
            }
        }
    }

    QString m_source;
    bool m_editing = false;
    bool m_internal = false;       // cambios del propio editor, no del usuario
    bool m_clickPending = false;
};

// El chip de fecha es pulsable: cambiar el recordatorio no debería obligar a
// buscar la opción en el menú contextual.
class ClickableLabel : public QLabel {
public:
    ClickableLabel(const QString &text, std::function<void(const QPoint &)> onClick)
        : QLabel(text), m_click(std::move(onClick)) {
        setCursor(Qt::PointingHandCursor);
    }

    // Con el teclado el menú sale pegado a la propia etiqueta, que es donde
    // habría caído el clic.
    void makeKeyboardReachable() {
        keynav::activatable(this, [this] {
            if (m_click) m_click(mapToGlobal(QPoint(0, height())));
        });
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_click)
            m_click(e->globalPosition().toPoint());
    }

private:
    std::function<void(const QPoint &)> m_click;
};

// Tachado de un elemento hecho. Va en la etiqueta, no en la casilla, desde
// que el texto dejó de vivir dentro del QCheckBox.
void strikeOut(QLabel *label, bool on) {
    QFont f = label->font();
    f.setStrikeOut(on);
    label->setFont(f);
}

// Fila de un enlace: clic izquierdo abre, clic derecho da sus opciones. El
// menú se atiende aquí para que no salte el de la tarjeta entera.
class LinkRow : public QWidget {
public:
    explicit LinkRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("linkRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { if (activate) activate(); });
    }

    std::function<void()> activate;
    std::function<void(const QPoint &)> menu;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && activate)
            activate();
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (!menu) return;
        menu(e->globalPos());
        e->accept();
    }
};

// Una dirección sin esquema es lo que la gente teclea; se completa para que
// QDesktopServices sepa qué hacer con ella.
QString normalizedUrl(const QString &raw) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) return text;
    return QUrl(text).scheme().isEmpty() ? "https://" + text : text;
}

// Lo que se ve del enlace cuando no tiene nombre: sin esquema ni barra final,
// que es ruido en una tarjeta estrecha.
QString prettyUrl(const QString &url) {
    QString text = url;
    for (const QString &prefix : {"https://", "http://"})
        if (text.startsWith(prefix, Qt::CaseInsensitive)) text = text.mid(prefix.size());
    if (text.startsWith("www.", Qt::CaseInsensitive)) text = text.mid(4);
    if (text.endsWith("/")) text.chop(1);
    return text;
}

QString linkText(const Link &link) {
    return link.label.isEmpty() ? prettyUrl(link.url) : link.label;
}

QString formatMs(qint64 ms) {
    const qint64 total = ms / 1000;
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}

QString typeLabel(Note::Type t) {
    switch (t) {
        case Note::Check:    return L("Lista");
        case Note::Reminder: return L("Recordatorio");
        case Note::Voice:    return L("Nota de voz");
        default:             return L("Nota");
    }
}

// Asidero para reordenar la tarjeta. No arrastra la ventana como DragBar ni
// pide nada al compositor: el movimiento se queda dentro de la lista, así que
// solo reparte las posiciones del ratón y quien manda es el panel.
//
// El arrastre no empieza hasta pasar startDragDistance, para que un clic torpe
// sobre el asidero no reordene nada.
class DragHandle : public QToolButton {
public:
    std::function<void()> start;
    std::function<void(const QPoint &)> moved;
    std::function<void()> finished;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) {
            QToolButton::mousePressEvent(e);
            return;
        }
        m_press = e->globalPosition().toPoint();
        m_active = false;
        e->accept();     // Qt agarra el ratón: los movimientos siguen llegando aquí
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        const QPoint at = e->globalPosition().toPoint();
        if (!m_active &&
            (at - m_press).manhattanLength() < QApplication::startDragDistance())
            return;
        if (!m_active) {
            m_active = true;
            setDown(false);       // si no, se queda hundido al soltar
            setCursor(Qt::ClosedHandCursor);   // la mano se cierra al agarrar
            if (start) start();
        }
        if (moved) moved(at);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (!m_active) {
            QToolButton::mouseReleaseEvent(e);
            return;
        }
        m_active = false;
        setCursor(Qt::OpenHandCursor);
        if (finished) finished();
        e->accept();
    }

private:
    QPoint m_press;
    bool m_active = false;
};

QToolButton *roundButton(const QString &kind, const QColor &color, const QString &tip) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, color));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(28, 28);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------

NoteCard::NoteCard(Note *note, const Theme &theme, QWidget *parent)
    : QFrame(parent), m_note(note), m_theme(theme) {
    setObjectName("card");
    build();
}

void NoteCard::build() {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(11, 9, 11, 10);
    l->setSpacing(7);

    buildTitleRow(l);

    switch (m_note->type) {
        case Note::Check:    buildCheck(l);    break;
        case Note::Reminder: buildReminder(l); refreshDue(); break;
        case Note::Voice:    buildVoice(l);    break;
        default:             buildText(l);     break;
    }

    buildImages(l);

    m_linksBox = new QWidget;
    m_linksLayout = new QVBoxLayout(m_linksBox);
    m_linksLayout->setContentsMargins(0, 0, 0, 0);
    m_linksLayout->setSpacing(1);

    // Al final de la tarjeta, pero por encima del rótulo del tipo cuando lo
    // hay: el pie es lo último que se lee. En recordatorio y lista el rótulo
    // no es hijo directo de este layout, así que ahí van los últimos.
    const int at = m_meta ? l->indexOf(m_meta) : -1;
    if (at >= 0) l->insertWidget(at, m_linksBox);
    else l->addWidget(m_linksBox);
    refreshLinks();
}

// Título editable y, a su derecha, el asidero para reordenar. El asidero se
// pinta apagado y solo se enciende al pasar por encima: está siempre, pero no
// compite con el título.
void NoteCard::buildTitleRow(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    m_title = new QLineEdit(m_note->title);
    m_title->setContextMenuPolicy(Qt::NoContextMenu);
    m_title->setObjectName("cardTitleEdit");
    m_title->setPlaceholderText(L("Sin título"));
    connect(m_title, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_note->title = t;
        emit dirty();
    });
    row->addWidget(m_title, 1);

    auto *handle = new DragHandle;
    handle->setObjectName("dragHandle");
    handle->setIcon(paintIcon("grip", QColor(Theme::muted()), 13));
    handle->setIconSize(QSize(13, 13));
    handle->setFixedSize(20, 20);
    // Mano abierta, no la flecha de redimensionar: SizeVerCursor es el cursor
    // de estirar un borde y prometía cambiar el alto de la tarjeta.
    handle->setCursor(Qt::OpenHandCursor);
    handle->setToolTip(L("Arrastra para reordenar"));
    // Fuera del orden de tabulación: con el teclado se reordena desde el menú
    // de la tarjeta (Subir / Bajar), y pasar por un asidero en cada nota solo
    // alarga el camino.
    handle->setFocusPolicy(Qt::NoFocus);
    handle->start = [this] { emit dragStarted(); };
    handle->moved = [this](const QPoint &at) { emit dragMoved(at); };
    handle->finished = [this] { emit dragFinished(); };
    row->addWidget(handle, 0, Qt::AlignVCenter);

    l->addLayout(row);
}

void NoteCard::buildText(QVBoxLayout *l) {
    auto *body = new MarkdownEdit(m_note->body, m_theme.accent);
    autoGrow(body, L("Escribe… admite Markdown"));
    body->edited = [this](const QString &text) {
        m_note->body = text;
        emit dirty();
    };
    l->addWidget(body);

    m_meta = new QLabel(L("TEXTO"));
    m_meta->setObjectName("meta");
    l->addWidget(m_meta);
}

void NoteCard::buildReminder(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);

    // Icono de reloj/campana junto a la fecha: el estado se ve de un vistazo
    // sin tener que leer la etiqueta.
    m_dueIcon = new QLabel;
    m_dueIcon->setFixedSize(14, 14);
    row->addWidget(m_dueIcon, 0, Qt::AlignVCenter);

    m_chip = new ClickableLabel(m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel(),
                                [this](const QPoint &p) { openDuePopup(p); });
    m_chip->setObjectName("chip");
    m_chip->setToolTip(L("Clic para cambiar la fecha"));
    static_cast<ClickableLabel *>(m_chip)->makeKeyboardReachable();
    row->addWidget(m_chip);

    // Marca de repetición al lado de la fecha. Es un icono y no un chip con el
    // texto ("Cada semana") porque la fila ya va justa: con el chip, un
    // recordatorio semanal vencido pedía 296 px de ancho mínimo y la lista solo
    // tiene 284 con el panel en su tamaño mínimo -- y una tarjeta que pide de
    // más ensancha la lista entera y recorta a todas por la derecha (ver
    // CLAUDE.md). El rótulo de la derecha ya dice CADA SEMANA con todas sus
    // letras cuando no hay nada más urgente que contar.
    m_repeatChip = new ClickableLabel(QString(), [this](const QPoint &p) { openDuePopup(p); });
    m_repeatChip->setFixedSize(12, 12);
    row->addWidget(m_repeatChip, 0, Qt::AlignVCenter);
    refreshRepeat();

    // Solo aparece mientras suena la alarma; es la forma de callarla.
    m_dueBtn = roundButton("stop", QColor("#ff7a6b"), L("Detener aviso"));
    m_dueBtn->setObjectName("dueBtn");
    m_dueBtn->hide();
    connect(m_dueBtn, &QToolButton::clicked, this, [this] {
        emit dismissRequested(m_note);
    });
    row->addWidget(m_dueBtn);
    row->addStretch();

    m_meta = new QLabel(L("RECORDATORIO"));
    m_meta->setObjectName("meta");
    row->addWidget(m_meta);
    l->addLayout(row);

    // Sin detalles escritos, la tarjeta es su fecha y nada más: el editor
    // vacío ocupaba 46 px de nada en cada recordatorio de la lista.
    m_detailSlot = new QWidget;
    m_detailLayout = new QVBoxLayout(m_detailSlot);
    m_detailLayout->setContentsMargins(0, 0, 0, 0);
    m_detailLayout->setSpacing(0);
    l->addWidget(m_detailSlot);

    if (m_note->body.isEmpty()) showDetailsGhost();
    else showDetailsEditor(false);
}

// Lo que se ve en el hueco de detalles cuando no hay ninguno: una sola línea
// apagada que los pide. Vaciar el editor no vuelve aquí mientras se escribe
// -- el cursor se quedaría sin sitio a media frase --; se vuelve al salir de
// él, que es lo que da manera de cerrar un editor abierto por error.
void NoteCard::showDetailsGhost() {
    if (!m_detailLayout) return;
    m_detailBody = nullptr;
    while (QLayoutItem *it = m_detailLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    auto *ghost = new ClickableLabel(L("+ Añadir detalles"),
                                     [this](const QPoint &) { showDetailsEditor(true); });
    ghost->setObjectName("addDetails");
    ghost->setContextMenuPolicy(Qt::NoContextMenu);
    ghost->makeKeyboardReachable();
    ghost->setToolTip(L("Escribir detalles del recordatorio"));
    m_detailLayout->addWidget(ghost);
}

void NoteCard::showDetailsEditor(bool focus) {
    if (!m_detailLayout) return;
    while (QLayoutItem *it = m_detailLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    auto *body = new MarkdownEdit(m_note->body, m_theme.accent);
    autoGrow(body, L("Detalles…"));
    body->edited = [this](const QString &text) {
        m_note->body = text;
        emit dirty();
    };
    body->installEventFilter(this);
    m_detailBody = body;
    m_detailLayout->addWidget(body);
    if (focus) body->setFocus(Qt::OtherFocusReason);
}

// Un editor de detalles abierto y vacío no tenía puerta de salida: ocupaba su
// hueco hasta reconstruir la tarjeta. Al perder el foco (o con Escape) se
// recoge y vuelve la línea de "+ Añadir detalles".
//
// El cierre va diferido: aquí todavía se está despachando un evento del propio
// editor, y showDetailsGhost() lo destruye.
bool NoteCard::eventFilter(QObject *watched, QEvent *event) {
    // Escape deja el elemento como estaba. Al soltar el foco salta además
    // editingFinished, pero para entonces ya no hay fila en edición y el
    // guardado se descarta solo.
    if (watched == m_itemEdit && m_itemEdit && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        cancelItemEdit();
        m_itemEdit->clearFocus();
        return true;
    }

    if (watched == m_detailBody && m_detailBody) {
        const bool leaving = event->type() == QEvent::FocusOut;
        const bool escape = event->type() == QEvent::KeyPress &&
                            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
        // El cuerpo de la nota y no toPlainText(): formateado, el documento no
        // es el texto que se guarda.
        if ((leaving || escape) && m_note->body.trimmed().isEmpty()) {
            if (escape) m_detailBody->clearFocus();
            QTimer::singleShot(0, this, [this] {
                if (m_detailBody && !m_detailBody->hasFocus() &&
                    m_note->body.trimmed().isEmpty())
                    showDetailsGhost();
            });
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NoteCard::buildCheck(QVBoxLayout *l) {
    m_itemsLayout = new QVBoxLayout;
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    m_itemsLayout->setSpacing(3);
    l->addLayout(m_itemsLayout);
    rebuildItems();

    // Fila de alta: una casilla punteada (aún no existe) y un campo sin caja,
    // para que se lea como un elemento más de la lista y no como un formulario.
    auto *addRow = new QWidget;
    auto *al = new QHBoxLayout(addRow);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(8);

    auto *ghost = new QLabel;
    ghost->setFixedSize(13, 13);
    ghost->setPixmap(paintIcon("checkdots", QColor(Theme::muted()), 13).pixmap(13, 13));
    al->addWidget(ghost, 0, Qt::AlignVCenter);

    m_newItem = new QLineEdit;
    m_newItem->setContextMenuPolicy(Qt::NoContextMenu);
    m_newItem->setObjectName("newItemEdit");
    m_newItem->setPlaceholderText(L("Añadir elemento…"));
    connect(m_newItem, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_newItem->text().trimmed();
        if (text.isEmpty()) return;
        m_note->items.append(CheckItem{text, false});
        m_newItem->clear();
        rebuildItems();
        refreshProgress();
        emit dirty();
    });
    al->addWidget(m_newItem, 1);
    l->addWidget(addRow);

    auto *foot = new QHBoxLayout;
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);

    m_bar = new QProgressBar;
    m_bar->setTextVisible(false);
    // El acento llega por constructor, así que se aplica en línea: la hoja
    // global no se regenera al cambiar de color sin reconstruir las tarjetas.
    m_bar->setStyleSheet(QString("QProgressBar::chunk { background:%1; border-radius:2px; }")
                             .arg(m_theme.accent.name()));
    foot->addWidget(m_bar, 1);

    m_progress = new QLabel;
    m_progress->setObjectName("meta");
    foot->addWidget(m_progress);
    l->addLayout(foot);

    // Fila propia, no al lado del contador: el botón lleva texto y una fila de
    // anchos fijos es justo lo que estrecha la lista entera (ver el comentario
    // de addCheckRow). Solo aparece con la lista terminada.
    auto *doneRow = new QHBoxLayout;
    doneRow->setContentsMargins(0, 0, 0, 0);
    doneRow->addStretch();

    m_clearBtn = new QToolButton;
    m_clearBtn->setObjectName("listDone");
    m_clearBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_clearBtn->setIcon(paintIcon("trash", QColor("#ff7a6b"), 12));
    m_clearBtn->setIconSize(QSize(12, 12));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setText(L("Eliminar lista"));
    m_clearBtn->setToolTip(L("Ya está todo hecho: quitar esta nota"));
    m_clearBtn->hide();
    connect(m_clearBtn, &QToolButton::clicked, this, [this] {
        emit deleteRequested(m_note);
    });
    doneRow->addWidget(m_clearBtn);
    l->addLayout(doneRow);

    refreshProgress();
}

void NoteCard::addCheckRow(QVBoxLayout *l, int index) {
    const CheckItem &item = m_note->items.at(index);

    auto *row = new QWidget;
    auto *rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);

    // El texto va en una etiqueta aparte y no dentro del QCheckBox: con el
    // texto dentro, el sizeHint de la casilla es el ancho de la frase entera,
    // ese ancho se convierte en el mínimo de la tarjeta y el QScrollArea
    // ensancha toda la lista por encima del viewport. Como la barra horizontal
    // está desactivada, los elementos largos simplemente desaparecían por la
    // derecha. Con la etiqueta suelta y wordWrap, se parten en varias líneas.
    auto *box = new QCheckBox;
    box->setContextMenuPolicy(Qt::NoContextMenu);
    // Solo por Tab: con el foco por clic, marcar con el ratón dejaba el anillo
    // del teclado puesto en la casilla.
    box->setFocusPolicy(Qt::TabFocus);
    box->setChecked(item.done);
    box->setCursor(Qt::PointingHandCursor);
    rl->addWidget(box, 0, Qt::AlignTop);

    // En edición la fila enseña un campo en lugar de la etiqueta. Es un
    // QLineEdit y no la etiqueta hecha editable porque un QLineEdit no pide el
    // ancho de su texto —ya lo hacen el título de la tarjeta y la fila de
    // añadir—, así que renombrar no puede reventar el ancho de la lista.
    QLabel *text = nullptr;
    if (index == m_editingItem) {
        auto *edit = new QLineEdit(item.text);
        edit->setObjectName("checkTextEdit");
        edit->setContextMenuPolicy(Qt::NoContextMenu);
        edit->installEventFilter(this);   // Escape cancela; ver eventFilter
        m_itemEdit = edit;
        connect(edit, &QLineEdit::editingFinished, this, [this, index, edit] {
            // editingFinished salta con Enter y también al perder el foco, así
            // que llega dos veces seguidas; la segunda ya no hay nada que hacer.
            if (m_editingItem != index) return;
            commitItemEdit(index, edit->text());
        });
        rl->addWidget(edit, 1);
        // Diferido: quien lo construye es rebuildItems(), y pedir el foco en
        // mitad de ese reparto lo pierde al terminar de colocar las filas.
        QTimer::singleShot(0, edit, [edit] {
            edit->setFocus();
            edit->selectAll();
        });
    } else {
        text = new ClickableLabel(item.text, [box](const QPoint &) { box->toggle(); });
        text->setObjectName("checkText");
        text->setContextMenuPolicy(Qt::NoContextMenu);
        text->setWordWrap(true);
        // wordWrap parte por los espacios, no por dentro de una palabra: un
        // elemento que sea una sola palabra larga —una contraseña, un nombre de
        // fichero sin separadores— pedía como mínimo el ancho de esa palabra
        // (medido: 335 px contra los 284 que tiene la lista), y ese mínimo se
        // convierte en el de la tarjeta y recorta TODAS las de la lista, no
        // solo esta. Con el mínimo acotado, la palabra se corta por el borde de
        // su propia fila y las demás tarjetas se quedan como estaban; el texto
        // entero sigue estando en la ayuda emergente y en el campo al
        // renombrar. Es la misma regla que aplica ElidedLabel.
        text->setMinimumWidth(24);
        text->setToolTip(item.text);
        strikeOut(text, item.done);
        rl->addWidget(text, 1);
    }

    connect(box, &QCheckBox::toggled, this, [this, text, index](bool on) {
        if (index >= m_note->items.size()) return;
        m_note->items[index].done = on;
        if (text) strikeOut(text, on);
        refreshProgress();
        emit dirty();
    });

    // Renombrar. Va en un botón propio y no en el clic sobre el texto: ahí ya
    // hay un gesto —marcar el elemento— y quitárselo para meter este cambiaría
    // algo que funciona por añadir algo que falta.
    auto *rename = new QToolButton;
    rename->setIcon(paintIcon("pencil", QColor(Theme::muted()), 12));
    rename->setIconSize(QSize(12, 12));
    rename->setFixedSize(18, 18);
    rename->setCursor(Qt::PointingHandCursor);
    rename->setToolTip(L("Renombrar elemento"));
    rename->setVisible(index != m_editingItem);
    connect(rename, &QToolButton::clicked, this, [this, index] { beginItemEdit(index); });
    rl->addWidget(rename, 0, Qt::AlignTop);

    auto *del = new QToolButton;
    del->setIcon(paintIcon("minus", QColor(Theme::muted()), 12));
    del->setIconSize(QSize(12, 12));
    del->setFixedSize(18, 18);
    del->setCursor(Qt::PointingHandCursor);
    del->setToolTip(L("Quitar elemento"));
    connect(del, &QToolButton::clicked, this, [this, index] {
        if (index >= m_note->items.size()) return;
        m_note->items.removeAt(index);
        rebuildItems();
        refreshProgress();
        emit dirty();
    });
    rl->addWidget(del, 0, Qt::AlignTop);   // a la altura de la primera línea

    l->addWidget(row);
}

// El campo de edición se cierra solo al perder el foco, así que basta con
// apuntar cuál es la fila y rehacerlas.
void NoteCard::beginItemEdit(int index) {
    if (index < 0 || index >= m_note->items.size()) return;
    m_editingItem = index;
    rebuildItems();
}

// Un texto vacío no borra el elemento: para eso está el botón de quitar, y
// vaciarlo sin querer no puede costar la línea. Se deja como estaba.
void NoteCard::commitItemEdit(int index, const QString &text) {
    m_editingItem = -1;
    if (index < 0 || index >= m_note->items.size()) {
        rebuildItems();
        return;
    }

    const QString clean = text.trimmed();
    const bool changed = !clean.isEmpty() && clean != m_note->items.at(index).text;
    if (changed) m_note->items[index].text = clean;

    // Diferido: quien llama es el propio campo, desde su editingFinished, y
    // rebuildItems() lo destruye en mitad de su propia señal.
    QTimer::singleShot(0, this, [this] { rebuildItems(); });
    if (changed) emit dirty();
}

void NoteCard::cancelItemEdit() {
    if (m_editingItem < 0) return;
    m_editingItem = -1;
    QTimer::singleShot(0, this, [this] { rebuildItems(); });
}

void NoteCard::rebuildItems() {
    m_itemEdit = nullptr;   // lo que hubiera muere en este mismo barrido
    // Las filas capturan su índice, así que al borrar una hay que rehacerlas.
    // Se ocultan antes de borrarlas: hasta que corre deleteLater() siguen
    // pintadas donde estaban y siguen aceptando eventos.
    while (QLayoutItem *it = m_itemsLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    for (int i = 0; i < m_note->items.size(); ++i)
        addCheckRow(m_itemsLayout, i);
}

void NoteCard::refreshProgress() {
    if (!m_bar) return;
    const int total = m_note->items.size();
    const int done = m_note->doneCount();

    m_bar->setRange(0, total > 0 ? total : 1);
    m_bar->setValue(done);
    if (m_progress) m_progress->setText(QString("%1/%2").arg(done).arg(total));
    // Una lista vacía no está terminada, está sin empezar.
    if (m_clearBtn) m_clearBtn->setVisible(total > 0 && done == total);
}

// --- recordatorio ----------------------------------------------------------

void NoteCard::openDuePopup(const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Recordar"));

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime todaySix = QDateTime(now.date(), QTime(18, 0));
    if (todaySix <= now) todaySix = todaySix.addDays(1);

    const QList<QPair<QString, QDateTime>> presets = {
        {L("En 5 minutos"), now.addSecs(300)},
        {L("En 1 hora"), now.addSecs(3600)},
        {L("A las 18:00"), todaySix},
        {L("Mañana 09:00"), QDateTime(now.date().addDays(1), QTime(9, 0))},
    };

    // Los presets guardan el instante real: es lo que dispara el aviso.
    for (const auto &[label, when] : presets) {
        menu->addItem("clock", label, Lang::locale().toString(when, "ddd d MMM HH:mm"),
                      [this, when] {
            applyDue(when.toMSecsSinceEpoch(),
                     Lang::locale().toString(when, "ddd d MMM HH:mm"));
        });
    }

    menu->addSeparator();
    menu->addHeader(L("A mano · dd/MM HH:mm"));
    menu->addEditor(L("p. ej. 24/12 20:30"), dueFieldText(m_note), [this](const QString &value) {
        // Si el texto se puede interpretar como fecha, además suena; si no,
        // se queda como etiqueta suelta (comportamiento de siempre). Cuando sí
        // se entiende se guarda la etiqueta normal, la misma que ponen los
        // presets, y no lo tecleado: así el chip dice lo de siempre y el campo
        // vuelve a abrirse en su formato.
        const QDateTime parsed = parseDue(value);
        applyDue(parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0,
                 parsed.isValid() ? Lang::locale().toString(parsed, "ddd d MMM HH:mm") : value);
    });

    // La repetición vive aquí y no en un menú propio: es parte de "cuándo
    // suena esto", igual que la fecha, y con fecha libre no significa nada.
    if (m_note->isScheduled()) {
        menu->addSeparator();
        menu->addHeader(L("Repetir"));
        const QList<QPair<Note::Repeat, QPair<QString, QString>>> modes = {
            {Note::Once,   {L("No repetir"), L("Suena una vez")}},
            {Note::Weekly, {L("Cada semana"), Lang::locale().toString(m_note->dueAt(), "dddd")}},
            {Note::Yearly, {L("Cada año"), Lang::locale().toString(m_note->dueAt(), "d MMMM")}},
        };
        for (const auto &[mode, text] : modes)
            menu->addItem(m_note->repeat == mode ? "check" : "repeat", text.first, text.second,
                          [this, mode = mode] { setRepeat(mode); });
    }

    if (!m_note->due.isEmpty()) {
        menu->addSeparator();
        menu->addItem("minus", L("Quitar fecha"), QString(), [this] {
            applyDue(0, QString());
        });
    }
    menu->showAt(globalPos);
}

// El chip de repetición y el rótulo del tipo dicen lo mismo por dos caminos;
// el rótulo lo escribe refreshDue(), que también sabe si está sonando.
void NoteCard::refreshRepeat() {
    if (!m_repeatChip) return;
    m_repeatChip->setPixmap(paintIcon("repeat", m_theme.accent, 12).pixmap(12, 12));
    m_repeatChip->setToolTip(m_note->repeats()
                                 ? L("%1 · clic para cambiarlo").arg(m_note->repeatLabel())
                                 : L("Clic para cambiar la repetición"));
    m_repeatChip->setVisible(m_note->repeats());
}

// Mover la fecha de un aviso es también enterarse de él: aplazar no es
// ignorar, así que deja de sonar. Sin esto la nota se llevaba el 'ringing' a
// su fecha nueva -- el tono seguía sonando, el dock seguía rojo y el
// calendario pintaba de rojo, con su botón de parar, un día que aún no ha
// llegado. 'fired' se limpia porque el instante es otro y todavía no ha
// avisado de este.
void NoteCard::applyDue(qint64 whenMs, const QString &label) {
    const bool wasRinging = m_note->ringing;

    m_note->dueAtMs = whenMs;
    m_note->due = label;
    m_note->fired = false;
    m_note->ringing = false;

    if (m_chip) m_chip->setText(m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel());
    refreshRepeat();
    refreshDue();

    // Primero el aviso de que ya no suena: el panel mira el estado de todas
    // las notas y esta ya está puesta al día.
    if (wasRinging) emit rescheduled(m_note);
    emit dirty();
}

void NoteCard::setRepeat(Note::Repeat repeat) {
    m_note->repeat = repeat;
    // Un recordatorio que vuelve no puede quedarse marcado como ya avisado: si
    // no, la próxima vuelta no sonaría. Se recoloca en su siguiente fecha
    // cuando la que tiene ya pasó, y eso es un cambio de fecha como cualquier
    // otro. Dejar de repetirse no lo es: ahí 'fired' se respeta, o un aviso ya
    // dado volvería a sonar por haber tocado el menú.
    if (m_note->repeats()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        applyDue(m_note->dueAtMs <= now ? m_note->nextOccurrenceAfter(now) : m_note->dueAtMs,
                 m_note->due);
        return;
    }
    if (m_chip) m_chip->setText(m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel());
    refreshRepeat();
    refreshDue();
    emit dirty();
}

void NoteCard::focusTitle() {
    if (!m_title) return;
    m_title->setFocus();
    m_title->selectAll();
}

// --- imágenes adjuntas -----------------------------------------------------

// Cabecera plegable y debajo la tira de miniaturas. Cualquier tipo de nota
// puede llevar imágenes, igual que enlaces, así que esto se monta en build()
// y no dentro de una de las ramas por tipo.
void NoteCard::buildImages(QVBoxLayout *l) {
    m_imagesBox = new QWidget;
    auto *col = new QVBoxLayout(m_imagesBox);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(5);

    // LinkRow ya es una fila pulsable con menú propio y fondo de hoja de
    // estilos; aquí solo cambia el nombre con el que la pinta el tema.
    auto *header = new LinkRow;
    header->setObjectName("imgHeader");
    header->activate = [this] { toggleImages(); };
    header->menu = [this](const QPoint &at) { openImageMenu(-1, at); };

    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 2, 2, 2);
    hl->setSpacing(6);

    auto *icon = new QLabel;
    icon->setFixedSize(13, 13);
    icon->setPixmap(paintIcon("image", QColor(Theme::muted()), 13).pixmap(13, 13));
    hl->addWidget(icon, 0, Qt::AlignVCenter);

    m_imagesTitle = new QLabel;
    m_imagesTitle->setObjectName("imgTitle");
    hl->addWidget(m_imagesTitle, 1);

    m_imagesToggle = new QToolButton;
    m_imagesToggle->setIconSize(QSize(12, 12));
    m_imagesToggle->setFixedSize(18, 18);
    m_imagesToggle->setCursor(Qt::PointingHandCursor);
    connect(m_imagesToggle, &QToolButton::clicked, this, &NoteCard::toggleImages);
    hl->addWidget(m_imagesToggle, 0, Qt::AlignVCenter);
    col->addWidget(header);

    m_imagesStrip = new QWidget;
    m_imagesLayout = new QVBoxLayout(m_imagesStrip);
    m_imagesLayout->setContentsMargins(0, 0, 0, 0);
    m_imagesLayout->setSpacing(4);
    col->addWidget(m_imagesStrip);

    const int at = m_meta ? l->indexOf(m_meta) : -1;
    if (at >= 0) l->insertWidget(at, m_imagesBox);
    else l->addWidget(m_imagesBox);
    refreshImages();
}

void NoteCard::refreshImages() {
    if (!m_imagesLayout) return;

    // Ocultar antes de borrar: fuera del layout siguen pintadas hasta que
    // corre deleteLater(), y una miniatura es muy visible.
    while (QLayoutItem *it = m_imagesLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    const int count = int(m_note->images.size());
    m_imagesBox->setVisible(count > 0);
    if (count == 0) return;

    const bool hidden = m_note->imagesHidden;
    m_imagesTitle->setText(count == 1 ? L("1 IMAGEN") : L("%1 IMÁGENES").arg(count));
    m_imagesToggle->setIcon(paintIcon(hidden ? "chevronRight" : "chevronDown",
                                      QColor(Theme::muted()), 12));
    m_imagesToggle->setToolTip(hidden ? L("Mostrar imágenes") : L("Ocultar imágenes"));

    // Plegadas, ni siquiera se construyen: una tarjeta cerrada no tiene por
    // qué cargar en memoria las capturas que no se ven.
    m_imagesStrip->setVisible(!hidden);
    if (hidden) return;

    for (int i = 0; i < count; ++i) {
        auto *thumb = new ImageThumb(Note::imagePath(m_note->images.at(i)));
        thumb->activate = [this, i] {
            if (i < m_note->images.size())
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(Note::imagePath(m_note->images.at(i))));
        };
        thumb->menu = [this, i](const QPoint &at) { openImageMenu(i, at); };
        // La miniatura tiene un tamaño propio: pegada a la izquierda, no
        // centrada en un hueco que ya no llena.
        m_imagesLayout->addWidget(thumb, 0, Qt::AlignLeft);
    }
}

void NoteCard::toggleImages() {
    if (m_note->images.isEmpty()) return;
    m_note->imagesHidden = !m_note->imagesHidden;
    refreshImages();
    emit dirty();
}

// Las imágenes se copian a la carpeta de datos, no se enlazan: una nota tiene
// que seguir enseñando su captura aunque el original se mueva o se borre, y al
// cambiar de carpeta de guardado los adjuntos viajan con las notas.
void NoteCard::addImages() {
    const QStringList picked = QFileDialog::getOpenFileNames(
        this, L("Elegir imágenes"), QDir::homePath(),
        L("Imágenes") + " (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
    if (picked.isEmpty()) return;

    bool added = false;
    for (const QString &source : picked) {
        const QFileInfo info(source);
        const QString suffix = info.suffix().isEmpty() ? "png" : info.suffix().toLower();
        // El nombre lleva la nota y un sello de tiempo: dos ficheros con el
        // mismo nombre de origen no pueden pisarse.
        const QString name = QString("%1-%2.%3")
                                 .arg(m_note->id)
                                 .arg(QDateTime::currentMSecsSinceEpoch() +
                                      m_note->images.size())
                                 .arg(suffix);
        if (!QFile::copy(source, Note::imagePath(name))) continue;
        m_note->images.append(name);
        added = true;
    }
    if (!added) return;

    m_note->imagesHidden = false;   // se acaba de añadir: enseñarla
    refreshImages();
    emit dirty();
}

void NoteCard::openImageMenu(int index, const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Imágenes"));
    menu->addItem("plus", L("Añadir imagen…"), L("Se copia junto a la nota"),
                  [this] { addImages(); });

    if (!m_note->images.isEmpty())
        menu->addItem(m_note->imagesHidden ? "chevronDown" : "chevronRight",
                      m_note->imagesHidden ? L("Mostrar imágenes") : L("Ocultar imágenes"),
                      QString(), [this] { toggleImages(); });

    if (index >= 0 && index < m_note->images.size()) {
        const QString name = m_note->images.at(index);
        menu->addSeparator();
        menu->addItem("image", L("Abrir"), QString(), [name] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(Note::imagePath(name)));
        });
        menu->addItem("trash", L("Quitar imagen"), QString(), [this, index] {
            if (index >= m_note->images.size()) return;
            // El fichero es una copia nuestra: al quitarlo de la nota no queda
            // nadie que lo mire, así que se borra en vez de acumularse.
            QFile::remove(Note::imagePath(m_note->images.at(index)));
            m_note->images.removeAt(index);
            refreshImages();
            emit dirty();
        });
    }
    menu->showAt(globalPos);
}

// --- enlaces adjuntos ------------------------------------------------------

void NoteCard::refreshLinks() {
    if (!m_linksLayout) return;

    // Ocultar antes de borrar: una fila fuera del layout sigue pintada donde
    // estaba, y viva, hasta que corre deleteLater().
    while (QLayoutItem *it = m_linksLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    for (int i = 0; i < m_note->links.size(); ++i) {
        const Link &link = m_note->links.at(i);

        auto *row = new LinkRow;
        row->activate = [this, i] { openLink(i); };
        row->menu = [this, i](const QPoint &at) { openLinkMenu(i, at); };

        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(4, 3, 4, 3);
        rl->setSpacing(6);

        auto *icon = new QLabel;
        icon->setFixedSize(13, 13);
        icon->setPixmap(paintIcon("link", m_theme.accent, 13).pixmap(13, 13));
        rl->addWidget(icon, 0, Qt::AlignVCenter);

        auto *text = new ElidedLabel(linkText(link), m_theme.accent);
        text->setObjectName("linkText");
        text->setUnderlineOnHover(true);
        text->setContextMenuPolicy(Qt::NoContextMenu);
        text->setToolTip(link.url);
        rl->addWidget(text, 1);

        m_linksLayout->addWidget(row);
    }
    m_linksBox->setVisible(!m_note->links.isEmpty());
}

void NoteCard::openLink(int index) {
    if (index < 0 || index >= m_note->links.size()) return;
    QDesktopServices::openUrl(QUrl(m_note->links.at(index).url));
}

void NoteCard::openLinkEditor(int index, const QPoint &globalPos) {
    const bool isNew = index < 0 || index >= m_note->links.size();
    const Link current = isNew ? Link{} : m_note->links.at(index);

    auto *menu = new Popup(m_theme, this);
    menu->addHeader(isNew ? L("Nuevo enlace") : L("Editar enlace"));
    menu->addFields({L("https://ejemplo.com"), L("Nombre (opcional)")},
                    {current.url, current.label},
                    [this, index, isNew](const QStringList &values) {
                        const QString url = normalizedUrl(values.value(0));
                        if (url.isEmpty()) return;          // sin dirección no hay enlace

                        Link link{url, values.value(1)};
                        if (isNew) m_note->links.append(link);
                        else if (index < m_note->links.size()) m_note->links[index] = link;

                        refreshLinks();
                        emit dirty();
                    });
    menu->showAt(globalPos);
}

void NoteCard::openLinkMenu(int index, const QPoint &globalPos) {
    if (index < 0 || index >= m_note->links.size()) return;
    const Link link = m_note->links.at(index);

    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Enlace"));
    menu->addItem("link", L("Abrir"), prettyUrl(link.url), [this, index] { openLink(index); });
    menu->addItem("copy", L("Copiar dirección"), QString(),
                  [link] { QGuiApplication::clipboard()->setText(link.url); });
    menu->addItem("pencil", L("Editar…"), link.label.isEmpty() ? L("Sin nombre") : link.label,
                  [this, index, globalPos] { openLinkEditor(index, globalPos); });
    menu->addSeparator();
    menu->addItem("trash", L("Quitar enlace"), QString(), [this, index] {
        if (index >= m_note->links.size()) return;
        m_note->links.removeAt(index);
        refreshLinks();
        emit dirty();
    });
    menu->showAt(globalPos);
}

void NoteCard::refreshDue() {
    if (m_note->type != Note::Reminder) return;

    const bool ringing = m_note->ringing;
    const bool overdue = m_note->dueAtMs > 0 &&
                         QDateTime::currentMSecsSinceEpoch() >= m_note->dueAtMs;

    if (m_dueIcon)
        m_dueIcon->setPixmap(paintIcon(ringing || overdue ? "bell" : "clock",
                                       QColor(ringing || overdue ? "#ff7a6b" : Theme::muted()),
                                       14)
                                 .pixmap(14, 14));
    if (m_dueBtn) m_dueBtn->setVisible(ringing);
    if (m_chip) {
        // El texto se vuelve a leer de la nota: callar un recordatorio que se
        // repite lo adelanta a su siguiente vuelta, y el chip tiene que
        // enseñar la nueva fecha sin reconstruir la tarjeta.
        const QString label = m_note->dueLabel();
        m_chip->setText(label.isEmpty() ? L("Sin fecha") : label);
        m_chip->setProperty("state", ringing ? "ringing" : (overdue ? "overdue" : ""));
        // Cambiar una propiedad dinámica no repinta solo: hay que repolish.
        m_chip->style()->unpolish(m_chip);
        m_chip->style()->polish(m_chip);
    }
    if (m_meta) {
        // En reposo el rótulo dice cada cuánto vuelve, que es más útil que
        // repetir "RECORDATORIO" al lado de un icono de reloj.
        const QString idle = m_note->repeats() ? m_note->repeatLabel().toUpper()
                                               : L("RECORDATORIO");
        m_meta->setText(ringing ? L("¡AHORA!") : (overdue ? L("VENCIDO") : idle));
    }
    refreshRepeat();
}

// --- nota de voz -----------------------------------------------------------

void NoteCard::buildVoice(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);

    m_recBtn = roundButton("record", QColor("#ff7a6b"), L("Grabar"));
    m_recBtn->setObjectName("recBtn");
    connect(m_recBtn, &QToolButton::clicked, this, &NoteCard::toggleRecord);
    row->addWidget(m_recBtn);

    m_playBtn = roundButton("play", QColor(Theme::fg()), L("Reproducir"));
    m_playBtn->setObjectName("playBtn");
    connect(m_playBtn, &QToolButton::clicked, this, &NoteCard::togglePlay);
    row->addWidget(m_playBtn);

    m_wave = new Waveform;
    m_wave->setAccent(m_theme.accent);
    connect(m_wave, &Waveform::seeked, this, [this](qreal f) {
        if (m_player && m_player->duration() > 0)
            m_player->setPosition(qint64(f * m_player->duration()));
    });
    row->addWidget(m_wave, 1);

    m_progress = new QLabel;
    m_progress->setObjectName("meta");
    row->addWidget(m_progress);
    l->addLayout(row);

    m_meta = new QLabel;
    m_meta->setObjectName("meta");
    l->addWidget(m_meta);

    m_wave->setPeaks(m_note->peaks);
    refreshVoice();

    // Onda ya guardada; si falta (nota de una versión anterior), se calcula
    // leyendo el fichero una sola vez y se persiste. Se aplaza al siguiente
    // ciclo porque dirty() todavía no está conectado durante el constructor
    // —y así construir la lista no se queda leyendo ficheros de disco.
    if (m_note->peaks.isEmpty() && !m_note->audio.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            const WaveScan scan = scanWave(m_note->audioPath());
            if (!scan.ok) return;
            m_note->peaks = scan.peaks;
            m_note->level = int(scan.loudest * 100);
            m_wave->setPeaks(m_note->peaks);
            refreshVoice();
            emit dirty();
        });
    }
}

void NoteCard::ensureAudio() {
    if (m_recorder) return;

    m_recorder = new VoiceRecorder(this);
    connect(m_recorder, &VoiceRecorder::durationChanged, this, [this](qint64 ms) {
        m_note->durationMs = ms;
        if (m_wave) m_wave->setPeaks(m_recorder->peaks());
        refreshVoice();
    });
    connect(m_recorder, &VoiceRecorder::finished, this, [this](bool ok) {
        if (ok) {
            m_note->audio = m_note->id + ".wav";
            m_note->peaks = m_recorder->peaks();
            m_note->level = int(m_recorder->loudest() * 100);
            if (m_wave) {
                m_wave->setLive(false);
                m_wave->setPeaks(m_note->peaks);
            }
            // Regrabar reescribe el mismo fichero: si no se suelta la fuente,
            // el reproductor sigue sirviendo la toma anterior.
            if (m_player) m_player->setSource(QUrl());
            emit dirty();
        }
        refreshVoice();
    });

    m_player = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOut);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        const qint64 total = m_player->duration();
        if (m_wave && total > 0) m_wave->setProgress(qreal(pos) / total);
        if (m_progress) m_progress->setText(formatMs(pos));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState && m_wave) m_wave->setProgress(0);
                refreshVoice();
            });
}

void NoteCard::toggleRecord() {
    ensureAudio();

    if (m_recorder->isRecording()) {
        m_recorder->stop();
        return;
    }

    if (m_player) m_player->stop();
    m_note->durationMs = 0;
    m_note->peaks.clear();
    if (m_wave) {
        m_wave->setLive(true);
        m_wave->setPeaks({});
    }
    // Un fichero por nota: volver a grabar sustituye la toma anterior.
    m_recorder->start(audioDir() + "/" + m_note->id + ".wav");
    refreshVoice();
}

void NoteCard::togglePlay() {
    if (m_note->audio.isEmpty()) return;
    ensureAudio();

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        refreshVoice();
        return;
    }
    if (m_player->source().isEmpty())
        m_player->setSource(QUrl::fromLocalFile(m_note->audioPath()));
    m_player->play();
    refreshVoice();
}

void NoteCard::refreshVoice() {
    const bool recording = m_recorder && m_recorder->isRecording();
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const bool hasAudio = !m_note->audio.isEmpty();

    if (m_recBtn) {
        m_recBtn->setIcon(paintIcon(recording ? "stop" : "record", QColor("#ff7a6b")));
        m_recBtn->setToolTip(recording ? L("Detener") : (hasAudio ? L("Regrabar") : L("Grabar")));
    }
    if (m_playBtn) {
        m_playBtn->setIcon(paintIcon(playing ? "pause" : "play",
                                     QColor(hasAudio && !recording ? Theme::fg() : Theme::muted())));
        m_playBtn->setEnabled(hasAudio && !recording);
    }
    if (m_progress) m_progress->setText(formatMs(m_note->durationMs));

    if (!m_meta) return;
    if (recording) {
        m_meta->setText(L("GRABANDO…"));
    } else if (!hasAudio) {
        const QString err = m_recorder ? m_recorder->errorText() : QString();
        m_meta->setText(err.isEmpty() ? L("VOZ · SIN GRABAR") : err.toUpper());
    } else if (m_note->isSilentTake()) {
        // La toma existe pero no tiene señal audible; sin decirlo, el usuario
        // solo ve una nota que "no suena".
        m_meta->setText(L("VOZ · SIN SEÑAL, REVISA EL MICRÓFONO"));
    } else {
        m_meta->setText(L("VOZ"));
    }
}

// ---------------------------------------------------------------------------

void NoteCard::contextMenuEvent(QContextMenuEvent *e) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(typeLabel(m_note->type));

    if (m_note->type == Note::Check && m_newItem) {
        menu->addItem("plus", L("Añadir elemento"), L("Enter para confirmar"),
                      [this] { m_newItem->setFocus(); });
        menu->addSeparator();
    }
    if (m_note->type == Note::Reminder) {
        const QPoint at = e->globalPos();
        if (m_note->ringing)
            menu->addItem("stop", L("Detener aviso"), L("Silencia la alarma"),
                          [this] { emit dismissRequested(m_note); });
        menu->addItem("clock", L("Cambiar fecha"),
                      m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel(),
                      [this, at] { openDuePopup(at); });
        menu->addItem("repeat", L("Repetir"),
                      m_note->repeats() ? m_note->repeatLabel() : L("Suena una vez"),
                      [this, at] { openDuePopup(at); });
        if (m_note->body.isEmpty())
            menu->addItem("text", L("Añadir detalles"), L("Escribe bajo la fecha"),
                          [this] { showDetailsEditor(true); });
        menu->addSeparator();
    }
    if (m_note->type == Note::Voice) {
        menu->addItem("record", m_note->audio.isEmpty() ? L("Grabar") : L("Regrabar"),
                      L("Sustituye la toma actual"), [this] { toggleRecord(); });
        menu->addSeparator();
    }

    const QPoint at = e->globalPos();
    menu->addItem("link", L("Añadir enlace…"),
                  m_note->links.isEmpty()
                      ? L("Se abre en el navegador")
                      : L("%1 ya adjuntos").arg(m_note->links.size()),
                  [this, at] { openLinkEditor(-1, at); });
    menu->addItem("image", L("Añadir imagen…"),
                  m_note->images.isEmpty()
                      ? L("Se copia junto a la nota")
                      : L("%1 ya adjuntas").arg(m_note->images.size()),
                  [this] { addImages(); });
    if (!m_note->images.isEmpty())
        menu->addItem(m_note->imagesHidden ? "chevronDown" : "chevronRight",
                      m_note->imagesHidden ? L("Mostrar imágenes") : L("Ocultar imágenes"),
                      QString(), [this] { toggleImages(); });

    // Reordenar también desde aquí: arrastrar por el asidero es lo cómodo con
    // el ratón, pero con la lista larga (o sin ganas de arrastrar) un paso
    // cada vez llega más lejos sin pelearse con el scroll.
    menu->addSeparator();
    menu->addHeader(L("Orden"));
    menu->addItem("chevronUp", L("Subir"), QString(),
                  [this] { emit moveRequested(m_note, -1); });
    menu->addItem("chevronDown", L("Bajar"), QString(),
                  [this] { emit moveRequested(m_note, 1); });

    menu->addSeparator();
    menu->addItem("trash", L("Eliminar nota"), QString(),
                  [this] { emit deleteRequested(m_note); });
    menu->showAt(at);
}
