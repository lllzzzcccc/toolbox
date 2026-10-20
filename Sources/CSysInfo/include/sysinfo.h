#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdint.h>

/* 注意：本库的采样函数依赖静态缓存（上次采样值），非线程安全，
 * 须在单一线程调用（当前所有调用均来自主线程的 @MainActor Timer）。 */

/* CPU 使用率（0-100）。首次调用返回 0（需两次采样算增量）。 */
double sysinfo_cpu_usage(void);

/* 最近一次采样的细分：用户（含 nice）/ 系统 / 闲置，各 0-100。
 * 读取缓存值，不触发新采样；由 sysinfo_cpu_usage 更新。 */
double sysinfo_cpu_user_percent(void);
double sysinfo_cpu_system_percent(void);
double sysinfo_cpu_idle_percent(void);

/* 内存使用率（0-100）。 */
double sysinfo_mem_usage(void);

/* 磁盘使用率（0-100，根分区 /）。 */
double sysinfo_disk_usage(void);

/* 磁盘总容量 / 可用空间（字节，根分区 /）。 */
uint64_t sysinfo_disk_total_bytes(void);
uint64_t sysinfo_disk_avail_bytes(void);

/* 内存总量 / 已使用 / 已缓存 / 空闲（字节）。已使用口径与 sysinfo_mem_usage 一致。 */
uint64_t sysinfo_mem_total_bytes(void);
uint64_t sysinfo_mem_used_bytes(void);
uint64_t sysinfo_mem_cached_bytes(void);
uint64_t sysinfo_mem_free_bytes(void);

/* CPU 物理核心数 / 逻辑核心数。 */
int sysinfo_cpu_cores_physical(void);
int sysinfo_cpu_cores_logical(void);

#endif
