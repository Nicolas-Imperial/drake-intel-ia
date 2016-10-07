#include <stddef.h>
#include <stdint.h>

#ifndef DRAKE_MSR_H
#define DRAKE_MSR_H

#define DRAKE_MSR_DEVICE "/dev/cpu/%d/msr"

struct msr_attr
{
	int fd;
	uint64_t state_boost;
};
typedef struct msr_attr *msr_attr_tp;

msr_attr_tp msr_attr_open(char* device);
void msr_attr_close(msr_attr_tp attr);
int msr_turbo_boost_disable(msr_attr_tp attr);
int msr_turbo_boost_enable(msr_attr_tp attr);

#endif
