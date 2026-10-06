/* Credenciales de fabrica para la PRIMERA vez que arranca con NVS vacia.
 *
 * NO se versiona de verdad en el proyecto del 3,5" (ahi este fichero esta en
 * .gitignore) y la app las guarda luego en NVS para poder cambiar de P4 desde
 * Ajustes. En este proyecto de banco se usan directas para no arrastrar aun el
 * modulo de configuracion; cuando se copie la app del 35cabina, este fichero se
 * sustituye por el suyo (mismo nombre, mismo contenido).
 *
 * La P4 de la autocaravana levanta el AP "VictronConfig" (clave mw2yechk).
 */
#pragma once

#define WIFI_CRED_SSID  "VictronConfig"
#define WIFI_CRED_PASS  "mw2yechk"
