/*
 Copyright 2015 Nicolas Melot, Johan Janzén

 This file is part of Drake-SCC.

 Drake-SCC is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Drake-SCC is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Drake-SCC. If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <semaphore.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include <drake/eval.h>
#include <drake/platform.h>
#include <drake/stream.h>
#include <drake/intel-ia.h>
#include <pelib/malloc.h>
#include <string.h>
#include <stdint.h>
#include <sched.h>
#include <unistd.h>
#include <rapl.h>
#include <pelib/time.h>

#include "sysfs.h"
#include "cpu_manager.h"

#ifdef debug
#undef debug
#undef debug_int
#undef debug_addr
#undef debug_size_t
#endif

#if 1
#define debug(var) printf("[%s:%s:%d:CORE %zu] %s = \"%s\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_addr(var) printf("[%s:%s:%d:CORE %zu] %s = \"%p\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_int(var) printf("[%s:%s:%d:CORE %zu] %s = \"%d\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_double(var) printf("[%s:%s:%d:CORE %zu] %s = \"%lf\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_size_t(var) printf("[%s:%s:%d:CORE %zu] %s = \"%zu\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#else
#define debug(var)
#define debug_addr(var)
#define debug_int(var)
#define debug_size_t(var)
#endif

//#define DRAKE_IA_LINE_SIZE 64
//#define DRAKE_IA_SHARED_SIZE (256 * 1024)
//#define DRAKE_IA_SHARED_SIZE 512

enum phase { DRAKE_IA_CREATE, DRAKE_IA_INIT, DRAKE_IA_RUN, DRAKE_IA_DESTROY };

struct drake_time {
	double time;
};
typedef struct drake_time *drake_time_t;

typedef struct {
	size_t id;
	drake_platform_t handler;
} drake_thread_init_t;

struct drake_platform
{
	pthread_t *pthread;
	drake_stream_t *stream;
	pthread_barrier_t work_notify;
	sem_t ready, report_ready;
	size_t core_size;
	int *success;
	enum phase phase;
	int *frequency;
	float *voltage;
	void *task_args;
	void (*schedule_init)();
	void (*schedule_destroy)();
	void* (*task_function)(size_t, task_status_t);
	sem_t new_order;
#if MANAGE_CPU
	cpu_manager_t manager;
	uint64_t read_freq;
	int wait_after_scaling;
#endif
};
typedef struct drake_platform *drake_platform_t;

static size_t core_size;
static __thread size_t core_id = 0;
void **shared_buffer;
pelib_malloc_queue_t **shared;
pthread_barrier_t barrier;
pthread_mutex_t exclusive;

static
ia_arguments_t
parse_ia_arguments(size_t argc, char **argv)
{
	ia_arguments_t args;
	args.poll_at_idle = 0;
	args.wait_after_scaling = 1;
	args.num_cores = 1;

	//debug_size_t(argc);
	for(; argv[0] != NULL; argv++)
	{
		//debug(argv[0]);
		if(strcmp(argv[0], "--poll-at-idle") == 0)
		{
			args.poll_at_idle = 1;
			continue;
		}
		if(strcmp(argv[0], "--num-cores") == 0)
		{
			argv++;
			args.num_cores = atoi(argv[0]);
			//debug_int(args.num_cores);
			continue;
		}
		if(strcmp(argv[0], "--no-wait-after-scaling") == 0)
		{
			args.wait_after_scaling = 0;
			continue;
		}
	}

	return args;
}

static 
void*
drake_ia_thread(void* args)
{
	size_t i;
	// Collect its core id
	drake_thread_init_t *init = (drake_thread_init_t*)args;
	core_id = init->id;
	drake_platform_t handler = init->handler;
	if(posix_memalign(&shared_buffer[core_id], DRAKE_IA_LINE_SIZE, DRAKE_IA_SHARED_SIZE) != 0)
	{
		fprintf(stderr, "[%s:%d] Could not allocate communication memory. Aborting.\n", __FILE__, __LINE__);
		abort();
	}

#if MANAGE_CPU
	cpu_set_t *cpu = CPU_ALLOC(handler->manager.online.size);
	CPU_ZERO_S(CPU_ALLOC_SIZE(handler->manager.online.size), cpu);
	CPU_SET_S(handler->manager.global_core_id[core_id].vid, CPU_ALLOC_SIZE(handler->manager.online.size), cpu);
	sched_setaffinity(0, handler->manager.online.size, cpu);
	CPU_FREE(cpu);
#endif

	// Wait for all cores to have their shared memory buffer allocated
	pthread_barrier_wait(&handler->work_notify);
	// Notify master thread that all threads are ready
	if(sem_trywait(&handler->report_ready) == 0)
	{
		sem_post(&handler->ready);
		pthread_barrier_wait(&handler->work_notify);
		sem_post(&handler->report_ready);
	}
	else
	{
		pthread_barrier_wait(&handler->work_notify);
	}
	
	// Initialize memory allocator pointers for all cores
	shared[core_id] = malloc(DRAKE_IA_SHARED_SIZE);
	for(i = 0; i < core_size; i++)
	{
		pelib_mem_malloc_init(&shared[core_id][i], shared_buffer[i], DRAKE_IA_SHARED_SIZE);
	}

	// Wait for something to do
	int done = 0;
	while(!done)
	{
		// Wait for someone to tell to do something
		if(sem_trywait(&handler->report_ready) == 0)
		{
			sem_wait(&handler->new_order);
			pthread_barrier_wait(&handler->work_notify);
			sem_post(&handler->report_ready);
		}
		else
		{
			pthread_barrier_wait(&handler->work_notify);
		}

		// See what we have to do, and do it
		switch(handler->phase)
		{
			case DRAKE_IA_CREATE:
			{
				handler->stream[core_id] = drake_stream_create_explicit(handler->schedule_init, handler->schedule_destroy, handler->task_function, handler);
				handler->success[core_id] = 1;
			}
			break;
			case DRAKE_IA_INIT:
			{
				handler->success[core_id] = drake_stream_init(&handler->stream[core_id], handler->task_args);
			}
			break;
			case DRAKE_IA_RUN:
			{
				handler->success[core_id] = drake_stream_run(&handler->stream[core_id]);
			}
			break;
			case DRAKE_IA_DESTROY:
			{
				handler->success[core_id] = drake_stream_destroy(&handler->stream[core_id]);
				done = 1;
			}
			break;
		}

		// Wait for everyone to be done
		pthread_barrier_wait(&handler->work_notify);

		// One lucky thread will have to tell the master thread everyone is done
		if(sem_trywait(&handler->report_ready) == 0)
		{
			// Tell other threads they can already wait for new orders
			pthread_barrier_wait(&handler->work_notify);
			// Tell master thread everyone is done
			sem_post(&handler->ready);
			// Let another thread to notify master thread next time
			sem_post(&handler->report_ready);
		}
		else
		{
			// Just wait for notifier thread to come
			pthread_barrier_wait(&handler->work_notify);
		}
	}

	// Do some cleanup
	free(shared_buffer[core_id]);

	// Terminate
	return NULL;
}

void
drake_platform_sleep_enable(drake_platform_t pt, size_t core)
{
#if MANAGE_CPU && DISABLE_UNUSED_CORES
	size_t i;
	for(i = 0; i < pt->manager.cstate[0].nb_states; i++)
	{
		sysfs_attr_write(pt->manager.cstate[0].attr[i], ZERO);
	}
#endif
}

void
drake_platform_sleep_disable(drake_platform_t pt, size_t core)
{
#if MANAGE_CPU && DISABLE_UNUSED_CORES
	size_t i;
	for(i = 0; i < pt->manager.cstate[0].nb_states; i++)
	{
		sysfs_attr_write(pt->manager.cstate[0].attr[i], ONE);
	}
#endif
}

void
drake_platform_core_disable(drake_platform_t pt, size_t core)
{
#if MANAGE_CPU && DISABLE_UNUSED_CORES
	if(core > 0)
	{
		sysfs_attr_write(pt->manager.hotplug[pt->manager.global_core_id[core].vid], ZERO);
		//debug_size_t(core);
		//debug_size_t(pt->manager.global_core_id[core].vid);
		size_t i;
		for(i = 0; i < pt->manager.online.size; i++)
		{
			//debug_size_t(i);
			if(i != core)
			{
				//debug_size_t(pt->manager.global_core_id[i].vid);
				msr_turbo_boost_disable(pt->manager.turbo_boost[pt->manager.global_core_id[i].vid]);
			}
		}
	}
	else
	{
		drake_platform_sleep_enable(pt, 0);
	}
#endif
}

void
drake_platform_core_enable(drake_platform_t pt, size_t core)
{
#if MANAGE_CPU && DISABLE_UNUSED_CORES
	if(core > 0)
	{
		// Caution: sysfs permission will not allow changing the cpuidle settings of this core anymore
		sysfs_attr_write(pt->manager.hotplug[pt->manager.global_core_id[core].vid], ONE);
	}
	else
	{
		size_t i;
		for(i = 0; i < pt->manager.cstate[0].nb_states; i++)
		{
			sysfs_attr_write(pt->manager.cstate[0].attr[i], ZERO);
		}
	}
#endif
}

#define hexdump(obj) { char *ptr = (char*)&obj; size_t i; printf("[%s:%s:%d:CORE %zu] %s : ", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #obj); for(i = 0; i < sizeof(obj); i++) { printf("%02X ", ptr[i]); } printf("\n"); } fflush(NULL)

drake_platform_t
drake_platform_init(void* obj)
{
	drake_platform_t stream = drake_platform_private_malloc(sizeof(struct drake_platform));


	// Parse arguments
	char *config_mode = getenv("DRAKE_IA_CONFIG_ARGS");
	ia_arguments_t args;
	if(config_mode == NULL || strcmp(config_mode, "0") == 0)
	{
		// default mode: just use a structure
		args = *(ia_arguments_t*)obj;
	}
	else
	{
		// drake-eval mode: parse strings
		args_t *str_args = (args_t*)obj;
		args = parse_ia_arguments(str_args->argc, str_args->argv);
	}

	// Initialize hooks to cpufreq, cpuidle and hotplug, and switch off hyperthreading cores
#if MANAGE_CPU
	stream->manager = cpu_manager_init(args.poll_at_idle);
	stream->wait_after_scaling = args.wait_after_scaling;

	core_size = stream->manager.online.size;
#else
	core_size = args.num_cores;
#endif
	drake_thread_init_t *init = malloc(sizeof(drake_thread_init_t) * core_size);

	shared_buffer = malloc(sizeof(void*) * core_size);
	shared = malloc(sizeof(pelib_malloc_queue_t) * core_size);
	stream->pthread = drake_platform_private_malloc(sizeof(pthread_t) * core_size);
	stream->stream = drake_platform_private_malloc(sizeof(drake_stream_t) * core_size);
	stream->success = drake_platform_private_malloc(sizeof(int) * core_size);
	pthread_barrier_init(&stream->work_notify, NULL, core_size);
	pthread_barrier_init(&barrier, NULL, core_size);
	pthread_mutex_init(&exclusive, NULL);
	sem_init(&stream->report_ready, 0, 1);
	sem_init(&stream->ready, 0, 0);
	sem_init(&stream->new_order, 0, 0);

	size_t i;
	for(i = 0; i < core_size; i++)
	{
		init[i].handler = stream;
		init[i].id = i;
		pthread_create(&stream->pthread[i], NULL, drake_ia_thread, (void*)&init[i]);
	}

	// Wait for all threads to be created
	sem_wait(&stream->ready);

	return stream;
}

int
drake_platform_stream_create_explicit(drake_platform_t stream, void (*schedule_init)(), void (*schedule_destroy)(), void* (*task_function)(size_t id, task_status_t status))
{
	// Give the work to be done
	stream->phase = DRAKE_IA_CREATE;
	stream->schedule_init = schedule_init;
	stream->schedule_destroy = schedule_destroy;
	stream->task_function = task_function;

	// Unlock threads
	sem_post(&stream->new_order);

	// Wait for work completion notification
	sem_wait(&stream->ready);

	int success = 1;
	size_t i;
	for(i = 0; i < core_size; i++)
	{
		success = success && stream->success[i];
	}

	// Return newly built stream
	return success;
}

int
drake_platform_stream_init(drake_platform_t stream, void* arg)
{
	int success = 1;
	size_t i;
	stream->task_args = arg;
	stream->phase = DRAKE_IA_INIT;
	sem_post(&stream->new_order);
	sem_wait(&stream->ready);

	for(i = 0; i < drake_platform_core_size(); i++)
	{
		success = success && stream->success[i];
	}

	return success;	
}

int
drake_platform_stream_run(drake_platform_t stream)
{
	stream->phase = DRAKE_IA_RUN;
	sem_post(&stream->new_order);
	sem_wait(&stream->ready);

	size_t i;
	int success = 1;
	for(i = 0; i < drake_platform_core_size(); i++)
	{
		success = success && stream->success[i];
	}

	return success;	
}

int
drake_platform_stream_destroy(drake_platform_t stream)
{
	stream->phase = DRAKE_IA_DESTROY;
	sem_post(&stream->new_order);
	sem_wait(&stream->ready);

	size_t i;
	int success = 1;
	for(i = 0; i < drake_platform_core_size(); i++)
	{
		success = success && stream->success[i];
	}

	return success;	
}

int
drake_platform_destroy(drake_platform_t stream)
{
	pthread_barrier_destroy(&stream->work_notify);
	pthread_barrier_destroy(&barrier);
	pthread_mutex_destroy(&exclusive);
	sem_destroy(&stream->new_order);
	sem_destroy(&stream->ready);
	sem_destroy(&stream->report_ready);
	drake_platform_private_free(stream->pthread);
	drake_platform_private_free(stream->stream);
	drake_platform_private_free(stream->success);
#if MANAGE_CPU
	cpu_manager_destroy(stream->manager);
#endif

	return 0;
}

volatile void* drake_platform_shared_malloc(size_t size, size_t core)
{
	volatile void* addr = pelib_mem_malloc(&shared[core_id][core], size, DRAKE_IA_LINE_SIZE);
	return addr;
}

volatile void* drake_platform_shared_malloc_mailbox(size_t size, size_t core)
{
	volatile void* addr = pelib_mem_malloc(&shared[core_id][core], size, 0);
	return addr;
}

void
drake_platform_shared_free(volatile void* addr, size_t core)
{
	pelib_mem_free(&shared[core_id][core], (void*)addr);
}

void*
drake_platform_private_malloc(size_t size)
{
	return malloc(size);
}

void
drake_platform_private_free(void *addr)
{
	free((void*)addr);
}

void*
drake_platform_store_malloc(size_t size)
{
	abort();
	return malloc(size);
}

void
drake_platform_store_free(void *addr)
{
	abort();
	free(addr);
}

int drake_platform_pull(volatile void* addr)
{
	(void*)(addr);

	// This cannot go wrong
	return 1;
}

int drake_platform_commit(volatile void* addr)
{
	(void*)(addr);
	// Do nothing and let hardware cache coherency do the work

	// This cannot go wrong
	return 1;
}

size_t
drake_platform_core_id()
{
	return core_id;
}

size_t
drake_platform_core_size()
{
	return core_size;
}

size_t
drake_platform_core_max()
{
	return 16;
}

void
drake_platform_barrier(void* channel)
{
	pthread_barrier_wait(&barrier);
}

void
drake_platform_exclusive_begin()
{
	pthread_mutex_lock(&exclusive);
}

void
drake_platform_exclusive_end()
{
	pthread_mutex_unlock(&exclusive);
}

void*
drake_remote_addr(void* addr, size_t core)
{
	void* remote = shared_buffer[core] + (addr - shared_buffer[core_id]);
	return remote;
}

size_t
drake_platform_store_size()
{
	return 0;
}

size_t
drake_platform_shared_size()
{
	return DRAKE_IA_SHARED_SIZE;
}

size_t
drake_platform_shared_align()
{
	return DRAKE_IA_LINE_SIZE;
}

int
drake_platform_set_frequency(int freq /* in KHz */)
{
	return 1;
}

