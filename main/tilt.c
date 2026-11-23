/* tilt.c - Driver minimo del ADXL345 (registros directos, sin libreria de
 * terceros -- solo hace falta leer X/Y/Z, no vale la pena una dependencia
 * nueva del component registry para eso).
 *
 * Registros usados (datasheet Analog Devices ADXL345 Rev. G):
 *   0x00 DEVID        -- debe leer 0xE5, sirve de sanity-check del bus
 *   0x2D POWER_CTL     -- bit3 (Measure) = 1 para salir de standby
 *   0x31 DATA_FORMAT   -- bit3 (FULL_RES) = 1, rango +/-2g (bits 0:1 = 00)
 *   0x32..0x37 DATAX0..DATAZ1 -- 6 bytes, little-endian, 13 bit signados
 *
 * En modo FULL_RES la escala es fija: 3.9 mg/LSB, independiente del rango.
 *
 * BUS I2C PROPIO, no el compartido con el tactil (I2C_NUM_0, GPIO4/GPIO8):
 * el esquematico oficial del modulo (JC3248W535EN/5-IO pin distribution,
 * seccion "Extended IO") muestra que GPIO4/GPIO8 son cableado interno
 * pantalla+tactil, SIN pad accesible desde fuera -- no hay donde soldar.
 * Usamos un segundo bus I2C por hardware (I2C_NUM_1, el ESP32-S3 tiene dos).
 *
 * PINES: IO17(SDA)/IO18(SCL), los de los conectores JST de 4 pines. Se
 * eligieron sobre los IO5/IO6 del conector "Extended IO" de 8 pines (que
 * tambien valdrian, junto con IO7/IO9/IO14/IO15/IO16) por una razon de
 * cableado, no electrica: el conector de 8 pines lleva SOLO señales, asi que
 * el sensor habria que llevarlo a dos conectores a la vez (datos en uno,
 * 3V3/GND en otro). Los JST de 4 pines llevan 2 GPIO + 3V3 + GND, de modo
 * que el ADXL345 cuelga de UN solo cable de 4 hilos.
 *
 * En el S3 el I2C va por matriz GPIO, asi que reasignarlo es solo cambiar
 * los dos defines de abajo. Evitar: 33-37 (PSRAM octal, ver
 * CONFIG_SPIRAM_MODE_OCT), 26-32 (flash), 19/20 (USB), 0/3/45/46
 * (strapping: los pull-ups del I2C alterarian el arranque) y los que ya usa
 * la pantalla/tactil (1, 4, 8, 21, 38, 39, 40, 45, 47, 48).
 *
 * MAPEO DE EJES: VERIFICADO en placa real el 21-ago-2026 con el sensor montado
 * en su orientacion original. "pitch" sale como cabeceo delante-atras y "roll"
 * como balanceo izda-dcha, en el sentido correcto.
 *
 * El 22-ago-2026 el diseño del soporte obliga a montarlo GIRADO. Se gira el
 * vector medido antes de calcular los angulos, con lo que todo lo de abajo
 * sigue trabajando siempre en los ejes del VEHICULO y no hay que tocar ni el
 * nivel ni lo que se guarda en las paradas.
 *
 * 270 y no 90: COMPROBADO en la placa. Se llego a poner un selector en pantalla
 * para probar los cuatro giros, y se quito en cuanto se supo cual era -- en un
 * tactil dentro de un vehiculo en marcha, un roce habria girado los ejes Y
 * borrado la calibracion de paso, sin que nadie se enterara hasta ir a nivelar.
 * Un ajuste que solo se toca una vez en la vida no merece estar al alcance del
 * pulgar. Si el soporte vuelve a cambiar, se cambia esta linea.
 */
#include "tilt.h"
#include "config_storage.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "tilt";

#define TILT_I2C_PORT            I2C_NUM_1
#define TILT_I2C_SDA_GPIO        GPIO_NUM_17
#define TILT_I2C_SCL_GPIO        GPIO_NUM_18
#define TILT_I2C_CLK_HZ          400000

/* ── BNO055 (09-nov-2026) ───────────────────────────────────────────────────
 * El sensor que llego no es un BNO085: es un BNO055 (CHIP_ID 0xA0 en el
 * registro 0x00), en la direccion 0x29 (0x28 con ADDR bajo). Trae la fusion
 * hecha, asi que no hace falta el protocolo SH-2 del BNO085: se lee el VECTOR
 * DE GRAVEDAD por registros y se calculan los angulos igual que con el
 * ADXL345.
 *
 * Se usa el vector de GRAVEDAD (0x2E) y no la aceleracion: en modo fusion la
 * aceleracion lleva el movimiento del vehiculo y la gravedad es la referencia
 * estable. El vector de gravedad apunta HACIA ABAJO, asi que se niega para
 * dejarlo con el mismo signo que daba el acelerometro y no cambiar ni el
 * filtro ni la math de angulos. */
#define BNO055_ADDR_A           0x29
#define BNO055_ADDR_B           0x28
#define BNO_CHIP_ID             0x00
#define BNO_CHIP_ID_ESPERADO    0xA0
#define BNO_OPR_MODE            0x3D
#define BNO_PWR_MODE            0x3E
#define BNO_SYS_TRIGGER         0x3F
#define BNO_UNIT_SEL            0x3B
#define BNO_GRAVITY_X_LSB       0x2E
#define BNO_OPR_CONFIG          0x00
#define BNO_OPR_NDOF            0x0C   /* fusion 9 ejes: hace falta el
                                        * magnetometro para el RUMBO (la
                                        * brujula). El cabeceo/balanceo siguen
                                        * saliendo del vector de gravedad. */
