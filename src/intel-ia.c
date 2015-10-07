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

#ifdef debug
#undef debug
#undef debug_int
#undef debug_addr
#undef debug_size_t
#endif

#if 1
#define debug(var) printf("[%s:%s:%d:CORE %zu] %s = \"%s\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_addr(var) printf("[%s:%s:%d:CORE %zu] %s = \"%p\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_int(var) printf("[%s:%s:%d: CORE %zu] %s = \"%d\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#define debug_size_t(var) printf("[%s:%s:%d: CORE %zu] %s = \"%zu\"\n", __FILE__, __FUNCTION__, __LINE__, drake_platform_core_id(), #var, var); fflush(NULL)
#else
#define debug(var)
#define debug_addr(var)
#define debug_int(var)
#define debug_size_t(var)
#endif

#define DRAKE_IA_LINE_SIZE 64
#define DRAKE_IA_SHARED_SIZE (256 * 1024)

enum phase { DRAKE_IA_CREATE, DRAKE_IA_INIT, DRAKE_IA_RUN, DRAKE_IA_DESTROY };

struct drake_time {
	double time;
};
typedef struct drake_time *drake_time_t;

typedef struct mem_block
{
	void* space;			// pointer to space for data in block             
	size_t free_size;		// actual free space in block (0 or whole block)  
	struct mem_block *next;		// pointer to next block in circular linked list 
} mem_block_t;

typedef struct
{
	mem_block_t *tail;		// "last" block in linked list of blocks
} mem_queue_t;

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
};
typedef struct drake_platform *drake_platform_t;

static size_t core_size;
static __thread size_t core_id = 0;
void **shared_buffer;
mem_queue_t **shared;
pthread_barrier_t barrier;

static
ia_arguments_t
parse_ia_arguments(size_t argc, char **argv)
{
	ia_arguments_t args;

	for(; argv[0] != NULL; argv++)
	{
		if(strcmp(argv[0], "--size") == 0)
		{
			argv++;
			if(argv[0] != NULL)
			{
				args.size = atoi(argv[0]);
			}
			continue;
		}
	}

	return args;
}

static
void
ia_malloc_init(mem_queue_t* spacep, void* mem, size_t size)
{
	spacep->tail = (mem_block_t*) malloc(sizeof(mem_block_t));
	spacep->tail->free_size = size;
	spacep->tail->space = mem;
	/* make a circular list by connecting tail to itself */
	spacep->tail->next = spacep->tail;
}