int
drake_platform_set_frequency_autoscale(int frequency /* in KHz */)
{
	fprintf(stderr, "[%zu][ERROR] %s: not implemented.\n", drake_platform_core_id(), __FUNCTION__);
	return 0;
}

int
drake_platform_set_voltage(float voltage /* in volts */)
{
	return 0;
}

int
drake_platform_set_voltage_frequency(drake_platform_t stream, size_t freq /* in index of frequency set */)
{
	//struct timespec begin, end, period;
	//clock_gettime(CLOCK_MONOTONIC, &begin);
#if MANAGE_CPU && SCALE_FREQUENCY
	sysfs_attr_write_buffer(stream->manager.scaling[stream->manager.global_core_id[drake_platform_core_id()].vid], stream->manager.freq[stream->manager.global_core_id[drake_platform_core_id()].vid][stream->manager.nb_freq[stream->manager.global_core_id[drake_platform_core_id()].vid] - freq - 1], 8);
	if(stream->wait_after_scaling != 0)
	{
#if USE_USLEEP
#warning Wait for frequency switching delay with microsleep
		//debug_int(stream->manager.cpufreq_latency[stream->manager.global_core_id[drake_platform_core_id()].vid]);
		usleep(stream->manager.cpufreq_latency[stream->manager.global_core_id[drake_platform_core_id()].vid]);
#else
#warning Wait for frequency switching delay with polling
		struct timespec now, wakeup;
		clock_gettime(CLOCK_MONOTONIC, &wakeup);
		int latency = stream->manager.cpufreq_latency[stream->manager.global_core_id[drake_platform_core_id()].vid];
		if(wakeup.tv_nsec + latency <= 1000000000 - latency)
		{
			wakeup.tv_nsec += latency;
		}
		else
		{
			wakeup.tv_nsec = wakeup.tv_nsec + latency - 1000000000;
			wakeup.tv_sec += 1;
		}

		do	
		{
			clock_gettime(CLOCK_MONOTONIC, &now);
		}
		while(now.tv_sec < wakeup.tv_sec || now.tv_nsec < wakeup.tv_nsec);
#endif
	}
	/*
	clock_gettime(CLOCK_MONOTONIC, &end);
	pelib_timespec_subtract(&period, &end, &begin);
	printf("%li:%li\n", period.tv_sec, period.tv_nsec);
	*/
#endif

	return 1;
}

