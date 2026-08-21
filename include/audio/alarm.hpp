#pragma once

#include <QObject>
#include <QString>

class QSoundEffect;

// Aviso sonoro de los recordatorios.
//
// El tono se sintetiza y se deja en disco la primera vez que hace falta, así
// que la app no depende de ningún fichero de sonido del sistema ni de un
// recurso empotrado. Suena en bucle hasta que el usuario lo para.
class Alarm : public QObject {
    Q_OBJECT

public:
    explicit Alarm(QObject *parent = nullptr);

    void start();
    void stop();
    bool isRinging() const;

private:
    QString ensureToneFile();   // genera alarm.wav si no existe

    QSoundEffect *m_effect = nullptr;
};
