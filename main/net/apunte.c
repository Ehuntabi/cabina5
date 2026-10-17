/* apunte.c — monta el JSON de un registro para mandarlo a la P4.
 *
 * Vive aparte de view_registro.c porque aquel ya pasa de las 2.000 lineas y
 * esto no es interfaz: es el formato de lo que viaja por el cable.
 *
 * EL SELLO DE TIEMPO ES LO DELICADO. Cada apunte se fecha CUANDO OCURRE, no
 * cuando se entrega. Caso normal: repostas con la P4 apagada, el apunte se
 * queda en cola y sale dos dias despues al arrancar el motor; si la hora la
 * pusiera la P4 al recibirlo, ese repostaje figuraria dos dias tarde. De ahi
 * reloj_ahora(), que corrige el tiempo transcurrido desde que se supo la hora.
 *
 * Si esta pantalla NUNCA ha visto a la P4 desde que arranco, no hay hora de
 * ningun tipo: el apunte se marca "aprox":true y la P4 le pondra la de
 * recepcion. Va marcado a proposito, para que al leer el fichero se sepa que
 * ese dato no es exacto en vez de creerselo.
 *
 * SI NO CABE, SE DICE. Los helpers devuelven 0 en cuanto algo no entra, y ese 0
 * se arrastra hasta el final. Antes se dejaba 'used' como estaba y se seguia:
 * el buffer acababa con un JSON cortado a medias que SI pasaba el filtro de
 * longitud, la P4 lo rechazaba con un 400 y la cola lo descartaba por invalido.
 * Resultado: la pantalla decia "guardado" y el apunte no existia en ningun
 * sitio. Paso con la parada completa (505 bytes en un buffer de 384), que es
 * justo el apunte que mas cuesta rellenar. Detectado auditando el 22-ago-2026.
 *
 * Las claves de "datos" van SIEMPRE en el mismo orden para un tipo dado: la P4
 * hace la cabecera del CSV con ellas, y si cambiaran de orden las columnas se
 * desalinearian. Si algun dia se añade un campo, la P4 lo detecta al comparar
 * la cabecera y escribe una nueva (ver config_server_viaje.c).
 */
#include "apunte.h"
#include "reloj.h"
#include <stdio.h>
#include <string.h>

/* Copia 'in' a 'out' desde la posicion 'j' YA ESCAPADA para JSON.
 *
 * ANTES SOLO SE ESCAPABAN LA COMILLA Y LA BARRA INVERTIDA (17-oct-2026), con el
 * argumento de que "los textos los filtra el teclado". Falso: un salto de linea,
 * un tabulador o cualquier byte de control que llegue al campo (pegando texto, o
 * porque algun dia un campo se edita fuera de este teclado) deja el cuerpo en
 * JSON INVALIDO; la P4 contesta 400, y como la cola NO descarta lo rechazado a
 * proposito, ESE apunte se queda de cabeza para siempre y atasca los 14
 * siguientes y el cierre (los dos huecos de control solo salvan el fin).
 *
 * Se escapa todo lo que el JSON no admite dentro de una cadena:
 *   - la comilla y la barra invertida, con barra;
 *   - los bytes de control (< 0x20) y 0x7F, como \u00XX;
 *   - el resto (incluido UTF-8 de varios bytes, que es valido tal cual) se copia.
 *
 * DEVUELVE el nuevo 'j', o 0 si el texto NO CABE ENTERO. Se prefiere eso a
 * cortarlo: un texto a medias es un dato perdido sin que nadie se entere, y
 * quien llama ya tiene el camino de "no entra" (devuelve 0 y avisa). */
/* Longitud de la secuencia UTF-8 que empieza en 'p': 1..4, o 0 si es invalida.
 *
 * POR QUE HACE FALTA (17-oct-2026, arbitraje de entrada): json_esc_en copiaba
 * tal cual todo byte >= 0x80, asi que un byte suelto o una secuencia cortada
 * (0xC3 solo, 0xFF, un texto partido por un buffer) dejaban el cuerpo en un JSON
 * que no es UTF-8 valido. cJSON no valida y el byte roto acababa en el CSV, con
 * lo que el fichero deja de abrirse bien en el PC. Hoy no es alcanzable desde el
 * teclado (LVGL solo inserta glifos de su mapa y one_line descarta los saltos),
 * pero un set_text desde una NVS corrupta o un POST a mano si llegan aqui.
 *
 * Rechaza tambien las sobrecargas (E0 A0..BF) y los surrogates (ED 80..9F), que
 * es lo que la RFC 3629 no admite. */
static size_t utf8_largo(const unsigned char *p)
{
    if (p[0] < 0x80) return 1;
    if (p[0] >= 0xC2 && p[0] <= 0xDF) return ((p[1] & 0xC0) == 0x80) ? 2 : 0;
    if (p[0] >= 0xE0 && p[0] <= 0xEF) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
        if (p[0] == 0xE0 && p[1] < 0xA0) return 0;
        if (p[0] == 0xED && p[1] > 0x9F) return 0;
        return 3;
    }
    if (p[0] >= 0xF0 && p[0] <= 0xF4) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
            (p[3] & 0xC0) != 0x80) return 0;
        if (p[0] == 0xF0 && p[1] < 0x90) return 0;
        if (p[0] == 0xF4 && p[1] > 0x8F) return 0;
        return 4;
    }
    return 0;
}