#define BNO_EULER_H_LSB         0x1A   /* rumbo: int16, 1/16 grado */
#define BNO_CALIB_STAT          0x35   /* bits 1:0 = calidad del magnetometro */
#define BNO_CALIB_PROF          0x55   /* 22 bytes de perfil de calibracion */
#define BNO_CALIB_PROF_LEN      22

/* Perfil que hay ahora mismo en la NVS, para no reescribirlo igual en cada
 * arranque (ver tilt_get_heading, 22-nov-2026). Se rellena al restaurarlo. */
static uint8_t s_prof_guardado[BNO_CALIB_PROF_LEN];
static bool    s_prof_guardado_valido;
/* Se puso un perfil en el chip al arrancar: sirve para avisar si luego resulta
 * que el magnetometro sigue sin calibrar (ver tilt_diag_task, 22-nov-2026). */
static bool    s_prof_restaurado;

typedef enum { SENSOR_NINGUNO = 0, SENSOR_ADXL345, SENSOR_BNO055 } tilt_chip_t;
static tilt_chip_t s_chip = SENSOR_NINGUNO;

#define ADXL345_ADDR            0x53
#define REG_DEVID                0x00
#define REG_BW_RATE               0x2C
#define REG_POWER_CTL             0x2D
#define REG_DATA_FORMAT           0x31
#define REG_DATAX0                0x32
#define ADXL345_DEVID_EXPECTED   0xE5

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool  s_present = false;
static float s_pitch_offset_deg = 0.0f;
static float s_roll_offset_deg  = 0.0f;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

/* ¿ES UN PERFIL DE CALIBRACION DE VERDAD? (22-nov-2026)
 *
 * Un perfil real del BNO055 trae coeficientes (offsets y radios); todo a 0x00
 * -o 20 bytes a 0x00 y dos a 0xFF, que es justo lo que devolvia el chip al
 * leerlo en modo fusion- NO CALIBRA NADA. Ese blob se guardaba en la NVS y se
 * restauraba en cada arranque, borrando la calibracion del chip: la brujula se
 * descalibraba sola ("otra vez se ha borrado la calibracion"). Se exigen unos
 * cuantos bytes utiles para no descartar un perfil bueno con ceros sueltos. */
static bool perfil_valido(const uint8_t *prof)
{
    int utiles = 0, mag_utiles = 0;
    for (size_t i = 0; i < BNO_CALIB_PROF_LEN; i++) {
        if (prof[i] != 0x00 && prof[i] != 0xFF) {
            utiles++;
            if (i >= 8 && i < 16) mag_utiles++;   /* 0x5D..0x64: offsets y radio del magnetometro */
        }
    }
    /* Con el magnetometro a 3 (que es cuando se guarda), sus offsets y radio
     * tienen coeficientes. Si esa parte viniera a cero, el perfil no serviria
     * para la brujula: mejor no guardarlo. */
    return utiles >= 4 && mag_utiles >= 2;
}

/* Lee 'len' bytes del BNO055 byte a byte (definido mas abajo; ver alli el
 * porque). */
static esp_err_t bno_leer_bytes(uint8_t reg, uint8_t *buf, size_t len);

/* Lee los 22 bytes del perfil de calibracion DEL CHIP. HAY QUE HACERLO EN MODO
 * CONFIG: en los modos de fusion (NDOF) esos registros devuelven 0x00, y eso es
 * lo que se estaba guardando (ver perfil_valido). Despues de leer se vuelve a
 * NDOF, que es lo que necesita la brujula. */
static esp_err_t bno_leer_perfil(uint8_t *prof)
{
    uint8_t modo[2] = { BNO_OPR_MODE, BNO_OPR_CONFIG };
    esp_err_t err = i2c_master_transmit(s_dev, modo, 2, 100);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(30));          /* cambio de modo: ~7 ms tipicos */
    /* BYTE A BYTE, por lo mismo que la gravedad (09-nov-2026): en bloque este
     * chip devuelve corruptas las ultimas posiciones. Con 22 bytes salia basura
     * (el perfil que se guardaba era 20x00+FFFF). */
    err = bno_leer_bytes(BNO_CALIB_PROF, prof, BNO_CALIB_PROF_LEN);
    modo[1] = BNO_OPR_NDOF;
    if (i2c_master_transmit(s_dev, modo, 2, 100) != ESP_OK && err == ESP_OK) {
        err = ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return err;
}

/* ── CALIDAD DE LA CALIBRACION, POR SENSOR (22-nov-2026) ────────────────────
 *
 * CALIB_STAT (0x35) trae cuatro notas de 0 a 3, una por cosa:
 *     bits 1:0 magnetometro | 3:2 acelerometro | 5:4 giroscopio | 7:6 sistema
 * Hasta hoy solo se miraba el magnetometro (y solo para decidir si guardar el
 * perfil). Faltaba el resto: el ACELEROMETRO es de donde sale el NIVEL, y el
 * perfil que se guarda vale lo que valga el peor de los cuatro. Guardarlo con
 * el acelerometro a medias es guardar un nivel torcido que se restaura en cada
 * arranque; y como el perfil restaurado ya devuelve 3, se volvia a guardar lo
 * mismo en cada arranque sin aportar nada. */
