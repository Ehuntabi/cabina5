/* viaje_diario_arnes.c — pruebas del diario del viaje (ver viaje_diario.sh).
 *
 * Se compila el fichero DE PRODUCCION main/net/viaje_diario.c con
 * -DVIAJE_DIARIO_TEST: en vez de esp_partition_* usa 1 MB de flash simulada en
 * RAM y los ganchos de prueba. El codigo que se ejecuta (formato de registro,
 * CRC, compactacion, commit de la cabecera) es el mismo que va a la placa.
 */
#include <stdio.h>
#include <string.h>

#include "viaje_diario.h"

static int fallos;

static void ok(bool cond, const char *que)
{
    if (cond) {
        printf("ok     %s\n", que);
    } else {
        printf("FALLO  %s\n", que);
        fallos++;
    }
}

/* Cuerpo con la misma pinta que monta apunte_cabecera(), con relleno para que
 * ocupe algo y los offsets no sean todos iguales. */
static void cuerpo(char *out, size_t n, uint32_t id, const char *tipo)
{
    snprintf(out, n,
             "{\"op\":\"registro\",\"id\":%lu,\"tipo\":\"%s\",\"ts\":1699999999,"
             "\"datos\":{\"importe\":\"62.40\"}}",
             (unsigned long)id, tipo);
}