static size_t json_esc_en(char *out, size_t n, size_t j, const char *in)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; in && in[i]; i++) {
        unsigned char c = (unsigned char)in[i];
        size_t esc = (c == '"' || c == '\\') ? 2 : ((c < 0x20 || c == 0x7F) ? 6 : 1);
        /* Para los bytes >= 0x80 lo que ocupa es la longitud REAL de su
         * secuencia UTF-8 (o 1 si es invalida: luego se sustituye por '?'). */
        if (c >= 0x80) {
            size_t l = utf8_largo((const unsigned char *)in + i);
            esc = (l == 0) ? 1 : l;
        }
        if (j + esc + 1 >= n) return 0;      /* +1: el cero final */
        if (c >= 0x80) {
            if (esc == 1) {
                /* Secuencia rota: se sustituye por '?' y se sigue con el byte
                 * siguiente. */
                out[j++] = '?';
                continue;
            }
            for (size_t k = 0; k < esc; k++) out[j++] = in[i + k];
            i += esc - 1;
            continue;
        }
        if (esc == 2) {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (esc == 6) {
            out[j++] = '\\'; out[j++] = 'u'; out[j++] = '0'; out[j++] = '0';
            out[j++] = HEX[(c >> 4) & 0xF];
            out[j++] = HEX[c & 0xF];
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = 0;
    return j;
}

size_t apunte_cabecera(char *out, size_t n, uint32_t id, const char *tipo)
{
    uint32_t ts = 0;
    bool exacta = reloj_ahora(&ts);
    int w = snprintf(out, n,
        "{\"op\":\"registro\",\"id\":%lu,\"tipo\":\"%s\",\"ts\":%lu,\"aprox\":%s,\"datos\":{",
        (unsigned long)id, tipo, (unsigned long)ts, exacta ? "false" : "true");
    /* Misma regla que sus tres hermanas (apunte_campo_txt/num/cerrar), que ya la
     * tenian: si el snprintf NO cupo, se devuelve 0 y ese 0 se arrastra por todas
     * las llamadas siguientes -- quien llama lo ve (u == 0) y avisa, en vez de
     * mandar un JSON cortado a medias que la P4 rechaza con un 400 y la cola
     * acaba descartando. Aqui hoy no se llega nunca (la cabecera mide <= 89 B y
     * el buffer mas pequeño de los dos que la usan es de 384), pero era la unica
     * de las cuatro sin la comprobacion y el fallo habria sido mudo otra vez.
     * Auditoria del 16-oct-2026. */
    return (w > 0 && (size_t)w < n) ? (size_t)w : 0;
}

size_t apunte_campo_txt(char *out, size_t n, size_t used,
                        const char *clave, const char *valor)
{
    if (used == 0) return 0;               /* ya se desbordo antes */
    /* SIN buffer de paso (17-oct-2026): antes se copiaba a un char[64] y lo que
     * pasaba de ~61 bytes se TRUNCABA en silencio. Hoy los topes de los campos
     * son de 40, asi que no se llegaba, pero era una mina: el dia que un campo
     * crezca, ese texto se pierde sin avisar. Escribiendo directo en el cuerpo,
     * el unico tope es el del cuerpo (CUERPO_MAX) y, si no cabe, esto devuelve 0
     * y quien llama avisa. */
    int w = snprintf(out + used, n - used, "%s\"%s\":\"",
                     out[used - 1] != '{' ? "," : "", clave);
    if (w < 0 || (size_t)w >= n - used) return 0;
    size_t j = json_esc_en(out, n, used + (size_t)w, valor);
    if (j == 0 || j + 2 > n) return 0;
    out[j++] = '"';
    out[j] = 0;
    return j;
}

size_t apunte_campo_num(char *out, size_t n, size_t used,
                        const char *clave, long valor)
{
    if (used == 0) return 0;
    int w = snprintf(out + used, n - used, "%s\"%s\":%ld",
                     out[used - 1] != '{' ? "," : "", clave, valor);
    return (w > 0 && (size_t)w < n - used) ? used + (size_t)w : 0;
}

size_t apunte_cerrar(char *out, size_t n, size_t used, const char *resumen)
{
    if (used == 0) return 0;
    /* Igual que apunte_campo_txt: el resumen se escapa directo en el cuerpo, sin
     * el char[96] de antes (que truncaba todo lo que pasara de ~93 bytes). */
    int w = snprintf(out + used, n - used, "},\"resumen\":\"");
    if (w < 0 || (size_t)w >= n - used) return 0;
    size_t j = json_esc_en(out, n, used + (size_t)w, resumen ? resumen : "");
    if (j == 0 || j + 3 > n) return 0;
    out[j++] = '"';
    out[j++] = '}';
    out[j] = 0;
    return j;
}
