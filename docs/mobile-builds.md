# Builds móviles: iOS y Android

El workflow [`.github/workflows/mobile.yml`](../.github/workflows/mobile.yml)
compila Tagoror para:

| Plataforma | Fichero | Firma |
|---|---|---|
| iOS / iPadOS | `Tagoror-unsigned.ipa` | **Sin firmar.** La firma quien lo instala (SideStore, Sideloadly) con su Apple ID |
| Android (arm64) | `Tagoror-<versión>-android-arm64.apk` | Release, con tu keystore (secretos `ANDROID_*`) |
| Android (arm64) | `Tagoror-<versión>-android-arm64-debug.apk` | Clave debug de usar y tirar, si faltan los secretos |

> **Aviso:** la app se comporta como una app móvil (pantalla completa, pestañas
> abajo, botón atrás), pero los controles son los de escritorio: los botones
> son pequeños y algunas acciones piden clic derecho o doble clic. Ver
> [Limitaciones](#limitaciones).

---

## Cómo lanzarlo

Se lanza de tres formas:

1. **Con cada release, solo.** `release.yml` llama a `mobile.yml` igual que a
   `windows.yml`. A la release se adjuntan:
   - el `.ipa` sin firmar, **siempre**;
   - el APK, **solo si se firmó en release** con el keystore. Sin keystore, el APK
     debug se queda en los artifacts del workflow y no se publica.
2. **A mano.** Ve a *Actions → Mobile → Run workflow*, elige la rama y pulsa
   *Run workflow*. La versión sale de `CMakeLists.txt`.
3. **Al empujar una etiqueta `v*` a mano**, por ejemplo `git push origin v1.3.0`.
   Igual que `windows.yml`, cuelga el `.ipa` (y el APK, si está firmado en
   release) de la release de esa etiqueta.

   Las etiquetas que crea `release.yml` no disparan este camino: GitHub no lanza
   workflows por etiquetas creadas con `GITHUB_TOKEN`. Por eso la release llama a
   `mobile.yml` directamente.

Una ejecución completa tarda unos 15–25 minutos. La primera va más lenta porque
todavía no hay caché de Qt, del NDK ni de Gradle.

## Dónde descargar los ficheros

1. Abre *Actions → Mobile* y entra en la ejecución que quieras.
2. Abajo del todo, en **Artifacts**, están:
   - `tagoror-ios-<versión>`, con `Tagoror-unsigned.ipa`;
   - `tagoror-android-<versión>`, con el APK.
3. GitHub los entrega dentro de un `.zip`: descomprímelo para sacar el `.ipa` o
   el `.apk`. Los artifacts caducan a los 90 días. Los de una release están
   también en la página *Releases*, sin caducidad.

---

## Instalar en iPhone/iPad con SideStore

Un Apple ID gratuito puede firmar apps para sus propios dispositivos, con estas
condiciones de Apple:

- la firma **caduca a los 7 días**;
- como mucho **3 apps** instaladas así a la vez;
- como mucho 10 App IDs nuevos por semana.

[SideStore](https://sidestore.io) renueva la firma desde el propio iPhone, sin
ordenador, cada vez que lo abres.

### Primera vez

1. Instala SideStore siguiendo [su guía](https://docs.sidestore.io/docs/installation/prerequisites).
   Necesitas un ordenador una sola vez, para el *pairing file* y para instalar
   SideStore. También necesitas la VPN o la app de túnel que indica la guía
   (LocalDevVPN o StosVPN).
2. Pasa `Tagoror-unsigned.ipa` al iPhone con AirDrop, Archivos, iCloud Drive o
   una descarga directa desde la release.
3. En SideStore: *My Apps → +* y elige el `.ipa`. SideStore lo firma con tu
   Apple ID y lo instala.
4. La primera vez, iOS pide confiar en el desarrollador: *Ajustes → General →
   VPN y gestión de dispositivos →* tu Apple ID *→ Confiar*.
5. En iOS 16 o posterior hay que activar el **Modo desarrollador**: *Ajustes →
   Privacidad y seguridad → Modo desarrollador* y reiniciar.

### Renovación cada 7 días

- Abre SideStore con la VPN o el túnel activo y pulsa *Refresh All* en *My Apps*.
  Así renuevas la firma de Tagoror y la del propio SideStore.
- Si pasan los 7 días sin renovar, Tagoror no abre, pero **los datos no se
  pierden**. Al renovar vuelve a abrir con todo.
- SideStore puede renovar en segundo plano si se lo permites (*Settings →
  Background Refresh*), pero no siempre lo consigue. Conviene abrirlo una vez por
  semana.

### Actualizar a una versión nueva

Instala el `.ipa` nuevo desde SideStore igual que el primero. Al mantener el
mismo bundle ID, se instala encima y conserva los datos.

> SideStore añade a veces un sufijo con tu Team ID al bundle ID
> (`org.appstride.tagoror.XXXXXXXXXX`). Mientras uses siempre la misma cuenta,
> el sufijo es el mismo y las actualizaciones encajan.

### Alternativa: Sideloadly

[Sideloadly](https://sideloadly.io) hace lo mismo desde el ordenador: arrastra el
`.ipa` y escribe tu Apple ID. La renovación de 7 días también hay que hacerla
desde el ordenador, o activando su *auto-refresh* con el iPhone en la misma red
Wi-Fi.

---

## Instalar el APK en Android

1. Descarga el APK en el móvil (desde la release o sacándolo del zip del
   artifact).
2. Ábrelo. Android pedirá permitir **instalar apps de origen desconocido** para
   el navegador o el gestor de archivos que lo abre. Acéptalo para esa app.
3. *Instalar*. Si Play Protect avisa de una app desconocida, pulsa *Más detalles
   → Instalar de todas formas*.

Requisitos: Android 9 o superior (API 28) en un dispositivo arm64, que es
prácticamente cualquier móvil de los últimos años.

**APK firmado en release frente a APK debug.** Android solo deja instalar una
versión nueva encima de la anterior si las dos llevan **la misma firma**:

- **Release** (con tu keystore): todas las versiones llevan la misma firma, así
  que se actualiza encima y conservas los datos.
- **Debug**: cada ejecución del workflow genera una clave nueva. Para instalar
  otro APK debug hay que **desinstalar el anterior**, y eso **borra los datos**.
  Sincroniza con WebDAV antes. Úsalo solo para probar.

Pasar de debug a release también exige desinstalar.

---

## Keystore de Android: crearlo y añadir los secretos

> ### ⚠️ No pierdas el keystore
> Android identifica la app por su firma. **Sin el keystore original no se puede
> publicar ninguna actualización**: un APK firmado con otra clave no se instala
> encima del anterior, y cada usuario tendría que desinstalar y perder sus datos.
> No hay forma de recuperarlo ni de regenerarlo.
>
> Guarda **el fichero `.jks` y sus dos contraseñas** en al menos dos sitios
> fuera del repositorio. Por ejemplo, un gestor de contraseñas y una copia
> cifrada. **Nunca** lo subas al repositorio. El secreto de GitHub no sirve como
> copia: no se puede leer después de guardarlo.

### 1. Crear el keystore (una sola vez)

Necesitas `keytool`, que viene con cualquier JDK (`sudo apt install
openjdk-17-jdk-headless`, o `brew install openjdk@17`):

```sh
keytool -genkeypair -v \
  -keystore tagoror-release.jks \
  -alias tagoror \
  -keyalg RSA -keysize 4096 \
  -validity 36500 \
  -dname "CN=Tagoror, O=AppStride"
```

Pide dos contraseñas:

- la **del almacén** (`ANDROID_KEYSTORE_PASSWORD`);
- la **de la clave** (`ANDROID_KEY_PASSWORD`). Si `keytool` no la pide por
  separado, es la misma que la del almacén.

`-validity 36500` son unos 100 años: la clave no puede caducar mientras la app
exista.

### 2. Pasarlo a base64

```sh
# Linux
base64 -w0 tagoror-release.jks > tagoror-release.jks.b64
# macOS
base64 -i tagoror-release.jks -o tagoror-release.jks.b64
```

### 3. Añadir los cuatro secretos en GitHub

Ve a *Settings → Secrets and variables → Actions → New repository secret*:

| Secreto | Valor |
|---|---|
| `ANDROID_KEYSTORE_BASE64` | El contenido de `tagoror-release.jks.b64`, en una sola línea |
| `ANDROID_KEYSTORE_PASSWORD` | La contraseña del almacén |
| `ANDROID_KEY_ALIAS` | `tagoror`, o el alias que pusieras en `-alias` |
| `ANDROID_KEY_PASSWORD` | La contraseña de la clave |

También se pueden añadir con la CLI de GitHub:

```sh
gh secret set ANDROID_KEYSTORE_BASE64 < tagoror-release.jks.b64
gh secret set ANDROID_KEYSTORE_PASSWORD
gh secret set ANDROID_KEY_ALIAS --body tagoror
gh secret set ANDROID_KEY_PASSWORD
```

Después borra el `.b64`. En el log de la siguiente ejecución, el paso *Firmar*
debe decir «Firmando en release con el keystore de los secretos». Si falta
cualquiera de los cuatro, firma con clave debug y avisa con un *warning*.

---

## Qué cambia en el código

- `include/core/platform.hpp` define `TAGOROR_MOBILE` (iOS y Android) y
  `TAGOROR_LINUX_DESKTOP`. Este segundo hace falta porque Qt define `Q_OS_LINUX`
  también en Android.
- En móvil no existen: la bandeja del sistema, la instancia única por socket
  local, la compatibilidad X11/Wayland y el buscador de actualizaciones (una app
  instalada por SideStore o como APK no se actualiza desde la página de
  releases).
- La ventana ocupa toda la pantalla y respeta las safe areas: el notch o la isla
  dinámica en iOS, y las barras del sistema en Android. No tiene margen de
  sombra, esquinas redondeadas ni opacidad, y no restaura la geometría guardada.
  Las preferencias de ventana y opacidad son las del escritorio y viajan con la
  sincronización: en móvil se ignoran, sin cambiarlas.
- No hay nada propio del widget: ni plegar a burbuja, ni siempre encima, ni
  arrastre, ni tiradores de redimensionado, ni modo aplicación. En Ajustes
  desaparecen el grupo Ventana, la opacidad, la carpeta de guardado y el botón de
  salir.
- Ajustes es la sexta pestaña de la barra inferior. Para que las seis etiquetas
  quepan enteras, «Cumpleaños» y «Seguimiento» se acortan a «Cumples» y «Tiempo»
  (el título de la página mantiene el nombre largo), y la letra es 1 px menor.
- En horizontal la barra sigue abajo si el carril lateral no cabe a lo alto. La
  rotación es libre.
- El botón atrás de Android cierra, por orden, el menú, el diálogo, la paleta,
  el formulario de Seguimiento, la búsqueda o el campo que se está escribiendo.
  Si no hay nada abierto, vuelve a Notas. Solo desde Notas sale de la app.
- Los datos se guardan en cuanto la app pasa a segundo plano: el sistema puede
  cerrarla allí sin avisar.
- Antes de la primera grabación se pide el permiso del micrófono. La primera vez,
  el sistema pregunta y hay que volver a pulsar grabar.
- Permisos:
  - iOS: `NSMicrophoneUsageDescription` en `packaging/ios/Info.plist.in`;
  - Android: `RECORD_AUDIO` y `POST_NOTIFICATIONS`, que Qt inserta en
    `packaging/android/AndroidManifest.xml` desde `CMakeLists.txt`.
- En escritorio (Linux, Windows) el comportamiento no cambia.
- El ID de la app (`org.appstride.tagoror`) se cambia con
  `-DTAGOROR_BUNDLE_ID=...`, o con la variable `BUNDLE_ID` de `mobile.yml`.

## Limitaciones

- **Google Drive y Calendar, desactivados en móvil.** El inicio de sesión de
  escritorio recibe la respuesta de Google en `http://127.0.0.1:<puerto>`, un
  servidor local dentro de la app:
  - en iOS no funciona, porque la app queda suspendida mientras Safari está
    delante;
  - Google tampoco admite ese redirect para clientes iOS ni Android.

  Hará falta:
  - en **iOS**, un cliente OAuth de tipo iOS, sin secret, con redirect
    `com.googleusercontent.apps.<id>:/oauth2redirect`, el esquema declarado en
    `CFBundleURLTypes`, y recoger la respuesta con
    `QDesktopServices::setUrlHandler` o con `QOAuthUriSchemeReplyHandler` de Qt
    NetworkAuth;
  - en **Android**, Google ya no admite esquemas propios en clientes nuevos: hay
    que usar Credential Manager con la Authorization API de Play Services (JNI),
    o un App Link HTTPS verificado;
  - un ID de cliente por plataforma.

  Hasta entonces, la sincronización en el móvil es **WebDAV** (Nextcloud, ownCloud,
  Synology, etc.).
- **WebDAV en Android** va por HTTPS con el OpenSSL de KDAB, empaquetado en el
  APK. El workflow comprueba que `libssl_3.so` y `libcrypto_3.so` están dentro.
  Si el servidor usa `http://` sin cifrar, también funciona: Qt usa sus propios
  sockets, y ni la política *cleartext* de Android ni el ATS de iOS se aplican.
  Las contraseñas viajarían en claro, así que es mejor usar HTTPS.
- **Recordatorios:** suenan solo con la app abierta y en primer plano. iOS y
  Android suspenden las apps en segundo plano y Tagoror todavía no programa
  notificaciones del sistema. El permiso `POST_NOTIFICATIONS` ya está declarado
  para cuando las haya, con `UNUserNotificationCenter` en iOS y
  `AlarmManager`/`NotificationManager` en Android.
- **Copia en una carpeta y mover la carpeta de datos:** dentro del sandbox de
  iOS y Android apenas sirven.
- **Elegir ficheros** (importar un tono WAV, adjuntar imágenes): en Android el
  selector puede devolver rutas `content://` que la app todavía no sabe abrir.
- **Permisos de más en Android:** además de los nuestros, Qt Multimedia declara
  por su cuenta `CAMERA`, `BLUETOOTH`, `MODIFY_AUDIO_SETTINGS` y
  `WRITE_EXTERNAL_STORAGE`. No se piden mientras la app no los use, pero salen en
  la ficha del APK. Se pueden quitar con un `tools:node="remove"` en el
  manifiesto cuando moleste.
- **Tamaño:** el APK pesa unos 58 MB, porque en Android Qt Multimedia lleva
  FFmpeg dentro. En iOS se usa el backend nativo (AVFoundation) y no hace falta.
- **Icono en iOS:** sale el genérico. Hace falta un catálogo de assets con un PNG
  de 1024×1024.
