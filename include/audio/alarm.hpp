#pragma once

#include <QObject>
#include <QString>

class QSoundEffect;
class QTimer;

// Aviso sonoro de los recordatorios.
//
// El tono se sintetiza y se deja en disco la primera vez que hace falta, así
// que la app no depende de ningún fichero de sonido del sistema ni de un
// recurso empotrado. Suena en bucle hasta que el usuario lo para o hasta que
// pasa kMaxRingMs: un aviso que nadie oye no tiene que pitar toda la tarde.
// Callarse solo apaga el sonido; lo que sonaba sigue en rojo en la tarjeta, en
// la franja y en el icono hasta que alguien lo atienda.
class Alarm : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxRingMs = 60 * 1000;

    explicit Alarm(QObject *parent = nullptr);

    void start();
    void stop();
    bool isRinging() const;

private:
    QString ensureToneFile();   // genera alarm.wav si no existe

    QSoundEffect *m_effect = nullptr;
    QTimer *m_limit = nullptr;   // corta el bucle a los kMaxRingMs
};
