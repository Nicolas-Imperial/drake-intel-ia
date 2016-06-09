#include <stddef.h>

#ifndef SYSFS_H
#define SYSFS_H

#define SYSFS_PREFIX "/sys/"

struct simple_set
{
	size_t size;
	int *member;
};
typedef struct simple_set simple_set_t;

enum simple_set_parser_state { BEGIN_NUMBER, NUMBER, BEGIN_INTERVAL, INTERVAL };

struct sysfs_attr
{
	char *device;
	char **data;
	int fd;
	size_t size, *length;
};
typedef struct sysfs_attr *sysfs_attr_tp;

simple_set_t parse_simple_set(char* filename);

sysfs_attr_tp sysfs_attr_open_rw(char* device, char** values, size_t size);
sysfs_attr_tp sysfs_attr_open_ro(char* device);
void sysfs_attr_write(sysfs_attr_tp attr, size_t value);
void sysfs_attr_write_str(sysfs_attr_tp attr, char* str);
void sysfs_attr_write_buffer(sysfs_attr_tp attr, void*, size_t);
size_t sysfs_attr_read(sysfs_attr_tp attr, char*, size_t);
char* sysfs_attr_read_alloc(sysfs_attr_tp attr);
void sysfs_attr_close(sysfs_attr_tp attr);

#endif