typedef struct { uint8_t mag, acc, gyr, sys; } bno_cal_t;

static bno_cal_t bno_cal_leer(void)
{
    bno_cal_t c = { 0, 0, 0, 0 };
    uint8_t st = 0;
    if (reg_read(BNO_CALIB_STAT, &st, 1) != ESP_OK) return c;
    c.mag = (uint8_t)(st & 0x03);
    c.acc = (uint8_t)((st >> 2) & 0x03);
    c.gyr = (uint8_t)((st >> 4) & 0x03);
    c.sys = (uint8_t)((st >> 6) & 0x03);
    return c;
}

/* Lee el vector de gravedad del BNO055 y lo deja en "g" y con el signo del
 * acelerometro (negado). Escala: 1/100 m/s2 por LSB. */
/* Lee 'len' bytes del BNO055 UNO A UNO.
 *
 * POR QUE NO EN BLOQUE (medido el 09-nov-2026): con i2c_master_transmit_receive
 * de 6 bytes, este chip devuelve las ultimas posiciones corruptas (el Z_MSB
 * llegaba como 0xFF y la gravedad salia 0,01 en vez de 9,6 m/s2). Leyendo byte
 * a byte, estable. Es el clock stretching del BNO055: alargar la lectura le da
 * tiempo entre bytes. Cuesta ~6 lecturas de 1 byte cada 200 ms, nada.
 *
 * 22-nov-2026: LO MISMO VALE PARA EL PERFIL DE CALIBRACION. Se leia y escribia
 * en bloque de 22 bytes y volvian ceros/basura, asi que nunca se guardo una
 * calibracion de verdad y la brujula se "descalibraba" sola en cada arranque. */
