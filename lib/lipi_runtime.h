#ifndef LIPI_RUNTIME_H
#define LIPI_RUNTIME_H

#include <stdint.h>

typedef int8_t lipi_i8;
typedef int16_t lipi_i16;
typedef int32_t lipi_i32;
typedef long lipi_i64;
typedef uint8_t lipi_u8;
typedef uint16_t lipi_u16;
typedef uint32_t lipi_u32;
typedef unsigned long lipi_u64;
typedef long lipi_bool;
typedef void *lipi_ptr;
typedef const char *lipi_str;

struct lipi_array {
    long len;
    long *items;
};

typedef struct lipi_array lipi_list;

struct lipi_map_entry {
    const char *key;
    long value;
};

struct lipi_map {
    long len;
    struct lipi_map_entry *entries;
};

#define LIPI_ARRAY_LEN_OFFSET 0
#define LIPI_ARRAY_ITEMS_OFFSET 8
#define LIPI_MAP_LEN_OFFSET 0
#define LIPI_MAP_ENTRIES_OFFSET 8
#define LIPI_MAP_ENTRY_KEY_OFFSET 0
#define LIPI_MAP_ENTRY_VALUE_OFFSET 8

#endif
