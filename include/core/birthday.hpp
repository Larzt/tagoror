#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QUuid>

#include "core/lang.hpp"

// Un cumpleaños, y no una nota con la fecha puesta.
//
// Se podría haber hecho con un Note::Reminder anual -- de hecho la repetición
// anual nació para esto -- pero son dos cosas distintas: una nota se escribe,
// se lee y se acaba borrando, mientras que un cumpleaños es el dato de una
// persona que vuelve todos los años y que no se toca casi nunca. Metidos en la
// lista de notas, veinte cumpleaños son veinte tarjetas que nadie quiere leer
// ahí; en su propia página son una agenda.
//
// La fecha va partida en día, mes y año sueltos a propósito: de mucha gente se
// sabe el día pero no el año, y un QDate no admite quedarse a medias.
struct Birthday {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    int day = 1;
    int month = 1;
    int year = 0;            // 0 = no se sabe; entonces no hay "cumple 32"
    QString relation;        // texto libre: "hermana", "trabajo", "uni"…
    QTime remindAt;          // inválida = no avisa; válida = suena ese día
    int greetedYear = 0;     // el último año en el que se marcó como felicitado
    int firedYear = 0;       // el último año en el que sonó (y se calló)
    bool ringing = false;    // solo en memoria: está sonando ahora mismo

    // 2004 es bisiesto: así un 29 de febrero se admite al escribirlo, aunque
    // el año que viene no exista ese día.
    bool isValid() const { return QDate(2004, month, day).isValid(); }

    // La fecha que le toca a un año concreto. Un 29 de febrero sale inválido
    // en los años normales en vez de correrse al 28, que es la misma regla que
    // sigue Note::occursOn: adelantarlo sería inventarse un día que su dueño
    // no escribió.
    QDate dateIn(int y) const { return QDate(y, month, day); }

    // El próximo, contando hoy. Es un bucle y no una cuenta por el 29 de
    // febrero: en un año normal no hay tal día y hay que seguir buscando, que
    // es justo lo que hace que ese cumpleaños espere cuatro años.
    QDate nextDate(const QDate &from = QDate::currentDate()) const {
        for (int y = from.year(); y <= from.year() + 8; ++y) {
            const QDate d = dateIn(y);
            if (d.isValid() && d >= from) return d;
        }
        return {};
    }

    // Cuántos días faltan; -1 si la fecha no vale para nada.
    int daysUntil(const QDate &from = QDate::currentDate()) const {
        const QDate next = nextDate(from);
        return next.isValid() ? int(from.daysTo(next)) : -1;
    }

    bool isToday(const QDate &today = QDate::currentDate()) const {
        return dateIn(today.year()) == today;
    }

    // Los años que cumple en su próxima vuelta, o 0 si no se sabe el año.
    int turns(const QDate &from = QDate::currentDate()) const {
        const QDate next = nextDate(from);
        return (year > 0 && next.isValid()) ? next.year() - year : 0;
    }

    // Ya felicitado este año. Se guarda el año y no un booleano porque la
    // marca tiene que caducar sola: en enero nadie quiere ir borrando cruces.
    bool greeted(const QDate &today = QDate::currentDate()) const {
        return greetedYear > 0 && greetedYear == today.year();
    }

    // Suena este año si tiene hora puesta, es hoy y todavía no ha sonado.
    bool alarmDue(const QDateTime &now = QDateTime::currentDateTime()) const {
        if (!remindAt.isValid() || !isToday(now.date())) return false;
        if (firedYear == now.date().year()) return false;
        return now >= QDateTime(now.date(), remindAt);
    }

    // Una o dos letras para el círculo de la fila. Sin nombre va un
    // interrogante: un círculo vacío se lee como un fallo de dibujo.
    QString initials() const {
        QString out;
        for (const QString &word : name.split(' ', Qt::SkipEmptyParts)) {
            out += word.at(0).toUpper();
            if (out.size() == 2) break;
        }
        return out.isEmpty() ? QStringLiteral("?") : out;
    }

    // El día, escrito en el idioma de la interfaz. Sin año: la gracia de un
    // cumpleaños es el día, y el año sería siempre el de la próxima vez.
    QString dateLabel(const QDate &from = QDate::currentDate()) const {
        const QDate next = nextDate(from);
        return next.isValid() ? Lang::locale().toString(next, "d MMM") : QString();
    }

