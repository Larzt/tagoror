#pragma once

#include <QLocale>
#include <QString>

// Idioma de la interfaz: español o inglés.
//
// Los literales se siguen escribiendo en español en el código -- es el idioma
// del proyecto -- y L() los cambia por su equivalente inglés cuando ese es el
// elegido. Así el código se lee igual que antes y un texto que falte en la
// tabla sale en español en vez de desaparecer.
//
// No se usa QTranslator: obligaría a un paso de lupdate/lrelease y a instalar
// unos .qm junto al binario, y aquí toda la interfaz cabe en una tabla.
namespace Lang {

enum Code { Es, En };

Code current();
void setCurrent(Code c);

// Con qué idioma arranca una instalación nueva: el del sistema si habla
// español, inglés en cualquier otro caso. Los ficheros ya existentes no pasan
// por aquí (ver Store::load): quien ya tenía el panel en español lo conserva.
Code systemDefault();

Code fromString(const QString &s, Code fallback);
QString toString(Code c);

// El idioma con el que se escriben las fechas. El formato ("ddd d MMM") es el
// mismo en ambos; lo que cambia son los nombres de día y mes. Nunca
// QLocale::system(): el idioma lo manda el ajuste, no el entorno.
QLocale locale();

// Inicial de cada columna del calendario, de lunes a domingo (la semana
// empieza en lunes también en inglés: es la rejilla que dibuja MonthGrid).
QString weekdayInitial(int column);

}  // namespace Lang

// Un texto de interfaz, escrito en español.
QString L(const QString &es);
