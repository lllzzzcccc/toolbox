#ifndef SYSINFO_H
#define SYSINFO_H

/* CPU 使用率（0-100）。首次调用返回 0（需两次采样算增量）。 */
double sysinfo_cpu_usage(void);

/* 内存使用率（0-100）。 */
double sysinfo_mem_usage(void);

#endif
