#include "sysinfo.h"
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdint.h>

/* mach_host_self() 每次返回一个新的端口 send right（+1 引用），
 * 采样循环里反复调用会无界累积。这里缓存一次，进程生命周期内复用。 */
static mach_port_t host_port(void) {
    static mach_port_t host = MACH_PORT_NULL;
    if (host == MACH_PORT_NULL)
        host = mach_host_self();
    return host;
}

/* 最近一次采样的细分（用户含 nice；闲置），供详情面板读取，不重复采样 */
static double cpu_last_user = 0, cpu_last_system = 0, cpu_last_idle = 0;

double sysinfo_cpu_usage(void) {
    static uint64_t prev_total = 0, prev_user = 0, prev_system = 0, prev_idle = 0;
    host_cpu_load_info_data_t cpu;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host_port(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpu, &count) != KERN_SUCCESS)
        return 0.0;

    uint64_t user   = cpu.cpu_ticks[CPU_STATE_USER] + cpu.cpu_ticks[CPU_STATE_NICE];
    uint64_t system = cpu.cpu_ticks[CPU_STATE_SYSTEM];
    uint64_t idle   = cpu.cpu_ticks[CPU_STATE_IDLE];
    uint64_t total  = user + system + idle;

    double pct = 0.0;
    if (prev_total > 0 && total > prev_total) {
        double dt = (double)(total - prev_total);
        cpu_last_user   = (double)(user - prev_user) / dt * 100.0;
        cpu_last_system = (double)(system - prev_system) / dt * 100.0;
        cpu_last_idle   = (double)(idle - prev_idle) / dt * 100.0;
        pct = cpu_last_user + cpu_last_system;
    }

    prev_total  = total;
    prev_user   = user;
    prev_system = system;
    prev_idle   = idle;
    return pct;
}

double sysinfo_cpu_user_percent(void)   { return cpu_last_user; }
double sysinfo_cpu_system_percent(void) { return cpu_last_system; }
double sysinfo_cpu_idle_percent(void)   { return cpu_last_idle; }

double sysinfo_mem_usage(void) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host_port(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) != KERN_SUCCESS)
        return 0.0;

    uint64_t total_mem = 0;
    size_t len = sizeof(total_mem);
    sysctlbyname("hw.memsize", &total_mem, &len, NULL, 0);
    if (total_mem == 0)
        return 0.0;

    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        page = 4096;
    uint64_t used = (uint64_t)(vm.wire_count + vm.active_count +
                               vm.compressor_page_count + vm.speculative_count) * (uint64_t)page;

    return (double)used / (double)total_mem * 100.0;
}

double sysinfo_disk_usage(void) {
    struct statfs st;
    if (statfs("/", &st) != 0)
        return 0.0;
    if (st.f_blocks == 0)
        return 0.0;

    /* 用 f_bavail（普通用户可用）算使用率，比 f_bfree 更接近真实占用 */
    double free_ratio = (double)st.f_bavail / (double)st.f_blocks;
    if (free_ratio > 1.0) free_ratio = 1.0;
    return (1.0 - free_ratio) * 100.0;
}

uint64_t sysinfo_disk_total_bytes(void) {
    struct statfs st;
    if (statfs("/", &st) != 0)
        return 0;
    return (uint64_t)st.f_blocks * (uint64_t)st.f_bsize;
}

uint64_t sysinfo_disk_avail_bytes(void) {
    struct statfs st;
    if (statfs("/", &st) != 0)
        return 0;
    return (uint64_t)st.f_bavail * (uint64_t)st.f_bsize;
}

uint64_t sysinfo_mem_total_bytes(void) {
    uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, NULL, 0) != 0)
        return 0;
    return total;
}

static long page_size(void) {
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? page : 4096;
}

uint64_t sysinfo_mem_used_bytes(void) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host_port(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) != KERN_SUCCESS)
        return 0;
    return (uint64_t)(vm.wire_count + vm.active_count +
                      vm.compressor_page_count + vm.speculative_count) * (uint64_t)page_size();
}

uint64_t sysinfo_mem_cached_bytes(void) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host_port(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) != KERN_SUCCESS)
        return 0;
    /* 非活跃 + 可清除页近似"已缓存" */
    return (uint64_t)(vm.inactive_count + vm.purgeable_count) * (uint64_t)page_size();
}

uint64_t sysinfo_mem_free_bytes(void) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host_port(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) != KERN_SUCCESS)
        return 0;
    return (uint64_t)vm.free_count * (uint64_t)page_size();
}

int sysinfo_cpu_cores_physical(void) {
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.physicalcpu", &n, &len, NULL, 0) != 0)
        return 0;
    return n;
}

int sysinfo_cpu_cores_logical(void) {
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &len, NULL, 0) != 0)
        return 0;
    return n;
}
