#ifndef METRICS_H
#define METRICS_H

#include <stdio.h>
#include "types.h"

/*
 * Modulo de metricas — singleton.
 *
 * Registra timestamps en cada transicion de estado
 * y calcula promedios al final. El acceso a los arreglos por
 * camion es thread-safe sin mutex porque cada hilo escribe unicamente a su
 * propio indice (truck_id).
 *
 * Ciclo de uso esperado (desde simulation_run):
 *   metrics_init(num_trucks);
 *   // ... lanzar hilos, correr simulacion ...
 *   // truck.c llama record_* e increment_context_switches durante la corrida
 *   metrics_print_summary(cfg, stdout);
 *   metrics_destroy();
 *
 * Definiciones operativas:
 *   Tiempo de espera = ts_first_run - ts_arrival   (espera inicial en READY)
 *   Turnaround = ts_terminated - ts_arrival
 *   Cambios de contexto = transiciones RUNNING→READY provocadas por quantum
 */

/*
 * Inicializa el modulo reservando memoria para num_trucks camiones.
 * Retorna 0 en exito, -1 si falla malloc.
 * Pre: llamar antes de pthread_create.
 */
int metrics_init(int num_trucks);

/*
 * Registra timestamp de llegada a READY (justo cuando el camion se encola).
 * Thread-safe: cada truck_id escribe a su propio slot.
 */
void metrics_record_arrival(int truck_id);

/*
 * Registra timestamp del primer ingreso a RUNNING.
 * Llamar solo en la primera entrada al muelle; no en re-entradas por RR.
 * Thread-safe: cada truck_id escribe a su propio slot.
 */
void metrics_record_first_run(int truck_id);

/*
 * Registra timestamp de TERMINATED.
 * Thread-safe: cada truck_id escribe a su propio slot.
 */
void metrics_record_termination(int truck_id);

/*
 * Incrementa el contador global de cambios de contexto (transicion RUNNING→READY
 * causada por expiracion de quantum en RR). Atomic, no requiere mutex externo.
 */
void metrics_increment_context_switches(void);

/*
 * Imprime la tabla resumen al stream out.
 * Pre: llamar solo tras pthread_join de todos los hilos.
 * No es thread-safe (llamar desde un unico hilo al final).
 */
void metrics_print_summary(const SimConfig *cfg, FILE *out);

/*
 * Libera la memoria interna. Despues de esta llamada, el modulo no puede
 * usarse hasta un nuevo metrics_init.
 */
void metrics_destroy(void);

#endif /* METRICS_H */
