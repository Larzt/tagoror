#pragma once

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <functional>

#include "core/lang.hpp"
#include "core/note.hpp"

class QTimer;

// Todo lo que sobrevive al cierre de la aplicación: las notas y las
// preferencias que viajan con ellas. Está separado de Panel para que la
// ventana no tenga que saber de JSON, de QSettings ni de migraciones.
class Store : public QObject {
    Q_OBJECT

public:
    // Preferencias guardadas junto a las notas. El acento y la opacidad son
    // datos, no estilo: Panel construye su Theme a partir de ellos.
    struct Prefs {
        QColor accent{"#7c9cff"};
        int opacity = 96;              // 40..100
        QSize windowSize;              // tamaño del panel desplegado
        QByteArray input;              // micrófono elegido en ajustes
        bool onTop = false;            // por defecto vive en el escritorio
        Lang::Code lang = Lang::Es;    // idioma de la interfaz
    };

    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    // --- notas (propietario: se liberan en el destructor) ------------------
    const QList<Note *> &notes() const { return m_notes; }
    int count() const { return int(m_notes.size()); }
    void add(Note *n);                 // la más reciente, arriba
    void remove(Note *n);              // se lleva por delante su adjunto

    Prefs &prefs() { return m_prefs; }
    const Prefs &prefs() const { return m_prefs; }

    // Gancho para que el dueño refresque lo que solo él sabe (el tamaño de la
    // ventana) justo antes de escribir. Debe soltarse antes de destruirlo: el
    // Store guarda una última vez al morir, y para entonces ya no existe.
    std::function<void()> beforeSave;

    // --- persistencia -------------------------------------------------------
    QString path() const;              // notes.json
    void load();                       // siembra dos notas si no hay fichero
    void save();
    void scheduleSave();               // agrupa ráfagas de tecleo (600 ms)

    // Mueve la carpeta de datos copiando los adjuntos; deja los originales en
    // su sitio, así un fallo a mitad no destruye nada.
    void changeDataDir(const QString &to);

private:
    // Resuelve la carpeta antes de tocar disco: la override manda sobre la
    // ruta estándar, y de las marcas anteriores se hereda todo lo que haya.
    void resolveDataDir();

    QList<Note *> m_notes;
    Prefs m_prefs;
    QTimer *m_saveTimer = nullptr;
};