static
void*
ia_malloc(mem_queue_t *spacep, size_t size, int aligned)
{
	// simple memory allocator, loosely based on public domain code developed by
	// Michael B. Allen and published on "The Scripts--IT /Developers Network".
	// Approach: 
	// - maintain linked list of pointers to memory. A block is either completely
	//	malloced (free_size = 0), or completely free (free_size > 0).
	//	The space field always points to the beginning of the block
	// - malloc: traverse linked list for first block that has enough space
	// - free: Check if pointer exists. If yes, check if the new block should be
	//	merged with neighbors. Could be one or two neighbors.

	mem_block_t *b1, *b2, *b3;	 // running pointers for blocks

	if (size == 0) return NULL;

	// always first check if the tail block has enough space, because that
	// is the most likely. If it does and it is exactly enough, we still
	// create a new block that will be the new tail, whose free space is
	// zero. This acts as a marker of where free space of predecessor ends
	b1 = spacep->tail;
	// If we request aligned memory, then it's ok if the tail free memory can handle the size requested. Otherwise, we need to check if the tail can handle the size requested even after skipping as many bytes as necessary to get some aligned address.
	if ((b1->free_size >= size && !aligned) || (b1->free_size - DRAKE_IA_LINE_SIZE + ((size_t)b1->space % DRAKE_IA_LINE_SIZE) >= size && aligned)) {
		// need to insert new block; new order is: b1->b2 (= new tail)
		b2 = (mem_block_t*) malloc(sizeof(mem_block_t));
		b2->next = b1->next;
		b1->next = b2;
		b2->free_size = b1->free_size - size;
		b2->space  = b1->space + size;
		// need to update the tail
		spacep->tail = b2;

		if(!aligned || ((size_t)b1->space % DRAKE_IA_LINE_SIZE) == 0)
		{
			b1->free_size = 0;
			return b1->space;
		}
		else
		{
			b2 = (mem_block_t*) malloc(sizeof(mem_block_t));
			b2->next = b1->next;
			b1->next = b2;
			b2->space = b1->space + DRAKE_IA_LINE_SIZE - ((size_t)b1->space % DRAKE_IA_LINE_SIZE);
			b1->free_size = b1->free_size - DRAKE_IA_LINE_SIZE + ((size_t)b1->space % DRAKE_IA_LINE_SIZE);
			b2->free_size = 0;
			return b2->space;
		}
	}

	// tail didn't have enough space; loop over whole list from beginning
	// As for above, stop at the first free space big enough to handle the request if no aligned memory is requested
	// Otherwise, check each space after memory alignment
	while (((b1->next->free_size < size) && !aligned) || ((b1->next->free_size - DRAKE_IA_LINE_SIZE + ((size_t)b1->next->space % DRAKE_IA_LINE_SIZE)< size) && aligned))
	{
		if (b1->next == spacep->tail)
		{
			return NULL; // we came full circle
		}
		b1 = b1->next;
	}

	b2 = b1->next;
	// If memory alignment is requested and the block is bigger than requested, then we need to skip some bytes before allocating
	// If not, then we know that the free block is already aligned
	if (b2->free_size > size && aligned)
	{
		b3 = (mem_block_t*) malloc(sizeof(mem_block_t));
		b3->next = b2->next;
		b2->next = b3;
		b3->free_size = b2->free_size - DRAKE_IA_LINE_SIZE + ((size_t)b2->space % DRAKE_IA_LINE_SIZE);
		b2->free_size = b2->free_size - b3->free_size;
		b3->space = b2->space + DRAKE_IA_LINE_SIZE - ((size_t)b2->space % DRAKE_IA_LINE_SIZE);
		b2 = b3;
	}

	// If, perhaps after having split b2 into b2 and b3, b2 is still too big, then we need to create yet another block.
	if (b2->free_size > size)
	{
		// split block; new block order: b1->b2->b3
		b3 = (mem_block_t*) malloc(sizeof(mem_block_t));
		b3->next = b2->next; // reconnect pointers to add block b3
		b2->next = b3;
		b3->free_size = b2->free_size - size; // b3 gets remainder free space
		b3->space = b2->space + size; // need to shift space pointer
	}

	// Now b2 is the exact size and aligned as requested
	b2->free_size = 0; // block b2 is completely used
	return b2->space;
}

