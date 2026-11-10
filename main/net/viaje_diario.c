/* viaje_diario.c — copia duradera de cada apunte del viaje, en la flash de la
 * pantalla. La cabecera del .h explica el porque; aqui va el como.
 *
 * NADA DE SISTEMA DE FICHEROS. La particion se escribe registro a registro con
 * esp_partition_write(). Un FAT o un littlefs anaden piezas moviles (tablas,
 * superbloque) que un apagon a mitad puede dejar a medias, y justo eso es lo que
 * no nos podemos permitir en el unico sitio donde vive el viaje. Asi el unico
 * estado es "registros validos hasta aqui", que se reconstruye leyendo.
 */
#include "viaje_diario.h"

#include <string.h>

#ifndef VIAJE_DIARIO_TEST
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static const char *TAG = "viaje_diario";
#else
#include <stdio.h>
#include <stdint.h>
/* Reloj de mentira: avanza un segundo por llamada, solo para el limitador de
 * avisos (en la placa es esp_timer_get_time()). */
static int64_t s_reloj_falso;
static int64_t esp_timer_get_time(void) { return s_reloj_falso += 1000000; }
#define ESP_LOGE(tag, fmt, ...) printf("E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I %s: " fmt "\n", tag, ##__VA_ARGS__)
static const char *TAG = "viaje_diario";
#endif

/* ── Formato en flash ────────────────────────────────────────────────────────
 * La particion son DOS mitades iguales. Una esta activa (se escribe al final) y
 * la otra es el destino de la proxima compactacion (ver compacta()).
 *
 *   mitad = [cabecera 16 B][registro][registro]...
 *   cabecera = "VDH1" + generacion (u32) + dos u32 a cero
 *   registro = "VDA1" + id(u32) + len(u16) + flags(u16) + crc(u32) + cuerpo
 *
 * La cabecera de la mitad nueva se escribe AL FINAL de la copia: mientras no
 * este, la mitad nueva no vale y al arrancar se lee la vieja, que no se toca
 * hasta entonces. Eso es lo que hace segura una compactacion interrumpida.
 */
#define CAB_MAGIC   "VDH1"
#define REG_MAGIC   "VDA1"
#define CAB_TAM     16
#define REG_TAM     16

#define FLAG_PENDIENTE   0
#define FLAG_ENTREGADO   1
#define FLAG_DESCARTADO  2

typedef struct {
    uint32_t id;
    uint32_t off;      /* offset absoluto DENTRO de la particion */
    uint16_t len;
} pend_t;

static pend_t   s_pend[VIAJE_DIARIO_MAX_PEND];
static size_t   s_pend_n;
static uint32_t s_base;         /* offset de la mitad activa */
static uint32_t s_cursor;       /* donde va el proximo registro (absoluto) */
static uint32_t s_gen;          /* generacion de la mitad activa */
static uint32_t s_half;         /* tamano de cada mitad */
static bool     s_listo;
static bool     s_sin_sitio;    /* el indice de RAM se ha llenado */

/* CERROJO. Anotar puede llegar de la tarea de LVGL (el usuario pulsa GUARDAR) y
 * confirmar/compactar de la tarea del repartidor: sin cerrojo, una compactacion
 * a la vez que un alta pueden dejar el indice (y el cursor de la flash) hechos
 * un cristo. Es corto y sin llamadas hacia fuera (nada de avisar_cambio() con el
 * cerrojo cogido), asi que no puede haber interbloqueo con el lock de LVGL. */
#ifndef VIAJE_DIARIO_TEST
static SemaphoreHandle_t s_mtx;
#define DIARIO_LOCK()   do { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); } while (0)
#define DIARIO_UNLOCK() do { if (s_mtx) xSemaphoreGive(s_mtx); } while (0)
#else
#define DIARIO_LOCK()   do { } while (0)
#define DIARIO_UNLOCK() do { } while (0)
#endif

static bool compacta(void);   /* se define mas abajo; init la usa para limpiar */

/* ── Acceso a la flash: dos funciones y tres wrappers ───────────────────────
 * En la placa van a esp_partition_*; en el test, a un array en RAM del mismo
 * tamano que la particion. Todo lo de arriba se prueba con el codigo REAL. */
#ifndef VIAJE_DIARIO_TEST
static const esp_partition_t *s_part;