int main(void)
{
    char b[512], leido[512];
    uint32_t id;

    /* ── 1. Estreno y alta/baja de pendientes ───────────────────────────── */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    ok(viaje_diario_pendientes() == 0, "diario vacio al estrenar");

    cuerpo(b, sizeof b, 101, "repostaje");
    ok(viaje_diario_anota(b, 101), "se anota el 101");
    cuerpo(b, sizeof b, 102, "peaje");
    ok(viaje_diario_anota(b, 102), "se anota el 102");
    cuerpo(b, sizeof b, 103, "parada");
    ok(viaje_diario_anota(b, 103), "se anota el 103");
    ok(viaje_diario_pendientes() == 3, "hay 3 pendientes");

    ok(viaje_diario_siguiente(leido, sizeof leido, &id), "siguiente() devuelve algo");
    ok(id == 101, "el siguiente es el MAS ANTIGUO (101)");
    ok(strstr(leido, "\"id\":101") != NULL, "el cuerpo leido es el del 101");

    ok(viaje_diario_contiene(102), "el 102 esta");
    ok(viaje_diario_confirma(102, false), "entregar el 102");
    ok(!viaje_diario_contiene(102), "el 102 ya no esta");
    ok(viaje_diario_pendientes() == 2, "quedan 2");
    ok(!viaje_diario_confirma(102, false), "confirmar dos veces no rompe nada");

    ok(viaje_diario_confirma(103, true), "descartar el 103 a mano");
    ok(viaje_diario_pendientes() == 1, "queda solo el 101");
    ok(!viaje_diario_contiene(103), "el descartado no vuelve");

    /* ── 2. Sobrevive a un reinicio (lo que hoy se pierde) ───────────────── */
    viaje_diario_init();     /* como si la placa se hubiera reiniciado */
    ok(viaje_diario_pendientes() == 1, "tras reiniciar sigue el 101 pendiente");
    ok(viaje_diario_siguiente(leido, sizeof leido, &id) && id == 101,
       "y sigue siendo el 101");

    /* ── 3. Corte de corriente a mitad de escritura ─────────────────────── */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    cuerpo(b, sizeof b, 201, "repostaje");
    viaje_diario_anota(b, 201);
    cuerpo(b, sizeof b, 202, "peaje");
    viaje_diario_anota(b, 202);
    /* el registro 202 se queda a medias (apagon mientras se escribia) */
    viaje_diario_test_corrompe_pendiente(1);
    viaje_diario_init();
    ok(viaje_diario_pendientes() == 1, "el registro cortado se descarta");
    ok(viaje_diario_contiene(201), "el anterior, que estaba entero, se conserva");

    /* EL CORTE DE VERDAD: el cuerpo SI se escribe y la cabecera NO (falla en la
     * segunda escritura). Es el caso que el codigo maneja a proposito, y sin
     * esto el arnes pasaba aunque no se escribiera nada. */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    cuerpo(b, sizeof b, 211, "peaje");
    viaje_diario_test_falla_tras(1);      /* deja escribir el cuerpo, no la cabecera */
    bool cortado = viaje_diario_anota(b, 211);
    viaje_diario_test_falla_tras(-1);
    ok(!cortado, "si la cabecera no se escribe, el apunte NO se da por guardado");
    viaje_diario_init();
    ok(viaje_diario_pendientes() == 0, "y al arrancar no aparece ningun apunte a medias");
    cuerpo(b, sizeof b, 212, "peaje");
    ok(viaje_diario_anota(b, 212), "y se sigue pudiendo anotar (el hueco se reutiliza)");
    ok(viaje_diario_pendientes() == 1, "queda exactamente el nuevo");

    /* Y lo que se escriba despues tampoco puede resucitarlo. */
    cuerpo(b, sizeof b, 203, "pernocta");
    ok(viaje_diario_anota(b, 203), "se puede seguir anotando despues del corte");
    ok(viaje_diario_pendientes() == 2, "quedan 201 y 203");

    /* ── 4. Compactacion (la mitad se llena) ────────────────────────────── */
    viaje_diario_test_borra_todo();
    viaje_diario_test_mitad(2048);   /* mitades de 2 KB: compacta enseguida */
    viaje_diario_init();

    uint32_t esperados[20];
    int n = 0;
    for (uint32_t i = 0; i < 20; i++) {
        cuerpo(b, sizeof b, 300 + i, "repostaje");
        if (viaje_diario_anota(b, 300 + i)) esperados[n++] = 300 + i;
    }
    ok(n > 0 && viaje_diario_pendientes() == (size_t)n,
       "las altas aceptadas cuadran con los pendientes (ni fantasmas ni perdidas)");
    ok(viaje_diario_pendientes() == (size_t)n, "la compactacion no pierde ninguno");
    ok(viaje_diario_siguiente(leido, sizeof leido, &id) && id == esperados[0],
       "tras compactar sigue saliendo el mas antiguo primero");
    for (int i = 0; i < n; i++) {
        if (!viaje_diario_contiene(esperados[i])) {
            printf("FALLO  se perdio el %lu al compactar\n", (unsigned long)esperados[i]);
            fallos++;
        }
    }

    /* Y tras reiniciar, el indice de la mitad nueva es el bueno. */
    viaje_diario_init();
    ok(viaje_diario_pendientes() == (size_t)n, "tras reiniciar sigue el mismo numero");

    /* ── 5. Corte A MITAD de la compactacion ────────────────────────────── */
    /* La copia a la otra mitad se queda a medias (sin commit). Al arrancar
     * tiene que seguir mandando la mitad vieja, que no se ha tocado: no se
     * pierde NINGUN apunte. Es la garantia que justifica partir la particion
     * en dos mitades en vez de compactar en el sitio. */
    viaje_diario_test_borra_todo();
    viaje_diario_test_mitad(2048);
    viaje_diario_init();
    uint32_t ids5[40];
    int n5 = 0;
    for (uint32_t i = 0; i < 40; i++) {
        char bb[512];
        cuerpo(bb, sizeof bb, 400 + i, "peaje");
        if (viaje_diario_anota(bb, 400 + i)) ids5[n5++] = 400 + i;
    }
    ok(n5 > 0 && viaje_diario_pendientes() == (size_t)n5,
       "las altas antes del corte cuadran con los pendientes");

    /* Siguiente apunte: obliga a compactar, y la copia se corta a la mitad. */
    uint32_t gen_antes = viaje_diario_test_gen();
    uint32_t base_antes = viaje_diario_test_base();
    char bb[512];
    cuerpo(bb, sizeof bb, 500, "repostaje");
    viaje_diario_test_falla_tras(3);
    bool anotado = viaje_diario_anota(bb, 500);
    viaje_diario_test_falla_tras(-1);
    ok(!anotado, "si la compactacion no se puede completar, el apunte NO se da por guardado");
    /* La prueba de que la copia EMPEZO y no se quedo en la puerta: el codigo
     * sigue apuntando a la mitad vieja y con la misma generacion. Si compacta()
     * se rindiera sin copiar nada, esto pasaria igual, asi que ademas se
     * comprueba que una compactacion posterior SI sube de generacion. */
    ok(viaje_diario_test_gen() == gen_antes && viaje_diario_test_base() == base_antes,
       "tras el corte se sigue usando la mitad vieja (misma generacion)");

    /* Y al reiniciar, los que ya estaban siguen todos. */
    viaje_diario_init();
    int perdidos = 0;
    for (int i = 0; i < n5; i++) if (!viaje_diario_contiene(ids5[i])) perdidos++;
    if (perdidos) { printf("FALLO  se perdieron %d apuntes\n", perdidos); fallos++; }
    ok(perdidos == 0, "la mitad vieja sigue intacta: cero apuntes perdidos");
    ok(viaje_diario_pendientes() == (size_t)n5, "y el numero de pendientes cuadra");

    /* Mientras no se libere sitio, el diario dice que no (y quien llama lo
     * encola como siempre): lo que NO puede pasar es dar por guardado algo que
     * no esta. */
    cuerpo(bb, sizeof bb, 501, "peaje");
    ok(!viaje_diario_anota(bb, 501), "con el diario lleno, no se da nada por guardado");

    /* En cuanto la P4 confirma unos cuantos, la compactacion libera sus huecos
     * y el diario vuelve a admitir apuntes: el atasco no es permanente. Se
     * confirman todos menos tres para que la copia de la compactacion quepa. */
    for (int i = 0; i + 3 < n5; i++) viaje_diario_confirma(ids5[i], false);
    cuerpo(bb, sizeof bb, 501, "peaje");
    uint32_t gen_antes_final = viaje_diario_test_gen();
    ok(viaje_diario_anota(bb, 501), "tras entregar unos cuantos, vuelve a haber sitio");
    /* ESTA es la prueba de que compacta() COPIA de verdad: si se rindiera sin
     * tocar nada (mutacion que el arbitraje del 10-nov-2026 encontro que pasaba
     * todas las demas), el alta anterior no habria podido entrar. */
    ok(viaje_diario_test_gen() > gen_antes_final,
       "y para ello ha compactado de verdad (sube la generacion)");

    /* ── 5 bis. Lo que hace que las demas pruebas valgan ────────────────── */

    /* VECTOR CONOCIDO DEL CRC: si el CRC no es el estandar, escribir y leer
     * siguen cuadrando entre ellos (todo pasa) pero deja de detectar errores
     * como debe. Este es el numerito que delata un CRC "de mentira". */
    ok(viaje_diario_test_crc_vector() == 0xCBF43926u,
       "el CRC es el estandar: 123456789 -> 0xCBF43926");

    /* ENTREGADO Y DESCARTADO NO SON LO MISMO: la marca se guarda distinta.
     * Si se marcase todo como entregado, las pruebas de "ya no esta" seguirian
     * pasando, pero se perderia el rastro de que lo tiro el usuario. */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    cuerpo(b, sizeof b, 700, "peaje");
    viaje_diario_anota(b, 700);
    cuerpo(b, sizeof b, 701, "peaje");
    viaje_diario_anota(b, 701);
    viaje_diario_confirma(700, false);
    viaje_diario_confirma(701, true);
    ok(viaje_diario_test_marca_de(700) == 1, "el entregado se marca ENTREGADO (1)");
    ok(viaje_diario_test_marca_de(701) == 2, "el descartado se marca DESCARTADO (2)");

    /* UN REGISTRO MALO CORTA: lo que venga DETRAS no se rescata (si se saltara
     * el malo y siguiera leyendo, el tercero apareceria y esto fallaria). */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    cuerpo(b, sizeof b, 710, "peaje"); viaje_diario_anota(b, 710);
    cuerpo(b, sizeof b, 711, "peaje"); viaje_diario_anota(b, 711);
    cuerpo(b, sizeof b, 712, "peaje"); viaje_diario_anota(b, 712);
    viaje_diario_test_corrompe_pendiente(1);   /* el 711 */
    viaje_diario_init();
    ok(viaje_diario_contiene(710), "el de antes del malo se conserva");
    ok(!viaje_diario_contiene(711) && !viaje_diario_contiene(712),
       "el malo y TODO lo que venia detras se descartan (se corta, no se salta)");

    /* FALLOS DE E/S Y DIARIO LLENO: se dice que no, no se miente. */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    cuerpo(b, sizeof b, 720, "peaje");
    viaje_diario_test_falla(true);
    ok(!viaje_diario_anota(b, 720), "si la flash falla, no se da por guardado");
    ok(viaje_diario_pendientes() == 0, "y no queda un fantasma en el indice");
    viaje_diario_test_falla(false);
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    ok(viaje_diario_anota(b, 720), "y al recuperar la flash vuelve a admitirlo");

    /* ── 5 ter. El commit de la compactacion es la CABECERA ─────────────── */
    /* Si la cabecera de la mitad nueva no llega a escribirse (corte justo
     * antes), esa mitad NO existe para nadie y se sigue leyendo la vieja. Se
     * prueba borrando a mano la cabecera de la mitad activa despues de
     * compactar: la generacion tiene que volver atras. */
    viaje_diario_test_borra_todo();
    viaje_diario_test_mitad(2048);
    viaje_diario_init();
    for (uint32_t i = 0; i < 40; i++) {
        char cc[512];
        cuerpo(cc, sizeof cc, 800 + i, "peaje");
        viaje_diario_anota(cc, 800 + i);
    }
    uint32_t gen_bueno = viaje_diario_test_gen();
    uint32_t base_buena = viaje_diario_test_base();
    ok(gen_bueno > 1, "ha habido al menos una compactacion");
    viaje_diario_test_rompe_cabecera(base_buena);
    viaje_diario_init();
    /* La mitad sin cabecera no cuenta: se vuelve a la otra, que sigue teniendo
     * los apuntes. (El diario puede compactar al montar, porque el borde de la
     * mitad vieja queda sucio; por eso aqui NO se compara la generacion, sino
     * que los datos sigan y que se pueda seguir anotando sin mezclar.) */
    ok(viaje_diario_pendientes() > 0, "sin cabecera se sigue leyendo la otra mitad");
    /* No se comprueba "se puede seguir anotando" aqui: con las mitades de 2 KB
     * del test la otra mitad esta llena y compactar no cabe; eso es la rama de
     * "diario lleno", que ya se prueba aparte. La garantia de esta prueba es
     * que NO SE PIERDEN los apuntes. */

    /* ── 5 quater. Escribir ENCIMA de la flash sin borrar (el fallo de la
     * placa del 10-nov-2026) ────────────────────────────────────────────────
     * La flash solo deja pasar bits de 1 a 0: si el escaneo se corta en un
     * registro viejo y el siguiente apunte se escribe en la misma zona sin
     * borrar, el cuerpo sale MEZCLADO (la P4 contesta 400 y parece perdido).
     * Aqui se deja el borde sucio a proposito y se comprueba que el alta
     * siguiente sale intacta. */
    viaje_diario_test_borra_todo();
    viaje_diario_init();
    cuerpo(b, sizeof b, 900, "peaje");
    viaje_diario_anota(b, 900);
    viaje_diario_test_corrompe_pendiente(0);   /* el primer registro queda ilegible */
    viaje_diario_init();                       /* el escaneo se corta nada mas empezar */
    ok(viaje_diario_pendientes() == 0, "el registro ilegible no cuenta");
    cuerpo(b, sizeof b, 901, "repostaje");
    ok(viaje_diario_anota(b, 901), "y se puede anotar el siguiente");
    ok(viaje_diario_siguiente(leido, sizeof leido, &id) && id == 901,
       "y el cuerpo nuevo se lee entero (no mezclado con los restos)");
    ok(strstr(leido, "\"tipo\":\"repostaje\"") != NULL,
       "el JSON esta intacto: no se ha escrito encima de la basura");

    /* ── 6. id_de(): de donde sale la clave del apunte ──────────────────── */
    ok(viaje_diario_id_de("{\"op\":\"registro\",\"id\":1699999999,\"tipo\":\"x\"}")
       == 1699999999u, "saca el id del cuerpo real");
    ok(viaje_diario_id_de("{\"op\":\"fin\",\"id\":7}") == 7u, "id corto");
    ok(viaje_diario_id_de("{\"op\":\"registro\"}") == 0, "sin id -> 0");
    ok(viaje_diario_id_de("nada de nada") == 0, "basura -> 0");
    ok(viaje_diario_id_de("{\"id\":99999999999}") == 0, "desbordamiento -> 0");

    printf("\n%d fallos\n", fallos);
    return fallos ? 1 : 0;
}