static
void
ia_free(mem_queue_t *spacep, void *ptr)
{
	mem_block_t *b1, *b2, *b3;	// running block pointers
	int j1, j2;			// booleans determining merging of blocks

	// loop over whole list from the beginning until we locate space ptr
	b1 = spacep->tail;
	while (b1->next->space != ptr && b1->next != spacep->tail)
	{
		b1 = b1->next;
	}

	// b2 is target block whose space must be freed
	b2 = b1->next;
	// tail either has zero free space, or hasn't been malloc'ed
	if (b2 == spacep->tail)
	{
		return;
	}

	// reset free space for target block (entire block)
	b3 = b2->next;
	b2->free_size = b3->space - b2->space;

	// determine with what non-empty blocks the target block can be merged
	j1 = (b1->free_size > 0 && b1 != spacep->tail); // predecessor block
	j2 = (b3->free_size > 0 || b3 == spacep->tail); // successor block

	if (j1)
	{
		if (j2)
		{
			// splice all three blocks together: (b1,b2,b3) into b1
			b1->next = b3->next;
			b1->free_size += b3->free_size + b2->free_size;
			if (b3 == spacep->tail)
			{
				spacep->tail = b1;
			}
			free(b3);
		}
		else
		{
			// only merge (b1,b2) into b1
			b1->free_size += b2->free_size;
			b1->next = b3;
		}
		free(b2);
	}
	else
	{
		if (j2)
		{
			// only merge (b2,b3) into b2
			b2->next = b3->next;
			b2->free_size += b3->free_size;
			if (b3 == spacep->tail)
			{
				spacep->tail = b2;
			}
			free(b3);
		}
	}
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
	shared_buffer[core_id] = malloc(DRAKE_IA_SHARED_SIZE);

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
		ia_malloc_init(&shared[core_id][i], shared_buffer[i], DRAKE_IA_SHARED_SIZE);
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
				handler->stream[core_id] = drake_stream_create_explicit(handler->schedule_init, handler->schedule_destroy, handler->task_function);
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

	core_size = args.size;
	
	drake_thread_init_t *init = malloc(sizeof(drake_thread_init_t) * core_size);

	shared_buffer = malloc(sizeof(void*) * core_size);
	shared = malloc(sizeof(mem_queue_t) * core_size);
	stream->pthread = drake_platform_private_malloc(sizeof(pthread_t) * core_size);
	stream->stream = drake_platform_private_malloc(sizeof(drake_stream_t) * core_size);
	stream->success = drake_platform_private_malloc(sizeof(int) * core_size);
	pthread_barrier_init(&stream->work_notify, NULL, core_size);
	pthread_barrier_init(&barrier, NULL, core_size);
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
	size_t i;
	stream->task_args = arg;
	stream->phase = DRAKE_IA_INIT;
	sem_post(&stream->new_order);
	sem_wait(&stream->ready);

	int success = 1;
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

void
drake_platform_stream_destroy(drake_platform_t stream)
{
	stream->phase = DRAKE_IA_DESTROY;
	sem_post(&stream->new_order);
	sem_wait(&stream->ready);
}

int
drake_platform_destroy(drake_platform_t stream)
{
	pthread_barrier_destroy(&stream->work_notify);
	pthread_barrier_destroy(&barrier);
	sem_destroy(&stream->new_order);
	sem_destroy(&stream->ready);
	sem_destroy(&stream->report_ready);
	drake_platform_private_free(stream->pthread);
	drake_platform_private_free(stream->stream);
	drake_platform_private_free(stream->success);

	return 0;
}

volatile void* drake_platform_shared_malloc(size_t size, size_t core)
{
	volatile void* addr = ia_malloc(&shared[core_id][core], size, 0);
	return addr;
}

int drake_platform_shared_free(volatile void* addr, size_t core)
{
	ia_free(&shared[core_id][core], (void*)addr);
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
	return core_size;
}

void
drake_barrier(void* channel)
{
	pthread_barrier_wait(&barrier);
}

void
drake_exclusive_begin()
{
	printf("[%s:%s:%d][Error] Not implemented\n", __FILE__, __FUNCTION__, __LINE__);
	abort();
}

void
drake_exclusive_end()
{
	printf("[%s:%s:%d][Error] Not implemented\n", __FILE__, __FUNCTION__, __LINE__);
	abort();
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
drake_platform_set_voltage_frequency(int freq /* in KHz */)
{
	return 1;
}

int
drake_platform_get_frequency() /* in KHz */
{
	return 0;
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

int drake_platform_time_substract(drake_time_t res, drake_time_t t1, drake_time_t t2)
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
	while(!drake_platform_time_greater(now_p, until_p))
	{
		drake_platform_time_get(now_p);
	}

	return 1;
}

drake_time_t drake_platform_time_alloc()
{
	return malloc(sizeof(drake_time_t));
}

FILE*
drake_platform_time_printf(FILE *stream, drake_time_t time)
{
	fprintf(stream, "%f", time->time);
	return stream;
}

void
drake_platform_time_destroy(drake_time_t time)
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

