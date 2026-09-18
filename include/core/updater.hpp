#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Mira si hay una versión publicada más nueva que la que se está ejecutando.
//
// No descarga nada ni instala nada: pregunta por la última release, compara y
// avisa. Lo que se haga con la respuesta es cosa del Panel, y lo único que
// ofrece al usuario es abrir la página de la novedad en su navegador —
// descargar un ejecutable y lanzarlo es otra cosa, y sin binarios firmados no
// toca hacerla.
//
// Vive en core porque no sabe nada de widgets; es lo único de la aplicación que
// habla por red, y solo cuando su dueño se lo pide.
class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject *parent = nullptr);

    // Lanza la consulta. Si ya hay una en vuelo no hace nada: el botón de
    // "buscar ahora" se puede pulsar dos veces seguidas.
    void check();
    bool busy() const { return m_reply != nullptr; }

    // La versión compilada, sin la 'v' de la etiqueta.
    static QString current();

    // Compara dos versiones tipo "2.0.0" o "v2.0.0". Devuelve <0, 0 o >0.
    // Lo que no sea un número se ignora, así que una etiqueta rara no hace
    // creer que hay actualización: en la duda, empata.
    static int compare(const QString &a, const QString &b);

signals:
    // Con error vacío, 'version' es la última publicada y 'url' su página.
    // Con error, los otros dos vienen vacíos y el texto ya está traducido.
    void finished(const QString &version, const QString &url, const QString &error);

private:
    void onReply();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
};