static esp_err_t bno_leer_bytes_en(i2c_master_dev_handle_t dev, uint8_t reg,
                                   uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t r = (uint8_t)(reg + i);
        if (i2c_master_transmit_receive(dev, &r, 1, &buf[i], 1, 100) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static esp_err_t bno_leer_bytes(uint8_t reg, uint8_t *buf, size_t len)
{
    return bno_leer_bytes_en(s_dev, reg, buf, len);
}

/* Escribe el perfil de calibracion BYTE A BYTE (mismo motivo que la lectura en
 * bloque: el chip corrompe las escrituras largas). Tiene que estar en CONFIG. */
static esp_err_t bno_escribir_perfil_en(i2c_master_dev_handle_t dev, const uint8_t *prof)
{
    for (size_t i = 0; i < BNO_CALIB_PROF_LEN; i++) {
        uint8_t b[2] = { (uint8_t)(BNO_CALIB_PROF + i), prof[i] };
        if (i2c_master_transmit(dev, b, sizeof b, 100) != ESP_OK) return ESP_FAIL;
    }
    return ESP_OK;
}

static bool bno_leer_g(float *ax, float *ay, float *az)
{
    uint8_t buf[6];
    if (bno_leer_bytes(BNO_GRAVITY_X_LSB, buf, sizeof buf) != ESP_OK) return false;
    int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t z = (int16_t)((buf[5] << 8) | buf[4]);
    /* SIN NEGAR: este BNO055 da el vector de gravedad apuntando HACIA ARRIBA
     * (Z = +9.6 m/s2 con el sensor plano y la serigrafia hacia el cielo),
     * medido el 09-nov-2026. Negarlo, como se hacia al principio, dejaba el
     * balanceo en 180 grados. Con el signo natural, el vector se comporta como
     * el del acelerometro y la math de angulos no cambia. */
    const float escala = 1.0f / (100.0f * 9.80665f);
    *ax = x * escala;
    *ay = y * escala;
    *az = z * escala;
    return true;
}

static bool read_raw_g(float *ax, float *ay, float *az)
{
    if (!s_present) return false;
    if (s_chip == SENSOR_BNO055) return bno_leer_g(ax, ay, az);
    uint8_t buf[6];
    if (reg_read(REG_DATAX0, buf, sizeof(buf)) != ESP_OK) return false;

    int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t z = (int16_t)((buf[5] << 8) | buf[4]);
    const float scale = 0.0039f;   /* g/LSB, fijo en modo full-res */
    *ax = x * scale;
    *ay = y * scale;
    *az = z * scale;
    return true;
}

/* --- Que la bola no tiemble ---------------------------------------------
 *
 * El ADXL345 mete alrededor de 1 LSB de ruido, y en full-res 1 LSB son 3,9 mg,
 * o sea unos 0,22 grados. En el dial del nivel eso NO es despreciable: el
 * circulo reparte 6 grados en 106 px, casi 18 px por grado, asi que el ruido
 * normal del chip se ve como un temblor de 3 o 4 px que no para nunca. Se ataca
 * por los dos lados:
 *
 * 1. EN EL SENSOR (BW_RATE, en tilt_init): a 100 Hz el ancho de banda es de 50
 *    Hz y el ruido entra entero. La autocaravana esta PARADA cuando se mira el
 *    nivel: no hay nada por encima de unos pocos hercios que merezca la pena
 *    medir. Bajando a 12,5 Hz (6,25 Hz de ancho de banda) el ruido cae con la
 *    raiz del ancho de banda, casi tres veces.
 *
 * 2. AQUI, media exponencial sobre el vector de gravedad -- no sobre los
 *    angulos: los angulos salen de una arcotangente y promediar despues de una
 *    funcion no lineal deforma el resultado cerca de los extremos.
 *
 * Con ALFA 0,2 y una lectura cada 200 ms la constante de tiempo es de ~0,8 s.
 * Se nota al mover la autocaravana, y por eso existe el SALTO: si la lectura se
 * aleja mas de lo que el ruido puede explicar (0,03 g son ~1,7 grados, y el
 * ruido es 0,004 g), el filtro se tira al valor nuevo de golpe en vez de ir
 * llegando. Asi se queda quieto parado y responde al instante al subir a una
 * rampa, que es justo lo que hace falta.
 *
 * Y OLVIDO: si hace mas de un segundo que nadie lee, el filtro empieza de cero.
 * Sin eso, la inclinacion que se guarda al aparcar (view_registro la pide una
 * sola vez, cada muchas horas) saldria mezclada con la de donde estabas la
 * ultima vez que se miro el nivel. */
/* 0,12 y no 0,20: con 0,20 (probado en la placa el 24-ago) el temblor bajaba
 * pero seguia viendose. Constante de tiempo ~1,5 s leyendo cada 200 ms. Lo que
 * salva la respuesta es TILT_SALTO_G, no el alfa. */
#define TILT_ALFA        0.12f
#define TILT_SALTO_G     0.03f
#define TILT_OLVIDO_US   (1000 * 1000)

static float   s_fx, s_fy, s_fz;
static bool    s_filtro_vivo;
static int64_t s_filtro_us;

static bool read_g(float *ax, float *ay, float *az)
{
    float x, y, z;
    if (!read_raw_g(&x, &y, &z)) return false;

    int64_t ahora = esp_timer_get_time();
    bool de_golpe = !s_filtro_vivo
                    || (ahora - s_filtro_us) > TILT_OLVIDO_US
                    || fabsf(x - s_fx) > TILT_SALTO_G
                    || fabsf(y - s_fy) > TILT_SALTO_G
                    || fabsf(z - s_fz) > TILT_SALTO_G;
    if (de_golpe) {
        s_fx = x; s_fy = y; s_fz = z;
    } else {
        s_fx += TILT_ALFA * (x - s_fx);
        s_fy += TILT_ALFA * (y - s_fy);
        s_fz += TILT_ALFA * (z - s_fz);
    }
    s_filtro_vivo = true;
    s_filtro_us   = ahora;

    *ax = s_fx; *ay = s_fy; *az = s_fz;
    return true;
}

/* Lleva el vector medido a los ejes del VEHICULO.
 *
 * DOS MONTAJES, segun el sensor:
 *
 * - BNO055 (09-nov-2026): colocado en su sitio, su eje **Y marca hacia la
 *   DERECHA** del vehiculo y su **X hacia ATRAS** (dato del dueño, medido en la
 *   ubicacion definitiva). La vertical (Z) ya es la Z. Asi que el eje de
 *   avance es -X del sensor y el lateral es +Y: solo hay que negar la X.
 *
 * - ADXL345 (el viejo): iba girado 270 grados (90 en sentido antihorario
 *   mirando la pantalla de frente): su X apuntaba a la izquierda y su Y hacia
 *   delante, de ahi el intercambio con cambio de signo. Se conserva para las
 *   placas que aun lo lleven.
 *
 * La Z no se toca en ninguno de los dos: la gravedad entra por ahi. */
static void a_ejes_vehiculo(float *ax, float *ay)
{
    if (s_chip == SENSOR_BNO055) {
        /* Probado en la placa el 09-nov-2026 (dos intentos, mirando la pantalla
         * con el sensor en su sitio): el eje de avance es -X del sensor y el
         * lateral es -Y. Con el signo natural en Y el balanceo salia al reves. */
        *ax = *ax;     /* X del sensor TAL CUAL: es el eje de avance */
        *ay = -*ay;    /* Y invertida: con el signo natural el balanceo iba al reves */
    } else {
        float x = *ax, y = *ay;
        *ax =  y;
        *ay = -x;
    }
}

static bool compute_angles(float *pitch_deg, float *roll_deg)
{
    float ax, ay, az;
    if (!read_g(&ax, &ay, &az)) return false;
    a_ejes_vehiculo(&ax, &ay);
    /* Signo del cabeceo invertido el 09-nov-2026, probado en la placa con el
     * sensor en su sitio: con el signo contrario, al levantar el morro la cifra
     * bajaba. Ahora CABECEO positivo = morro arriba. */
    *pitch_deg = atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;
    *roll_deg  = atan2f(ay, az) * 180.0f / (float)M_PI;
    return true;
}

/* Prueba a hablar con un BNO055 en una direccion: mete el dispositivo en el
 * bus, lee CHIP_ID y, si es el suyo, lo configura. Devuelve ESP_OK y deja
 * s_dev listo. */
static esp_err_t bno_probar(uint8_t addr)
{
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = TILT_I2C_CLK_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(s_bus, &dc, &dev) != ESP_OK) return ESP_FAIL;

    uint8_t id = 0;
    esp_err_t e = i2c_master_transmit_receive(dev, (uint8_t[]){BNO_CHIP_ID}, 1, &id, 1, 100);
    if (e != ESP_OK || id != BNO_CHIP_ID_ESPERADO) {
        i2c_master_bus_rm_device(dev);
        return ESP_ERR_NOT_FOUND;
    }

    /* ADOPTAR LA CALIBRACION QUE YA TIENE EL CHIP (22-nov-2026).
     *
     * El BNO055 NO se apaga cuando solo se reinicia el ESP32 (OTA, reinicio de
     * software o una grabacion por cable): su calibracion sigue viva en los
     * registros. Antes de tocar nada, si el chip dice estar calibrado (mag y
     * giroscopio a 3) se lee su perfil BYTE A BYTE y se guarda en la NVS. Asi
     * una actualizacion no obliga a recalibrar, y una calibracion recien hecha
     * no se pierde si el guardado automatico no llego a tiempo. */
    {
        uint8_t st = 0;
        if (i2c_master_transmit_receive(dev, (uint8_t[]){BNO_CALIB_STAT}, 1, &st, 1, 100) == ESP_OK) {
            uint8_t mag = st & 0x03, gyr = (st >> 4) & 0x03;
            if (mag == 3 && gyr == 3) {
                uint8_t prof[BNO_CALIB_PROF_LEN];
                uint8_t a_cfg[2] = { BNO_OPR_MODE, BNO_OPR_CONFIG };
                if (i2c_master_transmit(dev, a_cfg, 2, 100) == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(30));
                    if (bno_leer_bytes_en(dev, BNO_CALIB_PROF, prof, sizeof prof) == ESP_OK &&
                        perfil_valido(prof)) {
                        if (save_tilt_bno_calib(prof, sizeof prof) == ESP_OK) {
                            memcpy(s_prof_guardado, prof, sizeof prof);
                            s_prof_guardado_valido = true;
                            ESP_LOGI(TAG, "calibracion ADOPTADA del chip y guardada "
                                          "(m=3 g=3): %02X%02X%02X%02X %02X%02X%02X%02X ...",
                                     prof[0], prof[1], prof[2], prof[3],
                                     prof[4], prof[5], prof[6], prof[7]);
                        } else {
                            ESP_LOGW(TAG, "calibracion del chip leida pero no se pudo "
                                          "guardar en la NVS");
                        }
                    } else {
                        ESP_LOGW(TAG, "el chip dice estar calibrado pero su perfil no se "
                                      "pudo leer; se usara el de la NVS si vale");
                    }
                }
            }
        }
    }

    /* CONFIG -> valores por defecto -> IMU (fusion sin magnetometro). El
     * BNO055 pide ~20 ms entre cambios de modo. */
    uint8_t cfg[3][2] = {
        { BNO_OPR_MODE, BNO_OPR_CONFIG },
        { BNO_PWR_MODE, 0x00 },          /* normal */
        { BNO_UNIT_SEL, 0x00 },          /* m/s2, grados, Celsius */
    };
    for (int i = 0; i < 3; i++) {
        if (i2c_master_transmit(dev, cfg[i], 2, 100) != ESP_OK) {
            i2c_master_bus_rm_device(dev);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    /* Reset de la fusion y pasar a IMU. */
    {
        uint8_t rst[2] = { BNO_SYS_TRIGGER, 0x20 };
        i2c_master_transmit(dev, rst, 2, 100);
        vTaskDelay(pdMS_TO_TICKS(650));   /* el datasheet pide ~650 ms tras reset */
    }
    /* PERFIL DE CALIBRACION: se reescribe EN CONFIG y DESPUES del reset (en
     * modo fusion el chip IGNORA esos registros, y el reset borra la
     * calibracion). Asi no hay que girar el sensor en cada arranque. */
    {
        uint8_t prof[BNO_CALIB_PROF_LEN];
        if (load_tilt_bno_calib(prof, sizeof prof) == ESP_OK) {
            /* ¿ES UN PERFIL DE VERDAD? (22-nov-2026). Medido en la placa: el
             * blob guardado era 20x00 + FFFF (el chip devuelve ceros al leer los
             * registros en modo fusion), el magnetometro seguia a 0/3 y el
             * sistema a 0/3: la brujula NO estaba calibrada. Un perfil asi no se
             * restaura y ademas se BORRA, porque si se queda cada arranque
             * reescribe basura encima de la calibracion y la brujula se
             * descalibra sola (el "otra vez se ha borrado" del 22-nov-2026). */
            if (!perfil_valido(prof)) {
                ESP_LOGW(TAG, "perfil de calibracion guardado sin coeficientes (no "
                              "calibra nada): lo BORRO y la brujula arranca sin "
                              "calibrar. Girar el sensor hasta 3/3");
                erase_tilt_bno_calib();
            } else if (bno_escribir_perfil_en(dev, prof) == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(20));
                ESP_LOGI(TAG, "perfil restaurado (%02X%02X%02X%02X%02X%02X...)",
                         prof[0], prof[1], prof[2], prof[3], prof[4], prof[5]);
                /* Se apunta lo que hay guardado: si al volver a leer el
                 * perfil del chip sale igual, la tarea de guardado no
                 * reescribe la NVS (antes se escribia una vez por arranque,
                 * sin aportar nada). */
                memcpy(s_prof_guardado, prof, sizeof prof);
                s_prof_guardado_valido = true;
                s_prof_restaurado = true;
            } else {
                ESP_LOGW(TAG, "no pude escribir el perfil de calibracion en el "
                              "chip; la brujula arranca sin calibrar");
            }
        } else {
            ESP_LOGW(TAG, "no hay perfil de calibracion guardado: la brujula "
                          "arranca sin calibrar (girar el sensor hasta 3/3)");
        }
    }

    {
        uint8_t modo[2] = { BNO_OPR_MODE, BNO_OPR_NDOF };
        if (i2c_master_transmit(dev, modo, 2, 100) != ESP_OK) {
            i2c_master_bus_rm_device(dev);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    s_dev = dev;
    ESP_LOGI(TAG, "BNO055 OK en 0x%02X (fusion NDOF, brujula)", addr);
    return ESP_OK;
}

/* Seis lecturas al arrancar para poder medir el nivel sin la pantalla delante
 * (ver el final de tilt_init y el cuerpo de la tarea). */
static void tilt_diag_task(void *arg);
/* Guarda el perfil de la brujula en cuanto esta calibrada, mire o no la
 * pantalla (ver el cuerpo de la tarea). */
static void tilt_guardar_cal_task(void *arg);

esp_err_t tilt_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TILT_I2C_PORT,
        .sda_io_num = TILT_I2C_SDA_GPIO,
        .scl_io_num = TILT_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  /* red de seguridad si el cableado no trae pullups */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c_new_master_bus (IO%d/IO%d) fallo: %s",
                 TILT_I2C_SDA_GPIO, TILT_I2C_SCL_GPIO, esp_err_to_name(err));
        return err;
    }

    /* 1) El sensor nuevo: BNO055 (en 0x29 o 0x28 segun el pin ADDR). */
    if (bno_probar(BNO055_ADDR_A) == ESP_OK || bno_probar(BNO055_ADDR_B) == ESP_OK) {
        s_chip = SENSOR_BNO055;
        s_present = true;
    } else {
        /* 2) El viejo ADXL345, por si la placa que se actualiza aun lo lleva. */
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = ADXL345_ADDR,
            .scl_speed_hz    = TILT_I2C_CLK_HZ,
        };
        err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2c_master_bus_add_device fallo: %s", esp_err_to_name(err));
            return err;
        }
        uint8_t devid = 0;
        err = reg_read(REG_DEVID, &devid, 1);
        if (err != ESP_OK || devid != ADXL345_DEVID_EXPECTED) {
            ESP_LOGW(TAG, "ningun sensor de inclinacion responde (ADXL345 devid=0x%02X, "
                          "esperado 0x%02X) -- nivel no disponible", devid, ADXL345_DEVID_EXPECTED);
            s_present = false;
            return ESP_ERR_NOT_FOUND;
        }
        /* 12,5 Hz (bits 0111, LOW_POWER a 0): menos ancho de banda, menos ruido.
         * Ver el bloque del filtro, arriba. Sigue habiendo muestra nueva cada
         * 80 ms y el dial se refresca cada 200, asi que no se pierde ninguna. */
        reg_write(REG_BW_RATE, 0x07);
        reg_write(REG_DATA_FORMAT, 0x08);  /* FULL_RES=1, +/-2g */
        reg_write(REG_POWER_CTL, 0x08);    /* Measure=1, sale de standby */
        s_chip = SENSOR_ADXL345;
        s_present = true;
        ESP_LOGI(TAG, "ADXL345 OK (montado a 270 grados)");
    }

    {
        float p = 0, r = 0, hd = 0;
        uint8_t cal = 0;
        /* 1.5 s y no 300 ms: la fusion del BNO055 tarda en asentar, y a 300 ms
         * la primera lectura sale con el rumbo de fabrica y con el vector de
         * gravedad todavia sin filtrar. Se apuntan los CUATRO estados de
         * calibracion (22-nov-2026): son los que dicen si el nivel y el rumbo
         * valen algo, y antes solo se veia el del magnetometro. */
        vTaskDelay(pdMS_TO_TICKS(1500));
        bool a1 = tilt_get(&p, &r);
        bool a2 = tilt_get_heading(&hd, &cal);
        bno_cal_t c = bno_cal_leer();
        float gx = 0, gy = 0, gz = 0;
        bool g_ok = (s_chip == SENSOR_BNO055) && bno_leer_g(&gx, &gy, &gz);
        ESP_LOGI(TAG, "lectura: pitch=%.2f roll=%.2f (%d) rumbo=%.1f cal=%u (%d) | "
                      "calib mag=%u acc=%u gyr=%u sys=%u | gravedad %.3f %.3f %.3f (%d)",
                 p, r, (int)a1, hd, (unsigned)cal, (int)a2,
                 (unsigned)c.mag, (unsigned)c.acc, (unsigned)c.gyr, (unsigned)c.sys,
                 gx, gy, gz, (int)g_ok);
    }

    /* SI CAMBIO EL SENSOR, el "cero" guardado no vale: los ejes y el signo de
     * la gravedad son otros (el del ADXL345 daba roll_off=93 grados con el
     * BNO055). Se descarta una vez y se anota que sensor hay. */
    uint8_t guardado = 0;
    /* 7 = BNO055 con los ejes y el signo del cabeceo definitivos.
         * 3 y 2 eran el mismo chip con los ejes al reves: al cambiar, el cero
         * guardado no vale y se descarta una vez (si no, el nivel queda con el
         * error de los ejes viejos y no hay forma de saberlo). */
    uint8_t actual   = (s_chip == SENSOR_BNO055) ? 7 : (s_chip == SENSOR_ADXL345) ? 1 : 0;
    if (load_tilt_sensor(&guardado) != ESP_OK || guardado != actual) {
        save_tilt_calibration(0, 0);
        save_tilt_sensor(actual);
        ESP_LOGW(TAG, "sensor cambiado (%u -> %u): se descarta el cero de nivel",
                 (unsigned)guardado, (unsigned)actual);
    }

    int16_t p_centi = 0, r_centi = 0;
    load_tilt_calibration(&p_centi, &r_centi);
    s_pitch_offset_deg = p_centi / 100.0f;
    s_roll_offset_deg  = r_centi / 100.0f;
    ESP_LOGI(TAG, "Calibracion: pitch_off=%.2f roll_off=%.2f",
             s_pitch_offset_deg, s_roll_offset_deg);
    /* SEIS LECTURAS EN LOS PRIMEROS 12 s (22-nov-2026): es la unica forma de
     * medir el nivel sin tener la pantalla delante. Con la autocaravana quieta
     * y en un sitio llano, la diferencia entre estas seis lineas dice si
     * tiembla (ruido), si se va (deriva) o si esta desviado (sesgo). Despues la
     * tarea se muere y no queda nada en el log de un viaje. */
    if (s_present) xTaskCreate(tilt_diag_task, "tilt_diag", 4096, NULL, 2, NULL);
    /* Y la tarea que guarda la calibracion de la brujula en cuanto esta a 3/3,
     * mire o no la pantalla de inclinacion (22-nov-2026). */
    if (s_present && s_chip == SENSOR_BNO055) {
        xTaskCreate(tilt_guardar_cal_task, "tilt_cal", 3072, NULL, 2, NULL);
    }
    return ESP_OK;
}

