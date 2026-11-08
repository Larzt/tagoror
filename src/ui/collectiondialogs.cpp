#include "ui/collectiondialogs.hpp"

#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/coverimage.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"
#include "ui/overlay.hpp"
#include "ui/popup.hpp"
#include "ui/switch.hpp"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QImageReader>
#include <QMimeData>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

namespace {

/// Its size hint is its content's: the Overlay sizes the dialog from it, and
/// only when the panel is too short does the form scroll (as in Welcome).
class FitScroll : public QScrollArea {
public:
    QSize sizeHint() const override {
        return widget() ? widget()->sizeHint() + QSize(2 * frameWidth(), 2 * frameWidth())
                        : QScrollArea::sizeHint();
    }
};

QScrollArea *scrolled(QWidget *form) {
    auto *scroll = new FitScroll;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(form);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("welcomeViewport");
    form->setObjectName("welcomeList");
    return scroll;
}

QLabel *fieldLabel(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("setFieldLabel");
    return l;
}

QLabel *caption(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("pageSection");
    l->setContentsMargins(0, 0, 0, 0);
    return l;
}

QWidget *labelled(const QString &label, QWidget *field) {
    auto *box = new QWidget;
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    col->addWidget(fieldLabel(label));
    col->addWidget(field);
    return box;
}

QLineEdit *field(const QString &text, bool mono = false) {
    auto *e = new QLineEdit(text);
    e->setObjectName("setField");
    e->setMinimumWidth(30);
    if (mono) {
        QFont f = e->font();
        f.setFamily("IBM Plex Mono");
        e->setFont(f);
    }
    return e;
}

QToolButton *button(const char *name, const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName(name);
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

void setChosen(QToolButton *b, bool chosen) {
    b->setProperty("chosen", chosen);
    b->style()->unpolish(b);
    b->style()->polish(b);
}

/// A segmented control: one of @p labels chosen.
QFrame *segments(const QStringList &labels, QList<QToolButton *> *out) {
    auto *track = new QFrame;
    track->setObjectName("segTrack");
    auto *l = new QHBoxLayout(track);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(2);
    for (const QString &text : labels) {
        auto *b = button("segOption", text);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumWidth(24);
        l->addWidget(b, 1);
        out->append(b);
    }
    return track;
}

/// The cover on the card: its picture or its colour. A click opens what can be
/// done with it — choose a picture, remove it, change the colour — and an
/// image file dropped on it becomes the picture. Nothing is copied until
/// Guardar: a picture chosen and then cancelled leaves no file behind.
class CoverButton : public QToolButton {
public:
    int color = 0;
    QString title;
    QString current;   ///< The saved picture's path; empty: none.
    QString picked;    ///< A picture chosen now, to import on saving.
    bool cleared = false;

    explicit CoverButton(const Theme *theme) : m_theme(theme) {
        setFixedSize(58, 84);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAcceptDrops(true);
        setToolTip(L("Portada: elegir imagen o color"));
        connect(this, &QToolButton::clicked, this, [this] { openMenu(); });
    }

    /// What is shown: the picture chosen now, the saved one, or none.
    QString shown() const {
        if (!picked.isEmpty()) return picked;
        return cleared ? QString() : current;
    }

    /// Whether @p path is a picture Qt can read.
    static bool readable(const QString &path) {
        return !path.isEmpty() && QImageReader(path).canRead();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 4, 4);
        const bool picture = coverimage::draw(p, path, QRectF(rect()), shown());
        if (!picture) {
            p.fillPath(path, CollectionItem::coverColor(color));
            p.save();
            p.setClipPath(path);
            p.fillRect(QRectF(0, 0, 4, height()), QColor(0, 0, 0, 72));
            p.restore();
        }
        if (keynav::showsFocus(this) || m_dropping) {
            p.setPen(QPen(m_dropping ? m_theme->accent : QColor(Theme::fg()), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
        }
        if (picture) return;
        QFont f = font();
        f.setPixelSize(9);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 216));
        p.drawText(QRectF(rect()).adjusted(10, 7, -6, -7), Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap,
                   title.simplified().split(' ', Qt::SkipEmptyParts).mid(0, 3).join(' '));
    }

    void dragEnterEvent(QDragEnterEvent *e) override {
        if (droppedFile(e->mimeData()).isEmpty()) return;
        e->acceptProposedAction();
        m_dropping = true;
        update();
    }
    void dragLeaveEvent(QDragLeaveEvent *) override {
        m_dropping = false;
        update();
    }
    void dropEvent(QDropEvent *e) override {
        m_dropping = false;
        const QString file = droppedFile(e->mimeData());
        if (!file.isEmpty()) {
            pick(file);
            e->acceptProposedAction();
        }
        update();
    }

private:
    const Theme *m_theme;
    bool m_dropping = false;

    static QString droppedFile(const QMimeData *data) {
        if (!data || !data->hasUrls()) return QString();
        for (const QUrl &url : data->urls())
            if (url.isLocalFile() && readable(url.toLocalFile())) return url.toLocalFile();
        return QString();
    }

    void pick(const QString &file) {
        picked = file;
        cleared = false;
        update();
    }

    void openMenu() {
        auto *menu = new Popup(*m_theme, this);
        menu->addHeader(L("Portada"));
        QPointer<CoverButton> self(this);
        menu->addItem("image", shown().isEmpty() ? L("Elegir imagen…") : L("Cambiar imagen…"),
                      L("O arrástrala aquí"), [self] {
            if (!self) return;
            const QString file = QFileDialog::getOpenFileName(
                self->window(), L("Elegir imagen"), QDir::homePath(),
                L("Imágenes") + " (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
            if (self && readable(file)) self->pick(file);
        });
        if (!shown().isEmpty()) {
            menu->addItem("trash", L("Quitar imagen"), L("Vuelve al color"), [self] {
                if (!self) return;
                self->picked.clear();
                self->cleared = true;
                self->update();
            });
        } else {
            menu->addItem("palette", L("Cambiar el color"), QString(), [self] {
                if (!self) return;
                self->color = (self->color + 1) % CollectionItem::kCovers;
                self->update();
            });
        }
        menu->showUnder(this);
    }
};

QString dateText(qint64 ms) {
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).date().toString("dd/MM/yyyy") : QString();
}

/// "30/09/2026", "30/9/26" or empty (today). -1 if unreadable.
qint64 parseDate(const QString &text, qint64 fallback) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return fallback;
    for (const char *fmt : {"d/M/yyyy", "d/M/yy", "d-M-yyyy", "yyyy-M-d"}) {
        QDate d = QDate::fromString(t, QString::fromLatin1(fmt));
        if (d.isValid()) {
            if (d.year() < 100) d = d.addYears(2000);
            // Keep the time of day it had, so re-saving the same date changes nothing.
            const QTime time = fallback > 0 ? QDateTime::fromMSecsSinceEpoch(fallback).time() : QTime(12, 0);
            return QDateTime(d, time).toMSecsSinceEpoch();
        }
    }
    return -1;
}

}  // namespace