size_t
drake_platform_get_frequency(drake_platform_t stream) /* in index of frequency set */
{
#if MANAGE_CPU && SCALE_FREQUENCY
	sysfs_attr_read(stream->manager.cpufreq_current[stream->manager.global_core_id[drake_platform_core_id()].vid], (char*)&stream->read_freq, 8);
	size_t i;
	for(i = 0; i < stream->manager.nb_freq[stream->manager.global_core_id[drake_platform_core_id()].vid]; i++)
	{
		if(stream->read_freq == *(uint64_t*)stream->manager.freq[stream->manager.global_core_id[drake_platform_core_id()].vid][i])
		{
			break;
		}
	}

	return i;
#else
	return 0;
#endif
}

float
drake_platform_get_voltage() /* in volts */
{
	return 0;
}

int drake_platform_time_get(drake_time_t container)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	container->time = t.tv_sec * 1e3 + t.tv_nsec / 1e6;
	return 1;
}

int drake_platform_time_subtract(drake_time_t res, drake_time_t t1, drake_time_t t2)
{
	res->time = t1->time - t2->time;
	return 1;
}

int drake_platform_time_add(drake_time_t res, drake_time_t t1, drake_time_t t2)
{
	res->time = t1->time + t2->time;
	return 1;
}

int drake_platform_time_greater(drake_time_t t1, drake_time_t t2)
{
	return t1->time > t2->time;
}