/* Ver el comentario del final de tilt_init(). */
static void tilt_diag_task(void *arg)
{
    (void)arg;
    for (int i = 0; i < 6; i++) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        float p = 0, r = 0, hd = 0;
        uint8_t cal = 0, m = 0, a = 0, g = 0, sy = 0;
        bool ok1 = tilt_get(&p, &r);
        bool ok2 = tilt_get_heading(&hd, &cal);
        bool ok3 = tilt_cal_get(&m, &a, &g, &sy);
        ESP_LOGI(TAG, "nivel[%d]: pitch=%+.2f roll=%+.2f (%d) | rumbo=%.1f cal=%u "
                      "(%d) | calib m=%u a=%u g=%u s=%u (%d)",
                 i, p, r, (int)ok1, hd, (unsigned)cal, (int)ok2,
                 (unsigned)m, (unsigned)a, (unsigned)g, (unsigned)sy, (int)ok3);
        /* OJO CON BORRAR AQUI (aprendido el 22-nov-2026, buscado en las fuentes):
         * Adafruit lo dice en su propio ejemplo de restore_offsets: "siempre hay
         * que recalibrar el magnetometro, se descalibra muy a menudo" y despues
         * de cargar los offsets el estado NO vuelve a 3 hasta que el sensor se
         * mueve. O sea: que m siga a 0 unos segundos despues de arrancar es
         * NORMAL, y borrar el perfil por eso es tirar la calibracion buena (fue
         * justo lo que paso). Aqui solo se avisa, sin tocar nada. */
        if (i == 5 && s_prof_restaurado && ok3 && m == 0) {
            ESP_LOGI(TAG, "perfil cargado; el magnetometro marca %u/3: se pondra a 3 "
                          "en cuanto el vehiculo se mueva (es lo normal)", (unsigned)m);
        }
    }
    vTaskDelete(NULL);
}

