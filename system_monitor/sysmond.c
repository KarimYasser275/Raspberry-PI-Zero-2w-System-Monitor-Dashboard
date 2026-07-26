#include "sys_monitor.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

SysMonitorCtx_t *g_sys_mon = NULL;

static void gracefull_exit(int signo);

int main(void)
{
    g_sys_mon = sys_monitor_init();
    sys_monitor_start(g_sys_mon);
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