int drake_platform_time_equals(drake_time_t t1, drake_time_t t2)
{
	return t1->time == t2->time;
}

int drake_platform_time_init(drake_time_t t, double ms)
{
	t->time = ms;
	return 1;
}

int drake_platform_sleep(drake_time_t period)
{
	// Todo: improve with a proper sleep() system call
	struct drake_time now, until;
	drake_time_t now_p = &now, until_p = &until;

	drake_platform_time_get(now_p);
	until_p->time = now_p->time + period->time;
	usleep(period->time * 1e3);
	/*
	while(!drake_platform_time_greater(now_p, until_p))
	{
		drake_platform_time_get(now_p);
	}
	*/

	return 1;
}

drake_time_t drake_platform_time_alloc()
{
	return malloc(sizeof(struct drake_time));
}

FILE*
drake_platform_time_printf(FILE *stream, drake_time_t time)
{
	fprintf(stream, "%f", time->time);
	return stream;
}

double
drake_platform_time_double(drake_time_t time)
{
	return time->time;
}

char*
drake_platform_time_str(drake_time_t time)
{
	if(time->time == 0)
	{
		char *str = malloc(sizeof(char) * 2);
		str[0] = '0';
		str[0] = '\0';
		return str;
	}
	else
	{
		size_t size;
		if(time->time <= 1 && time->time >= -1)
		{
			// One digit for integer 0
			size = 1;
			//debug_size_t(size);
		}
		else
		{
			// As many digits as needed for integer part
			size = ceil(log10(time->time));
			//debug_size_t(size);
		}
		size += time->time < 0 ? 1 : 0; // Minus sign
		//debug_size_t(size);
		size += 1; // Point
		//debug_size_t(size);
		size += 6; // decimal digits
		//debug_size_t(size);
		size += 1; // Null terminator
		//debug_size_t(size);
		//printf("%.6lf\n", time->time);
		// Number of digits (log10() + 1) + minus sign (time >= 0?) + point (1) + decimals (6) + termination (1)
		char *str = malloc(sizeof(char) * size);
		sprintf(str, "%.6lf", time->time);
		return str;
	}
}