/* GUARDAR LA CALIBRACION SIN DEPENDER DE LA PANTALLA (22-nov-2026).
 *
 * Antes se intentaba solo desde tilt_get_heading(), o sea con la pantalla de
 * inclinacion delante. Si la brujula se calibraba mirando otra pantalla (o de
 * viaje, sin mirar), el perfil no se guardaba y al apagar se perdia: "otra vez
 * se ha borrado la calibracion".
 *
 * Esta tarea mira el estado cada 2 s (una lectura de CALIB_STAT, barata) y
 * cuando el magnetometro y el giroscopio estan a 3 lee el perfil -EN MODO
 * CONFIG, ver bno_leer_perfil- y lo guarda si es distinto del que ya hay. Se
 * "arma" cuando baja de 3, para poder guardar una recalibracion; mientras todo
 * siga a 3 y ya este guardado, no vuelve a tocar el chip. */
static void tilt_guardar_cal_task(void *arg)
{
    (void)arg;
    bool armado = true;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!s_present || s_chip != SENSOR_BNO055) continue;
        bno_cal_t c = bno_cal_leer();
        if (c.mag < 3 || c.gyr < 3) { armado = true; continue; }
        if (!armado) continue;
        /* Primera vez que llega a 3/3 desde el arranque. Si habia un perfil
         * restaurado, esto demuestra que el perfil SIRVE: nada mas arrancar el
         * magnetometro puede marcar 0 aunque el perfil sea bueno, y vuelve a 3
         * con el movimiento (ver el comentario de tilt_diag_task). */
        if (s_prof_restaurado) {
            ESP_LOGI(TAG, "brujula a 3/3 con el perfil cargado (m=3 g=3)");
            s_prof_restaurado = false;
        }
        uint8_t prof[BNO_CALIB_PROF_LEN];
        if (bno_leer_perfil(prof) != ESP_OK) {
            ESP_LOGW(TAG, "no pude leer el perfil de calibracion del chip");
            continue;
        }
        if (!perfil_valido(prof)) {
            ESP_LOGW(TAG, "el perfil leido del chip no tiene coeficientes: no lo guardo");
            continue;
        }
        if (s_prof_guardado_valido && memcmp(prof, s_prof_guardado, sizeof prof) == 0) {
            armado = false;   /* ya esta guardado: no se vuelve a leer */
            continue;
        }
        if (save_tilt_bno_calib(prof, sizeof prof) == ESP_OK) {
            memcpy(s_prof_guardado, prof, sizeof prof);
            s_prof_guardado_valido = true;
            armado = false;
            ESP_LOGI(TAG, "perfil de calibracion guardado (m=%u a=%u g=%u s=%u): "
                          "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X "
                          "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X",
                     (unsigned)c.mag, (unsigned)c.acc,
                     (unsigned)c.gyr, (unsigned)c.sys,
                     prof[0], prof[1], prof[2], prof[3], prof[4], prof[5],
                     prof[6], prof[7], prof[8], prof[9], prof[10], prof[11],
                     prof[12], prof[13], prof[14], prof[15], prof[16], prof[17],
                     prof[18], prof[19], prof[20], prof[21]);
        } else {
            ESP_LOGW(TAG, "no pude guardar el perfil de calibracion; reintento en 2 s");
        }
    }
}