namespace colldlg {

// ---------------------------------------------------------------------------
// The item's card

void editItem(const Theme *theme, QWidget *host, Store *store, Collection *c, CollectionItem *item,
              const ItemHooks &hooks) {
    if (!store || !c) return;
    const bool isNew = item == nullptr;
    const QString itemName = L(c->itemName());
    auto *ov = new Overlay(theme, host, isNew ? L("Añadir %1").arg(itemName) : L("Editar %1").arg(itemName));
    ov->setMaxWidth(380);

    // What the form edits: a copy, written back only on Guardar.
    auto draft = std::make_shared<CollectionItem>(item ? *item : CollectionItem());
    if (isNew) {
        draft->collection = c->id;
        draft->state = c->firstState();
        // A new book takes the next cover colour after the last one added.
        int last = -1;
        for (const CollectionItem *i : store->collectionItems())
            if (i->collection == c->id) last = i->color;
        draft->color = (last + 1) % CollectionItem::kCovers;
    }
    const QString itemId = item ? item->id : QString();
    const QString collId = c->id;

    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 4, 0);
    col->setSpacing(12);

    // Cover, title and author.
    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    auto *cover = new CoverButton(theme);
    cover->color = draft->color;
    cover->current = draft->coverPath();
    cover->title = draft->title;
    top->addWidget(cover, 0, Qt::AlignTop);
    auto *names = new QVBoxLayout;
    names->setSpacing(8);
    QLineEdit *title = field(draft->title);
    QFont tf = title->font();
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    title->setPlaceholderText(L("Título"));
    QObject::connect(title, &QLineEdit::textChanged, cover, [cover](const QString &t) {
        cover->title = t;
        cover->update();
    });
    names->addWidget(labelled(L("Título"), title));
    QLineEdit *author = nullptr;
    QLineEdit *authorKey = nullptr;
    if (c->has(Collection::kAuthor)) {
        author = field(draft->author);
        names->addWidget(labelled(L(c->authorLabel()), author));
    }
    names->addStretch();
    top->addLayout(names, 1);
    col->addLayout(top);
    if (author) {
        // What it is sorted by: the last word unless written here, since that fails
        // with "Liu Cixin" or "García Márquez".
        authorKey = field(draft->authorSortKey);
        authorKey->setPlaceholderText(draft->sortAuthor());
        QObject::connect(author, &QLineEdit::textChanged, authorKey, [authorKey](const QString &a) {
            CollectionItem probe;
            probe.author = a;
            authorKey->setPlaceholderText(probe.sortAuthor());
        });
        col->addWidget(labelled(L("Se ordena por"), authorKey));
    }

    // Year, length and the day it was added, side by side.
    auto *numbers = new QGridLayout;
    numbers->setHorizontalSpacing(8);
    numbers->setVerticalSpacing(0);
    int column = 0;
    QLineEdit *year = nullptr, *total = nullptr;
    if (c->has(Collection::kYear)) {
        year = field(draft->year > 0 ? QString::number(draft->year) : QString(), true);
        year->setValidator(new QIntValidator(0, 9999, year));
        numbers->addWidget(labelled(L("Publicación"), year), 0, column++);
    }
    if (c->has(Collection::kProgress)) {
        total = field(draft->total > 0 ? QString::number(draft->total) : QString(), true);
        total->setValidator(new QIntValidator(0, 999999, total));
        QString unit = L(c->unitName());
        unit = unit.left(1).toUpper() + unit.mid(1);
        numbers->addWidget(labelled(unit, total), 0, column++);
    }
    QLineEdit *added = field(dateText(draft->addedMs > 0 ? draft->addedMs : QDateTime::currentMSecsSinceEpoch()), true);
    added->setPlaceholderText(QStringLiteral("dd/mm/aaaa"));
    numbers->addWidget(labelled(L("Añadido"), added), 0, column++);
    for (int i = 0; i < column; ++i) numbers->setColumnStretch(i, 1);
    col->addLayout(numbers);

