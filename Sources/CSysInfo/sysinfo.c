#include "sysinfo.h"
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdint.h>

double sysinfo_cpu_usage(void) {
    static uint64_t prev_total = 0, prev_used = 0;
    host_cpu_load_info_data_t cpu;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpu, &count) != KERN_SUCCESS)
        return 0.0;

    uint64_t user   = cpu.cpu_ticks[CPU_STATE_USER];
    uint64_t system = cpu.cpu_ticks[CPU_STATE_SYSTEM];
    uint64_t idle   = cpu.cpu_ticks[CPU_STATE_IDLE];
    uint64_t nice   = cpu.cpu_ticks[CPU_STATE_NICE];
    uint64_t total  = user + system + idle + nice;
    uint64_t used   = user + system + nice;

    double pct = 0.0;
    if (prev_total > 0 && total > prev_total)
        pct = (double)(used - prev_used) / (double)(total - prev_total) * 100.0;

    prev_total = total;
    prev_used  = used;
    return pct;
}

double sysinfo_mem_usage(void) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
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
