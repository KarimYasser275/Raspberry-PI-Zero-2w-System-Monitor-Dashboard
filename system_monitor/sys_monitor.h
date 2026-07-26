/**
 * @file    sys_monitor.h
 * @brief   System telemetry harvester for Raspberry Pi Zero 2 W OLED dashboard.
 *
 * Provides a thread-safe interface for collecting CPU load, RAM usage,
 * SoC temperature, and network (wlan0) information from Linux procfs/sysfs.
 * The harvester runs in its own pthread and updates a shared SystemMetrics_t
 * struct protected by a mutex.
 *
 * @author  Karim
 * @date    2026-07-25
 */

#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

/*==========================================================================
 *  INCLUDES
 *=========================================================================*/
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/*==========================================================================
 *  CONSTANTS / CONFIGURATION
 *=========================================================================*/

/** Polling interval for the harvester thread (seconds). */
#define SYS_MON_POLL_INTERVAL_S 2

/** Maximum length for an IPv4 address string (e.g. "255.255.255.255\0"). */
#define SYS_MON_IP_STR_LEN 16

/** Network interface to monitor. */
#define SYS_MON_IFACE "wlan0"

/** Name of the POSIX shared memory object. */
#define SYS_MON_SHM_NAME "/sys_monitor_shm"

/* ---- procfs / sysfs paths ---- */
#define SYS_MON_STAT_PATH "/proc/stat"
#define SYS_MON_MEMINFO_PATH "/proc/meminfo"
#define SYS_MON_THERMAL_PATH "/sys/class/thermal/thermal_zone0/temp"

/*==========================================================================
 *  DATA TYPES
 *=========================================================================*/

/**
 * @brief Snapshot of the system metrics collected by the harvester.
 *
 * All fields are written by the harvester thread and read by consumer apps.
 * Access MUST be guarded by the accompanying mutex.
 */
typedef struct {
  float cpu_load;       /**< CPU utilization ratio (0.0 to 1.0) from /proc/stat  */
  uint32_t mem_total_kb;     /**< Total RAM in KiB (from /proc/meminfo)     */
  uint32_t mem_available_kb; /**< Available RAM in KiB                        */
  float temp_celsius;        /**< SoC temperature in degrees Celsius         */
  char ip_addr[SYS_MON_IP_STR_LEN]; /**< IPv4 address string for wlan0 */
} SystemMetrics_t;

/**
 * @brief Handle that bundles metrics data with its synchronization primitive in shared memory.
 *
 * Created by sys_monitor_init() in shared memory and mapped by consumer processes.
 */
typedef struct {
  SystemMetrics_t metrics; /**< Latest metric snapshot                    */
  pthread_mutex_t lock;    /**< Process-shared mutex protecting @c metrics */
} SysMonitorCtx_t;

/*==========================================================================
 *  PUBLIC API
 *=========================================================================*/

/**
 * @brief  Initialise and map the system-monitor shared memory context.
 *
 * Creates POSIX shared memory, sets its size, maps it, and initialises
 * the process-shared mutex.
 *
 * @return  Pointer to the mapped SysMonitorCtx_t on success, NULL on failure.
 */
SysMonitorCtx_t *sys_monitor_init(void);
/**
 * @brief  Start the harvester thread.
 *
 * Spawns a detached or joinable pthread that periodically reads
 * CPU, RAM, temperature, and network data, then writes the results
 * into @c ctx->metrics under the mutex.
 *
 * @param[in,out] ctx  Initialized context (from sys_monitor_init()).
 * @return  0 on success, -1 on pthread_create failure.
 */
int sys_monitor_start(SysMonitorCtx_t *ctx);

/**
 * @brief  Stop the harvester thread gracefully.
 *
 * Sets the running flag to @c false, joins the thread, and
 * destroys the mutex.
 *
 * @param[in,out] ctx  Running context.
 */
void sys_monitor_stop(SysMonitorCtx_t *ctx);

/**
 * @brief  Copy the latest metrics into a caller-supplied buffer.
 *
 * Thread-safe: acquires the mutex, performs a shallow copy,
 * and releases the mutex.
 *
 * @param[in]  ctx  Running or stopped context.
 * @param[out] out  Destination for the metric snapshot.
 */
void sys_monitor_get_metrics(SysMonitorCtx_t *ctx, SystemMetrics_t *out);

#endif /* SYS_MONITOR_H */