static bool fl_leer(uint32_t off, void *dst, size_t n)
{
    return esp_partition_read(s_part, off, dst, n) == ESP_OK;
}
static bool fl_escribir(uint32_t off, const void *src, size_t n)
{
    return esp_partition_write(s_part, off, src, n) == ESP_OK;
}
static bool fl_borrar(uint32_t off, size_t n)
{
    return esp_partition_erase_range(s_part, off, n) == ESP_OK;
}
#else
/* Test en el PC: 1 MB de flash simulada. El tamano tiene que ser el de la
 * particion de verdad (ver partitions.csv). */
#define TEST_FLASH_TAM  (1024u * 1024u)
static uint8_t s_flash[TEST_FLASH_TAM];
static bool    s_fallar;
static int     s_fallan_tras = -1;   /* -1 = no; >=0 = escrituras que quedan */

static bool fl_leer(uint32_t off, void *dst, size_t n)
{
    if (off + n > TEST_FLASH_TAM) return false;
    memcpy(dst, s_flash + off, n);
    return true;
}
static bool fl_escribir(uint32_t off, const void *src, size_t n)
{
    if (s_fallar) return false;
    if (s_fallan_tras == 0) return false;          /* corte a mitad de la copia */
    if (s_fallan_tras > 0) s_fallan_tras--;
    if (off + n > TEST_FLASH_TAM) return false;
    memcpy(s_flash + off, src, n);
    return true;
}
static bool fl_borrar(uint32_t off, size_t n)
{
    if (s_fallar) return false;
    if (off + n > TEST_FLASH_TAM) return false;
    memset(s_flash + off, 0xFF, n);
    return true;
}
void viaje_diario_test_falla_tras(int n) { s_fallan_tras = n; }
void viaje_diario_test_falla(bool fallar) { s_fallar = fallar; }
/* Media mitad, para forzar compactaciones sin escribir 500 KB. */
void viaje_diario_test_mitad(uint32_t bytes)
{
    s_half = bytes;
    s_listo = false;
    s_pend_n = 0;
    s_base = s_cursor = s_gen = 0;
}

/* Rompe el cuerpo del i-esimo pendiente (simula corte a mitad de escritura). */
void viaje_diario_test_corrompe_pendiente(size_t i)
{
    if (i >= s_pend_n) return;
    uint32_t off = s_pend[i].off + REG_TAM;
    s_flash[off] ^= 0xFF;
}

uint32_t viaje_diario_test_base(void) { return s_base; }

/* Borra la cabecera de una mitad (simula corte en el commit de compactacion). */
void viaje_diario_test_rompe_cabecera(uint32_t base)
{
    if (base + CAB_TAM > TEST_FLASH_TAM) return;
    memset(s_flash + base, 0xFF, CAB_TAM);
}

void viaje_diario_test_borra_todo(void)
{
    memset(s_flash, 0xFF, sizeof s_flash);
    s_fallar = false;
    s_fallan_tras = -1;
    s_listo = false;
    s_pend_n = 0;
    s_base = s_cursor = s_gen = 0;
    s_half = TEST_FLASH_TAM / 2;
    s_sin_sitio = false;
}
#endif

/* ── CRC-32 (IEEE, reflejado). Se implementa aqui para que el test del PC
 * pruebe exactamente el mismo calculo sin arrastrar el ROM del chip. ──────── */
static uint32_t crc_tabla[16];

static void crc_init(void)
{
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t c = i;
        for (int k = 0; k < 4; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_tabla[i] = c;
    }
}

/* CRC-32 IEEE (el de siempre: "123456789" da 0xCBF43926). OJO: el primer
 * intento procesaba UN SOLO nibble por byte y daba 0x020B999F para esa misma
 * cadena -- un CRC que parece un CRC, es consistente consigo mismo y por eso
 * las pruebas pasaban, pero no es el estandar y detecta peor los errores (un
 * byte cambiado en el nibble alto se le colaba mas facil). Corregido el
 * 10-nov-2026 al volcar la particion de la placa y comparar con zlib. */
static uint32_t crc32(const void *datos, size_t n)
{
    const uint8_t *p = (const uint8_t *)datos;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c = crc_tabla[(c ^ p[i]) & 0x0F] ^ (c >> 4);
        c = crc_tabla[(c ^ (p[i] >> 4)) & 0x0F] ^ (c >> 4);
    }
    return c ^ 0xFFFFFFFFu;
}

