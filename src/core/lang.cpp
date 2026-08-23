#include "core/lang.hpp"

#include <QHash>

namespace {

Lang::Code g_current = Lang::Es;

// Español -> inglés. La clave es el literal tal cual aparece en el código,
// marcadores de posición incluidos ("%1 DE %2"), porque el .arg() se aplica
// después de traducir. Lo que no esté aquí se queda en español.
const QHash<QString, QString> &table() {
    static const QHash<QString, QString> t = {
        // --- panel: cabecera, pie y estado vacío ---------------------------
        {"Buscar", "Search"},
        {"Nueva nota", "New note"},
        {"Calendario", "Calendar"},
        {"Ver notas", "Back to notes"},
        {"Ajustes", "Settings"},
        {"Plegar a icono", "Fold to icon"},
        {"Filtrar notas…", "Filter notes…"},
        {"Todavía no hay notas", "No notes yet"},
        {"Crear la primera", "Create the first one"},
        {"clic dcho · opciones", "right click · options"},
        {"%1 EN EL TAGOROR", "%1 IN THE TAGOROR"},
        {"%1 CON FECHA", "%1 SCHEDULED"},
        {"%1 DE %2", "%1 OF %2"},
        {"Abrir Tagoror · arrastra para mover", "Open Tagoror · drag to move"},
        {"Recordatorio vencido · clic para parar", "Reminder overdue · click to stop"},

        // --- selector de nueva nota ----------------------------------------
        {"Texto", "Text"},
        {"Una nota libre", "A free-form note"},
        {"Checklist", "Checklist"},
        {"Tareas con progreso", "Tasks with progress"},
        {"Recordatorio", "Reminder"},
        {"Con fecha y aviso", "With a date and an alarm"},
        {"Nota de voz", "Voice note"},
        {"Graba desde el micrófono", "Records from the microphone"},

        // --- títulos de las notas nuevas ------------------------------------
        {"Nueva lista", "New list"},
        {"Nuevo recordatorio", "New reminder"},

        // --- ajustes --------------------------------------------------------
        {"Acento", "Accent"},
        {"Color personalizado…", "Custom colour…"},
        {"Color de acento", "Accent colour"},
        {"Opacidad", "Opacity"},
        {"Idioma", "Language"},
        {"Ventana", "Window"},
        {"Siempre encima", "Always on top"},
        {"Activado · por encima de todo", "On · above everything else"},
        {"Desactivado · pegado al escritorio", "Off · stuck to the desktop"},
        {"Compatibilidad X11", "X11 compatibility"},
        {"Desactivada · Wayland nativo", "Off · native Wayland"},
        {"Activada · %1", "On · %1"},
        {"Datos", "Data"},
        {"Carpeta de guardado…", "Storage folder…"},
        {"Carpeta donde guardar las notas", "Folder to keep the notes in"},
        {"Micrófono", "Microphone"},
        {"En uso", "In use"},
        {"Salir", "Quit"},
        {"Mostrar", "Show"},
        {"Ocultar", "Hide"},
        {"Recordatorio vencido", "Reminder overdue"},

        // --- tarjetas -------------------------------------------------------
        {"Nota", "Note"},
        {"Lista", "List"},
        {"Sin título", "Untitled"},
        {"Escribe…", "Write…"},
        {"Detalles…", "Details…"},
        {"TEXTO", "TEXT"},
        {"RECORDATORIO", "REMINDER"},
        {"VENCIDO", "OVERDUE"},
        {"¡AHORA!", "NOW!"},
        {"Añadir elemento", "Add item"},
        {"Añadir elemento…", "Add item…"},
        {"Enter para confirmar", "Enter to confirm"},
        {"Quitar elemento", "Remove item"},
        {"Eliminar nota", "Delete note"},
        {"Eliminar lista", "Delete list"},
        {"Ya está todo hecho: quitar esta nota", "All done: remove this note"},

        // --- fechas y avisos -------------------------------------------------
        {"Sin fecha", "No date"},
        {"Clic para cambiar la fecha", "Click to change the date"},
        {"Cambiar fecha", "Change date"},
        {"Quitar fecha", "Remove date"},
        {"Detener aviso", "Stop the alarm"},
        {"Silencia la alarma", "Silences the alarm"},
        {"Recordar", "Remind me"},
        {"En 5 minutos", "In 5 minutes"},
        {"En 1 hora", "In 1 hour"},
        {"A las 18:00", "At 18:00"},
        {"Mañana 09:00", "Tomorrow 09:00"},
        {"A mano · dd/MM HH:mm", "By hand · dd/MM HH:mm"},
        {"p. ej. 24/12 20:30", "e.g. 24/12 20:30"},
        {"A mano · HH:mm", "By hand · HH:mm"},
        {"p. ej. 20:30", "e.g. 20:30"},
        {"Ya pasado", "Already past"},

        // --- repetición de recordatorios --------------------------------------
        {"Repetir", "Repeat"},
        {"No repetir", "Do not repeat"},
        {"Suena una vez", "Rings once"},
        {"Cada semana", "Every week"},
        {"Cada año", "Every year"},
        {"CADA SEMANA", "EVERY WEEK"},
        {"CADA AÑO", "EVERY YEAR"},
        {"Clic para cambiar la repetición", "Click to change how often it repeats"},
        {"%1 · clic para cambiarlo", "%1 · click to change it"},

        // --- detalles de un recordatorio --------------------------------------
        {"+ Añadir detalles", "+ Add details"},
        {"Añadir detalles", "Add details"},
        {"Escribir detalles del recordatorio", "Write the reminder's details"},
        {"Escribe bajo la fecha", "Writes under the date"},

        // --- imágenes adjuntas -------------------------------------------------
        {"Imágenes", "Images"},
        {"Añadir imagen…", "Add image…"},
        {"Elegir imágenes", "Choose images"},
        {"Se copia junto a la nota", "Copied next to the note"},
        {"%1 ya adjuntas", "%1 already attached"},
        {"1 IMAGEN", "1 IMAGE"},
        {"%1 IMÁGENES", "%1 IMAGES"},
        {"Mostrar imágenes", "Show images"},
        {"Ocultar imágenes", "Hide images"},
        {"Quitar imagen", "Remove image"},

        // --- orden de las tarjetas ---------------------------------------------
        {"Orden", "Order"},
        {"Subir", "Move up"},
        {"Bajar", "Move down"},
        {"Arrastra para reordenar", "Drag to reorder"},

        // --- enlaces ---------------------------------------------------------
        {"Enlace", "Link"},
        {"Nuevo enlace", "New link"},
        {"Editar enlace", "Edit link"},
        {"Añadir enlace…", "Add link…"},
        {"Se abre en el navegador", "Opens in the browser"},
        {"%1 ya adjuntos", "%1 already attached"},
        {"https://ejemplo.com", "https://example.com"},
        {"Nombre (opcional)", "Name (optional)"},
        {"Abrir", "Open"},
        {"Copiar dirección", "Copy address"},
        {"Editar…", "Edit…"},
        {"Sin nombre", "No name"},
        {"Quitar enlace", "Remove link"},

        // --- notas de voz ------------------------------------------------------
        {"Grabar", "Record"},
        {"Regrabar", "Re-record"},
        {"Sustituye la toma actual", "Replaces the current take"},
        {"Reproducir", "Play"},
        {"Detener", "Stop"},
        {"GRABANDO…", "RECORDING…"},
        {"VOZ", "VOICE"},
        {"VOZ · SIN GRABAR", "VOICE · NOT RECORDED"},
        {"VOZ · SIN SEÑAL, REVISA EL MICRÓFONO", "VOICE · NO SIGNAL, CHECK THE MICROPHONE"},
        {"No hay micrófono disponible", "No microphone available"},
        {"El micrófono no admite PCM 16 bits", "The microphone does not support 16-bit PCM"},
        {"No se pudo escribir ", "Could not write "},
        {"No se pudo abrir el micrófono", "Could not open the microphone"},

        // --- calendario ---------------------------------------------------------
        {"Mes anterior", "Previous month"},
        {"Mes siguiente", "Next month"},
        {"Hoy", "Today"},
        {"Volver a hoy", "Back to today"},
        {"%1 · HOY", "%1 · TODAY"},
        {"1 AVISO", "1 REMINDER"},
        {"%1 AVISOS", "%1 REMINDERS"},
        {"Sin recordatorios este día", "No reminders on this day"},
        {"Mostrar los avisos del día", "Show the day's reminders"},
        {"Plegar los avisos del día", "Fold away the day's reminders"},

        // --- notas de ejemplo del primer arranque ---------------------------------
        {"Escribir changelog", "Write the changelog"},
        {"Publicar en el foro", "Post on the forum"},
        {"Escalado en Wayland", "Wayland scaling"},
        {"El escalado fraccional emborrona el widget en el panel 4K.",
         "Fractional scaling blurs the widget on the 4K panel."},
    };
    return t;
}

}  // namespace

namespace Lang {

Code current() { return g_current; }
void setCurrent(Code c) { g_current = c; }

Code systemDefault() {
    return QLocale::system().language() == QLocale::Spanish ? Es : En;
}

Code fromString(const QString &s, Code fallback) {
    if (s == "es") return Es;
    if (s == "en") return En;
    return fallback;
}

QString toString(Code c) { return c == Es ? "es" : "en"; }

QLocale locale() {
    static const QLocale es(QLocale::Spanish);
    static const QLocale en(QLocale::English);
    return g_current == Es ? es : en;
}

QString weekdayInitial(int column) {
    static const char *const es[7] = {"L", "M", "X", "J", "V", "S", "D"};
    static const char *const en[7] = {"M", "T", "W", "T", "F", "S", "S"};
    if (column < 0 || column > 6) return QString();
    return QString::fromLatin1(g_current == Es ? es[column] : en[column]);
}

}  // namespace Lang

QString L(const QString &es) {
    if (g_current == Lang::Es) return es;
    return table().value(es, es);
}
