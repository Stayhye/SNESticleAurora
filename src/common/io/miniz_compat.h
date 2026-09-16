#ifndef MINIZ_COMPAT_H
#define MINIZ_COMPAT_H

/* Minimal helpers that replace the gz / zip APIs the SNESticle code
   used to call into zlib + minizip-unzip. Backed by miniz instead.
   Both helpers go through newlib stdio (fopen / fread), which after
   init_ps2_filesystem_driver() resolves through iomanX, so PS2 paths
   like cdfs:/, host:/, mass:/ and mc0:/ all work transparently. */

#ifdef __cplusplus
extern "C" {
#endif

/* AURORA_LOAD_BYTE_PROGRESS_V1_20260915
 * Progress is reported in uncompressed/output bytes. */
typedef void (*MinizProgressCallback)(
    int done, int total, void *user);

/* Reads a `.gz` file at `path`, decompresses the deflate stream into
   `out_buf`, returning the number of decompressed bytes (>0) or -1
   on any failure (open / parse / decompress). At most `out_max`
   bytes are written. */
int MinizReadGZToBuffer(const char *path,
                        void *out_buf,
                        int out_max);

int MinizReadGZToBufferProgress(const char *path,
                                void *out_buf,
                                int out_max,
                                MinizProgressCallback progress,
                                void *progress_user);

/* Opens the zip at `path`, walks the central directory and decompresses
   the first non-directory entry whose name is accepted by `name_filter`
   (or the first entry if `name_filter` is NULL) into `out_buf`.
   Returns the uncompressed byte count (>0) or -1 if no entry matched
   or any miniz / IO step failed. The matched file's name (within the
   archive) is written into `out_filename` if non-NULL, truncated to
   `filename_max` bytes incl. NUL. */
int MinizReadZipFirstMatch(const char *path,
                           void *out_buf,
                           int out_max,
                           char *out_filename,
                           int filename_max,
                           int (*name_filter)(const char *name));

/* AURORA_DYNAMIC_ROM_BUFFER_V1_20260823 */
int MinizGetGZUncompressedSize(const char *path);
int MinizReadGZPrefix(const char *path, void *out_buf, int out_max);

typedef int (*MinizZipEntryFilter)(
    const char *name, unsigned int uncompressed_size);

int MinizProbeZipFirstMatchInfo(const char *path,
                                unsigned int *out_file_index,
                                char *out_filename,
                                int filename_max,
                                MinizZipEntryFilter entry_filter);

int MinizReadZipEntryToBuffer(const char *path,
                              unsigned int file_index,
                              void *out_buf,
                              int out_max,
                              char *out_filename,
                              int filename_max);

int MinizReadZipEntryToBufferProgress(
    const char *path,
    unsigned int file_index,
    void *out_buf,
    int out_max,
    char *out_filename,
    int filename_max,
    MinizProgressCallback progress,
    void *progress_user);

int MinizReadZipEntryPrefix(const char *path,
                            unsigned int file_index,
                            void *out_buf,
                            int out_max);

#ifdef __cplusplus
}
#endif

#endif /* MINIZ_COMPAT_H */