void
drake_platform_time_destroy(drake_time_t time)
{
	free(time);
}

struct drake_power {
	double *power_chip, *power_core, *power_mc;
	struct drake_time *time;
	size_t max_samples;
	size_t collected;
	sem_t run;
	int running;
	int measurement;
	pthread_t thread;
	drake_platform_t pt;
};
typedef struct drake_power *drake_power_t;

#define RAW_RAPL 0
#if RAW_RAPL
static
double
get_rapl_energy_info(uint64_t power_domain, uint64_t node)
{
    int          err;
    double       total_energy_consumed;

    switch (power_domain) {
    case PKG:
        err = get_pkg_total_energy_consumed(node, &total_energy_consumed);
        break;
    case PP0:
        err = get_pp0_total_energy_consumed(node, &total_energy_consumed);
        break;
    case PP1:
        err = get_pp1_total_energy_consumed(node, &total_energy_consumed);
        break;
    case DRAM:
        err = get_dram_total_energy_consumed(node, &total_energy_consumed);
        break;
    default:
        err = MY_ERROR;
        break;
    }

    return total_energy_consumed;
}
#endif

static
int
read_power(uint64_t node, int measurement, double *chip, double *core, double *mc)
{
	int          err = 0;
	double       total_energy_consumed;

	*chip = 0;
	if(measurement & (1 << DRAKE_POWER_CHIP))
	{
		double pkg = 0;
		if(is_supported_domain(PKG))
		{
			err += get_pkg_total_energy_consumed(node, &pkg);
		}
		else
		{
			err += 1;
		}
		// Apparently, PP1 is graphics (https://software.intel.com/en-us/forums/software-tuning-performance-optimization-platform-monitoring/topic/557622) so better not read it
		//err += get_pp1_total_energy_consumed(node, &pp1);
		*chip = pkg;
	}

	// Per core measurement is not supported
	*core = 0;
	if(measurement & (1 << DRAKE_POWER_CORE))
	{
		// Report an error
		err += 1;
		// Do nothing
	}
	
	*mc = 0;
	if(measurement & (1 << DRAKE_POWER_MEMORY_CONTROLLER))
	{
		if(is_supported_domain(DRAM))
		{
			err += get_dram_total_energy_consumed(node, mc);
		}
		else
		{
			err += 1;
		}
	}

	return err;
}

