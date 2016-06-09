#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <dirent.h> 
#include <stdio.h> 
#include <time.h>
#include <stdint.h>

#include "sysfs.h"
#include "cpu_manager.h"

#define debug(var) printf("[%s:%s:%d] %s = \"%s\"\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_int(var) printf("[%s:%s:%d] %s = %d\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_long(var) printf("[%s:%s:%d] %s = %ld\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_size_t(var) printf("[%s:%s:%d] %s = %zu\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_addr(var) printf("[%s:%s:%d] %s = %p\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);

char *zeroone_str[2] = { "0\n", "1\n" };
char *cpuidle_governor_str[2] = { "ladder\n", "menu\n" };
char *cpufreq_governor_str[2] = { "ondemand\n", "userspace\n" };

cpu_manager_t
cpu_manager_init()
{
	size_t i;
	cpu_manager_t manager;
	manager.present = parse_simple_set("/sys/devices/system/cpu/present");

	// Switch all cores on and prepare dives to switch off hyperthread cores
#define SYSFS_ONLINE_DEVICE_PATTERN "devices/system/cpu/cpu%u/online"
	manager.hotplug = malloc(sizeof(sysfs_attr_tp) * (manager.present.size));
	for(i = 0; i < manager.present.size; i++)
	{
		int core = manager.present.member[i];
		if(core != 0)
		{
			char *sysfs_online_device = malloc(sizeof(char) * (strlen(SYSFS_ONLINE_DEVICE_PATTERN) - 2 + (i == 0 ? 1 : floor(log10(i)) + 1) + 1));
			sprintf(sysfs_online_device, SYSFS_ONLINE_DEVICE_PATTERN, core);
			manager.hotplug[i] = sysfs_attr_open_rw(sysfs_online_device, zeroone_str, 2);
			sysfs_attr_write(manager.hotplug[i], ONE);
			free(sysfs_online_device);
		}
	}

	// Find and switch off all hyperthreading cpus
	// Not necessary when we have scheduling for frequency islands
#define SYSFS_CORE_ID_DEVICE_PATTERN "devices/system/cpu/cpu%u/topology/core_id"
	int active = 0;
	for(i = 0; i < manager.present.size; i++)
	{
		char *sysfs_core_id_device = malloc(sizeof(char) * (strlen(SYSFS_CORE_ID_DEVICE_PATTERN) - 2 + (i == 0 ? 1 : floor(log10(i)) + 1) + 1));
		sprintf(sysfs_core_id_device, SYSFS_CORE_ID_DEVICE_PATTERN, manager.present.member[i]);
		sysfs_attr_tp sysfs_core_id_attr = sysfs_attr_open_ro(sysfs_core_id_device);
		char *core_id_str = sysfs_attr_read_alloc(sysfs_core_id_attr);
		size_t core_id = atoi(core_id_str);

		if(((active >> core_id) & 1) == 0)
		{
			active = active | (1 << core_id);
		}
		else
		{
			//debug("Switching core off");
			sysfs_attr_write(manager.hotplug[i], ZERO);
		}

		sysfs_attr_close(sysfs_core_id_attr);
		free(sysfs_core_id_device);
	}

	// Get the set of cores still online
	manager.online = parse_simple_set("/sys/devices/system/cpu/online");

#define SYSFS_SCALING_PATTERN "devices/system/cpu/cpu%d/cpufreq/scaling_setspeed"
#define SYSFS_FREQ_PATTERN "devices/system/cpu/cpu%d/cpufreq/scaling_available_frequencies"
#define SYSFS_CPUFREQ_CURRENT_PATTERN "devices/system/cpu/cpufreq/policy%d/cpuinfo_cur_freq"
#define SYSFS_CPUFREQ_GOVERNOR_PATTERN "devices/system/cpu/cpu%d/cpufreq/scaling_governor"
	manager.scaling = malloc(sizeof(sysfs_attr_tp) * (manager.online.size));
	manager.cpufreq_current = malloc(sizeof(sysfs_attr_tp) * (manager.online.size));
	manager.cpufreq_governor = malloc(sizeof(sysfs_attr_tp) * (manager.online.size));
	manager.freq = malloc(sizeof(char **) * (manager.online.size));
	manager.nb_freq = malloc(sizeof(size_t) * (manager.online.size));
	for(i = 0; i < manager.online.size; i++)
	{
		char *sysfs_cpufreq_governor = malloc(sizeof(char) * (strlen(SYSFS_CPUFREQ_GOVERNOR_PATTERN) - 2 + (manager.online.member[i] == 0 ? 1 : floor(log10(manager.online.member[i])) + 1) + 1));
		sprintf(sysfs_cpufreq_governor, SYSFS_CPUFREQ_GOVERNOR_PATTERN, manager.online.member[i]);
		manager.cpufreq_governor[i] = sysfs_attr_open_rw(sysfs_cpufreq_governor, cpufreq_governor_str, 2);
		sysfs_attr_write(manager.cpufreq_governor[i], CPUFREQ_USERSPACE);
		free(sysfs_cpufreq_governor);

		// Read frequency levels
		char *sysfs_freq = malloc(sizeof(char) * (strlen(SYSFS_FREQ_PATTERN) - 2 + (manager.online.member[i] == 0 ? 1 : floor(log10(manager.online.member[i])) + 1) + 1));
		sprintf(sysfs_freq, SYSFS_FREQ_PATTERN, manager.online.member[i]);
		sysfs_attr_tp freq = sysfs_attr_open_ro(sysfs_freq);
		char *freq_str = sysfs_attr_read_alloc(freq);
		manager.freq[i] = malloc(sizeof(char*));
		manager.freq[i][0] = freq_str;
		manager.nb_freq[i] = 1;
		size_t j;
		for(j = 0; freq_str[j] != '\0'; j++)
		{
			if(freq_str[j] == ' ' || freq_str[j] == 10)
			{
				freq_str[j] = '\0';
				j++;
				manager.freq[i] = realloc(manager.freq[i], sizeof(char*) * (manager.nb_freq[i] + 1));
				manager.freq[i][manager.nb_freq[i]] = &freq_str[j];
				if(freq_str[j] == 10 || freq_str[j] == '\0')
				{
					break;
				}
				else
				{
					manager.nb_freq[i]++;
				}
			}
		}
		sysfs_attr_close(freq);
		free(sysfs_freq);

		char *sysfs_scaling = malloc(sizeof(char) * (strlen(SYSFS_SCALING_PATTERN) - 2 + (i == 0 ? 1 : floor(log10(manager.online.member[i])) + 1) + 1));
		sprintf(sysfs_scaling, SYSFS_SCALING_PATTERN, manager.online.member[i]);
		manager.scaling[i] = sysfs_attr_open_rw(sysfs_scaling, manager.freq[i], manager.nb_freq[i]);
		//sysfs_attr_write(manager.scaling[i], 0);
		free(sysfs_scaling);

		char *sysfs_cpufreq_current = malloc(sizeof(char) * (strlen(SYSFS_CPUFREQ_CURRENT_PATTERN) - 2 + (manager.online.member[i] == 0 ? 1 : floor(log10(manager.online.member[i])) + 1) + 1));
		sprintf(sysfs_cpufreq_current, SYSFS_CPUFREQ_CURRENT_PATTERN, manager.online.member[i]);
		manager.cpufreq_current[i] = sysfs_attr_open_ro(sysfs_cpufreq_current);
		//debug(sysfs_attr_read_alloc(manager.cpufreq_current[i]));
		free(sysfs_cpufreq_current);
	}

#define SYSFS_CSTATE_PATTERN_PART1 "devices/system/cpu/cpu%u/cpuidle"
#define SYSFS_CSTATE_PATTERN_PART2 "state%u/disable"
	manager.cstate = malloc(sizeof(cstate_t) * (manager.online.size));
	for(i = 0; i < manager.online.size; i++)
	{
		DIR *d;
		struct dirent *dir;
		char *cstate_dir = malloc(sizeof(char) * (strlen(SYSFS_PREFIX SYSFS_CSTATE_PATTERN_PART1) - 2 + (manager.online.member[i] == 0 ? 1 : floor(log10(manager.online.member[i])) + 1) + 1));
		sprintf(cstate_dir, SYSFS_PREFIX SYSFS_CSTATE_PATTERN_PART1, manager.online.member[i]);
		d = opendir(cstate_dir);
		if(d)
		{
			manager.cstate[i].nb_states = 0;
			manager.cstate[i].state = NULL;
			while ((dir = readdir(d)) != NULL)
			{
				int state;
				int found = sscanf(dir->d_name, "state%u", &state);
				if(found > 0 && state > 0)
				{
					manager.cstate[i].state = realloc(manager.cstate[i].state, manager.cstate[i].nb_states + 1);
					if(manager.cstate[i].state == NULL)
					{
						perror("Error while reallocating cstate array");
						abort();
					}
					manager.cstate[i].state[manager.cstate[i].nb_states] = state;
					manager.cstate[i].nb_states++;
				}
			}
			closedir(d);
			manager.cstate[i].attr = malloc(sizeof(sysfs_attr_tp) * manager.cstate[i].nb_states);
			size_t j;
			for(j = 0; j < manager.cstate[i].nb_states; j++)
			{
				char *sysfs_cstate = malloc(sizeof(char) * (strlen(SYSFS_CSTATE_PATTERN_PART1) - 2 + 1 + strlen(SYSFS_CSTATE_PATTERN_PART2) - 2 + (manager.online.member[i] == 0 ? 1 : floor(log10(manager.online.member[i])) + 1) + (j == 0 ? 1 : floor(log10(j)) + 1) + 1));
				sprintf(sysfs_cstate, SYSFS_CSTATE_PATTERN_PART1, manager.online.member[i]);
				sprintf(sysfs_cstate + strlen(SYSFS_CSTATE_PATTERN_PART1) - 2 + (unsigned int)(manager.online.member[i] == 0 ? 1 : floor(log10(manager.online.member[i])) + 1), "/" SYSFS_CSTATE_PATTERN_PART2, manager.cstate[i].state[j]);
				manager.cstate[i].attr[j] = sysfs_attr_open_rw(sysfs_cstate, zeroone_str, 2);
				sysfs_attr_write(manager.cstate[i].attr[j], ONE);
				free(sysfs_cstate);
			}
		}

		free(cstate_dir);
	}

	uint64_t buf[2];
	sysfs_attr_read(manager.cpufreq_current[0], (char*)&buf[0], 8);
	sysfs_attr_read(manager.cpufreq_current[1], (char*)&buf[1], 8);

	return manager;
}

