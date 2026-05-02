/*
 * Logger thread-safe para eventos de la simulacion.
 *
 * Singleton: existe exactamente un archivo de log cada vez que el proyecto corre.
 * Pasar el file pointer como parametro a cada funcion que loguea no tiene sentido.
 *
 * Formato de linea: [timestamp_ms] [truck=ID|sys] [STATE] mensaje. Si truck=sys, es evento de sistema (i. e. inicio del sistema)
 * 
 * El timestamp es tomado al momento del evento.
 *
 * Siempre se adquiere primero el semaforo de muelles y despues el mutex del log. 
 * El logger nunca adquiere otros recursos mientras mantiene su mutex.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include "types.h"

/*
 * Inicializa el singleton. Abre el archivo en modo append o lo crea si no existe.
 *
 * Parametros:
 *   path    -- ruta del archivo de logs (not NULL).
 *   verbose -- si true, cada evento tambien se escribe a stdout.
 *
 * Retorna: 0 en exito, -1 si falla fopen (imprime perror en ese caso).
 *
 * Pre:  no llamar dos veces sin un logger_close intermedio.
 * Post: logger_event esta listo para usarse desde cualquier hilo.
 * 
 * Se invoca una sola vez en main para que no tenga concurrencia.
 */
int logger_init(const char *path, bool verbose);

/*
 * Escribe una linea en el archivo de logs.

 * Si truck_id es -1, se imprime [truck=sys]
 *
 * Parametros:
 *   truck_id -- identificador del camion, o -1.
 *   state    -- estado actual del camion (o TRUCK_NEW si no aplica).
 *   fmt, ... -- mensaje estilo printf.
 * 
 * OJO: No enviar un string con formato sin sus respectivos valores o esto falla
 *
 * Pre:  logger_init exitoso previamente.
 * Maneja concurrencia con un mutex interno.
 */
void logger_event(int truck_id, TruckState state, const char *fmt, ...);

/*
 * Cierra el archivo de log y destruye el mutex interno.
 *
 * Pre:  logger_init fue llamado.
 * Post: logger_event no debe llamarse despues.
 * 
 * No se debe llamar concurrentemente con logger_event.
 */
void logger_close(void);

#endif /* LOGGER_H */
