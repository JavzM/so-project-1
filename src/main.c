#include <stdio.h>

#include "config.h"
#include "logger.h"
#include "scheduler.h"
#include "simulation.h"
#include "types.h"

int main(int argc, char *argv[]) {
    SimConfig cfg;

    int rc = config_parse_args(argc, argv, &cfg);
    if (rc == 1) {
        /* se imprimió --help */
        return 0;
    }
    if (rc != 0) {
        return 1;
    }

    if (config_validate(&cfg) != 0) {
        return 1;
    }

    config_print(&cfg);

    if (logger_init(cfg.log_file, cfg.verbose) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el logger\n");
        return 1;
    }

    logger_event(-1, TRUCK_NEW,
                 "Terminal iniciada (algoritmo=%s, trucks=%d, docks=%d)",
                 schedule_algorithm_name(cfg.algorithm),
                 cfg.num_trucks, cfg.num_docks);

    Scheduler *sched = NULL;
    if (cfg.algorithm == ALGO_FIFO) {
        sched = scheduler_fifo_create(&cfg);
    } else if (cfg.algorithm == ALGO_RR) {
        sched = scheduler_rr_create(&cfg);
    } else {
        fprintf(stderr, "Error: algoritmo no soportado\n");
        logger_event(-1, TRUCK_TERMINATED, "Terminal abortada: algoritmo no soportado");
        logger_close();
        return 2;
    }
    if (sched == NULL) {
        fprintf(stderr, "Error: no se pudo crear el scheduler\n");
        logger_close();
        return 1;
    }

    int sim_rc = simulation_run(&cfg, sched);

    scheduler_destroy(sched);

    logger_event(-1, TRUCK_TERMINATED, "Terminal finalizada");
    logger_close();
    return sim_rc == 0 ? 0 : 1;
}
