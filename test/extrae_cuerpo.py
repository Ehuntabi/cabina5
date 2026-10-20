"""Saca el cuerpo de una funcion C de un fuente, para los arneses de los tests.

Contar llaves a pelo no vale: p4_api_cuerpo_fin() arma el JSON a trozos y lleva
llaves DENTRO de las cadenas de formato ("{\"op\":\"fin\"...), asi que el
contador se va hasta el final del fichero y el arnes se queda sin funcion
(paso el 18-oct-2026 al partir el snprintf en dos). Aqui se saltan cadenas,
caracteres y comentarios, que es donde no puede haber llaves de bloque.
"""


def desde(fuente, m):
    """La funcion entera, de la firma a la llave que la cierra.

    'm' es el match de la firma; tiene que acabar en la llave de apertura.
    """
    i = m.start()
    k = fuente.index('{', m.end() - 1)
    prof = 0
    while k < len(fuente):
        c = fuente[k]
        if c == '"' or c == "'":
            k += 1
            while k < len(fuente) and fuente[k] != c:
                k += 2 if fuente[k] == '\\' else 1
        elif fuente.startswith('/*', k):
            fin = fuente.find('*/', k + 2)
            if fin < 0:
                break
            k = fin + 1
        elif fuente.startswith('//', k):
            fin = fuente.find('\n', k)
            if fin < 0:
                break
            k = fin
        elif c == '{':
            prof += 1
        elif c == '}':
            prof -= 1
            if prof == 0:
                return fuente[i:k + 1]
        k += 1
    raise SystemExit('no encuentro el final de la funcion en el fuente')