/* RUMBO (brujula). Devuelve false si no hay BNO055: el ADXL345 no la tiene.
 * 'cal' es la calidad del MAGNETOMETRO (0-3; 3 = calibrado).
 *
 * AQUI YA NO SE GUARDA EL PERFIL: eso lo hace tilt_guardar_cal_task, que corre
 * siempre (no solo con la pantalla de inclinacion delante). Asi, si el que
 * calibra la brujula esta en otra pantalla, la calibracion se guarda igual y no
 * se pierde al apagar (22-nov-2026). */
bool tilt_get_heading(float *heading_deg, uint8_t *cal)
{
    if (!s_present || s_chip != SENSOR_BNO055) return false;
    uint8_t buf[2];
    if (bno_leer_bytes(BNO_EULER_H_LSB, buf, sizeof buf) != ESP_OK) return false;
    int16_t h = (int16_t)((buf[1] << 8) | buf[0]);
    float deg = h / 16.0f;
    while (deg < 0.0f)    deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    if (heading_deg) *heading_deg = deg;

    if (cal) {
        bno_cal_t c = bno_cal_leer();
        *cal = c.mag;
    }
    return true;
}

bool tilt_is_present(void)
{
    return s_present;
}

bool tilt_cal_get(uint8_t *mag, uint8_t *acc, uint8_t *gyr, uint8_t *sys)
{
    if (!s_present || s_chip != SENSOR_BNO055) return false;
    bno_cal_t c = bno_cal_leer();
    if (mag) *mag = c.mag;
    if (acc) *acc = c.acc;
    if (gyr) *gyr = c.gyr;
    if (sys) *sys = c.sys;
    return true;
}

