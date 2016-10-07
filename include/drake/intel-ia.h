#ifndef DRAKE_INTEL_IA
#define DRAKE_INTEL_IA

struct ia_arguments
{
	int poll_at_idle, wait_after_scaling;
	int num_cores;
};
typedef struct ia_arguments ia_arguments_t;

#endif
