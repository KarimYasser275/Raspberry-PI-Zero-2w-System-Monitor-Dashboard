/**
 * @file    sys_monitor-CLI.c
 * @brief   CLI consumer application for Raspberry Pi Zero 2W system monitor.
 *
 * Connects to POSIX shared memory (/sys_monitor_shm) created by sysmond,
 * reads metrics safely under a process-shared mutex, and renders status to terminal stdout.
 *
 * Supports snapshot display (-s) and continuous in-place terminal streaming (-r).
 *
 * @author  Karim
 * @date    2026-07-26
 */

#include "sys_monitor.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

/** File descriptor for opened POSIX shared memory. */
static int fd = 0;

/** Pointer to mapped shared memory region. */
static SysMonitorCtx_t *ctx = NULL;

/**
 * @brief Signal handler for SIGINT / SIGTERM to unmap shm before exit.
 * @param[in] signo Signal number received.
 */
static void gracefull_exit(int signo);

/**
 * @brief CLI application entry point.
 *
 * Parses command-line flags (-s for snapshot, -r for continuous stream),
 * attaches to POSIX shared memory, reads metrics snapshot safely under lock,
 * and prints formatted data to terminal.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Argument vector.
 * @return 0 on success, -1 on failure.
 */
int main(int argc, char *argv[])
{
    int opt = 0;
    static bool stat_snapshot = false;

    (void)signal(SIGINT, gracefull_exit);
    (void)signal(SIGTERM, gracefull_exit);

    while((opt = getopt(argc, argv, "rs")) != -1)
    {
        switch(opt)
        {
            case 'r': /* Runnable / continuous stream */
                stat_snapshot = false;
                break;

            case 's': /* Snapshot / single read */
                stat_snapshot = true;
                break;

            default:
                printf("Usage: %s [-s snapshot] [-r continuous]\n", argv[0]);
                return -1;
        }
    }

    fd = shm_open(SYS_MON_SHM_NAME, O_RDWR, 0);
    if(fd == -1)
    {
        perror("shm_open (sysmond daemon running?)");
        return -1;
    }

    ctx = mmap(NULL, sizeof(SysMonitorCtx_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(ctx == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return -1;
    }

    close(fd);
    float memory_usage = 0;
    SystemMetrics_t snap;

    while(1)
    {
        pthread_mutex_lock(&ctx->lock);
        snap = ctx->metrics;
        pthread_mutex_unlock(&ctx->lock);

        if(snap.mem_total_kb > 0)
        {
            memory_usage = 1.0f - ((float)snap.mem_available_kb / (float)snap.mem_total_kb);
        }

        /* Clear screen and move cursor to top-left (home) position */
        printf("\033[H\033[J");
        printf("=== SYSTEM MONITOR ===\n");
        printf("CPU Usage: %.2f%%\n", snap.cpu_load * 100);
        printf("RAM Usage: %.2f%%\n", memory_usage * 100);
        printf("CPU Temp : %.2f °C\n", snap.temp_celsius);
        printf("IP Addr  : %s\n", snap.ip_addr);

        fflush(stdout);
        sleep(1);

        if(stat_snapshot == true)
        {
            break;
        }
    }

    munmap(ctx, sizeof(SysMonitorCtx_t));
    return 0;
}

static void gracefull_exit(int signo)
{
    (void)signo;
    printf("\nTerminating System monitor Runnable Application\n");
    if(ctx != NULL && ctx != MAP_FAILED)
    {
        munmap(ctx, sizeof(SysMonitorCtx_t));
    }
    exit(EXIT_SUCCESS);
}