#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

/* cpus.c */
int qemu_init_main_loop(void);
void qemu_main_loop_start(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);
bool qemu_system_is_ready(void);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);

/* vl.c */
extern int smp_cores;
extern int smp_threads;
void vm_state_notify(int running, int reason);
bool cpu_exec_all(void);
void set_numa_modes(void);
void set_cpu_log(const char *optarg);
void set_cpu_log_filename(const char *optarg);
void list_cpus(FILE *f, fprintf_function cpu_fprintf, const char *optarg);

#endif
