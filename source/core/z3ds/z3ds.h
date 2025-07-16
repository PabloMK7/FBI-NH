#pragma once

#include <3ds.h>
#include <zstd_seekable.h>

typedef void Z3DS_FILE;

bool Z3DS_IsFile(Handle handle, const void* underlying_magic, size_t underlying_magic_size);

Z3DS_FILE* Z3DS_Create(Handle handle);
Result Z3DS_GetUncompressedSize(Z3DS_FILE* file, u64* sizeOut);
Result Z3DS_Read(Z3DS_FILE* file, u32* bytesRead, u64 offset, void* buffer, u32 size);
void Z3DS_Free(Z3DS_FILE* file, bool closeHandle);

typedef struct {
    bool found;

    const u8* value; // Valid until Z3DS_Free is called
    u32 size;
} Z3DS_Metadata_Item;

Z3DS_Metadata_Item Z3DS_GetMetadataItem(Z3DS_FILE* file, const char* metadata_name);