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

static bool read_raw_g(float *ax, float *ay, float *az)
{
    if (!s_present) return false;
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
 * El sensor va montado girado 270 grados (o sea, 90 en sentido antihorario
 * mirando la pantalla de frente): su eje X pasa a apuntar a la izquierda del
 * vehiculo y su eje Y hacia delante. De ahi el intercambio con el cambio de
 * signo. Z no se toca: el giro es sobre el plano de la pantalla y la gravedad
 * sigue entrando igual por ahi. */
static void a_ejes_vehiculo(float *ax, float *ay)
{
    float x = *ax, y = *ay;
    *ax =  y;
    *ay = -x;
}

static bool compute_angles(float *pitch_deg, float *roll_deg)
{
    float ax, ay, az;
    if (!read_g(&ax, &ay, &az)) return false;
    a_ejes_vehiculo(&ax, &ay);
    *pitch_deg = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;
    *roll_deg  = atan2f(ay, az) * 180.0f / (float)M_PI;
    return true;
}

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
        ESP_LOGW(TAG, "ADXL345 no responde (err=%s devid=0x%02X, esperado 0x%02X) -- "
                      "sensor no instalado/cableado todavia",
                 esp_err_to_name(err), devid, ADXL345_DEVID_EXPECTED);
        s_present = false;
        return ESP_ERR_NOT_FOUND;
    }

    /* 12,5 Hz (bits 0111, LOW_POWER a 0): menos ancho de banda, menos ruido.
     * Ver el bloque del filtro, arriba. Sigue habiendo muestra nueva cada 80 ms
     * y el dial se refresca cada 200, asi que no se pierde ninguna. */
    reg_write(REG_BW_RATE, 0x07);
    reg_write(REG_DATA_FORMAT, 0x08);  /* FULL_RES=1, +/-2g */
    reg_write(REG_POWER_CTL, 0x08);    /* Measure=1, sale de standby */
    s_present = true;

    int16_t p_centi = 0, r_centi = 0;
    load_tilt_calibration(&p_centi, &r_centi);
    s_pitch_offset_deg = p_centi / 100.0f;
    s_roll_offset_deg  = r_centi / 100.0f;

    ESP_LOGI(TAG, "ADXL345 OK (montado a 270 grados). Calibracion: pitch_off=%.2f roll_off=%.2f",
             s_pitch_offset_deg, s_roll_offset_deg);
    return ESP_OK;
}

bool tilt_is_present(void)
{
    return s_present;
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
                      "valdra solo hasta el proximo apagado",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Calibrado (N=%d): pitch_off=%.2f roll_off=%.2f",
             ok, s_pitch_offset_deg, s_roll_offset_deg);
    return true;
}
