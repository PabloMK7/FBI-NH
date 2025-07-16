#pragma once

#include <3ds.h>

typedef struct {
    u8 magic[4];
    u8 underlying_magic[4];
    u8 version;
    u8 reserved;
    u16 header_size;
    u32 metadata_size;
    u64 compressed_size;
    u64 uncompressed_size;
} Z3DSFileHeader;

enum {
    Z3DS_MD_TYPE_END = 0,
    Z3DS_MD_TYPE_BINARY = 1,
};

typedef struct {
    u8 type;
    u8 name_len;
    u16 data_len;
} Z3DSMetadataItemHeader;

#define Z3DS_MAGIC "Z3DS"
#define Z3DS_VERSION 1
#define Z3DS_METADATA_VERSION 1

typedef struct {
    Z3DSFileHeader* header;
    u8* metadata;
    ZSTD_seekable* seekable;

    Handle handle;

    u64 current_pos;
} Z3DS_FILE_IMPL;