    // Cuánto falta, en palabras. Los tramos son los que se leen de un vistazo:
    // los días exactos la primera quincena, y de ahí en adelante semanas y
    // meses redondeados, que es toda la precisión que le interesa a nadie.
    QString whenLabel(const QDate &from = QDate::currentDate()) const {
        const int d = daysUntil(from);
        if (d < 0) return QString();
        if (d == 0) return L("hoy");
        if (d == 1) return L("mañana");
        if (d < 14) return L("en %1 días").arg(d);
        if (d < 60) return L("en %1 sem").arg(d / 7);
        return L("en %1 meses").arg(d / 30);   // d >= 60, así que nunca es "1 mes"
    }

    // La segunda línea de la fila: "cumple 36 · trabajo". Cada mitad puede
    // faltar, y con las dos vacías la fila se queda solo con el nombre.
    QString subtitle(const QDate &from = QDate::currentDate()) const {
        QStringList parts;
        if (const int age = turns(from); age > 0) parts << L("cumple %1").arg(age);
        if (!relation.trimmed().isEmpty()) parts << relation.trimmed();
        return parts.join(" · ");
    }

    // Lo que se enseña en el campo de la fecha al editar: con año si se sabe.
    QString dateText() const {
        const QString dm = QString("%1/%2").arg(day, 2, 10, QChar('0'))
                                           .arg(month, 2, 10, QChar('0'));
        return year > 0 ? dm + "/" + QString::number(year) : dm;
    }

    // Lo contrario: "24/12", "24/12/1990", "24-12-90", "24.12.1990". El año es
    // opcional porque de mucha gente se sabe el día y no el año; sin él
    // simplemente no se dice cuántos cumple. Devuelve false sin tocar nada si
    // lo escrito no es una fecha, que es lo que deja el editor sin efecto en
    // vez de guardar un 1 de enero inventado.
    static bool parseDate(const QString &text, int *day, int *month, int *year) {
        QString norm = text.trimmed();
        norm.replace('-', '/').replace('.', '/');
        const QStringList parts = norm.split('/', Qt::SkipEmptyParts);
        if (parts.size() < 2 || parts.size() > 3) return false;

        bool okD = false, okM = false;
        const int d = parts.at(0).toInt(&okD);
        const int m = parts.at(1).toInt(&okM);
        if (!okD || !okM || !QDate(2004, m, d).isValid()) return false;   // 2004 es bisiesto

        int y = 0;
        if (parts.size() == 3) {
            bool okY = false;
            y = parts.at(2).toInt(&okY);
            if (!okY) return false;
            // Dos cifras se reparten alrededor del año en curso, que es la
            // única lectura razonable de "90" o de "05" en un año de
            // nacimiento. Por encima del actual, el siglo pasado.
            if (parts.at(2).size() <= 2) y += (y > QDate::currentDate().year() % 100) ? 1900 : 2000;
            if (y < 1900 || y > QDate::currentDate().year()) return false;
        }

        *day = d;
        *month = m;
        *year = y;
        return true;
    }

    bool matches(const QString &query) const {
        if (query.isEmpty()) return true;
        return name.contains(query, Qt::CaseInsensitive) ||
               relation.contains(query, Qt::CaseInsensitive);
    }

    // --- serialización -----------------------------------------------------

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        o["day"] = day;
        o["month"] = month;
        o["year"] = year;
        o["relation"] = relation;
        o["remindAt"] = remindAt.isValid() ? remindAt.toString("HH:mm") : QString();
        o["greetedYear"] = greetedYear;
        o["firedYear"] = firedYear;
        return o;
    }

    static Birthday *fromJson(const QJsonObject &o) {
        auto *b = new Birthday;
        b->id = o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        b->name = o["name"].toString();
        b->day = o["day"].toInt(1);
        b->month = o["month"].toInt(1);
        b->year = o["year"].toInt(0);
        b->relation = o["relation"].toString();
        b->remindAt = QTime::fromString(o["remindAt"].toString(), "HH:mm");
        b->greetedYear = o["greetedYear"].toInt(0);
        b->firedYear = o["firedYear"].toInt(0);
        return b;
    }
};
