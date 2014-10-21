#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include "queue.h"

#define debug(fmt, args...) do { fprintf(stderr, "SIMPLE-FS: " fmt "\n", ##args); } while (0)

/* Please make sure you have enough memory if you want to enlarge any of the
 * following parameters... */

#define MAX_FILE_N (10)
#define MAX_FILE_SIZE (1024)

#define SFS_MIN(a, b) ((a) > (b) ? (a) : (b))

struct sfs_file {
        STAILQ_ENTRY(sfs_file) sfs_link;
        char *sfs_name;
        char *sfs_data;
        int sfs_size;
};

struct sfs_ctx {
        STAILQ_HEAD(,sfs_file) sfs_files;
        int sfs_file_n;
};

static struct sfs_ctx ctx;

static int sfs_file_new (char *name, char *content, int len)
{
        struct sfs_file *new_file;
        if (ctx.sfs_file_n >= MAX_FILE_N) {
                debug("reach max file n");
                return -1;
        }
        if (len < 0 || len >= MAX_FILE_SIZE) {
                debug("reach max file size");
                return -1;
        }
        if (name[0] == '/')
                name = name + 1;
        debug("created new file: %s", name);

        new_file = malloc(sizeof(*new_file));

        if (!new_file)
                return -1;

        new_file->sfs_name = strdup(name);
        if (!new_file->sfs_name) {
                free(new_file);
                return -1;
        }

        new_file->sfs_data = malloc(len);
        if (!new_file->sfs_data) {
                free(new_file->sfs_name);
                free(new_file);
                return -1;
        }
        memcpy(new_file->sfs_data, content, len);

        new_file->sfs_size = len;

        STAILQ_INSERT_HEAD(&ctx.sfs_files, new_file, sfs_link);
        ctx.sfs_file_n++;

        debug("created new file (%d now): %s (%s)", ctx.sfs_file_n,
              new_file->sfs_name, new_file->sfs_data);

        return 0;
}

static void sfs_file_free (struct sfs_file *file)
{
        debug("destroy file: %s", file->sfs_name);
        free(file->sfs_name);
        free(file->sfs_data);
        free(file);
}

static struct sfs_file *sfs_file_lookup (char *name)
{
        struct sfs_file *file = NULL;

        if (name[0] == '/')
                /* skip the first '/' */
                name = name + 1;

        STAILQ_FOREACH(file, &ctx.sfs_files, sfs_link) {
                if (!strcmp(name, file->sfs_name))
                        return file;
        }
        return NULL;
}

static int sfs_file_read (struct sfs_file *file, char *buf, size_t size, off_t offset)
{
        size_t file_size = file->sfs_size;
        int to_read = 0;

        if (offset >= file_size)
                return 0;

        to_read = SFS_MIN(size, file_size - offset);
        memcpy(buf, file->sfs_data + offset, to_read);

        return to_read;
}

static int sfs_file_write (struct sfs_file *file, char *buf, size_t size, off_t offset)
{
        size_t file_size = file->sfs_size;
        int to_write = 0;

        if (offset >= file_size)
                return 0;

        to_write = SFS_MIN(size, file_size - offset);
        memcpy(file->sfs_data + offset, buf, to_write);

        return to_write;
}

static int simplefs_getattr(const char *path, struct stat *stbuf)
{
        int ret = 0;
        struct sfs_file *file = NULL;

        memset(stbuf, 0, sizeof(struct stat));

        /* the root */
        if (strcmp(path, "/") == 0) {
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 2;
        } else if (path[0] == '/' &&
                   (file = sfs_file_lookup((char *)path))) {
                stbuf->st_mode = S_IFREG | 0666;
                stbuf->st_nlink = 1;
                stbuf->st_size = file->sfs_size;
        } else {
                ret = -ENOENT;
        }
        return ret;
}

static int simplefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
        struct sfs_file *file = NULL;

        if (strcmp(path, "/") != 0)
                return -ENOENT;

        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        STAILQ_FOREACH(file, &ctx.sfs_files, sfs_link) {
                filler(buf, file->sfs_name, NULL, 0);
        }

        return 0;
}

static int simplefs_open(const char *path, struct fuse_file_info *fi)
{
        struct sfs_file *file = NULL;

        file = sfs_file_lookup((char *)path);

        if (!file)
                return -ENOENT;

        return 0;
}

static int simplefs_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi)
{
        struct sfs_file *file = NULL;

        file = sfs_file_lookup((char *)path);

        if (!file)
                return -ENOENT;

        return sfs_file_read(file, buf, size, offset);
}

static void init_ctx (void)
{
        debug("init ctx");
        STAILQ_INIT(&ctx.sfs_files);
        ctx.sfs_file_n = 0;
}

static void destroy_ctx (void)
{
        struct sfs_file *file;
        while ((file = STAILQ_FIRST(&ctx.sfs_files))) {
                STAILQ_REMOVE_HEAD(&ctx.sfs_files, sfs_link);
                sfs_file_free(file);
                ctx.sfs_file_n--;
        }
}

static void *simplefs_init (struct fuse_conn_info *conn)
{
        init_ctx();
        /* create the new hello file */
        sfs_file_new("hello", "hello world!", 13);
        return NULL;
}

static void simplefs_destroy (void *data)
{
        destroy_ctx();
        debug("EXIT");
}

static int simplefs_create (const char *name, mode_t mode, struct fuse_file_info *data)
{
        struct sfs_file *file = NULL;

        if (ctx.sfs_file_n >= MAX_FILE_N) {
                debug("reach file max");
                return -ENOSPC;
        }

        /* only require the name here... */
        if (0 == sfs_file_new((char *)name, "", 1))
                return 0;
        else
                return -ENOSPC;
}

static int simplefs_write (const char *name, const char *buf, size_t size, off_t offset,
                           struct fuse_file_info *data)
{
        struct sfs_file *file = NULL;

        file = sfs_file_lookup((char *)name);

        if (!file)
                return -ENOENT;

        return sfs_file_write (file, (char *)buf, size, offset);
}

static struct fuse_operations simplefs_oper = {
        .init           = simplefs_init,
        .destroy        = simplefs_destroy,
        .getattr        = simplefs_getattr,
        .readdir        = simplefs_readdir,
        .create         = simplefs_create,
        .open           = simplefs_open,
        .read           = simplefs_read,
        .write          = simplefs_write,
};

int main(int argc, char *argv[])
{
        return fuse_main(argc, argv, &simplefs_oper, NULL);
}
