/* Arnés del test de la actualizacion OTA (ver ota_version.sh).
 *
 * El bloque "INICIO/FIN BLOQUE PRUEBA" de main/net/ota_rx.c se copia TAL CUAL
 * aqui (lo mete el script): son las funciones que deciden si un fichero se
 * instala o no. Se prueban sin placa porque un error aqui no se ve hasta que la
 * pantalla se queda con un firmware que no arranca -- y para eso ya no hay
 * pantalla donde mirar.
 *
 * Casos que importan de verdad:
 *   - la comparacion es NUMERICA: v1.9 es MAS VIEJA que v1.10 (como texto, al
 *     reves). Si se comparara texto, la 5" no se actualizaria nunca de la v1.9
 *     a la v1.10.
 *   - una version IGUAL o MAS VIEJA no se instala (gasta un hueco de flash y no
 *     arregla nada; una mas vieja quita lo que ya funcionaba).
 *   - un sha256 que no cuadra no se instala: es el unico dato que dice que los
 *     2 MB que llegaron son exactamente el fichero que la P4 anuncio.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

__FN__

static int fallos;
static int pruebas;

static void ok(const char *que)
{
    pruebas++;
    printf("ok     %s\n", que);
}

static void mal(const char *que)
{
    pruebas++;
    fallos++;
    printf("FALLO  %s\n", que);
}

static void caso_mas_nueva(const char *nueva, const char *actual, bool esperado)
{
    bool got = ota_rx_version_es_mas_nueva(nueva, actual);
    char que[128];
    snprintf(que, sizeof que, "mas nueva? %-12s > %-12s -> %s (esperado %s)",
             nueva ? nueva : "(null)", actual ? actual : "(null)",
             got ? "si" : "no", esperado ? "si" : "no");
    if (got == esperado) ok(que); else mal(que);
}

static void caso_aceptable(const char *anunciada, const char *actual,
                           const char *embebida, bool esperado)
{
    bool got = ota_rx_version_aceptable(anunciada, actual, embebida);
    char que[160];
    snprintf(que, sizeof que, "instalar? anunciada=%s actual=%s fichero=%s -> %s (esperado %s)",
             anunciada ? anunciada : "(null)", actual ? actual : "(null)",
             embebida ? embebida : "(null)",
             got ? "si" : "no", esperado ? "si" : "no");
    if (got == esperado) ok(que); else mal(que);
}

static void caso_sha(const uint8_t calculado[32], const char *hex, bool esperado,
                     const char *que)
{
    bool got = ota_rx_sha_coincide(calculado, hex);
    char linea[192];
    snprintf(linea, sizeof linea, "sha256 %s -> %s (esperado %s)",
             que, got ? "vale" : "no vale", esperado ? "vale" : "no vale");
    if (got == esperado) ok(linea); else mal(linea);
}

/* sha256 de la cadena vacia (e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855).
 * Sirve igual que cualquiera: lo que se prueba es la COMPARACION, no el hash. */
static const uint8_t SHA_VACIO[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
};
#define SHA_VACIO_HEX "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define SHA_VACIO_MAYUS "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"

int main(void)
{
    /* ── 1. Comparacion de versiones: NUMERICA ───────────────────────────── */
    caso_mas_nueva("v1.31", "v1.30", true);
    caso_mas_nueva("v1.30", "v1.30", false);   /* igual no es mas nueva */
    caso_mas_nueva("v1.29", "v1.30", false);
    /* El caso que hunde la comparacion de texto. */
    caso_mas_nueva("v1.10", "v1.9", true);
    caso_mas_nueva("v1.9", "v1.10", false);
    caso_mas_nueva("v1.2", "v1.10", false);
    caso_mas_nueva("v2.0", "v1.99", true);
    /* Tres numeros: v1.30.1 es mas nueva que v1.30. */
    caso_mas_nueva("v1.30.1", "v1.30", true);
    caso_mas_nueva("v1.30", "v1.30.1", false);
    caso_mas_nueva("v1.31", "v1.30.9", true);
    /* Lo que no se puede comparar NO se actualiza (build sin tags, sufijos). */
    caso_mas_nueva("v1.31", "v?", false);
    caso_mas_nueva("v?", "v1.30", false);
    caso_mas_nueva("v1.31", "v1.30-dirty", false);
    caso_mas_nueva("v1.31", NULL, false);
    caso_mas_nueva(NULL, "v1.30", false);
    caso_mas_nueva("", "v1.30", false);
    caso_mas_nueva("1.31", "v1.30", false);   /* sin la "v": no es el formato */

    /* ── 2. Solo se instala lo que es MAS NUEVO y es el fichero anunciado ── */
    caso_aceptable("v1.31", "v1.30", "v1.31", true);
    caso_aceptable("v1.30", "v1.30", "v1.30", false);  /* no mas nueva */
    caso_aceptable("v1.29", "v1.30", "v1.29", false);  /* mas vieja */
    caso_aceptable("v1.31", "v1.30", "v1.30", false);  /* el .bin NO es el anunciado */
    caso_aceptable("v1.31", "v1.30", "v1.32", false);  /* ni ese otro */
    caso_aceptable("v1.31", "v1.30", "", false);
    caso_aceptable("v1.31", "v?", "v1.31", false);     /* actual desconocida */

    /* ── 3. El sha256: un solo byte distinto NO vale ─────────────────────── */
    caso_sha(SHA_VACIO, SHA_VACIO_HEX, true, "el anunciado");
    caso_sha(SHA_VACIO, SHA_VACIO_MAYUS, true, "el anunciado en mayusculas");
    caso_sha(SHA_VACIO,
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b856",
             false, "con el ultimo digito cambiado");
    caso_sha(SHA_VACIO,
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85",
             false, "con 63 digitos");
    caso_sha(SHA_VACIO,
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855ff",
             false, "con 66 digitos");
    caso_sha(SHA_VACIO,
             "zzb0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
             false, "con algo que no es hex");
    caso_sha(SHA_VACIO, "", false, "vacio");
    caso_sha(SHA_VACIO, NULL, false, "sin anunciar");

    /* Un byte del calculado distinto: el fichero se corrompio por el camino. */
    uint8_t corrompido[32];
    memcpy(corrompido, SHA_VACIO, sizeof corrompido);
    corrompido[7] ^= 0x01;
    caso_sha(corrompido, SHA_VACIO_HEX, false, "con un byte del fichero distinto");

    printf("\n%d pruebas, %d fallos\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