/* ¿Hay perfil guardado y aplicado? (ver tilt.h). Se pone al restaurarlo al
 * arrancar y al guardar uno nuevo. */
bool tilt_tiene_perfil(void)
{
    return s_prof_guardado_valido;
}

bool tilt_get(float *pitch_deg, float *roll_deg)
{
    float p, r;
    if (!compute_angles(&p, &r)) return false;
    *pitch_deg = p - s_pitch_offset_deg;
    *roll_deg  = r - s_roll_offset_deg;
    return true;
}

bool tilt_calibrate(void)
{
    if (!s_present) return false;

    const int N = 20;
    float sum_pitch = 0.0f, sum_roll = 0.0f;
    int ok = 0;
    for (int i = 0; i < N; i++) {
        float p, r;
        if (compute_angles(&p, &r)) {
            sum_pitch += p;
            sum_roll  += r;
            ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (ok == 0) {
        ESP_LOGW(TAG, "Calibracion: 0 lecturas validas, se mantiene la anterior");
        return false;
    }

    s_pitch_offset_deg = sum_pitch / ok;
    s_roll_offset_deg  = sum_roll / ok;
    esp_err_t err = save_tilt_calibration((int16_t)(s_pitch_offset_deg * 100.0f),
                                          (int16_t)(s_roll_offset_deg * 100.0f));
    if (err != ESP_OK) {
        /* El cero nuevo queda puesto en RAM, asi que vale para esta sesion,
         * pero al apagar se pierde: quien lo llame debe avisarlo. */
        ESP_LOGW(TAG, "No se pudo guardar la calibracion en NVS (%s): "
                      "valdrá solo hasta el próximo apagado",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Calibrado (N=%d): pitch_off=%.2f roll_off=%.2f",
             ok, s_pitch_offset_deg, s_roll_offset_deg);
    return true;
}
