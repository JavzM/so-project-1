#ifndef TRUCK_H
#define TRUCK_H

#include "types.h"

/*
 * Funcion de hilo del camion y helper de generacion aleatoria.
 *
 * El argumento de pthread_create es Truck*. Antes de lanzar el hilo, el
 * simulador (simulation.c) debe asignar truck->sim_ctx con un puntero
 * al SimContext compartido (Dock + Scheduler).
 */

/*
 * Funcion de hilo POSIX. Controla el ciclo de estados:
 *   NEW -> READY -> BLOCKED -> RUNNING -> TERMINATED
 *
 * Cada transicion se registra con timestamp en el log.
 *
 * Orden de recursos: el camion adquiere primero el dock (sem_wait)
 * y solo despues invoca logger_event.
 */
void *truck_run(void *arg);

/*
 * Inicializa los campos de un camion con valores aleatorios coherentes
 * con la configuracion.
 *
 * Pre: srand ya fue llamado.
 */
void truck_init_random(Truck *t, int id, const SimConfig *cfg);

#endif /* TRUCK_H */
