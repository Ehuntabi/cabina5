# Brújula en la página del ADXL (BNO055)

**Estado: pendiente de que llegue el módulo.** Este fichero es el plan acordado
el 29-oct-2026 para no tener que reconstruirlo cuando llegue.

## Qué se quiere

Que la pantalla de cabina (5" y 3,5") enseñe una **brújula** que indique el norte,
en la página donde hoy está el nivel (la del ADXL345).

## Por qué no vale lo que hay

- **El ADXL345 no puede.** Es un acelerómetro: mide la gravedad, o sea qué lado
  está abajo. El rumbo no está ahí; para saber dónde está el norte hace falta una
  referencia magnética.
- **El GPS tampoco, parado.** El rumbo sobre el suelo (COG, campo 8 del RMC) solo
  existe mientras el vehículo se mueve; por debajo de 1-2 km/h el módulo repite el
  último o da basura. Y una brújula se mira aparcado. Además hoy la P4 solo parsea
  la velocidad, y **no manda posición ni rumbo** a propósito (decisión del
  23-ago-2026): llevarlo a las pantallas obligaría a subir el protocolo y regrabar
  las tres placas.

## Hardware elegido

**GY-BNO055** (chip BNO055 de Bosch, 12 × 20 mm, 3-5 V, I2C `0x28`):

- Lleva acelerómetro, giroscopio **y magnetómetro**, y **hace la fusión dentro**
  (un Cortex-M0): le pides los ángulos de Euler y te da el rumbo ya calculado.
- **Se autocalibra** y dice cuánto de bien (estado 0..3 del magnetómetro).
- 12 × 20 mm entra en el soporte del ADXL (~15,5 × 20) sin rehacerlo.

Cable: el **mismo de 4 hilos** del ADXL — 3V3, GND, **SDA a IO17**, **SCL a IO18**
(JST de la pantalla; bus I2C_NUM_1, ver la cabecera de `main/tilt.c`). Ojo con el
pad `PS`: tiene que quedar en **I2C** (de fábrica viene así).

## Plan

1. **Driver `brujula.c`**: `CHIP_ID` (0x00 = 0xA0), reset, modo **NDOF** (0x0C) y
   lectura de los ángulos de Euler (0x1A..0x1F, 1/16 de grado).
2. **`tilt_get(pitch, roll)` sigue igual por fuera**: la página de inclinación, el
   montaje girado 270° y lo que se guarda en las paradas no se tocan. Cambia el
   driver por dentro, no el contrato.
3. **Calibración en NVS** — lo que no se puede olvidar: **el BNO055 pierde la
   calibración al quitarle la corriente**, y en la autocaravana se va en cada
   parada. Se leen sus 22 bytes de compensación (0x55..0x6A) y se reinyectan al
   arrancar; si no, cada vez que se gira la llave pide calibrar otra vez.
4. **UI**: en la página del nivel, una rosa, la aguja al norte y un botón
   **Calibrar** (girar 360° con el vehículo). Y un aviso **«sin calibrar»**
   mientras el estado del magnetómetro sea bajo: en un vehículo de acero va a
   pasar más de lo que gustaría, y una aguja que miente es peor que un aviso.
5. **Declinación**: el chip da el norte **magnético**; en España son 0-2°, se
   compensa con una constante y ya.

## Lo que hay que comprobar en placa (no vale en el host)

- **Sentido de los ejes**: en el ADXL el soporte va girado 270° (`a_ejes_vehiculo`
   en `tilt.c`); en el BNO055 los ejes son otros y hay que volver a verificar en
  qué sentido sale el cabeceo y el balanceo. Es lo primero que hay que mirar.
- Que la **calibración aguanta un corte de corriente** (quitar y volver a poner).
- Que el rumbo cuadra con lo que marca un móvil o una brújula de mano, con el
  vehículo aparcado y en varias orientaciones.

## Orden de trabajo acordado

- Probar el driver **en la 5"** primero, dejando la **3,5" con el ADXL** como
  referencia (así siempre hay una pantalla que funciona si algo va mal).
- Cuando esté validado, se porta a la 3,5" (el código es gemelo; solo cambia la
  parte de LVGL, que en la 3,5" es la 8).

## Lo que NO se hace en esta tanda

- Rumbo por GPS (necesita protocolo nuevo y no sirve aparcado). Si algún día se
  quiere, lo útil es usarlo para **autocalibrar**: circulando recto, comparar con
  el rumbo del GPS y aprender el desvío sin el ritual del giro.
