#ifndef CPUFREQ_H
#define CPUFREQ_H

enum zeroone { ZERO = 0, ONE = 1 };
enum cpuidle_governor { CPUIDLE_LADDER, CPUIDLE_MENU };
enum cpufreq_governor { CPUFREQ_ONDEMAND, CPUFREQ_USERSPACE };

struct cstate
{
	sysfs_attr_tp *attr;
	size_t nb_states;
	int *state;
};
typedef struct cstate cstate_t;

struct cpuid
{
	size_t vid, rid;
};
typedef struct cpuid cpuid_t;

struct cpu_manager
{
	simple_set_t present, online;
	sysfs_attr_tp *hotplug, *scaling, *cpufreq_current, *cpufreq_governor;
	cstate_t *cstate;
	char ***freq;
	size_t *nb_freq;
	cpuid_t *global_core_id, *rglobal_core_id;
	int *cpufreq_latency;
};

typedef struct cpu_manager cpu_manager_t;
cpu_manager_t cpu_manager_init();
void cpu_manager_destroy(cpu_manager_t manager);
void cpu_manager_set_frequency(cpu_manager_t manager, size_t core, char *freq);

#endif
