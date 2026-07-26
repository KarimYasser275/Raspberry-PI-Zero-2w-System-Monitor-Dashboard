#include "sys_monitor.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static int fd = 0;
static SysMonitorCtx_t *ctx = NULL;

static void gracefull_exit(int signo);

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
            case 'r': /*Runnable*/
                stat_snapshot = false;
                break;

            case 's': /*snapshot*/
                stat_snapshot = true;
                break;

            default:
                printf("Usage: -s for getting stat, -r for continuous display of system stats\n");
                return -1;
        }
    }
    fd = shm_open("/sys_monitor_shm", O_RDWR, 0);
    if(fd == -1)
    {
        perror("shm_open");
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
        printf("CPU Usage: %f\n", snap.cpu_load * 100);
        printf("RAM Usage: %f\n", memory_usage * 100);
        printf("CPU Temp : %f\n", snap.temp_celsius);
        printf("IP Addr  : %s\n", snap.ip_addr);

        fflush(stdout);
        sleep(1);
        if(stat_snapshot == true)
        {
            break;
        }
    }
    return 0;
}

static void gracefull_exit(int signo)
{
    (void)signo;
    printf("Terminating System monitor Runnable Application\n");
    munmap(ctx, sizeof(SysMonitorCtx_t));
    close(fd);
    exit(EXIT_SUCCESS);
}