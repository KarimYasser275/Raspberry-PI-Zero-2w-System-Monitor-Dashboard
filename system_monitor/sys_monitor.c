/**
 * @file    sys_monitor.c
 * @brief   System telemetry harvester implementation.
 *
 * Periodically reads CPU load, RAM usage, SoC temperature, and
 * the wlan0 IPv4 address from Linux procfs/sysfs and stores the
 * results in a mutex-protected SystemMetrics_t struct.
 */

/*==========================================================================
 *  INCLUDES
 *=========================================================================*/
#include "sys_monitor.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*==========================================================================
 *  GLOBAL VARIABLES
 *=========================================================================*/
pthread_t thread;             /**< Harvester thread handle                   */
volatile bool thread_running; /**< Flag to signal thread termination         */
/*==========================================================================
 *  PRIVATE HELPERS
 *=========================================================================*/

/**
 * @brief  Read 1-minute load average from /proc/loadavg.
 * @return Load average on success, -1.0f on failure.
 */
static float read_cpu_load(void)
{
    FILE *fp = fopen(SYS_MON_STAT_PATH, "r");
    if(fp == NULL)
    {
        return -1.0f;
    }

    /*
     * First line of /proc/stat (aggregate across all cores):
     *   cpu  user nice system idle iowait irq softirq steal guest guest_nice
     *
     * All values are in "jiffies" (clock ticks).
     * CPU usage % = 1 - (Δidle / Δtotal)  between two samples.
     */
    uint32_t vals[10];
    char line[256];

    if(fgets(line, sizeof(line), fp) == NULL)
    {
        fclose(fp);
        return -1.0f;
    }
    fclose(fp);

    int matched = sscanf(line, "cpu  %u %u %u %u %u %u %u %u %u %u", &vals[0], &vals[1], &vals[2],
                         &vals[3], &vals[4], &vals[5], &vals[6], &vals[7], &vals[8], &vals[9]);
    if(matched < 4)
    {
        return -1.0f;
    }

    uint32_t idle = vals[3];
    static uint32_t prev_idle = 0, prev_total = 0;
    uint32_t total = 0;
    for(int i = 0; i < matched; i++)
    {
        total += vals[i];
    }

    /* Compute delta since the last sample */
    uint32_t d_total = total - prev_total;
    uint32_t d_idle = idle - prev_idle;

    prev_idle = idle;
    prev_total = total;

    /* First call has no previous sample → return 0 */
    if(d_total == 0)
    {
        return 0.0f;
    }
    return 1.0f - ((float)d_idle / (float)d_total);
}

/**
 * @brief  Read MemTotal and MemAvailable from /proc/meminfo.
 *
 * @param[out] total_kb      Total RAM in KiB.
 * @param[out] available_kb  Available RAM in KiB.
 * @return 0 on success, -1 on failure.
 */
static int read_mem_info(uint32_t *total_kb, uint32_t *available_kb)
{
    FILE *fp = fopen(SYS_MON_MEMINFO_PATH, "r");
    if(fp == NULL)
    {
        return -1;
    }

    /*
     * /proc/meminfo format (first few lines):
     *   MemTotal:         469536 kB
     *   MemFree:           12345 kB
     *   MemAvailable:     234567 kB
     *   ...
     */
    char line[128];
    unsigned long val;
    bool got_total = false;
    bool got_available = false;

    while(fgets(line, sizeof(line), fp) != NULL)
    {
        if(sscanf(line, "MemTotal: %lu kB", &val) == 1)
        {
            *total_kb = (uint32_t)val;
            got_total = true;
        }
        else if(sscanf(line, "MemAvailable: %lu kB", &val) == 1)
        {
            *available_kb = (uint32_t)val;
            got_available = true;
        }

        if(got_total && got_available)
        {
            break; /* no need to read the rest of the file */
        }
    }

    fclose(fp);
    return (got_total && got_available) ? 0 : -1;
}

/**
 * @brief  Read the SoC temperature from the thermal zone.
 * @return Temperature in °C on success, -1.0f on failure.
 */
static float read_temperature(void)
{
    FILE *fp = fopen(SYS_MON_THERMAL_PATH, "r");
    if(fp == NULL)
    {
        return -1.0f;
    }

    char buf[16];
    if(fgets(buf, sizeof(buf), fp) == NULL)
    {
        fclose(fp);
        return -1.0f;
    }
    fclose(fp);

    /* Kernel reports millidegrees (e.g. 42300 → 42.3 °C) */
    long millideg = strtol(buf, NULL, 10);
    return (float)millideg / 1000.0f;
}

/**
 * @brief  Retrieve the IPv4 address of SYS_MON_IFACE (wlan0).
 *
 * @param[out] buf   Destination buffer (at least SYS_MON_IP_STR_LEN bytes).
 * @param[in]  len   Size of @p buf.
 * @return 0 on success, -1 if the interface has no IPv4 address.
 */
