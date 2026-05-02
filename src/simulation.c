#define _POSIX_C_SOURCE 200809L

#include "simulation.h"
#include "truck.h"
#include "logger.h"
#include "metrics.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Simula una corrida completa.
 *
 * Las llegadas se escalonan con nanosleep(rand() % arrival_max_ms). Esto NO
 * es sincronizacion: solo dispersa los pthread_create en el tiempo para
 * que el orden FIFO sea observable en el log.
 */

static void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);
}

int simulation_run(const SimConfig *cfg, Scheduler *sched) {
    if (cfg == NULL || sched == NULL) {
        fprintf(stderr, "simulation_run: parametros invalidos\n");
        return -1;
    }

    if (metrics_init(cfg->num_trucks) != 0) {
        fprintf(stderr, "simulation_run: no se pudo inicializar metrics\n");
        return -1;
    }

    SimContext ctx;
    ctx.sched = sched;
    ctx.cfg   = cfg;

    if (dock_init(&ctx.dock, cfg->num_docks) != 0) {
        metrics_destroy();
        return -1;
    }

    /* La semilla viene del CLI; srand aqui asegura reproducibilidad. */
    srand(cfg->seed);

    Truck *trucks = (Truck *)calloc((size_t)cfg->num_trucks, sizeof(Truck));
    if (trucks == NULL) {
        perror("simulation_run: calloc trucks");
        dock_destroy(&ctx.dock);
        metrics_destroy();
        return -1;
    }

    for (int i = 0; i < cfg->num_trucks; i++) {
        truck_init_random(&trucks[i], i, cfg);
        trucks[i].sim_ctx = &ctx;
    }

    logger_event(-1, TRUCK_NEW,
                 "Simulacion arranca: algoritmo=%s trucks=%d docks=%d quantum=%dms",
                 sched->name, cfg->num_trucks, cfg->num_docks, cfg->quantum_ms);

    int created   = 0;
    int rc_global = 0;
    for (int i = 0; i < cfg->num_trucks; i++) {
        int rc = pthread_create(&trucks[i].thread, NULL, truck_run, &trucks[i]);
        if (rc != 0) {
            fprintf(stderr, "simulation_run: pthread_create truck %d: %s\n",
                    i, strerror(rc));
            rc_global = -1;
            break;
        }
        created++;

        /* Llegada escalonada (modelado de llegadas, no sincronizacion). */
        if (cfg->arrival_max_ms > 0) {
            sleep_ms(rand() % cfg->arrival_max_ms);
        }
    }

    /* Join solo de los hilos que arrancaron para no dejar zombies. */
    int joined_ok = 0;
    for (int i = 0; i < created; i++) {
        int rc = pthread_join(trucks[i].thread, NULL);
        if (rc != 0) {
            fprintf(stderr, "simulation_run: pthread_join truck %d: %s\n",
                    i, strerror(rc));
            rc_global = -1;
        } else {
            joined_ok++;
        }
    }

    logger_event(-1, TRUCK_TERMINATED,
                 "Simulacion finaliza: completados=%d/%d",
                 joined_ok, cfg->num_trucks);

    /* Tabla de resultados — delega en metrics para calculo y formato. */
    metrics_print_summary(cfg, stdout);

    free(trucks);
    dock_destroy(&ctx.dock);
    metrics_destroy();

    if (rc_global != 0) return -1;
    return (joined_ok == cfg->num_trucks) ? 0 : -1;
}