void
cpu_manager_destroy(cpu_manager_t manager)
{
	size_t i;
	// Restore full system cores and "reasonnable" power saving settings
	//sysfs_attr_write(cpuidle_governor, CPUIDLE_LADDER);
	//sysfs_attr_close(cpuidle_governor);

	// Teardown
	for(i = 0; i < manager.online.size; i++)
	{
		sysfs_attr_close(manager.scaling[i]);

		sysfs_attr_write(manager.cpufreq_governor[i], CPUFREQ_ONDEMAND);
		sysfs_attr_close(manager.cpufreq_governor[i]);
		size_t j;
		for(j = 0; j < manager.cstate[i].nb_states; j++)
		{
			sysfs_attr_write(manager.cstate[i].attr[j], ZERO);
			sysfs_attr_close(manager.cstate[i].attr[j]);
		}

		free(manager.freq[i]);
	}
	free(manager.freq);
	free(manager.nb_freq);
	free(manager.cstate);
	free(manager.scaling);
	free(manager.cpufreq_current);
	free(manager.cpufreq_governor);

	for(i = 0; i < manager.present.size; i++)
	{
		if(manager.present.member[i] != 0)
		{
			sysfs_attr_write(manager.hotplug[i], ONE);
			sysfs_attr_close(manager.hotplug[i]);
		}
	}

	free(manager.hotplug);
}

void
cpu_manager_set_frequency(cpu_manager_t manager, size_t core, char *freq)
{
	sysfs_attr_write_str(manager.scaling[core], freq);
}