    QLineEdit *isbn = nullptr;
    if (c->has(Collection::kIsbn)) {
        isbn = field(draft->isbn, true);
        col->addWidget(labelled(QStringLiteral("ISBN"), isbn));
    }
    // A few lines, so a box that wraps rather than a one-line field.
    QPlainTextEdit *synopsis = nullptr;
    if (c->has(Collection::kSynopsis)) {
        synopsis = new QPlainTextEdit(draft->synopsis);
        synopsis->setObjectName("setFieldArea");
        synopsis->setTabChangesFocus(true);
        synopsis->setFixedHeight(84);
        synopsis->setPlaceholderText(L("De qué va, en unas líneas"));
        col->addWidget(labelled(L("Sinopsis"), synopsis));
    }
    QList<QPair<QString, QLineEdit *>> customs;
    for (const CollectionField &f : c->custom) {
        QLineEdit *e = field(draft->extra.value(f.key).toString());
        col->addWidget(labelled(f.name, e));
        customs.append({f.key, e});
    }

    // State, as a segmented control; the progress block only while in progress.
    QList<QToolButton *> stateBtns;
    QStringList stateNames;
    for (const CollectionState &s : c->states) stateNames << L(s.name);
    auto *stateBox = new QWidget;
    auto *sv = new QVBoxLayout(stateBox);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(5);
    sv->addWidget(fieldLabel(L("Estado")));
    sv->addWidget(segments(stateNames, &stateBtns));
    col->addWidget(stateBox);

    auto *progressBox = new QFrame;
    progressBox->setObjectName("timerHero");
    auto *pv = new QVBoxLayout(progressBox);
    pv->setContentsMargins(10, 10, 10, 10);
    pv->setSpacing(6);
    auto *pageRow = new QHBoxLayout;
    pageRow->addWidget(fieldLabel(L("Voy por la página")), 1);
    QLineEdit *page = field(draft->progress > 0 ? QString::number(draft->progress) : QString(), true);
    page->setValidator(new QIntValidator(0, 999999, page));
    page->setFixedWidth(70);
    page->setAlignment(Qt::AlignRight);
    pageRow->addWidget(page);
    auto *ofTotal = new QLabel;
    ofTotal->setObjectName("timerSub");
    pageRow->addWidget(ofTotal);
    pv->addLayout(pageRow);
    auto *started = new QLabel;
    started->setObjectName("timerSub");
    pv->addWidget(started);
    col->addWidget(progressBox);

    // The day it was finished, once finished (1.3.6): what the yearly goal
    // counts by. Empty is "no date", for something finished long ago.
    QLineEdit *finished = field(dateText(draft->finishedMs), true);
    finished->setPlaceholderText(L("Sin fecha · dd/mm/aaaa"));
    auto *finishedBox = labelled(L("Terminado el"), finished);
    col->addWidget(finishedBox);

    // Rating, once finished.
    QList<QToolButton *> ratingBtns;
    auto *ratingBox = new QWidget;
    if (c->has(Collection::kRating)) {
        auto *rv = new QVBoxLayout(ratingBox);
        rv->setContentsMargins(0, 0, 0, 0);
        rv->setSpacing(5);
        rv->addWidget(fieldLabel(L("Valoración")));
        rv->addWidget(segments({QStringLiteral("—"), "1", "2", "3", "4", "5"}, &ratingBtns));
        col->addWidget(ratingBox);
    }

    QList<QToolButton *> prioBtns;
    if (c->has(Collection::kPriority)) {
        auto *pb = new QWidget;
        auto *pl = new QVBoxLayout(pb);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(5);
        pl->addWidget(fieldLabel(L("Prioridad en la cola")));
        pl->addWidget(segments({L("Baja"), L("Media"), L("Alta")}, &prioBtns));
        col->addWidget(pb);
    }

