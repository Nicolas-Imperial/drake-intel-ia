#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "msr.h"

#define MSR_DISABLE_BOOST 0x4000850089
#define MSR_DISABLE_BOOST_OFFSET 0x1a0

msr_attr_tp
msr_attr_open(char* device)
{
#if MANAGE_TURBO_BOOST
	msr_attr_tp obj = malloc(sizeof(struct msr_attr));
	obj->fd = open(device, O_RDWR);
	if(obj->fd == -1)
	{
		perror("Error while opening MSR device");
		free(obj);
		return NULL;
	}

	
	if(pread(obj->fd, &obj->state_boost, sizeof(uint64_t), MSR_DISABLE_BOOST_OFFSET) != sizeof(uint64_t))
	{
		perror("Cannot read current value of turbo boost setting");
	}
	//printf("[%s:%s:%d] %016lX\n", __FILE__, __FUNCTION__, __LINE__, obj->state_boost);

	return obj;
#else
	return NULL;
#endif
}

void
msr_attr_close(msr_attr_tp attr)
{
#if MANAGE_TURBO_BOOST
	//printf("[%s:%s:%d] %016lX\n", __FILE__, __FUNCTION__, __LINE__, attr->state_boost);
	if(pwrite(attr->fd, &attr->state_boost, sizeof(uint64_t), MSR_DISABLE_BOOST_OFFSET) != sizeof(uint64_t))
	{
		perror("Cannot restore od msr value");
	}
	close(attr->fd);
	free(attr);
#endif
}

int msr_turbo_boost_disable(msr_attr_tp attr)
{
#if MANAGE_TURBO_BOOST
	uint64_t data = attr->state_boost | ((uint64_t)1 << 38);
	//printf("[%s:%s:%d] %016lX\n", __FILE__, __FUNCTION__, __LINE__, data);
	if(pwrite(attr->fd, &data, sizeof(uint64_t), MSR_DISABLE_BOOST_OFFSET) != sizeof(uint64_t))
	{
		perror("Error while disabling intel boost");
		return -1;
	}
#endif

	return 0;
}

int msr_turbo_boost_enable(msr_attr_tp attr)
{
#if MANAGE_TURBO_BOOST
	uint64_t data = attr->state_boost & ~((uint64_t)1 << 38);
	//printf("[%s:%s:%d] %016lX\n", __FILE__, __FUNCTION__, __LINE__, data);
	if(pwrite(attr->fd, &data, sizeof(uint64_t), MSR_DISABLE_BOOST_OFFSET) != sizeof(uint64_t))
	{
		perror("Error while disabling intel boost");
		return -1;
	}
#endif
	return 0;
}

