
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z3ds.h"
#include "z3ds_impl.h"

#include "../error.h"

static int zstd_read(void* opaque, void* buffer, size_t n) {
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)opaque;
    u32 bytesRead = 0;
    Result res = FSFILE_Read(file->handle, &bytesRead, file->current_pos, buffer, n);
    if (R_FAILED(res) || bytesRead != n) {
        return -1;
    }
    file->current_pos += n;
    return 0;
}

static int zstd_seek(void* opaque, long long offset, int origin) {
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)opaque;
    Z3DSFileHeader* header = (Z3DSFileHeader*)file->header;

    s64 start = 0;
    switch (origin) {
    case SEEK_SET:
        start = (s64)(header->metadata_size + header->header_size);
        break;
    case SEEK_CUR:
        start = (s64)file->current_pos;
        break;
    case SEEK_END:
        start = (s64)((header->compressed_size + header->metadata_size) + header->header_size);
        break;
    default:
        break;
    }
    s64 new_pos = start + offset;
    if (new_pos < 0)
        return -1;
    file->current_pos = new_pos;
    return 0;
}

bool Z3DS_IsFile(Handle handle, const void *underlying_magic, size_t underlying_magic_size)
{
    Z3DSFileHeader header;
    size_t msize = (underlying_magic_size > 4) ? 4 : underlying_magic_size;
    u32 bytesRead = 0;
    return R_SUCCEEDED(FSFILE_Read(handle, &bytesRead, 0, &header, sizeof(Z3DSFileHeader))) && 
            bytesRead == sizeof(Z3DSFileHeader) &&
            memcmp(header.magic, Z3DS_MAGIC, 4) == 0 &&
            header.version == Z3DS_VERSION &&
            memcmp(header.underlying_magic, underlying_magic, msize) == 0;
}

Z3DS_FILE *Z3DS_Create(Handle handle)
{
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)malloc(sizeof(Z3DS_FILE_IMPL));
    if (!file) {
        return file;
    }

    file->handle = handle;
    file->header = (Z3DSFileHeader*)malloc(sizeof(Z3DSFileHeader));
    Z3DSFileHeader* header = file->header;
    if (!file->header) {
        Z3DS_Free((Z3DS_FILE*)file, false);
        return NULL;
    }

    u32 bytesRead = 0;
    if (
        R_FAILED(FSFILE_Read(file->handle, &bytesRead, 0, header, sizeof(Z3DSFileHeader))) || bytesRead != sizeof(Z3DSFileHeader) ||
        memcmp(header->magic, Z3DS_MAGIC, 4) != 0 ||
        header->version != Z3DS_VERSION
    ) {
        Z3DS_Free((Z3DS_FILE*)file, false);
        return NULL;
    }

    file->metadata = NULL;
    if (header->metadata_size) {
        file->metadata = (u8*)malloc(header->metadata_size);
        if (!file->metadata || R_FAILED(FSFILE_Read(file->handle, &bytesRead, header->header_size, file->metadata, header->metadata_size)) || bytesRead != header->metadata_size) {
            Z3DS_Free((Z3DS_FILE*)file, false);
            return NULL;
        }
    }

    ZSTD_seekable_customFile custom_file;
    custom_file.opaque = file;
    custom_file.read = zstd_read;
    custom_file.seek = zstd_seek;

    file->seekable = ZSTD_seekable_create();
    size_t init_result = ZSTD_seekable_initAdvanced(file->seekable, custom_file);
    if (ZSTD_isError(init_result)) {
        Z3DS_Free((Z3DS_FILE*)file, false);
        return NULL;
    }

    return (Z3DS_FILE*)file;
}

Result Z3DS_GetUncompressedSize(Z3DS_FILE *file_user, u64* sizeOut)
{
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)file_user;
    if (!file) {
        return R_APP_INVALID_ARGUMENT;
    }

    *sizeOut = file->header->uncompressed_size;
    return 0;
}

Result Z3DS_Read(Z3DS_FILE *file_user, u32 *bytesRead, u64 offset, void *buffer, u32 size)
{
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)file_user;
    if (!file) {
        return R_APP_INVALID_ARGUMENT;
    }

    size_t result = ZSTD_seekable_decompress(file->seekable, buffer, size, offset);
    if (ZSTD_isError(result)) {
        return R_APP_BAD_DATA;
    }
    *bytesRead = (u32)result;
    return 0;
}

void Z3DS_Free(Z3DS_FILE *file_user, bool closeHandle)
{
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)file_user;
    if (file) {
        if (closeHandle) {
            FSFILE_Close(file->handle);
        }
        if (file->seekable) ZSTD_seekable_free(file->seekable);
        if (file->header) free(file->header);
        if (file->metadata) free(file->metadata);
        free(file);
    }
}

Z3DS_Metadata_Item Z3DS_GetMetadataItem(Z3DS_FILE *file_user, const char *metadata_name)
{
    Z3DS_FILE_IMPL* file = (Z3DS_FILE_IMPL*)file_user;
    Z3DS_Metadata_Item ret = {0};
    
    if (!file->metadata || file->metadata[0] != Z3DS_METADATA_VERSION) {
        return ret;
    }

    size_t md_name_len = strlen(metadata_name);

    u8* start = file->metadata + 1;
    u8* end = file->metadata + file->header->metadata_size;

    while (start < end) {
        Z3DSMetadataItemHeader it_header;
        memcpy(&it_header, start, sizeof(Z3DSMetadataItemHeader));
        start += sizeof(Z3DSMetadataItemHeader);

        if (it_header.type == Z3DS_MD_TYPE_END) {
            break;
        }
        if (it_header.type != Z3DS_MD_TYPE_BINARY) {
            start += (u32)it_header.name_len + (u32)it_header.data_len;
            continue;
        }

        u8* name = start;
        u8* value = start + it_header.name_len;
        start += (u32)it_header.name_len + (u32)it_header.data_len;
        if (it_header.name_len == md_name_len && memcmp(name, metadata_name, md_name_len) == 0) {
            ret.found = true;
            ret.value = value;
            ret.size = it_header.data_len;
            break;
        }
    }
    return ret;
}