void*
measure_power(void* arg)
{
	int i;
	int domain = 0;
	drake_power_t tracker = (drake_power_t)arg;
#if MANAGE_CPU && MONITOR_POWER
	uint64_t num_pkg, num_core_per_pkg;
	APIC_ID_t **pkg_map;
	tracker->power_chip[0] = 0;

	// Initialize stuff
	if (0 != init_rapl())
	{
		fprintf(stdout, "RAPL initialization failed!\n");
		terminate_rapl();
		abort();
	}
	int num_node = get_num_rapl_nodes_pkg();
	pkg_map = rapl_get_topology(&num_pkg, &num_core_per_pkg);
#else
	int num_node = 1;
#endif

	sem_wait(&tracker->run);
	tracker->power_chip[0] = 0;

	// Wake up monitoring cores, in case drake switched them off
	
	double begin_chip = 0, begin_core = 0, begin_mc = 0;
	double end_chip = 0, end_core = 0, end_mc = 0;
    	double begin[num_node][RAPL_NR_DOMAIN];
    	double end[num_node][RAPL_NR_DOMAIN];
	// Begin measurement: Copy-pasted from RAPL cpu_power library
	/* Read initial values */
#if MANAGE_CPU && MONITOR_POWER
	for (i = 0; i < num_node && 1; i++)
	{
		// Switch on measurement core, if necessary
		uint64_t os_id = pkg_map[i][0].os_id;
		int pkg_off;
		double chip, core, mc;
		if(os_id > 0)
		{
			char *str = sysfs_attr_read_alloc(tracker->pt->manager.hotplug[os_id]);
			pkg_off = (strcmp(str, "0\n") == 0);
			sysfs_attr_write(tracker->pt->manager.hotplug[os_id], ONE);
		}
		// Start measurement
#if RAW_RAPL
		for (domain = 0; domain < RAPL_NR_DOMAIN; ++domain)
		{
			if(is_supported_domain(domain))
			{
				begin[i][domain] = get_rapl_energy_info(domain, i);
			}
		}
#endif
		read_power(i, tracker->measurement, &chip, &core, &mc);
		begin_chip += chip;
		begin_core += core;
		begin_mc += mc;

		// Switch of measurement core if not used by drake
		if(os_id > 0 && pkg_off)
		{
			sysfs_attr_write(tracker->pt->manager.hotplug[os_id], ZERO);
		}
	}
#else
	begin_chip = 0;
	begin_core = 0;
	begin_mc = 0;
#endif
	drake_platform_time_get(&tracker->time[0]);

	sem_wait(&tracker->run);
	tracker->power_chip[0] = 0;

#if MANAGE_CPU && MONITOR_POWER
	// Wake up threads used by rapl for measurements
	for(i = 0; i < num_pkg; i++)
	{
		uint64_t os_id = pkg_map[i][0].os_id;
		if(os_id > 0)
		{	
			sysfs_attr_write(tracker->pt->manager.hotplug[os_id], ONE);
		}
	}

	// Stop measurement
	for (i = 0; i < num_node && 1; i++)
	{
		double chip, core, mc;
#if RAW_RAPL
		for (domain = 0; domain < RAPL_NR_DOMAIN; ++domain)
		{
	        	if(is_supported_domain(domain))
			{
        			end[i][domain] = get_rapl_energy_info(domain, i);
			}
		}
#endif
		read_power(i, tracker->measurement, &chip, &core, &mc);
		end_chip += chip;
		end_core += core;
		end_mc += mc;
	}
#else
	end_chip = 0;
	end_core = 0;
	end_mc = 0;
#endif
	drake_platform_time_get(&tracker->time[1]);
	tracker->collected = 2;

	tracker->power_chip[0] = tracker->power_chip[1] = (end_chip - begin_chip) / ((tracker->time[1].time - tracker->time[0].time) / 1000);
	tracker->power_core[0] = tracker->power_core[1] = (end_core - begin_core) / ((tracker->time[1].time - tracker->time[0].time) / 1000);
	tracker->power_mc[0] = tracker->power_mc[1] = (end_mc - begin_mc) / ((tracker->time[1].time - tracker->time[0].time) / 1000);

#if RAW_RAPL
	// Show measurement output
	for (i = 0; i < num_node && 1; i++)
	{
		for (domain = 0; domain < RAPL_NR_DOMAIN; ++domain)
		{
			if(is_supported_domain(domain))
			{
				printf("Energy spent: %lf\n", end[i][domain] - begin[i][domain]);
			}
		}
	}
#endif

	return NULL;
}

