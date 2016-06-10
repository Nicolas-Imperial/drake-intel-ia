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

#include "sysfs.h"

#define debug(var) printf("[%s:%s:%d] %s = \"%s\"\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_int(var) printf("[%s:%s:%d] %s = %d\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_long(var) printf("[%s:%s:%d] %s = %ld\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_size_t(var) printf("[%s:%s:%d] %s = %zu\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_addr(var) printf("[%s:%s:%d] %s = %p\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);
#define debug_char(var) printf("[%s:%s:%d] %s = %c\n", __FILE__, __FUNCTION__, __LINE__, #var, var); fflush(stdout);

#define SYSFS_BLOCK_SIZE 32

static void
error(char* message)
{
	fprintf(stderr, "%s\n", message);
	abort();
}

static void
parsing_error(char* input, size_t len, char* message)
{
	size_t i;
	printf("%s\n", input);
	for(i = 0; i < len; i++)
	{
		printf(" ");
	}
	printf("^\n");
	error(message);
}

simple_set_t
parse_simple_set(char* filename)
{
	char car;

	simple_set_t set;
	set.size = 0;
	set.member = NULL;

	char *input = NULL;
	size_t counter = 0;
	int min, max, value;
	enum simple_set_parser_state state = BEGIN_NUMBER;

	int fd = open(filename, O_RDONLY); 
	int num = read(fd, &car, 1);
	for(; num > 0; num = read(fd, &car, 1))
	{
		// Do not count end of input characters as input
		if(car != 10 && car != '\0')
		{
			input = realloc(input, counter + 1);
			sprintf(input + counter, "%c", car);
			counter++;
		}

		switch(state)
		{
			case BEGIN_NUMBER:
			{
				if(car < '0' || car > '9')
				{
					parsing_error(input, counter, "A number can only be composed of integer digits.");
				}
				else if(car == 10 || car == '\0')
				{
					// End of input, do nothing
					break;
				}
				value = car - '0';
				state = NUMBER;
			}
			break;
			case NUMBER:
			{
				if(car < '0' || car > '9')
				{
					if(car == ',' || car == 10 || car == '\0')
					{
						set.member = realloc(set.member, sizeof(int) * (set.size + 1));
						if(set.member == NULL)
						{
							error("Failed to reallocate memory");
						}
						set.member[set.size] = value;
						set.size++;
						state = BEGIN_NUMBER;
						break;
					}
					else if(car == '-')
					{
						min = value;
						state = BEGIN_INTERVAL;
						break;
					}
					else
					{
						parsing_error(input, counter, "Expected an integer digit 0-9, a comma or a dash.");
					}
				}

				value = value * 10;
				value += car - '0';
			}
			break;
			case BEGIN_INTERVAL:
			{
				if(car < '0' || car > '9')
				{
					parsing_error(input, counter, "A number can only be composed of integer digits.");
				}
				value = car - '0';
				state = INTERVAL;
			}
			break;
			case INTERVAL:
			{
				if(car < '0' || car > '9')
				{
					if(car == ',' || car == 10 || car == '\0')
					{
						max = value;
						set.member = realloc(set.member, sizeof(int) * (set.size + (max - min + 1)));
						if(set.member == NULL)
						{
							error("Failed to reallocate memory");
						}
						size_t i;
						for(i = min; i <= max; i++)
						{
							set.member[set.size + i - min] = i;
						}
						set.size += max - min + 1;
						state = BEGIN_NUMBER;
						break;
					}
					else if(car == '-')
					{
						parsing_error(input, counter, "An interval can be composed of only one dash.");
					}
					else
					{
						parsing_error(input, counter, "Expected an integer digit 0-9 or a comma.");
					}
				}
				value = value * 10;
				value += car - '0';	
			}
			break;
			default:
			{
				parsing_error(input, counter, "Illegal parsing state. Aborting.\n");
			}
		}
	}
	close(fd);

	return set;
}