/* Vector conocido de CRC: "123456789" TIENE que dar 0xCBF43926. Sin esto, un
 * CRC que no es el estandar (el que se colo hasta el 10-nov-2026, que daba
 * 0x020B999F) pasa todas las demas pruebas, porque escribir y leer usan la
 * misma funcion. */
uint32_t viaje_diario_test_crc_vector(void)
{
    crc_init();
    return crc32("123456789", 9);
}


/* ── Cabecera de la mitad ────────────────────────────────────────────────── */
typedef struct {
    char     magic[4];
    uint32_t gen;
    uint32_t cero[2];
} __attribute__((packed)) cab_t;

static bool cab_leer(uint32_t base, cab_t *out)
{
    if (!fl_leer(base, out, sizeof *out)) return false;
    if (memcmp(out->magic, CAB_MAGIC, 4) != 0) return false;
    if (out->cero[0] != 0 || out->cero[1] != 0) return false;
    return true;
}

static bool cab_escribir(uint32_t base, uint32_t gen)
{
    cab_t c;
    memset(&c, 0, sizeof c);
    memcpy(c.magic, CAB_MAGIC, 4);
    c.gen = gen;
    /* Se escribe DENTRO de la mitad ya rellena: es el commit. */
    return fl_escribir(base, &c, sizeof c);
}

/* ── Registro ─────────────────────────────────────────────────────────────── */
typedef struct {
    char     magic[4];
    uint32_t id;
    uint16_t len;
    uint16_t flags;
    uint32_t crc;
} __attribute__((packed)) reg_t;

static uint32_t reg_tam_total(uint16_t len)
{
    /* cabecera + cuerpo + relleno hasta multiplo de 4 (esp_partition_write
     * quiere offsets y tamanos alineados) */
    return REG_TAM + (uint32_t)((len + 3u) & ~3u);
}

/* Flags con los que quedo marcado un id (1 entregado, 2 descartado). Se busca
 * la ULTIMA marca de ese id en la mitad activa. Devuelve 0xFFFF si no hay. */
uint16_t viaje_diario_test_marca_de(uint32_t id)
{
    uint32_t off = s_base + CAB_TAM;
    uint16_t ultima = 0xFFFF;
    while (off + REG_TAM <= s_base + s_half) {
        reg_t r;
        if (!fl_leer(off, &r, sizeof r)) break;
        if (memcmp(r.magic, REG_MAGIC, 4) != 0) break;
        if (r.len > VIAJE_DIARIO_CUERPO_MAX) break;
        if (r.id == id && r.len == 0) ultima = r.flags;
        off += reg_tam_total(r.len);
    }
    return ultima;
}
uint32_t viaje_diario_test_gen(void) { return s_gen; }


static void pend_quita(size_t i)
{
    if (i >= s_pend_n) return;
    for (size_t k = i; k + 1 < s_pend_n; k++) s_pend[k] = s_pend[k + 1];
    s_pend_n--;
    s_sin_sitio = false;
}

static bool pend_anade(uint32_t id, uint32_t off, uint16_t len)
{
    if (s_pend_n >= VIAJE_DIARIO_MAX_PEND) {
        /* No se pierde el dato (esta en la flash), pero no se puede vigilar:
         * se avisa y a partir de aqui no se anotan mas. Un viaje con mas de
         * VIAJE_DIARIO_MAX_PEND apuntes sin entregar ya no es un viaje normal. */
        s_sin_sitio = true;
        ESP_LOGE(TAG, "indice de pendientes lleno (%u); no se anotan mas",
                 (unsigned)s_pend_n);
        return false;
    }
    s_pend[s_pend_n].id = id;
    s_pend[s_pend_n].off = off;
    s_pend[s_pend_n].len = len;
    s_pend_n++;
    return true;
}

