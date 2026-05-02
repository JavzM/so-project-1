#define _POSIX_C_SOURCE 200809L

#include "metrics.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Estado interno del modulo (singleton).
 *
 * Los arreglos de timestamps son indexados por truck_id [0..num_trucks-1].
 * Cada hilo escribe exclusivamente en su propia ranura, por lo que no se
 * necesita mutex para ellos.
 * context_switches es atomic_int para que multiples hilos puedan incrementarlo
 * concurrentemente sin carrera.
 */
typedef struct {
    long long  *arrival_ts;       /* ts cuando el camion entro a READY */
    long long  *first_run_ts;     /* ts del primer ingreso a RUNNING    */
    long long  *terminated_ts;    /* ts cuando el camion llego a TERMINATED */
    int         num_trucks;
    atomic_int  context_switches; /* El atomic int no se ve afectado por los threads https://en.cppreference.com/c/language/atomic */
    bool        initialized;
} MetricsState;

static MetricsState g_metrics;

/* Timestamp monotónico en milisegundos. */
static long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("metrics: clock_gettime");
        return 0;
    }
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

int metrics_init(int num_trucks) {
    if (num_trucks <= 0) {
        fprintf(stderr, "metrics_init: num_trucks debe ser > 0\n");
        return -1;
    }

    g_metrics.arrival_ts    = (long long *)calloc((size_t)num_trucks, sizeof(long long));
    g_metrics.first_run_ts  = (long long *)calloc((size_t)num_trucks, sizeof(long long));
    g_metrics.terminated_ts = (long long *)calloc((size_t)num_trucks, sizeof(long long));

    if (!g_metrics.arrival_ts || !g_metrics.first_run_ts || !g_metrics.terminated_ts) {
        perror("metrics_init: calloc");
        free(g_metrics.arrival_ts);
        free(g_metrics.first_run_ts);
        free(g_metrics.terminated_ts);
        g_metrics.arrival_ts = g_metrics.first_run_ts = g_metrics.terminated_ts = NULL;
        return -1;
    }

    g_metrics.num_trucks = num_trucks;
    atomic_init(&g_metrics.context_switches, 0);
    g_metrics.initialized = true;
    return 0;
}

void metrics_record_arrival(int truck_id) {
    if (!g_metrics.initialized || truck_id < 0 || truck_id >= g_metrics.num_trucks) return;
    g_metrics.arrival_ts[truck_id] = now_ms();
}

void metrics_record_first_run(int truck_id) {
    if (!g_metrics.initialized || truck_id < 0 || truck_id >= g_metrics.num_trucks) return;
    g_metrics.first_run_ts[truck_id] = now_ms();
}

void metrics_record_termination(int truck_id) {
    if (!g_metrics.initialized || truck_id < 0 || truck_id >= g_metrics.num_trucks) return;
    g_metrics.terminated_ts[truck_id] = now_ms();
}

void metrics_increment_context_switches(void) {
    if (!g_metrics.initialized) return;
    atomic_fetch_add(&g_metrics.context_switches, 1);
}

void metrics_print_summary(const SimConfig *cfg, FILE *out) {
    if (!g_metrics.initialized || cfg == NULL || out == NULL) return;

    int    completed       = 0;
    double sum_wait        = 0.0;
    double sum_turnaround  = 0.0;

    for (int i = 0; i < g_metrics.num_trucks; i++) {
        if (g_metrics.terminated_ts[i] == 0) continue; /* no termino */
        completed++;

        /* Tiempo de espera: desde llegada a READY hasta primer RUNNING. */
        if (g_metrics.first_run_ts[i] > 0 && g_metrics.arrival_ts[i] > 0) {
            double wait = (double)(g_metrics.first_run_ts[i] - g_metrics.arrival_ts[i]);
            if (wait < 0.0) wait = 0.0;
            sum_wait += wait;
        }

        /* Turnaround: desde llegada a READY hasta TERMINATED. */
        if (g_metrics.arrival_ts[i] > 0) {
            double ta = (double)(g_metrics.terminated_ts[i] - g_metrics.arrival_ts[i]);
            if (ta < 0.0) ta = 0.0;
            sum_turnaround += ta;
        }
    }

    double avg_wait       = (completed > 0) ? (sum_wait       / completed) : 0.0;
    double avg_turnaround = (completed > 0) ? (sum_turnaround / completed) : 0.0;
    int    cs             = atomic_load(&g_metrics.context_switches);

    /* Quantum: leer del scheduler via cfg. Para FIFO es 0 → "N/A". */
    const char *algo_name  = schedule_algorithm_name(cfg->algorithm);
    const char *prio_str   = cfg->priority_enabled ? "on" : "off";

    fprintf(out, "\n===== Resumen de la simulacion =====\n");
    fprintf(out, "Algoritmo: %s (prioridad: %s)\n", algo_name, prio_str);

    if (cfg->algorithm == ALGO_RR) {
        fprintf(out, "Camiones: %d   Muelles: %d   Quantum: %dms\n",
                cfg->num_trucks, cfg->num_docks, cfg->quantum_ms);
    } else {
        fprintf(out, "Camiones: %d   Muelles: %d   Quantum: N/A\n",
                cfg->num_trucks, cfg->num_docks);
    }

    fprintf(out, "-------------------------------------\n");
    fprintf(out, "Tiempo espera promedio: %.2f ms\n", avg_wait);
    fprintf(out, "Turnaround promedio:    %.2f ms\n", avg_turnaround);
    fprintf(out, "Cambios de contexto:    %d\n",      cs);
    fprintf(out, "Camiones completados:   %d/%d\n",   completed, cfg->num_trucks);
    fprintf(out, "=====================================\n");
}

void metrics_destroy(void) {
    if (!g_metrics.initialized) return;
    free(g_metrics.arrival_ts);
    free(g_metrics.first_run_ts);
    free(g_metrics.terminated_ts);
    g_metrics.arrival_ts    = NULL;
    g_metrics.first_run_ts  = NULL;
    g_metrics.terminated_ts = NULL;
    g_metrics.num_trucks    = 0;
    g_metrics.initialized   = false;
}
