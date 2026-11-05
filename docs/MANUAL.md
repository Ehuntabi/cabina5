# Manual de uso — pantalla de 5" de la cabina

Guía de las anotaciones del viaje: viaje, paradas, repostajes, peajes, bombonas
y mantenimiento. Escrita para usarla en la carretera, no para programar.

> **Antes de nada, lo importante:** todo lo que anotas aquí **ya llega a la P4 y
> se guarda en su tarjeta**, en la carpeta del viaje. Probado el 22-ago-2026.
>
> **Y no hace falta que la P4 esté encendida.** Si no está, el apunte se queda
> esperando y sale solo en cuanto vuelve. Mientras haya algo sin entregar verás
> abajo en la pantalla principal un aviso naranja: **"2 sin enviar"**. No se
> pierde nada al quitar el contacto.
>
> Lo único que **no** se guarda es lo que anotes **sin un viaje en marcha**: no
> hay carpeta donde escribirlo. La pantalla te avisa cuando pasa.

---

## 1. Moverse por la pantalla

Hay **tres pantallas** y se cambia **deslizando el dedo** a izquierda o derecha:

```
   INCLINACIÓN   <—>   DATOS   <—>   REGISTROS
    (nivelar)         (arranque)      (anotar)
```

Al encender siempre aparece **Datos**, la del centro.

Dos cosas que conviene saber:

- **Deslizar no es tocar.** Aunque empieces el gesto con el dedo encima de un
  botón, no se pulsa. Puedes deslizar desde cualquier punto sin miedo.
- **Al salir de Registros, se cierra lo que tuvieras abierto.** Si estabas a
  medio rellenar un formulario y te vas a otra pantalla, al volver encuentras el
  menú de iconos limpio, no el formulario a medias. Es a propósito: **lo que no
  guardaste, se pierde.**
- **Dos toques seguidos en la pantalla de Datos cambian el brillo.** Ver más
  abajo.

---

## 2. Pantalla de Datos

Lo que manda la P4, una vez por segundo. No se toca nada aquí, solo se mira.

- **Batería** (la tarjeta grande de arriba): el dibujo de la batería se rellena
  según la carga y **cambia de color** con el nivel. Debajo, voltios y amperios.
  - Amperios en **verde**: está cargando.
  - Amperios en **naranja**: está gastando.
  - A la derecha, **MOTOR**: el voltaje de la batería de arranque.
- **Aguas** (abajo a la izquierda): cuatro tramos para el agua limpia
  (1/4 … 4/4) y el bloque de abajo para las **grises**, que se pone **rojo** y
  pone "LLENO" cuando lo están.
- **Temperaturas** (abajo a la derecha): frigo y exterior, cada una con una
  **flecha de tendencia**:
  - **Flecha naranja hacia arriba** — la temperatura está subiendo.
  - **Flecha azul hacia abajo** — se mantiene o está bajando.

  La flecha tarda un poco en cambiar, a propósito: mira la tendencia de fondo y
  no cada oscilación del sensor, para que no esté bailando todo el rato.

> **La pantalla de Datos lleva además una tarjeta en medio**, de lado a lado, con
> los **kilómetros de la salida en curso** a la izquierda y el **tiempo
> conduciendo** a la derecha, con sus unidades: **`2h 35m`** y, pasado un día,
> **`1d 2h 35m`** (26-oct-2026; ver §9). Los dos números los manda la P4; sin
> viaje abierto, o con la P4 sin hablar, se quedan en `--`.

### Silenciar una alarma desde aquí

