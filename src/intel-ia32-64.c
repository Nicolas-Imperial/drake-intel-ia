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

#include <drake/platform.h>
#if 0
#define debug(var) printf("[%s:%s:%d:CORE %zu] %s = \"%s\"\n", __FILE__, __FUNCTION__, __LINE__, drake_core(), #var, var); fflush(NULL)
#define debug_addr(var) printf("[%s:%s:%d:CORE %zu] %s = \"%X\"\n", __FILE__, __FUNCTION__, __LINE__, drake_core(), #var, var); fflush(NULL)
#define debug_int(var) printf("[%s:%s:%d: CORE %zu] %s = \"%d\"\n", __FILE__, __FUNCTION__, __LINE__, drake_core(), #var, var); fflush(NULL)
#define debug_size_t(var) printf("[%s:%s:%d: CORE %zu] %s = \"%zu\"\n", __FILE__, __FUNCTION__, __LINE__, drake_core(), #var, var); fflush(NULL)
#else
#define debug(var)
#define debug_addr(var)
#define debug_int(var)
#define debug_size_t(var)
#endif

typedef struct args
{
	size_t argc;
	char **argv;
} intel_args_t;

#define VOLTAGE_LOW 3.296
#define VOLTAGE_HIGH 3.9
static int *frequency;
static float *voltage;

// Required for RCCE power and frequency scaling

struct drake_time {
	double time;
};
typedef struct drake_time *drake_time_t;

int drake_arch_init(void* obj)
{
	return 0;
}

int drake_arch_finalize(void* obj)
{
	return 0;
}

volatile void* drake_arch_alloc(size_t size, int core)
{
	// Not implemented
	fprintf(stderr, "Not implemented for SCC: %s(size_t size, int core)\n", __FUNCTION__);
	abort();
	return NULL;
}

int drake_arch_free(volatile void* addr)
{
	// Not implemented
	fprintf(stderr, "Not implemented for SCC: %s(void* addr)\n", __FUNCTION__);
	abort();
	return 0;
}

int drake_arch_pull(volatile void* addr)
{
	(void*)(addr);
	pelib_scc_cache_invalidate();

	// This cannot go wrong
	return 1;
}

int drake_arch_commit(volatile void* addr)
{
	(void*)(addr);
	// Wherever you wrote, just invalidate the line
	pelib_scc_force_wcb();

	// This cannot go wrong
	return 1;
}

size_t
drake_core()
{
	return 0;
}

size_t
drake_core_size()
{
	return 0;
}

size_t
drake_core_max()
{
	return 0;
}

void
drake_barrier(void* channel)
{
}

void
drake_exclusive_begin()
{
}

void
drake_exclusive_end()
{
}

void*
drake_local_malloc(size_t size)
{
	return NULL;
}

void
drake_local_free(void *addr)
{
}

void*
drake_private_malloc(size_t size)
{
	return NULL;
}

void
drake_private_free(void *addr)
{
}

void*
drake_remote_addr(void* addr, size_t core)
{
	return NULL;
}

size_t
drake_arch_store_size()
{
	return 0;
}

size_t
drake_arch_local_size()
{
	return 0;
}

int
drake_arch_set_frequency(int freq /* in KHz */)
{
	return 1;
}

int
drake_arch_set_frequency_autoscale(int frequency /* in KHz */)
{
	fprintf(stderr, "[%zu][ERROR] %s: not implemented.\n", drake_core(), __FUNCTION__);
	return 0;
}

int
drake_arch_set_voltage(float voltage /* in volts */)
{
	return 0;
}

int
drake_arch_set_voltage_frequency(int freq /* in KHz */)
{
	return 1;
}

int
drake_arch_get_frequency() /* in KHz */
{
	return 0;
}

float
drake_arch_get_voltage() /* in volts */
{
	return 0;
}

int drake_get_time(drake_time_t container)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	container->time = t.tv_sec * 1e3 + t.tv_nsec / 1e6;
	//rdtsc() / drake_arch_get_frequency();
	return 1;
}

int drake_time_substract(drake_time_t res, drake_time_t t1, drake_time_t t2)
{
	res->time = t1->time - t2->time;
	return 1;
}

int drake_time_add(drake_time_t res, drake_time_t t1, drake_time_t t2)
{
	res->time = t1->time + t2->time;
	return 1;
}

int drake_time_greater(drake_time_t t1, drake_time_t t2)
{
	return t1->time > t2->time;
}

int drake_time_equals(drake_time_t t1, drake_time_t t2)
{
	return t1->time == t2->time;
}

int drake_time_init(drake_time_t t, double ms)
{
	t->time = ms;
	return 1;
}

int drake_arch_sleep(drake_time_t period)
{
	// Todo: improve with a proper sleep() system call
	struct drake_time now, until;
	drake_time_t now_p = &now, until_p = &until;

	drake_get_time(now_p);
	until_p->time = now_p->time + period->time;
	while(!drake_time_greater(now_p, until_p))
	{
		drake_get_time(now_p);
	}

	return 1;
}

drake_time_t drake_time_alloc()
{
	return malloc(sizeof(drake_time_t));
}

FILE*
drake_time_printf(FILE *stream, drake_time_t time)
{
	fprintf(stream, "%f", time->time);
	return stream;
}

void
drake_time_destroy(drake_time_t time)
{
	free(time);
}

struct drake_power {
	double *power_chip, *power_core, *power_mc, *time;
	size_t max_samples;
	size_t collected;
	sem_t run;
	int running;
	int measurement;
	pthread_t thread;
};
typedef struct drake_power *drake_power_t;

// Measurement enabled for SCC_3V3 (fixed supply voltage for chip. Not really expected to change, but might fluctuate slightly), 

void*
measure_power(void* arg)
{
	return NULL;
}

drake_power_t
drake_platform_power_init(size_t samples, int measurement)
{
	drake_power_t tracker = malloc(sizeof(struct drake_power));
	tracker->power_chip = malloc(sizeof(double) * samples);
	tracker->power_mc = malloc(sizeof(double) * samples);
	tracker->power_core = malloc(sizeof(double) * samples);
	tracker->time = malloc(sizeof(double) * samples);
	double *stuff = malloc(sizeof(double) * samples);
	tracker->max_samples = samples;
	tracker->collected = 0;
	tracker->running = 0;
	tracker->measurement = measurement;

	sem_init(&tracker->run, 0, 0);
	int retval = pthread_create(&(tracker->thread), NULL, measure_power, tracker);
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

	fprintf(stream, "%f%s%f", tracker->time[i], separator, line);
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

	fprintf(stream, "%f%s", tracker->time[i], separator);
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

