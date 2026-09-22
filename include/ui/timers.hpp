#pragma once

#include <QList>
#include <QWidget>

#include "core/timer.hpp"
#include "ui/theme.hpp"

class QLabel;
class QLineEdit;
class QVBoxLayout;

// Página de temporizadores: la de m_body que abre el reloj de la cabecera.
//
// Como las demás páginas no guarda nada ni cambia el estado por su cuenta:
// pinta la lista del Store y pide a Panel cada cambio, que es quien guarda y
// quien hace sonar la alarma. Lo único suyo es cuál está abierto en grande.
//
// Se repinta de dos maneras distintas, y la diferencia importa: refresh()
// rehace la página (un temporizador nuevo, uno que se pausa) y tick() solo
// reescribe los números y los anillos. tick() corre cada segundo mientras
// algo cuenta, y rehacer la página a ese ritmo le quitaría el foco -- y el
// texto a medio escribir -- al campo del nombre del temporizador nuevo.
class TimerView : public QWidget {
    Q_OBJECT

public:
    explicit TimerView(const Theme &theme, QWidget *parent = nullptr);

    void setSource(const QList<Timer *> *timers);
    void setTheme(const Theme &theme);
    void refresh();
    // El formulario de alta no se rehace con refresh() (ver m_creator), así
    // que cambiar de idioma lo monta de nuevo aparte.
    void retranslate();
    void tick();

    // Abre uno en grande (el reloj de la cabecera lleva al que está en marcha).
    void focusTimer(Timer *t);

signals:
    void createRequested(const QString &name, qint64 ms);
    void toggleRequested(Timer *t);    // iniciar o pausar
    void resetRequested(Timer *t);
    void removeRequested(Timer *t);

private:
    void addSection(const QString &title, int count);
    void addRow(Timer *t);
    void addFocusCard(Timer *t);
    void addCreator();
    QString subtitle(const Timer *t) const;

    Theme m_theme;
    const QList<Timer *> *m_timers = nullptr;
    Timer *m_focus = nullptr;
    QVBoxLayout *m_layout = nullptr;   // contenido + stretch final

    // Lo que tick() reescribe sin rehacer nada.
    struct Live {
        Timer *timer;
        QLabel *time;
        QWidget *ring;
    };
    QList<Live> m_live;

    // El formulario de alta sobrevive a los refresh(): se escribe en él
    // mientras otros temporizadores cambian de estado.
    QWidget *m_creator = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_h = nullptr;
    QLineEdit *m_m = nullptr;
    QLineEdit *m_s = nullptr;
};