/* ── Recorrer una mitad valida y reconstruir el indice ───────────────────── */
static bool escanear(uint32_t base, uint32_t *gen_out, bool *sucio_out)
{
    cab_t c;
    if (sucio_out) *sucio_out = true;
    if (!cab_leer(base, &c)) return false;

    uint32_t off = base + CAB_TAM;
    uint32_t fin = base + s_half;

    while (off + REG_TAM <= fin) {
        reg_t r;
        if (!fl_leer(off, &r, sizeof r)) break;
        if (memcmp(r.magic, REG_MAGIC, 4) != 0) break;
        if (r.len > VIAJE_DIARIO_CUERPO_MAX) break;
        uint32_t total = reg_tam_total(r.len);
        if (off + total > fin) break;

        if (r.len > 0) {
            /* ESTATICO Y NO DE PILA: esta funcion corre en la tarea main, que
             * tiene 3,5 KB (CONFIG_ESP_MAIN_TASK_STACK_SIZE). Con 896 B aqui y
             * 1,5 KB mas en compacta() la pila se desbordaba y la placa
             * arrancaba en bucle (LoadProhibited, visto el 10-nov-2026 con la
             * pantalla en blanco y negro). El cerrojo del diario hace seguro
             * compartir el buffer. */
            static char cuerpo[VIAJE_DIARIO_CUERPO_MAX];
            if (!fl_leer(off + REG_TAM, cuerpo, r.len)) break;
            if (crc32(cuerpo, r.len) != r.crc) {
                /* Escritura interrumpida: este y lo que venga detras no valen. */
                ESP_LOGW(TAG, "registro con CRC malo en %lu: se corta ahi",
                         (unsigned long)off);
                break;
            }
            if (r.flags == FLAG_PENDIENTE) pend_anade(r.id, off, r.len);
        } else if (r.flags == FLAG_ENTREGADO || r.flags == FLAG_DESCARTADO) {
            for (size_t i = 0; i < s_pend_n; i++)
                if (s_pend[i].id == r.id) { pend_quita(i); break; }
        }
        off += total;
    }

    s_base = base;
    s_cursor = off;
    s_gen = c.gen;
    *gen_out = c.gen;
    /* ¿Queda basura detras del ultimo registro valido? La flash solo deja
     * pasar bits de 1 a 0: escribir encima de bytes ya escritos NO los
     * reemplaza, los MEZCLA. Si no se limpia, el siguiente apunte se graba
     * sobre los restos del anterior y sale un cuerpo corrupto. */
    /* SUCIO = hay bytes escritos justo detras del cursor (no simplemente que
     * quede sitio). Antes se miraba "off < fin de la mitad", que es SIEMPRE
     * cierto porque se reserva el margen de la cabecera: el diario compactaba en
     * CADA arranque y avisaba siempre (hallazgo del arbitraje del 10-nov-2026).
     * Se miran los 16 bytes siguientes: si son 0xFF, la zona esta limpia. */
    if (sucio_out) {
        uint8_t siguiente[REG_TAM];
        *sucio_out = false;
        if (off + REG_TAM <= base + s_half && fl_leer(off, siguiente, sizeof siguiente)) {
            for (size_t k = 0; k < sizeof siguiente; k++) {
                if (siguiente[k] != 0xFF) { *sucio_out = true; break; }
            }
        }
    }
    return true;
}

void viaje_diario_init(void)
{
    crc_init();
#ifndef VIAJE_DIARIO_TEST
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
#endif
    s_pend_n = 0;
    s_sin_sitio = false;

#ifndef VIAJE_DIARIO_TEST
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      (esp_partition_subtype_t)0x40,
                                      VIAJE_DIARIO_PARTICION);
    if (!s_part) {
        ESP_LOGW(TAG, "no hay particion '%s': el viaje se queda sin copia "
                      "duradera (solo la cola de la NVS)", VIAJE_DIARIO_PARTICION);
        s_listo = false;
        return;
    }
    s_half = (uint32_t)s_part->size / 2u;
    s_half &= ~0xFFFu;    /* multiplo de sector, para poder borrar por mitades */