**El pitido de las alarmas lo hace la P4** (la pantalla de 7"), que es la que
lleva el altavoz; esta de cabina no tiene. Para callarlo desde el asiento del
conductor —que es justo lo que antes obligaba a ir hasta atrás en marcha— esta
pantalla le manda la orden a la P4.

Cuando la P4 tiene una alarma activa (agua en reserva, aguas grises llenas,
batería baja o congelador), en la esquina de **la tarjeta que toca** sale un
**altavoz en ROJO**: **rojo vivo con ondas** si esa alarma está sonando en la P4,
y **rojo oscuro y tachado** si ya se ha callado (desde el 5-nov-2026 va en rojo
siempre, para que cante de un vistazo).

| Lo que ves | Qué significa |
|---|---|
| **Altavoz rojo con ondas** | Esa alarma está activa y **está sonando** en la P4 |
| **Altavoz rojo oscuro y tachado** | Está activa pero **callada** |
| **No hay altavoz** | Esa tarjeta no tiene ninguna alarma |

- **Un toque en el altavoz**: se calla. **Otro toque**: vuelve a sonar.
- **Tocar la tarjeta también lo silencia** (toda la tarjeta, no hace falta
  acertarle al icono: la zona que responde es más grande que el dibujo). En la
  de aguas calla el agua limpia, en la de batería la batería y en la de
  temperaturas el congelador.
- Abajo al centro sale un aviso pequeño: **«Silenciar agua: enviado»** o, si la
  P4 no contesta en 10 segundos, **«Sin respuesta de la P4»** (y el altavoz
  vuelve a su sitio: lo que se ve aquí es lo que pasa de verdad).
- **Nada se calla para siempre.** Si la alarma se recupera (rellenas el
  depósito, sube la batería), se rearma sola y la próxima vez vuelve a sonar en
  las dos pantallas.

Lo que **no** cambia al silenciar: la tarjeta sigue **parpadeando** y el aviso
de la P4 sigue diciendo qué pasa (con "(silenciada)" detrás). Se calla el
pitido, no la señal visual.

### Cambiar el brillo: dos toques

**Dos toques seguidos en cualquier parte de la pantalla de Datos** bajan el brillo;
otros dos lo devuelven al **100 %**. Baja al **mínimo que hayas elegido**
(21-oct-2026). Tal como sale de fábrica ese mínimo es el **80 %**, así que al
principio parece que el doble toque no hace nada: por encima de ahí el panel
apenas cambia.

**En la 5" ese mínimo se elige en Configuración → Brillo mínimo**, con **−** y
**+**, de 5 en 5 entre el **5 %** y el **80 %**. Si la pantalla está en ese momento
en el nivel bajo, el cambio se ve en el acto; si está al 100 %, se aplica en el
siguiente doble toque. El tope del 80 % es a propósito: por debajo, este panel ya
se ve muy oscuro a la luz del día, y el doble toque está para no deslumbrar de
noche, no para dejar la pantalla ilegible.

- **El brillo no se recuerda al arrancar.** Enciendes y vuelve **siempre al 100 %**,
  aunque el mínimo elegido sí se guarda. Es a propósito: con la pantalla oscura, un
  arranque en el nivel bajo se ve como si estuviera apagada y no habría forma de
  subirla sin saber dónde tocar.
- Los toques tienen que ir seguidos: si tardas más de **0,65 segundos** entre uno y
  otro, no cuenta y no pasa nada.
- **Deslizar para cambiar de pantalla no cuenta como toque**, así que no vas a
  cambiar el brillo sin querer al moverte.
- La única parte que no vale es la **pastilla de "sin enviar"** de abajo: ahí un
  toque abre la lista, que es lo suyo.

Está puesto aquí y no en Configuración a propósito: lo que surge conduciendo se
resuelve en esta pantalla, sin menús y sin tener que apuntar con el dedo.

Si ves **"--"** en vez de números, es que **no está llegando nada de la P4**
(apagada, fuera de alcance o Wi-Fi mal configurado). Ver el apartado 10.

**El aviso naranja "N sin enviar"**, abajo del todo, aparece cuando hay apuntes
que todavía no han llegado a la P4. No es una avería: se entregan solos en
cuanto la P4 vuelva, y **no se pierden al quitar el contacto**. Está siempre a
la vista mientras quede algo, porque esta pantalla no puede saber que vas a
apagar — se queda sin corriente y ya.

> **Si ese aviso se pone ROJO y dice «CASI LLENA», hazle caso.** Caben **14**
> apuntes esperando (la cola son 16 huecos y 2 están reservados para que el cierre
> del viaje entre siempre), y como la P4 está siempre encendida lo normal es que la
> cola se vacíe sola en segundos: que llegue a acumular tantos significa que no le
> están llegando —la P4 sin corriente, o esta pantalla fuera de su wifi—. Si se
> llena del todo, el siguiente apunte **no se puede anotar** y te lo dice.

> **El aviso de «sin cerrar» se ve aquí, no solo en el cuaderno.** Si dejaste
> algo declarado, abajo al centro sale una pastilla naranja **«2 sin cerrar ›»**.
> Es la pantalla que está puesta mientras conduces, así que es donde tiene que
> verse. **Se toca** y te lleva derecho a la lista para cerrarlo o borrarlo.
>
> Comparte sitio con el aviso de «N sin enviar» (apuntes que aún no han llegado
> a la P4); si hay de los dos, salen juntos. Ese no se toca: se arregla solo en
> cuanto la P4 aparezca.

---

## 3. Pantalla de Inclinación (nivelar)

Un nivel de burbuja para aparcar. La bola **rueda hacia el lado bajo**: si se va
hacia ti, ese lado es el que hay que subir.

Colores:

| Color | Significado |
|---|---|
| 🟢 Verde | Nivelada (menos de 1°) |
| 🟡 Ámbar | Aceptable — hasta 3° de lado a lado, hasta 6° de morro a cola |
| 🔴 Rojo | Demasiado inclinada |

El límite de 3° de lado a lado **no es un capricho: es el del frigorífico**. Por
encima de eso el frigo de absorción trabaja mal.

**La forma ovalada amarilla del centro es esa zona aceptable**, y es oval a
propósito: no es lo mismo de lado a lado que de morro a cola. Mientras la bola se
quede **dentro de ese óvalo**, sigues dentro de lo que aguanta el frigo; en cuanto
se sale, el dial se pone **rojo**. Dibujarlo redondo sería mentir por los dos lados
a la vez: asustaría de más cabeceando y de menos balanceando.

> **La bola tarda un instante en asentarse, y es a propósito.** El sensor tiene
> un ruido propio que, ampliado por el dial, se veía como un temblor que no
> paraba con la autocaravana quieta. Ahora la lectura va suavizada: al subir a
> una rampa la bola sigue el movimiento al momento, pero cuando paras tarda un
> segundo largo en quedarse fija del todo. Si tiembla estando parada, entonces sí
> es que se mueve algo de verdad.

**Botón "Calibrar nivel":** pon el cero cuando la autocaravana esté realmente
nivelada (comprobado con un nivel de burbuja de verdad). Corrige que la pantalla
no esté montada perfectamente recta.

> El sensor va montado **girado** en su soporte, y el programa ya lo tiene en
> cuenta. No hay nada que tocar. Si algún día se cambia el soporte y el nivel
> empieza a marcar el balanceo donde debería marcar el cabeceo, es eso — hay que
> avisar para corregirlo en el programa.

---

## 4. Registros: la salida

La pantalla de la derecha es el cuaderno de a bordo, y se organiza alrededor de
la **salida**: la autocaravana se mueve por un motivo, y el viaje es solo uno de
ellos.

> **La idea que lo sostiene todo.** Esta pantalla se apaga al quitar el
> contacto. Eso parecía un estorbo y resulta ser el mecanismo:
> **declaras al llegar, rellenas al salir.** Con el motor aún en marcha pulsas
> «Repostaje». Echas gasolina. Al girar la llave, la pantalla arranca y ya sabe
> que dejó un repostaje abierto.

**Un botón grande por pantalla.** Lo que se hace siempre tiene que verse desde
lejos y acertarse con el dedo con el vehículo en marcha. **Configuración** queda
pequeño y gris en las pantallas donde sale (la principal y la del viaje).

Arriba, una franja fina con **la hora** y dos puntos: **GPS** y **P4**. Verde =
va; ámbar = el GPS todavía busca; gris = no hay. Los dos puntos **caducan con el
enlace**: si se cae la comunicación con la P4 se apagan, porque un indicador que
miente justo cuando algo va mal es peor que no tenerlo.

### Sin salida en marcha

Un solo botón: **NUEVA SALIDA**. Debajo, pequeño, Configuración.

### Qué tipo de salida

| | Qué es |
|---|---|
| 🔵 **Viaje** | Dura días, tiene nombre y carpeta propia, y dentro caben muchos apuntes |
| 🟠 **Puntual** | Sales a repostar, a la ITV, a por una bombona o al taller. Se trata como un viaje de una sola parada: carpeta propia, descargable igual |

### Mientras dura el viaje

Este pasa a ser el menú principal:

- **AÑADIR PARADA** — grande, es lo único que se hace cada día.
- **Terminar salida** y **Configuración** — comparten fila abajo: entre los dos
  gastan lo que gastaría uno solo.

Encima, una tira con **el nombre del viaje, el día que va** y, si hay algo sin
cerrar, **cuántas cosas quedan** («2 sin cerrar»).

> No pone lo gastado. Esta pantalla no lleva la cuenta del dinero — los importes
> se rellenan al volver a dar el contacto y viven en la P4 — y poner un `0,00 €` sería mentir.

### Las seis cosas que se anotan en un viaje

| Casilla | Qué hace al tocarla |
|---|---|
| 🟢 **Parada** | Pregunta **por qué** paras |
| 🔵 **Aguas** | Un toque. Qué hiciste —wc, grises, llenar agua— y lo que costó se preguntan al arrancar |
| 🟠 **Repostaje** | Un toque y listo |
| 🟣 **Peaje** | **La excepción**: abre el formulario ahí mismo |
| ⬜ **Bombona** | Un toque |
| ⬜ **Avería** | Avería o mantenimiento |

El **peaje** se rellena en el momento a propósito: se paga con el motor en
marcha y lo teclea el copiloto.

### Por qué paras

Visita · Descanso · Comer · Cenar · Compras · **PERNOCTA**.

**Pernocta va en verde** y las otras cinco en gris: es la única que lleva cola
—dónde, servicios, precio, valoración— y no debe pulsarse por error creyendo que
es un descanso. Al tocarla pregunta **dónde pasas la noche**:

Parking gratis · Parking de pago · Área gratis · Área de pago · Camping.

**Verde gratis, ámbar de pago.** El color ya dice si esto va a costar dinero, y
decide qué te preguntará al marcharte. Y ahí acaba lo que se toca al llegar:
llegas de noche, cansado y con el motor en marcha, y ahí no se rellena un
formulario de siete campos.

### Salida puntual

Cuatro opciones: **Repostaje · Bombona · ITV · Avería/Mant.** Al tocar una, la P4
abre su carpeta ahí mismo —igual que un viaje— y empieza a grabar el trayecto:
sirve para que quede el recorrido de ida completo, no solo el de vuelta.

Ese toque **todavía no declara nada**: es solo la salida del garaje. Mientras
conduces hacia el sitio, la **pantalla de datos** (la de batería, aguas y
temperaturas) enseña abajo un aviso ámbar, **«Toca al llegar a Repostaje»**.
Tócalo al llegar —o el botón **Ya he llegado** de esta misma pantalla, hace lo
mismo— y ahí sí se guarda la hora y se abre el apunte, como cualquier otra
declaración.

Antes de llegar (sin declarar todavía), la flecha «Atrás» de esta pantalla
**cancela la salida**: no hay progreso real que perder, así que deshacerla es lo
único que tiene sentido si te arrepientes. **En cuanto declaras**, la flecha
cambia de comportamiento: solo navega, sin cancelar nada — esta pantalla ya no
lleva ningún dato útil para el trayecto de vuelta, así que puedes irte a mirar
la de datos sin miedo a perder la salida. **Terminar salida** te espera aquí,
como botón grande, para cuando vuelvas a entrar en Puntual ya en el garaje.

Una salida puntual es **siempre una sola parada**: en cuanto declaras, no se
vuelve a ofrecer «Ya he llegado». La carpeta en la P4 se cierra al pulsar
**Terminar salida**, no al rellenar el dato — puede que rellenes el repostaje
mucho antes de llegar de vuelta, y eso no es lo mismo que terminar la salida.

### Qué pasa cuando declaras algo

Sale un **«Anotado»** con dos botones: **Vale** (verde) y **Deshacer** (rojo).
En las preguntas, **el color va con lo que hace el botón, no con dónde está**:
rojo lo que borra o deshace, verde lo demás. Se abre
de un toque, así que un dedo equivocado tiene que tener vuelta atrás. El apunte queda **abierto** y guardado en la
memoria interna, así que **sobrevive a que se apague la pantalla** — que es
justo lo que va a pasar en cuanto quites el contacto.

**Caben cuatro cosas abiertas a la vez.** Cuatro y no una porque en una estancia
larga conviven: pernoctas cinco días en un camping y por el medio sales a
repostar, compras una bombona y pagas un peaje.

**Hace falta la P4 encendida** para declarar: el apunte lleva su hora de inicio y
esta pantalla no tiene reloj propio. Si no está, avisa y no anota.

### Al volver a dar el contacto: cerrar lo que dejaste abierto

Al encender se te pregunta por lo que quedó abierto, **en orden de cuándo lo
declaraste**. **Se cierra todo**: la parada, las **aguas**, el repostaje, la
bombona, la avería y la **ITV**. (El peaje no aparece nunca aquí: se rellena en
el momento y no queda abierto.)

Solo se pregunta una vez por encendido, y solo cuando la P4 ya ha dicho la hora
—sin ella no hay nada que calcular.

#### La parada

```
        Parada en curso
        Descanso
        desde las 19:40  -  2 h 15 min

        [ Prolongar ]        [ Terminarla ]
```

- **Prolongar** no pregunta nada y **no toca la hora de inicio**: sigues ahí, y
  la parada sigue contando desde el principio. Se te volverá a preguntar la
  próxima vez.
- **Terminarla** la cierra y la manda a la P4 con **su hora de entrada y de
  salida reales**, no tecleadas, más los minutos que duró.

#### La parada que no declaraste

Paras a comer, no tocas nada y quitas el contacto. Al volver, la pantalla se da
cuenta del hueco y te lo ofrece:

```
        Estuviste parado?
        Estuviste parado desde las 14:10,
        1 h 35 min.

        Si fue una parada, dime de que
        y la anoto con esa hora.

        [ No ]              [ Anotarlo ]
```

Si dices **Anotarlo**, te lleva a la pantalla de motivos de siempre y lo que
elijas se guarda **con la hora en que se apagó la pantalla**, no con la de ahora.
Si dices que fue una **pernocta**, te pedirá el sitio y luego el precio y los
servicios, como cualquier otra.

Solo aparece si estabas **de viaje**, la pantalla estuvo apagada **más de media
hora** y no habías dejado nada anotado. Se pregunta **una sola vez** por
encendido: si dices que no, no insiste.

#### La pernocta, al terminarla

Si la parada era una **pernocta**, «Terminarla» no la guarda todavía: abre una
pantalla con lo que solo se sabe al marcharse.

```
        PERNOCTA
        Area de pago
        2 noches

        Precio  [ 18.00 ][ EUR ][ Noche | 24 h ]

        [ Servicios > ]   [ Guardar noche ]
```

- Arriba, **dónde dormiste y cuántas noches**. No se pregunta: lo dijiste al
  llegar y las horas las sabe el aparato. Está ahí para que veas que estás
  cerrando la de anoche y no otra cosa.
- **El precio es por noche** (o por 24 h, que es como cobran algunas áreas): es
  lo que se anuncia a la entrada, y la cuenta la echa la P4 multiplicando por las
  noches que fueron. En un **camping** el selector ni sale, porque allí siempre
  se cobra por noches.
- En un sitio **gratis no sale el precio**, pero sí los servicios y la nota: un
  parking gratis con agua y buena noche es justo lo que interesa recordar.
- **Servicios** lleva a la lista, donde **cada uno tiene su precio al lado**
  (ver más abajo), y desde ahí a la **valoración** del sitio.

La hora de fin se congela al pulsar «Terminarla», no al guardar: rellenar esto
son dos minutos y la noche no duró dos minutos más por eso.

La pernocta se guarda **en su propia hoja** (`pernoctas.csv`), no entre las
paradas: lleva sitio, precio, servicios y nota, y mezclarla dejaría la hoja de
paradas con dos cabeceras distintas. Sigue contando como parada en el resumen, y
lo que costaron las noches sale en su línea de **Alojamiento**.

#### Las aguas, el repostaje, la bombona, la avería y la ITV

Todos funcionan igual: un cartel con la hora a la que lo anotaste y dos
botones, **Rellenarlo** (abre su formulario) y **Luego** (sigue
abierto, se vuelve a preguntar).

```
        FINALIZAR REPOSTAJE
        Lo anotaste a las 19:02.
        Ahora ya sabes el importe,
        los litros y los kilómetros.

        [ Luego ]          [ Rellenarlo ]
```

**Rellenarlo** abre el formulario **y te lleva a la página del cuaderno**, que es
donde vive. La pregunta aparece encima de la pantalla que estés mirando —
normalmente la de datos, que es la que sale al dar el contacto—, así que si no te
llevara parecería que el botón no hace nada. **Luego** no descarta nada: sigue
abierto y se vuelve a preguntar — rellenarlo con el surtidor delante no siempre
se puede.

Al guardar, el apunte va a la P4 **con el mismo número que se le reservó al
declararlo**, así que aunque se reintente no se duplica.

**El formulario de aguas** son tres líneas, una por cosa, con su casilla y su
importe al lado:

```
        AGUAS
        [x] Vaciar grises   [      ]
        [x] Vaciar wc       [  2.00]
        [ ] Llenar agua     [      ]
              Moneda   [ EUR ]

        [    Guardar aguas    ]
```

**Marcas lo que hiciste y solo pones precio a lo que pagaste**, y **si escribes
un precio, la casilla se marca sola** — un importe en una línea sin marcar no se
guardaría, así que no hay manera de perderlo por ese despiste.

La confirmación enseña **las tres líneas siempre**, y la que no lleva importe
sale como **gratis**:

```
        Guardar aguas?
        Vaciar grises:  gratis
        Vaciar wc:  2.00 EUR
        Llenar agua:  gratis
```

Así ves de un vistazo lo que ha costado la parada. Si no marcas nada y no pones
ningún importe, se resume en *«todo gratis»* en vez de tres líneas diciendo lo
mismo. Un precio por cada cosa y no uno solo: en muchas áreas el vaciado es
gratis y el agua se paga, y con un importe único no habría manera de saber cuál
era cuál.

> Lo que se **guarda** sigue siendo lo que marcaste: una línea sin marcar sale
> como «gratis» en la confirmación, pero en la hoja de cálculo va como *no
> hecha*. La casilla es la que dice qué hiciste; el precio, lo que costó.

**El formulario de la ITV** pide **resultado, kilómetros y precio**:

```
        ITV
        Resultado
        [ Favorable ][ Desfavorable ][ Negativa ]

        Kilometros  [ 148320 ]
        Precio      [  49.50 ][ EUR ]

        [     Guardar ITV     ]
```

Los tres resultados son los **nombres oficiales** de la inspección y no un
«pasada / no pasada»: con **desfavorable** puedes seguir circulando y tienes que
volver, con **negativa** el vehículo no se mueve de allí. La diferencia importa
demasiado para perderla al anotarla.

### Borrar algo que anotaste antes

La tira de arriba, cuando hay algo abierto, pone **«2 sin cerrar ›»**. **Tócala**
y sale la lista de lo que tienes pendiente: qué es, a qué hora lo anotaste y los
botones de cada uno:

- **Terminar** (verde) — en todos: paradas, aguas, repostajes, bombonas,
  averías e ITV. Saca la misma pregunta que al dar el contacto.
- **Borrar** (rojo) — en todos. Pide confirmación, y al borrar el último te
  devuelve al menú.

A la lista también se llega **desde la pantalla de datos**, tocando la pastilla
naranja de «N sin cerrar» (apartado 2).

Es la red de seguridad de un toque equivocado: se declara con un dedo y sin esta
lista se quedaría ahí hasta que lo cerrases con datos inventados.

### Terminar con algo abierto

Ni «Terminar salida» ni cancelar una salida puntual te lo impiden, pero **te
dicen cuántos apuntes se van a perder** y piden un segundo toque. No se impide a
propósito: cerrar un apunte solo se puede al volver a dar el contacto, así que
prohibirlo dejaría la pantalla sin salida.

---

## 5. Viaje: empezar y terminar

Al elegir **Viaje** te pide dos cosas, una detrás de otra:

1. **El destino**, con el teclado grande: no un nombre para el viaje, sino *a
   dónde vas*, que es mucho más fácil de contestar con el motor en marcha.
2. **Los kilómetros que marca el cuadro** en ese momento (16-oct-2026), con el
   teclado de números. Es **opcional**: se puede dejar en blanco y el viaje
   empieza igual. Sirve para saber después lo que ha hecho el viaje de verdad y
   compararlo con lo que mide el GPS (ver §9), y se pide **ahora** porque es
   cuando lo tienes delante.

Después sale el cartel de confirmación con los dos datos: si te has equivocado
al teclear el número, se ve ahí antes de empezar nada. Con eso la P4 crea la
carpeta en su tarjeta, con la fecha delante para que se ordenen solas:

```
/sdcard/viajes/2026-08-22_Zumaia/
```

- Sin acentos ni ñ, y máximo 20 caracteres. La pantalla no te deja teclear otra
  cosa. Los kilómetros son seis cifras como mucho.
- **El nombre no se puede cambiar después** (los kilómetros, tampoco: van con el
  viaje). Te avisa antes de empezar.
- **Hace falta la P4 encendida.** Si no, avisa («Enciende la P4 primero») y no
  empieza: la carpeta lleva la fecha en el nombre y esta pantalla no tiene reloj.
- **Si el GPS todavía no tiene posición**, pregunta si continuar igual o
  cancelar: sin fix no se graba nada del trayecto hasta que lo encuentre, y así
  te enteras antes de arrancar en vez de descubrirlo al volver a casa. Nada se
  ha creado todavía en la P4 en este punto, así que cancelar no deshace nada.
  Lo mismo pasa al empezar una salida puntual.

### Si la P4 dice que ya hay un viaje abierto

Pasa cuando se empezó un viaje y esta pantalla perdió el hilo (o se empezó desde
otro sitio). **Se resuelve desde la cabina**, sin ir a la P4: sale una pantalla
con dos salidas.

| Botón | Qué hace en la P4 |
|---|---|
| **GUARDARLO** | Lo cierra como bueno y le escribe su resumen |
| **Apartarlo (era una prueba)** | Renombra su carpeta a `DESCARTADO_…` y la suelta. **No borra nada** |

Las dos van por la cola de pendientes, así que tardan unos segundos en llegar.
Después ya puedes empezar el viaje nuevo. La flecha «Atrás» te saca sin tocar
nada.

> «Apartarlo» no pide el recuento de apuntes del viaje viejo a propósito: no lo
> sabemos, no era nuestro, y mandar un número inventado lo marcaría como
> incompleto sin serlo.

**Un caso más, que no es un fallo de la clave**: si al cerrar o apartar el viaje
ajeno la P4 contesta que **pide su clave de administración** (no la del portal),
abajo sale la pastilla **«LA P4 PIDE CLAVE ADMIN»** y un aviso que lo explica
(29-oct-2026). Esa clave no se pone en esta pantalla: la P4 la pide para dejar
cerrar un viaje que está grabando **otra** pantalla. El aviso ofrece
**«Descartarlo»**, que tira ese cierre de la cola para que los apuntes siguientes
sigan su camino (el viaje ajeno se queda como estaba, abierto), y **«Esperar»**,
que no toca nada. Antes ese caso se confundía con «la clave está mal» y
mandaba a Configuración a reescribir una clave que estaba bien, sin dejar salir de ahí.

El viaje **no se da por empezado hasta que la P4 lo confirma**. Si algo falla te
lo dice y todo se queda como estaba, en vez de poner «viaje en curso» mientras en
la tarjeta no hay nada.

**Terminar salida** pide confirmación, pero **sí deja terminar con algo sin
cerrar**: antes te dice cuántos apuntes se van a perder y pide un segundo toque
(«Sí, descartar» / «No, dejarlo»), porque al cerrar la salida se olvidan los
apuntes abiertos y se pierden. Como se cuenta en el apartado 4, no se impide a
propósito: prohibirlo dejaría la pantalla sin salida.

Al pulsar **Finalizar viaje** pasa lo mismo que al empezar, al revés: primero te
pide **los kilómetros que marca el cuadro al llegar** (16-oct-2026, también
opcional) y después confirma, con el número a la vista. Con las dos lecturas —la
de la salida y la de la llegada— el resumen del viaje dice cuánto has hecho de
verdad y lo compara con el GPS (ver §9).

A diferencia del inicio, el fin **no exige la P4 delante**: entra en la cola de
pendientes, detrás de los apuntes que falten por entregar, y se manda cuando
aparezca. Lo que no puede pasar es que te quedes con «viaje en curso» en pantalla
porque la P4 estaba apagada.

**El estado sobrevive a apagar la pantalla.** Se guarda en la memoria interna; un
corte de corriente no lo pierde.

---

## 6. Cómo se cuenta el precio de la noche

Al cerrar una pernocta (apartado 4) te pide el precio, y ahí hay un detalle que
importa para la cuenta.

### Por noche o por 24 horas

Aparece una fila con **importe + moneda + tipo de cobro** en la misma línea:

- **Camping:** siempre **por noche**. El selector ni sale.
- **Área:** eliges **"noche"** o **"24 h"** — lo que ponga su cartel, que es lo
  que leíste al entrar.

> **La diferencia importa para la cuenta.** Por noche se cuentan cambios de día:
> llegas el viernes por la tarde y te vas el sábado por la mañana = **1 noche**.
> Por 24 h se cuentan periodos desde que entras, **redondeando hacia arriba**:
> 25 horas son **2 periodos**. Es como cobran ellos.

### Servicios

Botón **"Servicios →"**. Una línea por servicio, con su casilla y **lo que
costó** al lado:

```
        SERVICIOS
        [x] Agua potable     [  1.00]
        [x] Vaciado grises   [      ]
        [ ] Vaciado WC       [      ]
        [x] Electricidad     [  2.00]
        [ ] Duchas/WC        [      ]
        [x] Basura           [      ]
        [ Buena ][ Aceptable ][ Mala ]
```

**Marcado y sin importe significa gratis**, como en las aguas — y si escribes un
precio, la casilla se marca sola. En un **camping** normalmente van todos
marcados y sin importe: ya están pagados en el precio de la noche.

La **moneda** es la de la pernocta, la misma parada. Por eso su selector se queda
a la vista aunque el sitio sea gratis: dormir puede ser gratis y el agua costar
un franco.

> **Se guarda el desglose.** Cada servicio lleva su propia columna de precio en
> `pernoctas.csv`, así que sabrás que en esa área el agua costaba 1 € y la luz 2.
> En el resumen del viaje esos importes se suman con la noche en la línea de
> **Alojamiento**, que es lo que interesa para las cuentas.

### Valoración

**La última línea de Servicios**, tres botones de color:

🟢 **Buena** · 🟡 **Aceptable** · 🔴 **Mala**

Solo queda uno elegido: se pone a todo color y con un visto, y los otros dos se
apagan. Si no tocas ninguno, el sitio queda **sin valorar** — no se inventa una
nota.

Al pulsar **cualquiera** de los tres se abre además **«Pegas del sitio»**:

**Poco seguro · Ruidosa · Inclinada · Sin sombra**

Son casillas aparte de la nota, y salen con las tres a propósito: un sitio bueno
puede ser ruidoso o estar inclinado, y eso es justo lo que quieres recordar
antes de volver. Si no hay ninguna pega, sales con **Volver** sin marcar nada.

**Sin que tengas que hacer nada**, la parada guarda también **cómo quedó
nivelada** la autocaravana (el cabeceo y el balanceo del momento). Es un dato del
sitio, no del momento: si un área tiene mucha pendiente, conviene saberlo antes
de volver.

---

## 7. Paradas de varios días

Aquí está la parte que más conviene entender.

Una parada en un **área, un camping o una pernocta** no termina al llegar:
termina **cuando te vas**, que puede ser días después y con la pantalla apagada
por medio (se va con el contacto).

Por eso la parada queda **abierta**, y **al volver a encender** sale solo:

```
        Parada en curso
        Area de pago
        desde las 19:40  -  3 noches

        Al terminarla te pido el precio
        y que habia.

        [ Prolongar ]        [ Terminarla ]
```

- **"Prolongar"** — **sigues ahí**. No pregunta nada y no toca la hora de
  inicio: la parada sigue contando desde el principio.
- **"Terminarla"** — cierra la parada con sus horas de entrada y salida reales.
  Si es una **pernocta**, antes abre la pantalla del precio, los servicios y la
  nota (apartado 4).

**Esta pregunta sale en cada contacto.** Cada vez que enciendes —cada mañana,
cada alto del viaje—, si la parada sigue abierta te vuelve a preguntar, con las
noches (o las horas) ya puestas al día. **No hay ningún botón «Finalizar
parada»** en la pantalla del viaje: se cierra con **«Terminarla»** de esta
pregunta, o desde la lista de **«SIN CERRAR»** —toca la tira de «N sin cerrar»
y dale a **Terminar** en su fila. Si hay más de una cosa abierta, pregunta por
cada una, en el orden en que las anotaste.

Las aguas (**vaciado, llenado, agua**) **también dejan algo abierto**: al tocar
Aguas se abre un apunte y se pregunta al volver a dar el contacto —«Aguas
terminadas», con **Rellenarlo** o **Luego**—, igual que a la parada. Lo único
que se cierra en el sitio es el **peaje**, que se rellena en el momento.

### ⚠️ Necesita que la P4 esté encendida

Esta pantalla **no tiene reloj propio** (ni pila). La fecha y la hora se las da
la P4.

- Si anotas una parada y **la P4 no ha dado la hora todavía**, sale el aviso
  **«Enciende la P4 primero»** y no se anota: el apunte va con su hora de
  inicio.
- Si intentas **cerrarla** sin la P4, sale **«Sin la P4»**: sin fecha no se
  puede calcular la estancia ni las noches.
- Al encender, si la P4 no aparece, **no pregunta nada**: espera su hora, y si
  no llega lo deja para el siguiente encendido, con todo lo abierto todavía
  abierto.

Como norma: **enciende la P4 antes que esta pantalla**, o al menos deja que se
enlacen antes de anotar la parada.

---

## 8. Los demás formularios

### Repostaje

Importe (con su moneda), **litros** y **kilómetros**. Abajo, calculado solo: el
**precio por litro** y, si hay un repostaje anterior, los **litros a los cien**.

> **Los kilómetros son nuevos.** Con ellos salen solos el consumo y el coste por
> kilómetro. Se piden desde el primer día a propósito: los repostajes viejos ya
> no los van a tener nunca.
>
> El consumo solo sale si el número tiene sentido — hay repostaje anterior, el
> cuentakilómetros va hacia delante y el salto no es absurdo. Antes que una cifra
> inventada, no se dice nada.
Moneda · **Importe** y **Litros** en la misma línea · **Precio/litro**, que se
calcula solo mientras escribes.

### Peaje
Solo **Importe** y moneda. Es el más rápido de todos.

### Bombona
**Cuántas** (1 o 2) · **Precio total**.

### Mantenimiento
Seis casillas, y puedes marcar **varias a la vez** (con el mismo kilometraje
puedes haber hecho el aceite *y* su filtro):

Aceite · Filtros · Correa · Ruedas · Lavado · Otros

Al marcar **Filtros** se despliegan las cuatro opciones (aceite, gasoil, aire,
habitáculo) para marcar las que toquen. **Otros** lleva un motivo escrito, no
es un sí/no. Al marcar **Ruedas** aparece **cuántas** — solo **2 o 4** (se
cambian por eje, no sueltas); si no, ese selector ni se ve. Abajo, **Km** y
**Coste**.

### Configuración

Se llega con el botón **Configuración**, que solo sale en **dos pantallas del
cuaderno**: la principal (sin salida en marcha) y la del viaje en marcha, donde
comparte fila con «Terminar salida». Tiene cuatro cosas:

```
        [ < Volver ]        CONFIGURACION

        [  Wi-Fi  ]   [ Actualizar pantalla ]

        Brillo mínimo         ( - )   80 %   ( + )

              Version instalada
                  v1.48
             Oct 29 2026  09:47
```

- Dos botones: **Wi-Fi**, que lleva a los ajustes de abajo, y **Actualizar
  pantalla**, que mira si la P4 tiene una versión nueva y, si la hay, **pregunta
  antes** de instalarla (la actualización automática no pregunta).

  > **La versión nueva la sirve la P4.** El fichero (`.bin`) se sube **una sola
  > vez a la P4** —desde la web, `192.168.4.1:8081/pantallas`, o desde la app— y
  > a partir de ahí esta pantalla se entera **sola** en menos de un minuto, se lo
  > descarga, se reinicia y arranca con él. No hay que descolgarla ni meterle un
  > cable, y no hay que hacer nada en cada pantalla: si mañana hay dos pantallas
  > de cabina, las dos se actualizan igual.
  >
  > Mientras se actualiza se reinicia un par de veces: es normal. Y si la nueva
  > no arrancara bien, **vuelve sola a la de antes** (se escribe en el hueco
  > libre, nunca encima de la que está funcionando).

- **Brillo mínimo** — el nivel al que baja el doble toque en la pantalla de Datos
  (ver «Cambiar el brillo»). Con **−** y **+**, de 5 en 5 entre el **5 %** y el
  **80 %**; de fábrica el **80 %**. El número del medio es el que hay elegido.
- Y **la versión que lleva puesta**, en grande, con la fecha de compilación
  debajo. Es el primer dato que hace falta cuando algo va raro: te dice si la
  pantalla tiene lo último o se quedó en una versión vieja. La fecha distingue
  dos grabados de la misma versión.

### Wi-Fi
Cuatro campos, todos en la misma pantalla:

- **SSID** y **Password** — la red de la P4 a la que se conecta esta pantalla.
  Sirve para **cambiar a otra P4 sin reflashear**.
- **Usuario** y **Clave** — los del portal de la P4. Desde el 4-nov-2026 **la
  clave es la misma que la del Wi-Fi** (la del punto de acceso), así que si la
  pantalla ya está conectada a la red, ya tiene la clave buena. Hacen falta para
  todo lo que esta pantalla le manda: los apuntes del viaje y **callar las
  alarmas**. Se leen en la P4, en **Ajustes → Wi-Fi**, en la tarjeta del punto de
  acceso. La **otra**, la de **administración**, **no** se pone aquí: esa solo hace
  falta para actualizar, descargar, las claves de los Victron y borrar un viaje. Sin
  ellas, al iniciar un viaje sale "La P4 no acepta la clave".

**Para escribir en un campo, tócalo**: se abre un editor a pantalla completa con
el nombre del campo arriba, el texto en grande y el teclado debajo; se acepta con
el **✓** del teclado y se descarta con la **✗**. En la pantalla de ajustes el
**SSID** se ve tal cual, pero las dos claves (Password y Clave) van **tapadas**
con puntos; al abrir el editor sí se ven en claro, así que de ahí puedes leer la
clave del punto de acceso si la has perdido.

**Guardar y Cancelar salen solo cuando has cambiado algo** (cualquiera de los
cuatro campos), con el aviso *"Cambios sin guardar"* al lado. Mientras no toques
nada no hay botones: no hay nada que guardar. **Cancelar** devuelve los campos a
como estaban.

Al tocar **Guardar**:

- Si has **cambiado la red** (SSID o Password), **pide confirmación** antes de
  reconectar — es fácil dejarse la pantalla incomunicada por un dedazo en la
  contraseña. Al aceptar pone *"Guardado, reconectando..."* y tarda unos
  segundos en volver a enganchar.
- Si solo has tocado las **credenciales del portal**, guarda sin preguntar y
  pone *"Guardado."* (equivocarse ahí no deja la pantalla sin datos).

Se acuerda de todo **aunque se apague con el contacto**, que es como se apaga
siempre. Y si algún día cambias la clave del Wi-Fi en la P4, hay que cambiarla
también aquí.

El **Volver** de esta pantalla regresa a Configuración, no al cuaderno.

---

## 9. Traerte el viaje al ordenador

Cuando finalizas un viaje, la P4 escribe además un **`resumen.txt`** con lo que
uno quiere saber al volver, en castellano y no en columnas:

```
RESUMEN DEL VIAJE
=================
Cerrado el 2026-08-22 20:31
Duracion: 4 dias

GASTOS (EUR)
  Combustible ...    82.40   (54.2 litros)
  Peajes ........    23.10
  Gas ...........    18.00
  Mantenimiento .     0.00
  Aguas .........     6.00
  Alojamiento ...    36.00
  ----------------------
  TOTAL .........   165.50

  Precio medio del litro: 1.520 EUR

Paradas anotadas: 6

ENERGIA
  Cargados ......     8420 Wh
  Gastados ......     7160 Wh
  De la placa ...     6180 Wh   (31.4 h de sol)

Recorrido: 412.6 km
  Conduciendo: 6 h 12 min
  Cuadro al salir: 84320 km
  Cuadro al llegar: 84761 km
  El cuadro dice 441 km; el GPS midio 412.6 km (-28.4 km)
```

Las tres líneas de debajo del recorrido son del viaje y de nadie más:

- **Conduciendo** es tiempo de volante de verdad (lo que el vehículo estuvo en
  movimiento según el GPS), no el tiempo con el viaje abierto: las noches
  paradas de un viaje de tres días no cuentan.
- **Cuadro al salir / al llegar** son los kilómetros que tecleaste al empezar y
  al terminar. Con los dos se sabe lo que ha hecho el viaje **de verdad**, que es
  el número que uno mira al llegar a casa.
- Y la última línea los compara. Si el GPS midió bastante menos, es que se perdió
  un tramo (un túnel, un parking cubierto, la antena tapada); si midió más, es
  que alguna lectura está mal tecleada. Cuando al llegar marcas **menos** que al
  salir, el resumen lo dice en vez de restar, que daría un número negativo con
  pinta de dato. Y si el GPS no llegó a tener posición, solo sale lo del cuadro,
  porque no hay con qué comparar.

El **recorrido lo mide el GPS** de la P4 mientras conduces. Cuenta por lo bajo
a propósito: si pierde la señal —un túnel, un parking cubierto— no une los dos
extremos por la recta, porque no es por donde fuiste. Y estando parado no suma,
aunque el receptor se mueva unos metros él solo. Lo puedes ir viendo subir en la
P4, en Ajustes → GPS.

El apartado **ENERGIA es de este viaje y de ninguno más**: los contadores de la
P4 se ponen a cero solos cuando declaras el viaje, y se cierran cuando lo
finalizas. No tienes que tocar nada en la pantalla de atrás.

Y mientras dura el viaje, **sin que hagas nada**, va guardando cada 5 minutos la
batería, el solar, las temperaturas y las aguas, y cada hora un recuento de
horas y energía. Si el viaje se corta a lo bruto —se queda sin batería, una
avería— eso ya está escrito y no se pierde.

**También va dejando el recorrido**, en un `ruta.csv` dentro de la carpeta del
viaje: una línea cada 30 segundos con la posición y la altura, pero **solo
cuando te mueves**. Parado no apunta nada, así que una semana en un camping no
te deja miles de líneas repetidas. Al volver, ese fichero se abre en Google
Earth o en cualquier programa de mapas y ves por dónde fuiste. Un mes de viaje
ocupa medio mega.

**Y en el mismo mapa, dónde hiciste cada gasto.** Cada apunte que guardas
(repostaje, bombona, peaje, servicios…) se apunta **con la posición** que tenía
la P4 en ese momento, así que el viaje lleva dos ficheros: el `ruta.csv` de
siempre (el camino) y un **`viaje.gpx`** que se abre directamente en Google Earth
o en Google Maps y trae **el recorrido y una chincheta por apunte**, con su
nombre y su importe ("bombona 25.90 EUR"). El mapa se genera al cerrar el viaje y
también al bajarte la carpeta por Wi-Fi. Sin cobertura de GPS no hay chincheta de
ese apunte (el importe se guarda igual): mejor eso que una chincheta en mitad del
mar.

Los puntos se van juntando en la memoria de la P4 y bajan a la tarjeta cada
cinco minutos, no uno a uno: así la tarjeta se desgasta diez veces menos. Si se
corta la corriente a lo bruto puedes perder los últimos minutos de traza, nunca
el viaje.

### Bajártelo por Wi-Fi

Conéctate al Wi-Fi de la P4 desde el móvil o el portátil y entra en:

```
http://192.168.4.1/data/viajes
```

Sale la lista de viajes guardados, cada uno con su estado:

| Estado | Qué significa |
|---|---|
| 🟢 **Listo** | Cerrado y completo. Se descarga y ya |
| 🟠 **Incompleto** | Le faltan apuntes. Puedes bajarlo, pero avisando |
| ⚪ **En curso** | No lo has finalizado. No se puede bajar todavía |

Te bajas un `.tar` con la carpeta entera dentro: los registros, la telemetría y
el resumen.

> **Por qué se bloquean los incompletos.** Si a un viaje le faltara un
> repostaje, el archivo se bajaría **con pinta de estar entero** y no habría
> forma de notarlo: no falta un fichero, falta *una línea*. Así que la P4
> compara lo que la pantalla dice haber anotado con lo que ella guardó, y si no
> cuadra lo dice. Si aun así lo necesitas, se puede bajar — pero el archivo sale
> con **`_INCOMPLETO`** en el nombre para que no se confunda.

### Ojo: `viaje.tar` ya no es lo que era

Antes había un `viaje.tar` que **no era un viaje**: empaquetaba la batería, el
solar y el frigo de *todos* los días. Ese paquete sigue estando, pero ahora se
llama por lo que es: **`historico.tar`**.

---

## 10. Cuando algo no va

| Qué ves | Qué pasa |
|---|---|
| **"--"** en todos los datos | No llega nada de la P4. Comprueba que esté encendida y que el Wi-Fi de Ajustes apunte a su red |
| **"Enciende la P4 primero"** | Anotaste algo sin que la P4 diera la hora. Enciéndela: el apunte va con su hora de inicio |
| **"Sin la P4"** al finalizar parada | Lo mismo: sin fecha no se puede calcular la estancia |
| **"La P4 no acepta la clave"** | Falta el usuario (`p4`) o la **clave del punto de acceso** en Ajustes → Wi-Fi: la misma con la que esta pantalla entra en la red |
| **"Ya hay un viaje abierto"** | La P4 tiene uno sin cerrar. Termínalo antes de empezar otro |
| **"Sensor ADXL345 no detectado"** | El sensor de inclinación no responde. Es un problema de conexión, no de uso |
| El nivel marca torcido estando recta | Pulsa **Calibrar nivel** con la autocaravana bien nivelada |
| Volviste a Registros y estaba en blanco | Normal: al salir se limpia. Lo no guardado se pierde |
| **"N sin enviar"** en naranja | No es avería: hay apuntes esperando a la P4. Se van solos cuando aparezca |
| **"N sin enviar - CASI LLENA"** en rojo | Sí es aviso: sale cuando quedan 3 huecos de 16 (y sigue con menos). La P4 lleva rato sin recibir; mira si tiene corriente y si esta pantalla ve su wifi |
| **"Guardado solo aquí"** | Lo anotaste sin viaje en marcha, así que no va a la tarjeta. Inicia un viaje antes |

---

## 11. Resumen de lo que todavía no hace

Para no llevarse sorpresas:

1. **Ya se cierra todo**: parada, pernocta (con precio, servicios y
   valoración), aguas, repostaje, bombona, avería e ITV. El **peaje** se guarda
   entero desde el principio, porque se rellena en el momento y no queda
   abierto.
2. **Los apuntes no dicen dónde.** La P4 **ya tiene GPS** (desde el 24-ago-2026)
   y con él pone su reloj en hora sola, y ya mide el **recorrido del viaje**
   (ver §9, `Recorrido: ... km` en el resumen y `ruta.csv`). Lo que todavía no
   viaja es la posición de **cada apunte suelto**: ningún repostaje, parada o
   avería guarda el lugar donde se hizo, solo el viaje en conjunto.
3. **Las monedas no se convierten.** Si el viaje cruza a Suiza, sumar euros con
   francos daría un total que no significa nada. El resumen **avisa** cuando ha
   habido más de una moneda y te manda a los CSV, que llevan cada uno la suya.
4. **No tiene reloj propio.** Sin haber visto a la P4 desde que arrancó no sabe
   qué hora es; en ese caso el apunte se marca como de **hora aproximada** y la
   P4 le pone la de recepción.
5. **Sí lleva acentos y ñ.** Desde el 16-oct-2026 las fuentes son las Inter de
   `main/fonts/fonts_es.h`, con el castellano completo: por la pantalla se lee
   "Área de pago", "CONFIGURACIÓN", "Kilómetros". Lo que **no** traen son los
   símbolos de moneda (€, £) ni un par de letras raras (ł, č), así que las monedas
   van con su código (EUR, GBP) y "zl"/"Kc" sin diacríticos. Antes (con las
   Montserrat del SDK) sí era verdad lo de los cuadraditos.

Lo que viene: la posición de cada apunte suelto (el recorrido y los kilómetros
del viaje ya están, ver §9).
