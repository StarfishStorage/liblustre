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
#include <mntent.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <lustre/lustre.h>

const char *mtab_entry1 = "10.149.200.5@o2ib:10.149.200.4@o2ib:/snx11322/usda/data/project/sysadmin";
const char *mtab_entry2 = "192.168.11.42@tcp:/sf-l28 /lustre-2.8 lustre rw,noauto,noatime,user_xattr 0 0";
const char *mtab_entry3 = "/lustre-2.8";

#define _PATH_MOUNTED   "/etc/mtab"
char buffer[PATH_MAX];
char *etc_mtab_file = _PATH_MOUNTED;

char *get_mtab(const char *entry, char *p_out)
{
        size_t len;
        char *p;

        /* Found it. The Lustre fsname is part of the fsname
         * (ie. nodename@tcp:/lustre) so extract it. */
        p = strstr(entry, ":/");
        if (p == NULL)
            return NULL;

        /* Skip :/ */
        p += 2;

        len = strlen(p);
        if (len >= 1)
            strcpy(p_out, p);
        else
            printf("entry is empty\n");

        return(p_out);
}
void all_mtab(const char *mtab_path, const char *entry, char *buffer)
{
    int rc;
    char *p;
    FILE *f = NULL;
    struct mntent *ent;

    /* Retrieve the lustre filesystem name from /etc/mtab. */
    f = setmntent(mtab_path, "r");
    if (f == NULL) {
        rc = -EINVAL;
        fprintf(stderr,"The filesystem path is not in /etc/mtab\n");                                                              
        exit(1);
    }

    while ((ent = getmntent(f))) {
        size_t len;

        fprintf(stderr,"getmntent: %s type: %s dir %s\n",ent->mnt_fsname,ent->mnt_type,ent->mnt_dir);                                 
        /* will this match for all versions of ent->mnt_dir? is it a std form? Is mylfsh->mount_path a standard form?*/           
        if ((strcmp(ent->mnt_dir, entry) != 0) ||
            (strcmp(ent->mnt_type, "lustre") != 0))
            continue;

        /* Found it. The Lustre fsname is part of the fsname
         * (ie. nodename@tcp:/lustre) so extract it. */
        p = strstr(ent->mnt_fsname, ":/");
        if (p == NULL)
            break;

        /* Skip :/ */
        p += 2;

        strncpy(buffer, p, 8);
        buffer[8] = '\0';

    }
    endmntent(f);
}

/* Test fid2path with helper */
int main(int argc, char *argv[])
{
    int rc;
    int fd;

    get_mtab(mtab_entry1, (char *)buffer);
    printf("Buffer: %s\n",buffer);
    all_mtab(_PATH_MOUNTED,mtab_entry3,(char *)buffer); /* buffer modified side-effect */
    printf("1 All mtab Buffer: %s\n",buffer);
    printf("----------------------------------------------------------------------------------------\n");
    all_mtab("mtab","/lustre",(char *)buffer); /* buffer modified side-effect */
    printf("2 All mtab Buffer: %s\n",buffer);
    printf("----------------------------------------------------------------------------------------\n");

}