#endif
    /* En el test el tamano de mitad lo fija el arnes (ver test_mitad), para
     * poder forzar compactaciones sin escribir 500 KB. */

    uint32_t gen0 = 0, gen1 = 0;
    bool sucio0 = false, sucio1 = false;
    bool ok0 = escanear(0, &gen0, &sucio0);
    bool ok1 = escanear(s_half, &gen1, &sucio1);

    if (!ok0 && !ok1) {
        /* Primera vez (o las dos mitades inservibles): se estrena la primera. */
        if (!fl_borrar(0, s_half) || !cab_escribir(0, 1)) {
            ESP_LOGE(TAG, "no puedo estrenar el diario");
            s_listo = false;
            return;
        }
        s_base = 0;
        s_gen = 1;
        s_cursor = CAB_TAM;
        s_listo = true;
        ESP_LOGI(TAG, "diario estrenado (mitad de %lu B)", (unsigned long)s_half);
        return;
    }

    /* Manda la generacion mayor; si empate, la primera (no deberia pasar).
     * OJO: el escaneo de las dos mitades ha dejado en s_pend lo de las dos, asi
     * que el indice se vacia antes del escaneo definitivo. */
    s_pend_n = 0;
    s_sin_sitio = false;
    bool sucio;
    if (ok1 && (!ok0 || gen1 > gen0)) {
        escanear(s_half, &gen1, &sucio);
    } else {
        escanear(0, &gen0, &sucio);
    }
    s_listo = true;

    /* ── EL BORDE SUCIO SE LIMPIA COMPACTANDO (10-nov-2026) ───────────────
     * Esto se cazo en la placa: al cambiar el algoritmo de CRC (a su version
     * estandar), los registros viejos dejaron de cuadrar, el escaneo se corto en
     * el primero y el apunte siguiente se escribio ENCIMA de la zona sin borrar.
     * La flash solo deja pasar bits de 1 a 0, asi que el resultado fue un cuerpo
     * MEZCLADO: la P4 lo rechazo con 400 y el apunte parecia "irrecuperable".
     * Compactar copia los pendientes a la otra mitad (que se borra antes) y deja
     * el diario limpio detras del cursor. */
    if (sucio) {
        ESP_LOGW(TAG, "el diario tiene restos detras del ultimo registro: se "
                      "compacta para no escribir encima");
        if (!compacta()) {
            /* Sin poder limpiar, no se puede escribir con garantias: se marca
             * para que anota() diga que no, en vez de grabar basura. */
            s_sin_sitio = true;
            ESP_LOGE(TAG, "no puedo limpiar el diario: no se anotara nada hasta "
                          "que se pueda compactar");
        }
    }
    ESP_LOGI(TAG, "diario montado: %u pendientes, generacion %lu",
             (unsigned)s_pend_n, (unsigned long)s_gen);
}

uint32_t viaje_diario_id_de(const char *cuerpo)
{
    if (!cuerpo) return 0;
    /* El cuerpo lo monta apunte_cabecera()/p4_api.c: "id" va en la cabecera,
     * antes de cualquier texto libre, asi que buscar la primera aparicion de
     * "id": es seguro. */
    const char *p = strstr(cuerpo, "\"id\":");
    if (!p) return 0;
    p += 5;
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        uint32_t d = (uint32_t)(*p - '0');
        if (v > (UINT32_MAX - d) / 10u) return 0;   /* desbordaria */
        v = v * 10u + d;
        p++;
    }
    return v;
}

/* Copia los pendientes a la otra mitad y la marca como nueva (commit al final).
 * Si algo falla a mitad, la mitad vieja sigue siendo la valida. */
static bool compacta(void)
{
    uint32_t destino = (s_base == 0) ? s_half : 0;
    uint32_t gen_nueva = s_gen + 1;

    /* Si los pendientes no caben en UNA mitad, compactar no arregla nada y
     * ademas machacaria la copia buena: se dice que no y se deja todo como
     * esta. Con las mitades de la placa (512 KB) y el tope de 128 pendientes
     * esto no puede pasar; esta por si alguien cambia los tamanos. */
    uint64_t hace_falta = CAB_TAM;
    for (size_t i = 0; i < s_pend_n; i++) hace_falta += reg_tam_total(s_pend[i].len);
    if (hace_falta > s_half) {
        ESP_LOGE(TAG, "los pendientes (%u) no caben en una mitad (%lu B)",
                 (unsigned)s_pend_n, (unsigned long)s_half);
        return false;
    }

    if (!fl_borrar(destino, s_half)) {
        ESP_LOGE(TAG, "no puedo borrar la mitad de destino");
        return false;
    }

    /* ESTATICOS por el mismo motivo que en escanear(): compacta() se llama
     * desde init (pila de la tarea main) y desde anota()/confirma(). */
    static pend_t nuevos[VIAJE_DIARIO_MAX_PEND];
    size_t n_nuevos = 0;
    uint32_t off = destino + CAB_TAM;

    for (size_t i = 0; i < s_pend_n; i++) {
        static char cuerpo[VIAJE_DIARIO_CUERPO_MAX];
        if (!fl_leer(s_pend[i].off + REG_TAM, cuerpo, s_pend[i].len)) return false;
        if (!fl_escribir(off + REG_TAM, cuerpo, s_pend[i].len)) return false;
        reg_t r;
        memcpy(r.magic, REG_MAGIC, 4);
        r.id = s_pend[i].id;
        r.len = s_pend[i].len;
        r.flags = FLAG_PENDIENTE;
        r.crc = crc32(cuerpo, s_pend[i].len);
        if (!fl_escribir(off, &r, sizeof r)) return false;
        nuevos[n_nuevos].id = s_pend[i].id;
        nuevos[n_nuevos].off = off;
        nuevos[n_nuevos].len = s_pend[i].len;
        n_nuevos++;
        off += reg_tam_total(s_pend[i].len);
    }

    /* COMMIT: sin esta cabecera la mitad nueva no existe para nadie. */
    if (!cab_escribir(destino, gen_nueva)) return false;

    for (size_t i = 0; i < n_nuevos; i++) s_pend[i] = nuevos[i];
    s_pend_n = n_nuevos;
    s_base = destino;
    s_cursor = off;
    s_gen = gen_nueva;
    ESP_LOGI(TAG, "diario compactado: %u pendientes, generacion %lu",
             (unsigned)s_pend_n, (unsigned long)s_gen);
    return true;
}