    // Tags: chips that go away on a click, and a field that adds one on Enter.
    auto *tagBox = new QWidget;
    auto *tagCol = new QVBoxLayout(tagBox);
    tagCol->setContentsMargins(0, 0, 0, 0);
    tagCol->setSpacing(5);
    tagCol->addWidget(fieldLabel(L("Etiquetas")));
    auto *chips = new QWidget;
    auto *chipGrid = new QGridLayout(chips);
    chipGrid->setContentsMargins(0, 0, 0, 0);
    chipGrid->setSpacing(5);
    tagCol->addWidget(chips);
    QLineEdit *newTag = field(QString());
    newTag->setPlaceholderText(L("+ etiqueta · Intro"));
    tagCol->addWidget(newTag);
    col->addWidget(tagBox);
    auto tags = std::make_shared<QStringList>(draft->tags);
    auto rebuildTags = std::make_shared<std::function<void()>>();
    *rebuildTags = [chips, chipGrid, tags, rebuildTags] {
        while (QLayoutItem *it = chipGrid->takeAt(0)) {
            if (QWidget *w = it->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete it;
        }
        for (int i = 0; i < tags->size(); ++i) {
            const QString tag = tags->at(i);
            auto *chip = button("trackChip", tag + "  ×");
            chip->setToolTip(L("Quitar «%1»").arg(tag));
            QObject::connect(chip, &QToolButton::clicked, chips, [tags, tag, rebuildTags] {
                tags->removeAll(tag);
                (*rebuildTags)();
            });
            chipGrid->addWidget(chip, i / 3, i % 3, Qt::AlignLeft);
        }
        chipGrid->setColumnStretch(3, 1);
        chips->setVisible(!tags->isEmpty());
    };
    (*rebuildTags)();
    QObject::connect(newTag, &QLineEdit::returnPressed, newTag, [newTag, tags, rebuildTags] {
        const QString t = newTag->text().simplified();
        newTag->clear();
        if (t.isEmpty() || tags->contains(t)) return;
        tags->append(t);
        (*rebuildTags)();
    });

    // The reading notes: a Markdown note of its own, opened on its page.
    QToolButton *noteBtn = nullptr;
    if (hooks.openNote) {
        const QString linked = hooks.noteTitle ? hooks.noteTitle(draft->noteId) : QString();
        noteBtn = button("trackRecent", linked.isEmpty() ? L("Crear una nota de lectura") : linked);
        noteBtn->setIcon(paintIcon("markdown", QColor(Theme::muted()), 14));
        noteBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        noteBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        noteBtn->setMinimumWidth(24);
        noteBtn->setToolTip(isNew ? L("Se crea al guardar") : L("Nota Markdown, en el área abierta"));
        col->addWidget(labelled(L("Notas"), noteBtn));
    }

    auto *warn = new QLabel;
    warn->setObjectName("trackWarn");
    warn->setWordWrap(true);
    warn->setMinimumWidth(24);
    warn->hide();
    col->addWidget(warn);

    // Shared state of the form.
    auto stateIdx = std::make_shared<int>(qMax(0, c->stateIndex(draft->state)));
    auto prio = std::make_shared<int>(draft->priority);
    auto rating = std::make_shared<int>(draft->rating);
    const auto refreshState = [=] {
        for (int i = 0; i < stateBtns.size(); ++i) setChosen(stateBtns.at(i), i == *stateIdx);
        for (int i = 0; i < prioBtns.size(); ++i) setChosen(prioBtns.at(i), i + 1 == *prio);
        for (int i = 0; i < ratingBtns.size(); ++i) setChosen(ratingBtns.at(i), i == *rating);
        const QString sid = c->states.value(*stateIdx).id;
        progressBox->setVisible(c->has(Collection::kProgress) && c->isInProgress(sid));
        ratingBox->setVisible(c->has(Collection::kRating) && c->isFinal(sid));
        finishedBox->setVisible(c->isFinal(sid));
        // Moved to finished here: today, as the Terminado button would. One
        // that already was keeps what it had, even no date.
        if (c->isFinal(sid) && !c->isFinal(draft->state) && finished->text().isEmpty())
            finished->setText(dateText(QDateTime::currentMSecsSinceEpoch()));
        const int t = total ? total->text().toInt() : 0;
        const int p = page->text().toInt();
        ofTotal->setText(t > 0 ? QString("/ %1").arg(t) : QString());
        QStringList parts;
        if (draft->startedMs > 0) parts << L("Empezado %1").arg(Coll::shortDate(draft->startedMs));
        if (t > 0) parts << QString("%1%").arg(qBound(0, qRound(p * 100.0 / t), 100));
        started->setText(parts.join(" · "));
        ov->relayout();
    };
    for (int i = 0; i < stateBtns.size(); ++i)
        QObject::connect(stateBtns.at(i), &QToolButton::clicked, ov, [=] {
            *stateIdx = i;
            refreshState();
        });
    for (int i = 0; i < prioBtns.size(); ++i)
        QObject::connect(prioBtns.at(i), &QToolButton::clicked, ov, [=] {
            *prio = i + 1;
            refreshState();
        });
    for (int i = 0; i < ratingBtns.size(); ++i)
        QObject::connect(ratingBtns.at(i), &QToolButton::clicked, ov, [=] {
            *rating = i;
            refreshState();
        });
    if (total) QObject::connect(total, &QLineEdit::textChanged, ov, [=] { refreshState(); });
    QObject::connect(page, &QLineEdit::textChanged, ov, [=] { refreshState(); });

    // Footer.
    auto *content = new QWidget;
    auto *outer = new QVBoxLayout(content);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);
    outer->addWidget(scrolled(form), 1);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    QToolButton *del = nullptr;
    if (!isNew) {
        del = button("quitBtn", L("Eliminar"));
        buttons->addWidget(del);
    }
    buttons->addStretch();
    auto *cancel = button("pickCancel", L("Cancelar"));
    auto *save = button("pickSave", L("Guardar"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    outer->addLayout(buttons);
    ov->setContent(content);

    QPointer<Overlay> guard(ov);
    // Writes the form into the item (a new one is created here). @return it, or
    // null if something does not read.
    const auto commit = [=]() -> CollectionItem * {
        Collection *coll = store->collection(collId);
        if (!coll) return nullptr;
        const QString name = title->text().simplified();
        if (name.isEmpty()) {
            warn->setText(L("Falta el título."));
            warn->show();
            ov->relayout();
            title->setFocus();
            return nullptr;
        }
        const qint64 addedMs = parseDate(added->text(), draft->addedMs > 0 ? draft->addedMs
                                                                           : QDateTime::currentMSecsSinceEpoch());
        if (addedMs < 0) {
            warn->setText(L("La fecha de añadido tiene que ser dd/mm/aaaa."));
            warn->show();
            ov->relayout();
            added->setFocus();
            return nullptr;
        }
        // -1: unreadable; 0: no date.
        const qint64 finishedMs = finished->text().trimmed().isEmpty()
                                      ? 0
                                      : parseDate(finished->text(), draft->finishedMs > 0
                                                                        ? draft->finishedMs
                                                                        : QDateTime::currentMSecsSinceEpoch());
        if (coll->isFinal(coll->states.value(*stateIdx).id) && finishedMs < 0) {
            warn->setText(L("La fecha de fin tiene que ser dd/mm/aaaa."));
            warn->show();
            ov->relayout();
            finished->setFocus();
            return nullptr;
        }
        CollectionItem *target = itemId.isEmpty() ? nullptr : store->collectionItem(itemId);
        if (!itemId.isEmpty() && !target) return nullptr;   // deleted meanwhile (a sync)
        const bool fresh = !target;
        if (fresh) {
            target = new CollectionItem(*draft);
            target->addedMs = addedMs;
        }
        target->title = name;
        if (author) target->author = author->text().simplified();
        if (authorKey) target->authorSortKey = authorKey->text().simplified();
        if (year) target->year = year->text().toInt();
        if (total) target->total = total->text().toInt();
        if (isbn) target->isbn = isbn->text().trimmed();
        if (synopsis) target->synopsis = synopsis->toPlainText().trimmed();
        // The picture: copied in now, the one it replaces deleted.
        if (!cover->picked.isEmpty()) {
            const QString imported = Coll::importCover(cover->picked, target->id);
            if (!imported.isEmpty()) {
                if (!fresh) Coll::removeCover(target->cover);
                target->cover = imported;
                cover->picked.clear();   // saved: a second commit does not import it again
                cover->current = target->coverPath();
            }
        } else if (cover->cleared && !fresh) {
            Coll::removeCover(target->cover);
            target->cover.clear();
        }
        for (const auto &[key, e] : customs) {
            if (e->text().trimmed().isEmpty()) target->extra.remove(key);
            else target->extra[key] = e->text().trimmed();
        }
        target->color = cover->color;
        target->priority = *prio;
        target->rating = *rating;
        target->tags = *tags;
        target->addedMs = addedMs;
        const QString sid = coll->states.value(*stateIdx).id;
        if (fresh) {
            // In the queue first, so setState() records when it started or finished.
            target->state = coll->firstState();
            target->startedMs = target->finishedMs = 0;
            Coll::setState(target, *coll, sid);
            // A new book joins the end of its priority in the queue.
            double last = -1;
            for (const CollectionItem *i : store->collectionItems())
                if (i->collection == coll->id) last = qMax(last, i->queuePos);
            target->queuePos = last + 1;
        } else {
            Coll::setState(target, *coll, sid);
        }
        if (coll->isFinal(target->state)) target->finishedMs = qMax<qint64>(0, finishedMs);
        if (coll->has(Collection::kProgress) && coll->isInProgress(target->state)) {
            int p = page->text().toInt();
            if (target->total > 0) p = qMin(p, target->total);
            target->progress = qMax(0, p);
        }
        if (fresh) store->addCollectionItem(target);
        return target;
    };
    QObject::connect(save, &QToolButton::clicked, ov, [=] {
        if (!commit()) return;
        if (hooks.saved) hooks.saved();
        if (guard) guard->close();
    });
    QObject::connect(cancel, &QToolButton::clicked, ov, [guard] {
        if (guard) guard->close();
    });
    for (QLineEdit *e : form->findChildren<QLineEdit *>())
        if (e != newTag)
            QObject::connect(e, &QLineEdit::returnPressed, save, &QToolButton::click);
    if (noteBtn)
        QObject::connect(noteBtn, &QToolButton::clicked, ov, [=] {
            // The item has to exist before a note can point at it.
            CollectionItem *saved = commit();
            if (!saved) return;
            if (hooks.saved) hooks.saved();
            if (guard) guard->close();
            hooks.openNote(saved);
        });
    if (del) {
        // Twice: the first click asks, the second deletes.
        auto armed = std::make_shared<bool>(false);
        QObject::connect(del, &QToolButton::clicked, ov, [=] {
            if (!*armed) {
                *armed = true;
                del->setText(L("¿Eliminar? Otra vez"));
                return;
            }
            if (CollectionItem *target = store->collectionItem(itemId)) store->removeCollectionItem(target);
            if (hooks.saved) hooks.saved();
            if (guard) guard->close();
        });
    }

    refreshState();
    ov->open(title);
}

// ---------------------------------------------------------------------------
// The collection

namespace {

struct FieldInfo {
    const char *key;
    const char *name;
    const char *hint;
};

const QList<FieldInfo> &fieldInfo() {
    static const QList<FieldInfo> list{
        {Collection::kAuthor, "Autor", "texto · ordena por apellido"},
        {Collection::kYear, "Año de publicación", "número"},
        {Collection::kProgress, "Progreso", "páginas, episodios…"},
        {Collection::kPriority, "Prioridad", "baja · media · alta"},
        {Collection::kRating, "Valoración", "0–5"},
        {Collection::kIsbn, "ISBN", "texto"},
        {Collection::kSynopsis, "Sinopsis", "texto largo"},
    };
    return list;
}

}  // namespace

void editCollection(const Theme *theme, QWidget *host, Store *store, Collection *c,
                    std::function<void(Collection *)> saved, std::function<void()> deleted) {
    if (!store) return;
    const bool isNew = c == nullptr;
    auto *ov = new Overlay(theme, host, isNew ? L("Nueva colección") : L("Editar colección"));
    ov->setMaxWidth(380);
    const QString collId = c ? c->id : QString();

    // The draft: a template's for a new one, a copy otherwise.
    auto draft = std::make_shared<Collection>();
    if (c) {
        *draft = *c;
    } else {
        Collection *t = Collection::fromTemplate(Collection::Books, QString());
        *draft = *t;
        delete t;
    }
    auto nameTouched = std::make_shared<bool>(!isNew);

    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 4, 0);
    col->setSpacing(14);

    QLineEdit *name = field(c ? c->name : L("Biblioteca"));
    QFont nf = name->font();
    nf.setWeight(QFont::DemiBold);
    name->setFont(nf);
    QObject::connect(name, &QLineEdit::textEdited, name, [nameTouched] { *nameTouched = true; });
    col->addWidget(labelled(L("Nombre"), name));

    // Templates: only for a new one; the states and fields they bring can be
    // changed below either way.
    QList<QToolButton *> templateBtns;
    if (isNew) {
        auto *tb = new QWidget;
        auto *tv = new QVBoxLayout(tb);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(6);
        tv->addWidget(caption(L("Plantilla")));
        auto *grid = new QGridLayout;
        grid->setSpacing(6);
        for (int i = 0; i < Collection::kTemplates; ++i) {
            const auto t = Collection::Template(i);
            auto *b = button("segOption", L(Collection::templateName(t)));
            b->setIcon(paintIcon(Collection::templateIcon(t), QColor(Theme::muted()), 16));
            b->setIconSize(QSize(16, 16));
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setMinimumWidth(24);
            b->setFixedHeight(48);
            grid->addWidget(b, i / 3, i % 3);
            templateBtns << b;
        }
        auto *gridFrame = new QFrame;
        gridFrame->setObjectName("segTrack");
        gridFrame->setLayout(grid);
        grid->setContentsMargins(3, 3, 3, 3);
        tv->addWidget(gridFrame);
        col->addWidget(tb);
    }

    // States, one row each: name, "en curso", up, down, remove.
    auto *statesBox = new QWidget;
    auto *stv = new QVBoxLayout(statesBox);
    stv->setContentsMargins(0, 0, 0, 0);
    stv->setSpacing(6);
    auto *stHead = new QHBoxLayout;
    stHead->addWidget(caption(L("Estados · pestañas")), 1);
    auto *addState = button("pickCancel", QStringLiteral("+"));
    addState->setToolTip(L("Añadir estado"));
    stHead->addWidget(addState);
    stv->addLayout(stHead);
    auto *stateRows = new QWidget;
    auto *stateList = new QVBoxLayout(stateRows);
    stateList->setContentsMargins(0, 0, 0, 0);
    stateList->setSpacing(4);
    stv->addWidget(stateRows);
    auto *stateHint = new QLabel(L("El estado «en curso» muestra la tarjeta con progreso; el último "
                                   "guarda la fecha en que lo terminaste."));
    stateHint->setObjectName("popupText");
    stateHint->setWordWrap(true);
    stateHint->setMinimumWidth(24);
    stv->addWidget(stateHint);
    col->addWidget(statesBox);

    // Fields: the built-in ones as switches, then the user's own.
    auto *fieldsBox = new QWidget;
    auto *fv = new QVBoxLayout(fieldsBox);
    fv->setContentsMargins(0, 0, 0, 0);
    fv->setSpacing(2);
    fv->addWidget(caption(L("Campos")));
    auto *fieldRows = new QWidget;
    auto *fieldList = new QVBoxLayout(fieldRows);
    fieldList->setContentsMargins(0, 0, 0, 0);
    fieldList->setSpacing(2);
    fv->addWidget(fieldRows);
    auto *customRows = new QWidget;
    auto *customList = new QVBoxLayout(customRows);
    customList->setContentsMargins(0, 0, 0, 0);
    customList->setSpacing(4);
    fv->addWidget(customRows);
    auto *addField = button("noticeAction", L("+ Campo propio"));
    fv->addWidget(addField, 0, Qt::AlignLeft);
    col->addWidget(fieldsBox);

    // The yearly goal, for the wide mode's RETO.
    QLineEdit *goal = field(draft->goal > 0 ? QString::number(draft->goal) : QString(), true);
    goal->setValidator(new QIntValidator(0, 9999, goal));
    goal->setPlaceholderText(L("Sin reto"));
    col->addWidget(labelled(L("Reto del año (terminados)"), goal));

    auto *warn = new QLabel;
    warn->setObjectName("trackWarn");
    warn->setWordWrap(true);
    warn->setMinimumWidth(24);
    warn->hide();
    col->addWidget(warn);

    // Rebuilds the state and field rows from the draft.
    auto rebuild = std::make_shared<std::function<void()>>();
    auto clear = [](QLayout *l) {
        while (QLayoutItem *it = l->takeAt(0)) {
            if (QWidget *w = it->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete it;
        }
    };
    *rebuild = [=] {
        clear(stateList);
        for (int i = 0; i < draft->states.size(); ++i) {
            auto *r = new QWidget;
            auto *l = new QHBoxLayout(r);
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(4);
            QLineEdit *e = field(L(draft->states.at(i).name));
            QObject::connect(e, &QLineEdit::textEdited, e, [draft, i](const QString &t) {
                if (i < draft->states.size()) draft->states[i].name = t;
            });
            l->addWidget(e, 1);
            const bool last = i == draft->states.size() - 1;
            auto *busy = button("segOption", L("en curso"));
            setChosen(busy, draft->states.at(i).inProgress);
            busy->setEnabled(!last && i > 0);   // neither the queue nor the finished
            busy->setToolTip(L("Muestra la tarjeta con progreso"));
            QObject::connect(busy, &QToolButton::clicked, busy, [draft, i, rebuild] {
                const bool on = !draft->states.at(i).inProgress;
                for (CollectionState &s : draft->states) s.inProgress = false;
                draft->states[i].inProgress = on;
                (*rebuild)();
            });
            l->addWidget(busy);
            for (int dir : {-1, 1}) {
                auto *move = new QToolButton;
                move->setIcon(paintIcon(dir < 0 ? "chevronUp" : "chevronDown", QColor(Theme::muted()), 12));
                move->setFixedSize(22, 26);
                move->setCursor(Qt::PointingHandCursor);
                move->setFocusPolicy(Qt::TabFocus);
                move->setToolTip(dir < 0 ? L("Subir") : L("Bajar"));
                move->setEnabled(dir < 0 ? i > 0 : !last);
                QObject::connect(move, &QToolButton::clicked, move, [draft, i, dir, rebuild] {
                    const int j = i + dir;
                    if (j < 0 || j >= draft->states.size()) return;
                    draft->states.swapItemsAt(i, j);
                    // The queue and the finished state cannot be "in progress".
                    draft->states.first().inProgress = false;
                    draft->states.last().inProgress = false;
                    (*rebuild)();
                });
                l->addWidget(move);
            }
            auto *remove = new QToolButton;
            remove->setIcon(paintIcon("close", QColor(Theme::muted()), 12));
            remove->setFixedSize(22, 26);
            remove->setCursor(Qt::PointingHandCursor);
            remove->setFocusPolicy(Qt::TabFocus);
            remove->setToolTip(L("Quitar estado; lo que tenga pasa al primero"));
            remove->setEnabled(draft->states.size() > 2);
            QObject::connect(remove, &QToolButton::clicked, remove, [draft, i, rebuild] {
                if (draft->states.size() <= 2) return;
                draft->states.removeAt(i);
                draft->states.first().inProgress = false;
                draft->states.last().inProgress = false;
                (*rebuild)();
            });
            l->addWidget(remove);
            stateList->addWidget(r);
        }

        clear(fieldList);
        for (const FieldInfo &f : fieldInfo()) {
            auto *r = new QWidget;
            auto *l = new QHBoxLayout(r);
            l->setContentsMargins(2, 4, 2, 4);
            l->setSpacing(10);
            const QString key = QString::fromLatin1(f.key);
            QString label = L(f.name);
            if (key == Collection::kAuthor) label = L(draft->authorLabel());
            auto *n = new ElidedLabel(label, QColor(Theme::fg()));
            n->setObjectName("setRowText");
            l->addWidget(n, 1);
            auto *h = new QLabel(key == Collection::kProgress ? L(draft->unitName()) : L(f.hint));
            h->setObjectName("meta");
            l->addWidget(h);
            l->addWidget(new Switch(draft->fields.contains(key), theme, [draft, key](bool on) {
                draft->fields.removeAll(key);
                if (on) draft->fields << key;
            }));
            fieldList->addWidget(r);
        }

        clear(customList);
        for (int i = 0; i < draft->custom.size(); ++i) {
            auto *r = new QWidget;
            auto *l = new QHBoxLayout(r);
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(4);
            QLineEdit *e = field(draft->custom.at(i).name);
            e->setPlaceholderText(L("Nombre del campo"));
            QObject::connect(e, &QLineEdit::textEdited, e, [draft, i](const QString &t) {
                if (i < draft->custom.size()) draft->custom[i].name = t;
            });
            l->addWidget(e, 1);
            auto *remove = new QToolButton;
            remove->setIcon(paintIcon("close", QColor(Theme::muted()), 12));
            remove->setFixedSize(22, 26);
            remove->setCursor(Qt::PointingHandCursor);
            remove->setFocusPolicy(Qt::TabFocus);
            remove->setToolTip(L("Quitar campo"));
            QObject::connect(remove, &QToolButton::clicked, remove, [draft, i, rebuild] {
                if (i < draft->custom.size()) draft->custom.removeAt(i);
                (*rebuild)();
            });
            l->addWidget(remove);
            customList->addWidget(r);
        }
        customRows->setVisible(!draft->custom.isEmpty());

        for (int i = 0; i < templateBtns.size(); ++i) setChosen(templateBtns.at(i), i == int(draft->kind));
        QTimer::singleShot(0, ov, [ov] { ov->relayout(); });
    };

    for (int i = 0; i < templateBtns.size(); ++i)
        QObject::connect(templateBtns.at(i), &QToolButton::clicked, ov, [=] {
            const auto t = Collection::Template(i);
            Collection *made = Collection::fromTemplate(t, QString());
            const QString id = draft->id;
            const QList<CollectionField> custom = draft->custom;
            *draft = *made;
            delete made;
            draft->id = id;
            draft->custom = custom;
            // The name follows the template until it is typed.
            if (!*nameTouched)
                name->setText(t == Collection::Books ? L("Biblioteca") : L(Collection::templateName(t)));
            (*rebuild)();
        });
    QObject::connect(addState, &QToolButton::clicked, ov, [=] {
        CollectionState s;
        s.name = L("Nuevo estado");
        // Before the finished one, which stays last.
        draft->states.insert(qMax(0, int(draft->states.size()) - 1), s);
        (*rebuild)();
    });
    QObject::connect(addField, &QToolButton::clicked, ov, [=] {
        CollectionField f;
        f.name = L("Campo");
        draft->custom.append(f);
        (*rebuild)();
    });

    auto *content = new QWidget;
    auto *outer = new QVBoxLayout(content);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);
    outer->addWidget(scrolled(form), 1);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    QToolButton *del = nullptr;
    if (!isNew) {
        del = button("quitBtn", L("Eliminar"));
        del->setToolTip(L("Borra la colección y todo lo que tiene"));
        buttons->addWidget(del);
    } else {
        auto *hint = new QLabel(QStringLiteral("Ctrl+6 · ") + L("Colecciones"));
        hint->setObjectName("meta");
        buttons->addWidget(hint);
    }
    buttons->addStretch();
    auto *cancel = button("pickCancel", L("Cancelar"));
    auto *save = button("pickSave", isNew ? L("Crear colección") : L("Guardar"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    outer->addLayout(buttons);
    ov->setContent(content);

    QPointer<Overlay> guard(ov);
    QObject::connect(cancel, &QToolButton::clicked, ov, [guard] {
        if (guard) guard->close();
    });
    QObject::connect(save, &QToolButton::clicked, ov, [=] {
        const QString n = name->text().simplified();
        QStringList names;
        for (const CollectionState &s : draft->states) names << s.name.simplified();
        if (n.isEmpty() || names.contains(QString())) {
            warn->setText(n.isEmpty() ? L("Falta el nombre.") : L("Cada estado necesita un nombre."));
            warn->show();
            ov->relayout();
            return;
        }
        for (CollectionState &s : draft->states) s.name = s.name.simplified();
        for (int i = int(draft->custom.size()) - 1; i >= 0; --i)
            if (draft->custom.at(i).name.simplified().isEmpty()) draft->custom.removeAt(i);
        draft->name = n;
        draft->goal = goal->text().toInt();
        Collection *target = collId.isEmpty() ? nullptr : store->collection(collId);
        if (!collId.isEmpty() && !target) {   // deleted meanwhile (a sync)
            if (guard) guard->close();
            return;
        }
        if (!target) {
            target = new Collection(*draft);
            store->addCollection(target);
        } else {
            // Items in a state that went away move to the first one.
            const QString first = draft->firstState();
            for (CollectionItem *i : store->collectionItems())
                if (i->collection == target->id && draft->stateIndex(i->state) < 0) i->state = first;
            const int pos = target->pos;
            *target = *draft;
            target->pos = pos;
            // A sort by a field just turned off falls back to the title.
            const QString key = target->sortKey;
            if ((key == Collection::kByAuthor && !target->has(Collection::kAuthor)) ||
                (key == Collection::kByYear && !target->has(Collection::kYear)) ||
                (key == Collection::kByPriority && !target->has(Collection::kPriority) &&
                 !target->has(Collection::kRating))) {
                target->sortKey = Collection::kByTitle;
                target->sortAscending = true;
            }
        }
        if (saved) saved(target);
        if (guard) guard->close();
    });
    if (del) {
        auto armed = std::make_shared<bool>(false);
        QObject::connect(del, &QToolButton::clicked, ov, [=] {
            Collection *target = store->collection(collId);
            const int n = target ? Coll::countIn(store->collectionItems(), target->id) : 0;
            if (!*armed && n > 0) {
                *armed = true;
                del->setText(n == 1 ? L("¿Borrar con 1 elemento?") : L("¿Borrar con %1 elementos?").arg(n));
                return;
            }
            if (target) store->removeCollection(target);
            if (deleted) deleted();
            if (guard) guard->close();
        });
    }

    (*rebuild)();
    ov->open(name);
}

}  // namespace colldlg
