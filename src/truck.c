#define _POSIX_C_SOURCE 200809L

#include "truck.h"
#include "simulation.h"
#include "logger.h"
#include "dock.h"
#include "scheduler.h"
#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * Ciclo de vida del camion.
 *
 * Orden global de recursos (prevencion de deadlock):
 *   sem_wait(dock)   nivel 1  — adquirir muelle
 *   logger_event     nivel 2  — toma y libera log_mutex internamente
 *   sem_post(dock)   libera nivel 1
 *
 * Para RR, en cada expiracion de quantum:
 *   logger_event("quantum expirado")  — todavia retenemos el dock
 *   metrics_increment_context_switches()
 *   sem_post(dock)                    — liberar muelle
 *   enqueue(sched, t)                 — re-encolar
 *   sem_wait(dock)                    — esperar nuevo muelle
 * Nunca se llama al logger mientras se retiene el queue_mutex.
 */

/* Duerme exactamente ms milisegundos usando nanosleep */
static void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);
}

/* Timestamp monotónico en milisegundos */
static long long monotonic_ms_local(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("truck: clock_gettime");
        return 0;
    }
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static const char *cargo_name(CargoType c) {
    return c == CARGO_PERISHABLE ? "PERISHABLE" : "GENERAL";
}

/*
 * Agrega a la cola a t en el scheduler y, si priority_enabled y el camion es perecedero,
 * loguea "priority boost". Se llama DESPUES de que enqueue libera el
 * queue_mutex internamente, por lo que no hay riesgo de deadlock con el log.
 */
static void truck_enqueue(SimContext *ctx, Truck *t) {
    ctx->sched->enqueue(ctx->sched, t);
    if (ctx->cfg->priority_enabled && t->cargo == CARGO_PERISHABLE) {
        logger_event(t->id, TRUCK_READY, "priority boost: perecedero adelantado en cola");
    }
}

void truck_init_random(Truck *t, int id, const SimConfig *cfg) {
    if (t == NULL || cfg == NULL) return;
    t->id = id;
    int span = cfg->burst_max_ms - cfg->burst_min_ms;
    if (span < 0) span = 0;
    t->burst_ms     = cfg->burst_min_ms + (span > 0 ? (rand() % (span + 1)) : 0);
    t->remaining_ms = t->burst_ms;
    double r        = (double)rand() / (double)RAND_MAX;
    t->cargo        = (r < cfg->perishable_ratio) ? CARGO_PERISHABLE : CARGO_GENERAL;
    t->priority     = (t->cargo == CARGO_PERISHABLE) ? 1 : 0;
    t->state        = TRUCK_NEW;
    t->ts_arrival = t->ts_first_run = t->ts_terminated = 0;
    t->sim_ctx      = NULL;
}

void *truck_run(void *arg) {
    Truck *t = (Truck *)arg;
    if (t == NULL || t->sim_ctx == NULL) return NULL;
    SimContext *ctx = (SimContext *)t->sim_ctx;

    /* ── NEW ─────────────────────────────────────────────────────────────── */
    t->state = TRUCK_NEW;
    logger_event(t->id, TRUCK_NEW, "creado burst=%dms cargo=%s",
                 t->burst_ms, cargo_name(t->cargo));

    /* ── READY: entra a la cola de listos ───────────────────────────────── */
    t->ts_arrival = monotonic_ms_local();
    metrics_record_arrival(t->id);          /* punto de medicion: llegada */
    t->state = TRUCK_READY;
    if (ctx->sched != NULL && ctx->sched->enqueue != NULL) {
        truck_enqueue(ctx, t);
    }
    logger_event(t->id, TRUCK_READY, "en cola de listos");

    /* ── BLOCKED: bloqueado esperando muelle ────────────────────────────── */
    t->state = TRUCK_BLOCKED;
    logger_event(t->id, TRUCK_BLOCKED, "esperando muelle");

    if (dock_acquire(&ctx->dock) != 0) {
        logger_event(t->id, TRUCK_TERMINATED, "fallo al adquirir muelle");
        t->state = TRUCK_TERMINATED;
        return NULL;
    }

    /* ── RUNNING: primer ingreso al muelle ──────────────────────────────── */
    t->ts_first_run = monotonic_ms_local();
    metrics_record_first_run(t->id);        /* punto de medicion: primer run */
    t->state = TRUCK_RUNNING;
    logger_event(t->id, TRUCK_RUNNING, "ocupando muelle, burst=%dms", t->burst_ms);

    int q = ctx->sched->quantum_ms(ctx->sched);

    if (q <= 0) {
        /* ── FIFO: una sola rodaja hasta completar el burst ─────────────── */
        sleep_ms(t->remaining_ms);
        t->remaining_ms = 0;
    } else {
        /* ── RR: loop de trozos de quantum ──────────────────────────────── */
        while (t->remaining_ms > 0) {
            int slice = (t->remaining_ms < q) ? t->remaining_ms : q;
            sleep_ms(slice);
            t->remaining_ms -= slice;

            if (t->remaining_ms > 0) {
                /*
                 * Quantum expirado y aun hay trabajo.
                 * Orden: loguear (reteniendo dock) → incrementar contador →
                 *        sem_post → re-encolar → sem_wait.
                 */
                logger_event(t->id, TRUCK_RUNNING,
                             "quantum expirado, remaining=%dms", t->remaining_ms);
                metrics_increment_context_switches();

                if (dock_release(&ctx->dock) != 0) {
                    logger_event(t->id, TRUCK_TERMINATED,
                                 "fallo al liberar muelle (preemption)");
                    t->state = TRUCK_TERMINATED;
                    return NULL;
                }

                t->state = TRUCK_READY;
                truck_enqueue(ctx, t);
                logger_event(t->id, TRUCK_READY, "re-encolado tras quantum");

                t->state = TRUCK_BLOCKED;
                logger_event(t->id, TRUCK_BLOCKED, "esperando muelle (re-encola)");

                if (dock_acquire(&ctx->dock) != 0) {
                    logger_event(t->id, TRUCK_TERMINATED,
                                 "fallo al adquirir muelle (re-encola)");
                    t->state = TRUCK_TERMINATED;
                    return NULL;
                }

                t->state = TRUCK_RUNNING;
                logger_event(t->id, TRUCK_RUNNING,
                             "reanudando muelle, remaining=%dms", t->remaining_ms);
            }
        }
    }

    /*
     * ── TERMINATED ───────────────────────────────────────────────────────
     * Se loguea ANTES de soltar el muelle para evitar que otro camion
     * entre a RUNNING y se registre antes que nuestro TERMINATED.
     */
    t->ts_terminated = monotonic_ms_local();
    metrics_record_termination(t->id);      /* punto de medicion: fin */
    t->state = TRUCK_TERMINATED;
    logger_event(t->id, TRUCK_TERMINATED,
                 "carga completada turnaround=%lldms",
                 t->ts_terminated - t->ts_arrival);

    if (dock_release(&ctx->dock) != 0) {
        logger_event(t->id, TRUCK_TERMINATED, "fallo al liberar muelle");
    }
    return NULL;
}