static bool diario_anota(const char *cuerpo, uint32_t id)
{
    if (!s_listo || !cuerpo || !cuerpo[0] || id == 0) return false;

    /* G3 (revision del 10-nov-2026): si el indice de RAM esta lleno, NO se
     * escribe nada. Antes se escribia el registro y luego fallaba el alta en el
     * indice: quedaba un huerfano en la flash, s_cursor no avanzaba y la
     * siguiente alta PISABA ese registro. Asi, al menos, todo lo que se dice
     * guardado esta vigilado (y quien llama se entera y lo encola igual). */
    if (s_pend_n >= VIAJE_DIARIO_MAX_PEND) {
        if (!s_sin_sitio) {
            s_sin_sitio = true;
            ESP_LOGE(TAG, "el indice del diario esta lleno (%u): a partir de aqui "
                          "los apuntes van SOLO en la cola", (unsigned)s_pend_n);
        }
        return false;
    }
    size_t len = strlen(cuerpo);
    if (len > VIAJE_DIARIO_CUERPO_MAX) {
        ESP_LOGE(TAG, "apunte de %u B no cabe en el diario (tope %d)",
                 (unsigned)len, VIAJE_DIARIO_CUERPO_MAX);
        return false;
    }
    uint32_t total = reg_tam_total((uint16_t)len);

    /* ¿Cabe en la mitad activa, dejando sitio para la cabecera de la mitad de
     * destino? Ese margen garantiza que la copia de la compactacion siempre
     * entra (los pendientes ocupan como mucho lo que la mitad activa). */
    if (s_cursor + total + CAB_TAM > s_base + s_half) {
        if (!compacta()) return false;
        if (s_cursor + total + CAB_TAM > s_base + s_half) {
            static int64_t s_ultimo_aviso;
            int64_t ahora = esp_timer_get_time();
            if (ahora - s_ultimo_aviso > 60 * 1000 * 1000) {
                s_ultimo_aviso = ahora;
                ESP_LOGE(TAG, "el diario no tiene sitio ni compactado; los apuntes "
                              "siguen solo en la cola");
            }
            return false;
        }
    }

    /* Cuerpo PRIMERO y cabecera despues: si se va la corriente entre las dos,
     * el registro queda sin magic y al arrancar se descarta (y se reescribe
     * encima), en vez de quedar un registro "valido" con el cuerpo a medias. */
    if (!fl_escribir(s_cursor + REG_TAM, cuerpo, len)) return false;
    reg_t r;
    memcpy(r.magic, REG_MAGIC, 4);
    r.id = id;
    r.len = (uint16_t)len;
    r.flags = FLAG_PENDIENTE;
    r.crc = crc32(cuerpo, len);
    if (!fl_escribir(s_cursor, &r, sizeof r)) return false;

    if (!pend_anade(id, s_cursor, (uint16_t)len)) {
        /* Escrito en la flash pero sin poder vigilarlo: se dice que no para que
         * quien llama lo apunte en la cola como siempre (el dato no se pierde,
         * esta en la flash, pero no se promete el reenvio automatico). */
        return false;
    }
    s_cursor += total;
    return true;
}

