/*
 * An alternate Lustre user library.
 *
 * Copyright 2014, 2015 Cray Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/*
 * Tests for the FID related functions.
 */
#include <linux/types.h>
#include <linux/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <lustre/lustre.h>

/* Name of file/directory. Will be set once and will not change. */
/*
static char mainpath[PATH_MAX];
 */
static char *mainpath;
static const char *mainfile1 = "llapi_fid_test_name_9585766";
static const char *mainfile2 = "llapi_fid_test_name_1583792225";

static char *lustre_dir;        /* Test directory inside Lustre */

struct lus_fs_handle *lfsh;

/* Helper - call path2fid, fd2fid and fid2path against an existing
 * file/directory */
static void helper_fid2path(const char *filename, int fd)
{
    lustre_fid fid;
    lustre_fid fid2;
    lustre_fid fid3;
    char path[PATH_MAX];
    char path3[PATH_MAX];
    long long recno;
    unsigned int linkno;
    int rc;

    rc = lus_path2fid(filename, &fid);
    printf("        filepath %s -> FID [0x%x:0x%x:0x%x]\n",filename,fid.f_seq,fid.f_oid,fid.f_ver);

    recno = -1;
    linkno = 0;
    rc = lus_fid2path(lfsh, &fid, path, sizeof(path), &recno, &linkno);
    printf("reverse: filepath %s -> FID [0x%0x:0x%x:0x%x]\n",path,fid.f_seq,fid.f_oid,fid.f_ver);

    /* Try fd2fid and check that the result is still the same. */
    if (fd != -1) {
        rc = lus_fd2fid(fd, &fid3);

    }

    /* Pass the result back to fid2path and ensure the fid stays
     * the same. */
    rc = snprintf(path3, sizeof(path3), "%s/%s",
              lus_get_mountpoint(lfsh), path);
    rc = lus_path2fid(path3, &fid2);
}

/* Create a test file*/
char *mk_path(const char *mainfile,char *mainpath)
{
    int rc;
    struct stat st = {0};
    rc = snprintf(mainpath, sizeof(mainpath), "%s/%s", lustre_dir, mainfile);
    if (rc < 0 && rc >= sizeof(mainpath)) {
        fprintf(stderr, "Error: invalid name for mainpath\n");
        mainpath[0] = '\0';
    }
    return mainpath;
}
int mk_file(const char *filepath) 
{
    int fd;
    fd = creat(filepath, 0);
    return fd;
}
void argparse(int argc, char *argv[])
{
    int rc;
    int opt;
    while ((opt = getopt(argc, argv, "d:")) != -1) {
        switch (opt) {
        case 'd':
            lustre_dir = optarg;
            break;
        case '?':
        default:
            fprintf(stderr, "Unknown option '%c'\n", optopt);
            fprintf(stderr, "Usage: %s [-d lustre_dir]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}
/* Test fid2path with helper */
int main(int argc, char *argv[])
{
    int rc;
    int fd;
    char *mainpath;

    argparse(argc, argv);
    mainpath = (char *)calloc(PATH_MAX,1);

    if (lustre_dir == NULL)
        lustre_dir = "/lustre";

    rc = lus_open_fs(lustre_dir, &lfsh);
    if (rc != 0) {
        fprintf(stderr, "Error: '%s' is not a Lustre filesystem\n",
            lustre_dir);
        return EXIT_FAILURE;
    }
    mainpath = mk_path(mainfile1,mainpath);
    fd = mk_file(mainpath);
    helper_fid2path(mainpath, fd);
    mainpath = mk_path(mainfile2,mainpath);
    fd = mk_file(mainpath);
    helper_fid2path(mainpath, fd);

    lus_close_fs(lfsh);

    return EXIT_SUCCESS;
}