drake_power_t
drake_platform_power_init(drake_platform_t str, size_t samples, int measurement)
{
	drake_power_t tracker = malloc(sizeof(struct drake_power));
	tracker->power_chip = malloc(sizeof(double) * 2);
	tracker->power_mc = malloc(sizeof(double) * 2);
	tracker->power_core = malloc(sizeof(double) * 2);
	tracker->time = malloc(sizeof(struct drake_time) * 2);
	tracker->max_samples = 2;
	tracker->collected = 0;
	tracker->running = 0;
	tracker->measurement = measurement;
	tracker->pt = str;
	tracker->power_chip[0] = 0;

	sem_init(&tracker->run, 0, 0);
	tracker->power_chip[0] = 0;
	int retval = 0;
	retval = pthread_create(&(tracker->thread), NULL, measure_power, tracker);
	tracker->power_chip[0] = 0;
	if(retval != 0)
	{
		free(tracker->power_core);
		free(tracker->power_mc);
		free(tracker->time);
		free(tracker);
		tracker = NULL;
	}
	return tracker;
}

void
drake_platform_power_begin(drake_power_t tracker)
{
	sem_post(&tracker->run);
}

size_t
drake_platform_power_end(drake_power_t tracker)
{
	sem_post(&tracker->run);
  	pthread_join(tracker->thread, NULL);
	return tracker->collected;
}

