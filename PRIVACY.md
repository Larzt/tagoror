# Política de privacidad de Tagoror

*Última actualización: 22 de septiembre de 2026 · [English below](#privacy-policy)*

Tagoror es una aplicación de escritorio de código abierto para notas, recordatorios,
temporizadores y un planificador. No tiene servidores propios, no tiene cuentas de
usuario y no recoge estadísticas ni telemetría de ningún tipo.

## Qué datos maneja y dónde se guardan

Todo lo que escribes en Tagoror (notas, listas, recordatorios, notas de voz, imágenes,
cumpleaños, eventos y temporizadores) se guarda **únicamente en tu ordenador**, en la
carpeta de datos que elijas en los ajustes. Nadie más tiene acceso a ella a través de
la aplicación.

## Conexiones a internet

La aplicación solo se conecta a internet en dos casos, y los dos se pueden desactivar
desde los ajustes:

1. **Buscar actualizaciones.** Una vez al día consulta la API pública de GitHub para
   saber cuál es la última versión publicada. No envía ningún dato tuyo.
2. **Sincronización con Google Drive (opcional).** Solo si conectas tu cuenta de Google.

## Sincronización con Google Drive

Si decides conectar tu cuenta de Google:

- Tagoror pide únicamente el permiso **`drive.file`**, que le deja ver y modificar solo
  los ficheros que la propia aplicación crea. **No puede ver ni leer el resto de tu
  Google Drive.**
- Guarda tus datos (notas, cumpleaños, eventos, temporizadores, notas de voz e
  imágenes) en una carpeta llamada `Tagoror` de **tu propio** Google Drive, y los
  lee de ahí para mantener iguales todos tus equipos conectados a esa misma cuenta.
  Los datos van directamente entre tu ordenador y tu cuenta de Google; no pasan por
  ningún servidor del autor ni de terceros.
- Para identificar la cuenta conectada lee tu dirección de correo a través de la API
  de Drive, y solo la muestra en los ajustes de la aplicación.
- La credencial de acceso (token de Google) se guarda **solo en tu ordenador**, en el
  fichero de configuración de la aplicación. El autor de Tagoror nunca la recibe.
- Lo que se borra en un equipo se borra también en los demás equipos conectados a
  tu cuenta; es lo que significa sincronizar. Tus copias de seguridad locales no se
  sincronizan y siguen en cada equipo.
- El uso que hace Tagoror de la información recibida de las APIs de Google cumple la
  [Política de datos de usuario de los servicios de API de Google](https://developers.google.com/terms/api-services-user-data-policy),
  incluidos los requisitos de uso limitado. Estos datos no se venden, no se comparten
  con terceros ni se usan para publicidad.

Puedes **desconectar** la cuenta en cualquier momento desde *Ajustes → Google Drive →
Desconectar*, lo que revoca el permiso y borra la credencial local, o desde
[myaccount.google.com/permissions](https://myaccount.google.com/permissions). Los
ficheros ya subidos siguen en tu Drive hasta que tú los borres.

## Contacto

Para cualquier duda sobre privacidad, abre una incidencia en
[github.com/Larzt/tagoror/issues](https://github.com/Larzt/tagoror/issues).

---

# Privacy policy

*Last updated: 22 September 2026*

Tagoror is an open-source desktop application for notes, reminders, timers and a
planner. It has no servers of its own, no user accounts, and collects no statistics or
telemetry of any kind.

## What data it handles and where it is stored

Everything you write in Tagoror (notes, lists, reminders, voice notes, images,
birthdays, events and timers) is stored **only on your computer**, in the data folder
you choose in settings. Nobody else can access it through the application.

## Internet connections

The application connects to the internet in only two cases, and both can be turned off
in settings:

1. **Update check.** Once a day it asks GitHub's public API for the latest published
   version. It sends none of your data.
2. **Google Drive sync (optional).** Only if you connect your Google account.

## Google Drive sync

If you choose to connect your Google account:

- Tagoror requests only the **`drive.file`** permission, which lets it see and change
  only the files the application itself creates. **It cannot see or read the rest of
  your Google Drive.**
- It keeps your data (notes, birthdays, events, timers, voice notes and images) in a
  folder named `Tagoror` in **your own** Google Drive, and reads it back from there to
  keep all your computers connected to that same account in step. The data goes
  directly between your computer and your Google account; it does not pass through
  any server of the author or of third parties.
- To identify the connected account it reads your e-mail address through the Drive
  API, and shows it only in the application's settings.
- The access credential (Google token) is stored **only on your computer**, in the
  application's configuration file. The author of Tagoror never receives it.
- What you delete on one computer is deleted on your other connected computers too;
  that is what syncing means. Your local backups are not synced and stay on each
  computer.
- Tagoror's use of information received from Google APIs adheres to the
  [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy),
  including the Limited Use requirements. This data is not sold, not shared with third
  parties and not used for advertising.

You can **disconnect** the account at any time from *Settings → Google Drive →
Disconnect*, which revokes the permission and deletes the local credential, or from
[myaccount.google.com/permissions](https://myaccount.google.com/permissions). Files
already uploaded stay in your Drive until you delete them.

## Contact

For any privacy question, open an issue at
[github.com/Larzt/tagoror/issues](https://github.com/Larzt/tagoror/issues).