sysfs_attr_tp
sysfs_attr_open_rw(char* device, char** values, size_t size)
{
	sysfs_attr_tp attr = malloc(sizeof(struct sysfs_attr));
	attr->device = malloc(sizeof(char) * (strlen(SYSFS_PREFIX) + strlen(device) + 1));
	strcpy(attr->device, SYSFS_PREFIX);
	strcpy(attr->device + strlen(SYSFS_PREFIX), device);
	//debug(attr->device);
	attr->fd = open(attr->device, O_RDWR | O_DSYNC | O_TRUNC);
	// Check if everything went alright
	if(attr->fd == -1)
	{
		fprintf(stderr, "file: %s\n", device);
		perror("Error while opening sysfs device");
		abort();
	}

	// Copy possible values
	attr->data = malloc(sizeof(char*) * size);
	attr->length = malloc(sizeof(size_t) * size);
	attr->size = size;
	size_t i;
	for(i = 0; i < size; i++)
	{
		attr->length[i] = strlen(values[i] + 1);
		attr->data[i] = malloc(attr->length[i]);
		strcpy(attr->data[i], values[i]);
	}
	return attr;
}

sysfs_attr_tp
sysfs_attr_open_ro(char* device)
{
	sysfs_attr_tp attr = malloc(sizeof(struct sysfs_attr));
	attr->device = malloc(sizeof(char) * (strlen(SYSFS_PREFIX) + strlen(device) + 1));
	strcpy(attr->device, SYSFS_PREFIX);
	strcpy(attr->device + strlen(SYSFS_PREFIX), device);
	attr->fd = open(attr->device, O_RDONLY | O_DSYNC);

	// Check if everything went alright
	if(attr->fd == -1)
	{
		perror("Error while opening sysfs device");
		fprintf(stderr, "file: %s\n", device);
		abort();
	}

	// Copy possible values
	attr->data = NULL;
	attr->length = NULL;
	attr->size = 0;

	return attr;
}

inline
void
sysfs_attr_write(sysfs_attr_tp attr, size_t value)
{
	write(attr->fd, attr->data[value], attr->length[value]);
	lseek(attr->fd, 0, SEEK_SET);
}

void
sysfs_attr_write_str(sysfs_attr_tp attr, char* str)
{
	write(attr->fd, str, strlen(str));
	lseek(attr->fd, 0, SEEK_SET);
}

void
sysfs_attr_write_buffer(sysfs_attr_tp attr, void* buffer, size_t size)
{
	write(attr->fd, buffer, size);
	lseek(attr->fd, 0, SEEK_SET);
}

char*
sysfs_attr_read_alloc(sysfs_attr_tp attr)
{
	size_t size = SYSFS_BLOCK_SIZE;
	char *buffer = malloc(sizeof(char) * size);
	size_t len;
	//while((len = read(attr->fd, buffer + size - SYSFS_BLOCK_SIZE, SYSFS_BLOCK_SIZE)) == SYSFS_BLOCK_SIZE)
	while((len = sysfs_attr_read(attr, buffer + size - SYSFS_BLOCK_SIZE, SYSFS_BLOCK_SIZE)) == SYSFS_BLOCK_SIZE)
	{
		size += SYSFS_BLOCK_SIZE;
		buffer = realloc(buffer, size);
		if(buffer == NULL)
		{
			perror("Failed to allocate memory");
		}
	}

	// Place en of string after reading string
	buffer[size - SYSFS_BLOCK_SIZE + len] = '\0';

	return buffer;
}

size_t
sysfs_attr_read(sysfs_attr_tp attr, char* buffer, size_t size)
{
	return read(attr->fd, buffer, size);
}

void
sysfs_attr_close(sysfs_attr_tp attr)
{
	close(attr->fd);

	if(attr->data != NULL)
	{
		size_t i;
		for(i = 0; i < attr->size; i++)
		{
			free(attr->data[i]);
		}
		free(attr->data);
		free(attr->length);
	}

	free(attr->device);
	free(attr);
}