static int read_ip_address(char *buf, size_t len)
{
    int8_t retval = -1;
    struct ifaddrs *ifaddrs, *ifa;

    if(getifaddrs(&ifaddrs) == -1)
    {
        return -1;
    }

    for(ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next)
    {
        if(ifa->ifa_addr == NULL)
        {
            continue;
        }

        if((strcmp(ifa->ifa_name, SYS_MON_IFACE) == 0) && (ifa->ifa_addr->sa_family == AF_INET))
        {

            struct sockaddr_in *ip_addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &ip_addr->sin_addr, buf, (socklen_t)len);
            retval = 0;

            break;
        }
    }

    freeifaddrs(ifaddrs);
    return retval;
}

/*==========================================================================
 *  HARVESTER THREAD
 *=========================================================================*/

/**
 * @brief  Pthread entry point — polls metrics in a loop.
 *
 * Runs until thread_runningis set to false, sleeping
 * SYS_MON_POLL_INTERVAL_S seconds between iterations.
 */
static void *harvester_thread(void *arg)
{
    SysMonitorCtx_t *ctx = (SysMonitorCtx_t *)arg;

    while(thread_running)
    {

        /* --- Collect metrics into a local snapshot --- */
        SystemMetrics_t snap;
        memset(&snap, 0, sizeof(snap));

        snap.cpu_load = read_cpu_load();
        read_mem_info(&snap.mem_total_kb, &snap.mem_available_kb);
        snap.temp_celsius = read_temperature();
        read_ip_address(snap.ip_addr, sizeof(snap.ip_addr));

        /* --- Publish under the lock --- */
        pthread_mutex_lock(&ctx->lock);
        ctx->metrics = snap;
        pthread_mutex_unlock(&ctx->lock);

        sleep(SYS_MON_POLL_INTERVAL_S);
    }

    return NULL;
}

/*==========================================================================
 *  PUBLIC API
 *=========================================================================*/

/**
 * @brief  Initialise the system-monitor context.
 *
 * Zeroes out the metrics struct, initialises the mutex, and sets
 * the running flag to @c false.  Must be called before any other
 * sys_monitor_*() function.
 *
 * @param[out] ctx  Pointer to an uninitialised SysMonitorCtx_t.
 * @return  0 on success, -1 on failure (NULL pointer or mutex init error).
 */
SysMonitorCtx_t *sys_monitor_init(void)
{
    SysMonitorCtx_t *ctx = NULL;
    int fd = 0;

    fd = shm_open("/sys_monitor_shm", O_CREAT | O_RDWR, 0666);
    if(fd == -1)
    {
        perror("shm_open");
        return NULL;
    }
    if(ftruncate(fd, sizeof(SysMonitorCtx_t)) == -1)
    {
        perror("ftruncate");
        close(fd);
        shm_unlink("/sys_monitor_shm");
        return NULL;
    }
    ctx = mmap(NULL, sizeof(SysMonitorCtx_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(ctx == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        shm_unlink("/sys_monitor_shm");
        return NULL;
    }
    close(fd);
    thread_running = false;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&ctx->lock, &attr); // mutex lives IN shared memory
    pthread_mutexattr_destroy(&attr);

    return ctx;
}

/**
 * @brief  Start the harvester thread.
 *
 * Spawns a joinable pthread that periodically reads CPU, RAM,
 * temperature, and network data from procfs/sysfs and publishes
 * the results into @c ctx->metrics under the mutex.
 *
 * @pre   sys_monitor_init() has been called on @p ctx.
 *
 * @param[in,out] ctx  Initialised context.
 * @return  0 on success, -1 on pthread_create failure.
 */
int sys_monitor_start(SysMonitorCtx_t *ctx)
{
    if(ctx == NULL)
    {
        return -1;
    }

    thread_running = true;

    if(pthread_create(&thread, NULL, harvester_thread, ctx) != 0)
    {
        perror("sys_monitor: pthread_create");
        thread_running = false;
        return -1;
    }

    return 0;
}

/**
 * @brief  Stop the harvester thread gracefully.
 *
 * Clears the running flag, joins the harvester thread, and
 * destroys the mutex.  After this call the context must not
 * be used unless re-initialised with sys_monitor_init().
 *
 * @param[in,out] ctx  Running context (from sys_monitor_start()).
 */
void sys_monitor_stop(SysMonitorCtx_t *ctx)
{
    if(ctx == NULL)
    {
        return;
    }

    thread_running = false;
    pthread_join(thread, NULL);
    pthread_mutex_destroy(&ctx->lock);
    munmap(ctx, sizeof(SysMonitorCtx_t)); // also missing!
    shm_unlink("/sys_monitor_shm");
}

/**
 * @brief  Copy the latest metrics into a caller-supplied buffer.
 *
 * Thread-safe: acquires the mutex, performs a shallow copy of
 * the current SystemMetrics_t snapshot, and releases the mutex.
 *
 * @param[in]  ctx  Running or stopped context.
 * @param[out] out  Destination for the metric snapshot.
 */
void sys_monitor_get_metrics(SysMonitorCtx_t *ctx, SystemMetrics_t *out)
{
    if(ctx == NULL || out == NULL)
    {
        return;
    }

    pthread_mutex_lock(&ctx->lock);
    *out = ctx->metrics;
    pthread_mutex_unlock(&ctx->lock);
}