static bool diario_confirma(uint32_t id, bool descartado)
{
    if (!s_listo || id == 0) return false;

    size_t i = 0;
    for (; i < s_pend_n; i++) if (s_pend[i].id == id) break;
    if (i == s_pend_n) return false;    /* no estaba pendiente: nada que hacer */

    uint32_t total = reg_tam_total(0);
    if (s_cursor + total + CAB_TAM > s_base + s_half) {
        if (!compacta()) return false;
        for (i = 0; i < s_pend_n; i++) if (s_pend[i].id == id) break;
        if (i == s_pend_n) return false;
    }

    reg_t r;
    memcpy(r.magic, REG_MAGIC, 4);
    r.id = id;
    r.len = 0;
    r.flags = descartado ? FLAG_DESCARTADO : FLAG_ENTREGADO;
    r.crc = crc32(NULL, 0);
    if (!fl_escribir(s_cursor, &r, sizeof r)) return false;

    s_cursor += total;
    pend_quita(i);
    return true;
}

static size_t diario_pendientes(void) { return s_pend_n; }

static bool diario_contiene(uint32_t id)
{
    for (size_t i = 0; i < s_pend_n; i++) if (s_pend[i].id == id) return true;
    return false;
}

static bool diario_ultimo(char *out, size_t n, uint32_t *id_out)
{
    if (!s_listo || s_pend_n == 0 || !out || n == 0) return false;
    size_t k = s_pend_n - 1;
    uint16_t len = s_pend[k].len;
    if ((size_t)len + 1 > n) return false;
    if (!fl_leer(s_pend[k].off + REG_TAM, out, len)) return false;
    out[len] = '\0';
    if (id_out) *id_out = s_pend[k].id;
    return true;
}

static bool diario_en(size_t k, char *out, size_t n, uint32_t *id_out)
{
    if (!s_listo || k >= s_pend_n || !out || n == 0) return false;
    uint16_t len = s_pend[k].len;
    if ((size_t)len + 1 > n) return false;
    if (!fl_leer(s_pend[k].off + REG_TAM, out, len)) return false;
    out[len] = '\0';
    if (id_out) *id_out = s_pend[k].id;
    return true;
}

static bool diario_siguiente(char *out, size_t n, uint32_t *id_out)
{
    if (!s_listo || s_pend_n == 0 || !out || n == 0) return false;
    /* Se guardan en orden de escritura, que es el orden del viaje. */
    uint16_t len = s_pend[0].len;
    if ((size_t)len + 1 > n) return false;
    if (!fl_leer(s_pend[0].off + REG_TAM, out, len)) return false;
    out[len] = '\0';
    if (id_out) *id_out = s_pend[0].id;
    return true;
}

/* ── Envoltorios con cerrojo (ver DIARIO_LOCK arriba) ──────────────────────
 * Los cuerpos van sin cerrojo porque se llaman entre ellos (anota y confirma
 * compactan) y porque el que decide cuanto dura el bloqueo es este, no ellos. */
bool viaje_diario_anota(const char *cuerpo, uint32_t id)
{
    DIARIO_LOCK();
    bool r = diario_anota(cuerpo, id);
    DIARIO_UNLOCK();
    return r;
}

bool viaje_diario_confirma(uint32_t id, bool descartado)
{
    DIARIO_LOCK();
    bool r = diario_confirma(id, descartado);
    DIARIO_UNLOCK();
    return r;
}

size_t viaje_diario_pendientes(void)
{
    DIARIO_LOCK();
    size_t r = diario_pendientes();
    DIARIO_UNLOCK();
    return r;
}

bool viaje_diario_contiene(uint32_t id)
{
    DIARIO_LOCK();
    bool r = diario_contiene(id);
    DIARIO_UNLOCK();
    return r;
}

bool viaje_diario_en(size_t k, char *out, size_t n, uint32_t *id_out)
{
    DIARIO_LOCK();
    bool r = diario_en(k, out, n, id_out);
    DIARIO_UNLOCK();
    return r;
}

bool viaje_diario_disponible(void)
{
    DIARIO_LOCK();
    bool r = s_listo;
    DIARIO_UNLOCK();
    return r;
}

bool viaje_diario_ultimo(char *out, size_t n, uint32_t *id_out)
{
    DIARIO_LOCK();
    bool r = diario_ultimo(out, n, id_out);
    DIARIO_UNLOCK();
    return r;
}

bool viaje_diario_siguiente(char *out, size_t n, uint32_t *id_out)
{
    DIARIO_LOCK();
    bool r = diario_siguiente(out, n, id_out);
    DIARIO_UNLOCK();
    return r;
}
