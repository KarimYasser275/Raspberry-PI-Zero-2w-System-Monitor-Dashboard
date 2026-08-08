/**
 * @file    sysmond.c
 * @brief   System monitor harvester daemon main entry point.
 *
 * Runs as a background service, initializing POSIX shared memory,
 * spawning the telemetry harvester thread, and handling SIGINT/SIGTERM
 * signals for graceful shutdown.
 *
 * @author  Karim
 * @date    2026-07-26
 */

#include "sys_monitor.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/** Global handle for the mapped shared memory context. */
SysMonitorCtx_t *g_sys_mon = NULL;

/**
 * @brief Signal handler for graceful daemon shutdown (SIGINT / SIGTERM).
 * @param[in] signo Signal number received.
 */
static void gracefull_exit(int signo);

/**
 * @brief  Daemon main function.
 *
 * Initialises shared memory, starts harvester thread, registers signal handlers,
 * and blocks on pause() awaiting termination signals.
 *
 * @return 0 on exit.
 */
int main(void)
{
    pid_t pid = fork();
    if(pid == -1)
    {
        // Error
        fprintf(stderr, "Failed to fork");
        return EXIT_FAILURE;
    }
    else if(pid > 0)
    {
        // Parent process
        return EXIT_SUCCESS;
    }

    setsid();
    g_sys_mon = sys_monitor_init();
    if(g_sys_mon == NULL)
    {
        fprintf(stderr, "Failed to initialize sys_monitor daemon\n");
        return EXIT_FAILURE;
    }

    if(sys_monitor_start(g_sys_mon) != 0)
    {
        fprintf(stderr, "Failed to start harvester thread\n");
        return EXIT_FAILURE;
    }

    (void)signal(SIGINT, gracefull_exit);
    (void)signal(SIGTERM, gracefull_exit);

    pause();
    return 0;
}

static void gracefull_exit(int signo)
{
    (void)signo;
    printf("Terminating System monitor daemon\n");
    sys_monitor_stop(g_sys_mon);
    exit(EXIT_SUCCESS);
}