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

/* Escapa lo justo para no romper el JSON: comillas y barra invertida. El resto
 * del contenido ya es ASCII limitado (los campos son numeros y los textos los
 * filtra el teclado), asi que no hace falta un escapador completo. */
static void json_str(char *out, size_t n, const char *in)
{
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 2 < n; i++) {
        if (in[i] == '"' || in[i] == '\\') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = 0;
}

size_t apunte_cabecera(char *out, size_t n, uint32_t id, const char *tipo)
{
    uint32_t ts = 0;
    bool exacta = reloj_ahora(&ts);
    return (size_t)snprintf(out, n,
        "{\"op\":\"registro\",\"id\":%lu,\"tipo\":\"%s\",\"ts\":%lu,\"aprox\":%s,\"datos\":{",
        (unsigned long)id, tipo, (unsigned long)ts, exacta ? "false" : "true");
}

size_t apunte_campo_txt(char *out, size_t n, size_t used,
                        const char *clave, const char *valor)
{
    if (used == 0) return 0;               /* ya se desbordo antes */
    char v[64];
    json_str(v, sizeof(v), valor);
    int w = snprintf(out + used, n - used, "%s\"%s\":\"%s\"",
                     out[used - 1] != '{' ? "," : "", clave, v);
    return (w > 0 && (size_t)w < n - used) ? used + (size_t)w : 0;
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
    char r[96];
    json_str(r, sizeof(r), resumen ? resumen : "");
    int w = snprintf(out + used, n - used, "},\"resumen\":\"%s\"}", r);
    return (w > 0 && (size_t)w < n - used) ? used + (size_t)w : 0;
}