FILE*
drake_platform_power_printf_line_cumulate(FILE* stream, drake_power_t tracker, size_t i, int metrics, char *separator)
{
#if MANAGE_CPU && MONITOR_POWER
	double line = 0;
	if((metrics & (1 << DRAKE_POWER_CHIP)) != 0)
	{
		line += tracker->power_chip[i];
	}

	if((metrics & (1 << DRAKE_POWER_MEMORY_CONTROLLER)) != 0)
	{
		line += tracker->power_mc[i];
	}

	if((metrics & (1 << DRAKE_POWER_CORE)) != 0)
	{
		line += tracker->power_core[i];
	}

	fprintf(stream, "%lf%s%f", tracker->time[i].time, separator, line);
#else
	fprintf(stream, "%lf%s0", tracker->time[i].time, separator);
#endif
	return stream;
}

FILE*
drake_platform_power_printf_cumulate(FILE* stream, drake_power_t tracker, int metrics, char *separator)
{
	size_t i;
	for(i = 0; i < (tracker->collected > tracker->max_samples ? tracker->max_samples : tracker->collected); i++)
	{
		drake_platform_power_printf_line_cumulate(stream, tracker, i, metrics, separator);
	}

	return stream;
}

FILE*
drake_platform_power_printf_line(FILE* stream, drake_power_t tracker, size_t i, char* separator)
{
	if(separator == NULL)
	{
		separator = " ";
	}

	fprintf(stream, "%lf%s", tracker->time[i].time, separator);
	if(tracker->measurement & (1 << DRAKE_POWER_CHIP) != 0)
	{
		fprintf(stream, "%f", tracker->power_chip[i]);
		
		// If there is anything else than power chip, then add a separator
		if(((tracker->measurement >> DRAKE_POWER_CHIP) << DRAKE_POWER_CHIP) & ~(int)(1 << DRAKE_POWER_CHIP) != 0)
		{
			fprintf(stream, "%s", separator);
		}
	}

	if(tracker->measurement & (1 << DRAKE_POWER_MEMORY_CONTROLLER) != 0)
	{
		fprintf(stream, "%f", tracker->power_mc[i]);
		if(((tracker->measurement >> DRAKE_POWER_MEMORY_CONTROLLER) << DRAKE_POWER_MEMORY_CONTROLLER) & ~(int)(1 << DRAKE_POWER_MEMORY_CONTROLLER) != 0)
		{
			fprintf(stream, "%s", separator);
		}
	}

	if(tracker->measurement & (1 << DRAKE_POWER_CORE) != 0)
	{
		fprintf(stream, "%f", tracker->power_core[i]);
		if(((tracker->measurement >> DRAKE_POWER_CORE) << DRAKE_POWER_CORE) & ~(int)(1 << DRAKE_POWER_CORE) != 0)
		{
			fprintf(stream, "%s", separator);
		}
	}
	return stream;
}

FILE*
drake_platform_power_printf(FILE* stream, drake_power_t tracker, char *separator)
{
	if(separator == NULL)
	{
		separator = " ";
	}

	size_t i;
	for(i = 0; i < (tracker->collected > tracker->max_samples ? tracker->max_samples : tracker->collected); i++)
	{
		drake_platform_power_printf_line(stream, tracker, i, separator);
		fprintf(stream, "\n");
	}
	return stream;
}

void
drake_platform_power_destroy(drake_power_t tracker)
{
	if(tracker->running != 0)
	{
		drake_platform_power_end(tracker);
	}

	free(tracker->power_chip);
	free(tracker->power_mc);
	free(tracker->power_core);
	free(tracker->time);
	free(tracker);
}

