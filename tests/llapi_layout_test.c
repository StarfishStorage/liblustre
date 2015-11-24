/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 */

/*
 * These tests exercise the llapi_layout API which abstracts the layout
 * of a Lustre file behind an opaque data type.  They assume a Lustre
 * file system with at least 2 OSTs and a pool containing at least the
 * first 2 OSTs.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <limits.h>
#include <sys/stat.h>
#include <getopt.h>
#include <inttypes.h>

#include <check.h>

#include <lustre/lustre.h>

/* Not defined in check 0.9.8 - license is LGPL 2.1 or later */
#ifndef ck_assert_ptr_ne
#define _ck_assert_ptr(X, OP, Y) do { \
  const void* _ck_x = (X); \
  const void* _ck_y = (Y); \
  ck_assert_msg(_ck_x OP _ck_y, "Assertion '"#X#OP#Y"' failed: "#X"==%p, "#Y"==%p", _ck_x, _ck_y); \
} while (0)
#define ck_assert_ptr_eq(X, Y) _ck_assert_ptr(X, ==, Y)
#define ck_assert_ptr_ne(X, Y) _ck_assert_ptr(X, !=, Y)
#define ck_assert_int_gt(X, Y) _ck_assert_int(X, >, Y)
#define ck_assert_int_ge(X, Y) _ck_assert_int(X, >=, Y)
#endif

struct lustre_fs_h *lfsh;

static char *lustre_dir;
static char *poolname;
static int num_osts = -1;

#define T0FILE			"t0"
#define T0_STRIPE_COUNT		num_osts
#define T0_STRIPE_SIZE		1048576
#define T0_OST_OFFSET		(num_osts - 1)
#define T0_DESC		"Read/write layout attributes then create a file"
START_TEST(test0)
{
	int rc;
	int fd;
	uint64_t count;
	uint64_t size;
	struct llapi_layout *layout = llapi_layout_alloc(T0_STRIPE_COUNT);
	char path[PATH_MAX];
	char mypool[LOV_MAXPOOLNAME + 1] = { '\0' };

	ck_assert_msg(layout != NULL, "errno %d", errno);

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T0FILE);

	rc = unlink(path);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);

	/* stripe count */
	rc = llapi_layout_stripe_count_set(layout, T0_STRIPE_COUNT);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0 && count == T0_STRIPE_COUNT, "%"PRIu64" != %d", count,
		      T0_STRIPE_COUNT);

	/* stripe size */
	rc = llapi_layout_stripe_size_set(layout, T0_STRIPE_SIZE);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(rc == 0 && size == T0_STRIPE_SIZE, "%"PRIu64" != %d", size,
		      T0_STRIPE_SIZE);

	/* pool_name */
	rc = llapi_layout_pool_name_set(layout, poolname);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_pool_name_get(layout, mypool, sizeof(mypool));
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = strcmp(mypool, poolname);
	ck_assert_msg(rc == 0, "%s != %s", mypool, poolname);

	/* ost_index */
	rc = llapi_layout_ost_index_set(layout, 0, T0_OST_OFFSET);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	/* create */
	fd = llapi_layout_file_create(path, 0, 0660, layout);
	ck_assert_msg(fd >= 0, "path = %s, errno = %d", path, errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	llapi_layout_free(layout);
}
END_TEST

static void __test1_helper(struct llapi_layout *layout)
{
	uint64_t ost0;
	uint64_t ost1;
	uint64_t size;
	uint64_t count;
	int rc;
	char mypool[LOV_MAXPOOLNAME + 1] = { '\0' };

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert(rc == 0);
	ck_assert_msg(count == T0_STRIPE_COUNT, "%"PRIu64" != %d", count,
		      T0_STRIPE_COUNT);

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(size == T0_STRIPE_SIZE, "%"PRIu64" != %d", size,
		      T0_STRIPE_SIZE);

	rc = llapi_layout_pool_name_get(layout, mypool, sizeof(mypool));
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = strcmp(mypool, poolname);
	ck_assert_msg(rc == 0, "%s != %s", mypool, poolname);

	rc = llapi_layout_ost_index_get(layout, 0, &ost0);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_ost_index_get(layout, 1, &ost1);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(ost0 == T0_OST_OFFSET, "%"PRIu64" != %d", ost0, T0_OST_OFFSET);
	ck_assert_msg(ost1 != ost0, "%"PRIu64" == %"PRIu64, ost0, ost1);
}

#define T1_DESC		"Read test0 file by path and verify attributes"
START_TEST(test1)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T0FILE);
	struct llapi_layout *layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	__test1_helper(layout);
	llapi_layout_free(layout);
}
END_TEST

#define T2_DESC		"Read test0 file by FD and verify attributes"
START_TEST(test2)
{
	int fd;
	int rc;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T0FILE);

	fd = open(path, O_RDONLY);
	ck_assert_msg(fd >= 0, "open(%s): errno = %d", path, errno);

	struct llapi_layout *layout = llapi_layout_get_by_fd(fd, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	rc = close(fd);
	ck_assert_msg(rc == 0, "close(%s): errno = %d", path, errno);

	__test1_helper(layout);
	llapi_layout_free(layout);
}
END_TEST

#define T3_DESC		"Read test0 file by FID and verify attributes"
START_TEST(test3)
{
	int rc;
	struct llapi_layout *layout;
	lustre_fid fid;
	char fidstr[4096];
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T0FILE);

	rc = llapi_path2fid(path, &fid);
	ck_assert_msg(rc == 0, "rc = %d, errno = %d", rc, errno);
	snprintf(fidstr, sizeof(fidstr), "0x%"PRIx64":0x%x:0x%x",
		 (uint64_t)fid.f_seq, fid.f_oid, fid.f_ver);
	errno = 0;
	layout = llapi_layout_get_by_fid(lfsh, &fid, 0);
	ck_assert_msg(layout != NULL, "fidstr = %s, errno = %d", fidstr, errno);

	__test1_helper(layout);
	llapi_layout_free(layout);
}
END_TEST

#define T4FILE			"t4"
#define T4_STRIPE_COUNT		2
#define T4_STRIPE_SIZE		2097152
#define T4_DESC		"Verify compatibility with 'lfs setstripe'"
START_TEST(test4)
{
	int rc;
	uint64_t ost0;
	uint64_t ost1;
	uint64_t count;
	uint64_t size;
	const char *lfs = getenv("LFS");
	char mypool[LOV_MAXPOOLNAME + 1] = { '\0' };
	char cmd[4096];
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T4FILE);

	if (lfs == NULL)
		lfs = "/usr/bin/lfs";

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	snprintf(cmd, sizeof(cmd), "%s setstripe %s %s -c %d -S %d %s", lfs,
		 strlen(poolname) > 0 ? "-p" : "", poolname, T4_STRIPE_COUNT,
		 T4_STRIPE_SIZE, path);
	rc = system(cmd);
	ck_assert_msg(rc == 0, "system(%s): exit status %d", cmd, WEXITSTATUS(rc));

	errno = 0;
	struct llapi_layout *layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(count == T4_STRIPE_COUNT, "%"PRIu64" != %d", count,
		      T4_STRIPE_COUNT);

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(size == T4_STRIPE_SIZE, "%"PRIu64" != %d", size,
		      T4_STRIPE_SIZE);

	rc = llapi_layout_pool_name_get(layout, mypool, sizeof(mypool));
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = strcmp(mypool, poolname);
	ck_assert_msg(rc == 0, "%s != %s", mypool, poolname);

	rc = llapi_layout_ost_index_get(layout, 0, &ost0);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_ost_index_get(layout, 1, &ost1);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(ost1 != ost0, "%"PRIu64" == %"PRIu64, ost0, ost1);

	llapi_layout_free(layout);
}
END_TEST

#define T5FILE		"t5"
#define T5_DESC		"llapi_layout_get_by_path ENOENT handling"
START_TEST(test5)
{
	int rc;
	char path[PATH_MAX];
	struct llapi_layout *layout;

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T5FILE);

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	errno = 0;
	layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout == NULL && errno == ENOENT, "errno = %d", errno);
}
END_TEST

#define T6_DESC		"llapi_layout_get_by_fd EBADF handling"
START_TEST(test6)
{
	errno = 0;
	struct llapi_layout *layout = llapi_layout_get_by_fd(9999, 0);
	ck_assert_msg(layout == NULL && errno == EBADF, "errno = %d", errno);
}
END_TEST

#define T7FILE		"t7"
#define T7_DESC		"llapi_layout_get_by_path EACCES handling"
START_TEST(test7)
{
	int fd;
	int rc;
	uid_t myuid = getuid();
	char path[PATH_MAX];
	const char *runas = getenv("RUNAS_ID");
	struct passwd *pw;
	uid_t uid;

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T7FILE);
	ck_assert_msg(myuid == 0, "myuid = %d", myuid); /* Need root for this test. */

	/* Create file as root */
	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	fd = open(path, O_CREAT, 0400);
	ck_assert_msg(fd > 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	/* Become unprivileged user */
	if (runas != NULL) {
		uid = atoi(runas);
		ck_assert_msg(uid != 0, "runas = %s", runas);
	} else {
		pw = getpwnam("nobody");
		ck_assert_msg(pw != NULL, "errno = %d", errno);
		uid = pw->pw_uid;
	}
	rc = seteuid(uid);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	errno = 0;
	struct llapi_layout *layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout == NULL && errno == EACCES, "errno = %d", errno);
	rc = seteuid(myuid);
	ck_assert_msg(rc == 0, "errno = %d", errno);
}
END_TEST

/* llapi_layout_get_by_path() returns default layout for file with no
 * striping attributes. */
#define T8FILE		"t8"
#define T8_DESC		"llapi_layout_get_by_path ENODATA handling"
START_TEST(test8)
{
	int fd;
	int rc;
	struct llapi_layout *layout;
	uint64_t count;
	uint64_t size;
	uint64_t pattern;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T8FILE);

	rc = unlink(path);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	fd = open(path, O_CREAT, 0640);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(count == LLAPI_LAYOUT_DEFAULT, "count = %"PRIu64"\n", count);

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(size == LLAPI_LAYOUT_DEFAULT, "size = %"PRIu64"\n", size);

	rc = llapi_layout_pattern_get(layout, &pattern);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(pattern == LLAPI_LAYOUT_DEFAULT, "pattern = %"PRIu64"\n",
		      pattern);

	llapi_layout_free(layout);
}
END_TEST

/* Verify llapi_layout_patter_set() return values for various inputs. */
#define T9_DESC		"verify llapi_layout_pattern_set() return values"
START_TEST(test9)
{
	struct llapi_layout *layout;
	int rc;

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	errno = 0;
	rc = llapi_layout_pattern_set(layout, LLAPI_LAYOUT_INVALID);
	ck_assert_msg(rc == -1 && errno == EOPNOTSUPP, "rc = %d, errno = %d", rc,
		      errno);

	errno = 0;
	rc = llapi_layout_pattern_set(NULL, LLAPI_LAYOUT_DEFAULT);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc,
		      errno);

	errno = 0;
	rc = llapi_layout_pattern_set(layout, LLAPI_LAYOUT_DEFAULT);
	ck_assert_msg(rc == 0, "rc = %d, errno = %d", rc, errno);

	errno = 0;
	rc = llapi_layout_pattern_set(layout, LLAPI_LAYOUT_RAID0);
	ck_assert_msg(rc == 0, "rc = %d, errno = %d", rc, errno);

	llapi_layout_free(layout);
}
END_TEST

/* Verify stripe_count interfaces return errors as expected */
#define T10_DESC	"stripe_count error handling"
START_TEST(test10)
{
	int rc;
	uint64_t count;
	struct llapi_layout *layout;

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	/* invalid stripe count */
	errno = 0;
	rc = llapi_layout_stripe_count_set(layout, LLAPI_LAYOUT_INVALID);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	errno = 0;
	rc = llapi_layout_stripe_count_set(layout, -1);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL layout */
	errno = 0;
	rc = llapi_layout_stripe_count_set(NULL, 2);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL layout */
	errno = 0;
	rc = llapi_layout_stripe_count_get(NULL, &count);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL count */
	errno = 0;
	rc = llapi_layout_stripe_count_get(layout, NULL);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* stripe count too large */
	errno = 0;
	rc = llapi_layout_stripe_count_set(layout, LOV_MAX_STRIPE_COUNT + 1);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);
	llapi_layout_free(layout);
}
END_TEST

/* Verify stripe_size interfaces return errors as expected */
#define T11_DESC	"stripe_size error handling"
START_TEST(test11)
{
	int rc;
	uint64_t size;
	struct llapi_layout *layout;

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	/* negative stripe size */
	errno = 0;
	rc = llapi_layout_stripe_size_set(layout, -1);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* invalid stripe size */
	errno = 0;
	rc = llapi_layout_stripe_size_set(layout, LLAPI_LAYOUT_INVALID);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* stripe size too big */
	errno = 0;
	rc = llapi_layout_stripe_size_set(layout, (1ULL << 33));
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL layout */
	errno = 0;
	rc = llapi_layout_stripe_size_set(NULL, 1048576);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	errno = 0;
	rc = llapi_layout_stripe_size_get(NULL, &size);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL size */
	errno = 0;
	rc = llapi_layout_stripe_size_get(layout, NULL);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	llapi_layout_free(layout);
}
END_TEST

/* Verify pool_name interfaces return errors as expected */
#define T12_DESC	"pool_name error handling"
START_TEST(test12)
{
	int rc;
	struct llapi_layout *layout;
	char mypool[LOV_MAXPOOLNAME + 1] = { '\0' };

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	/* NULL layout */
	errno = 0;
	rc = llapi_layout_pool_name_set(NULL, "foo");
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL pool name */
	errno = 0;
	rc = llapi_layout_pool_name_set(layout, NULL);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL layout */
	errno = 0;
	rc = llapi_layout_pool_name_get(NULL, mypool, sizeof(mypool));
	ck_assert_msg(errno == EINVAL, "poolname = %s, errno = %d", poolname, errno);

	/* NULL buffer */
	errno = 0;
	rc = llapi_layout_pool_name_get(layout, NULL, sizeof(mypool));
	ck_assert_msg(errno == EINVAL, "poolname = %s, errno = %d", poolname, errno);

	/* Pool name too long*/
	errno = 0;
	rc = llapi_layout_pool_name_set(layout, "0123456789abcdef");
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	llapi_layout_free(layout);
}
END_TEST

/* Verify ost_index interface returns errors as expected */
#define T13FILE			"t13"
#define T13_STRIPE_COUNT	2
#define T13_DESC		"ost_index error handling"
START_TEST(test13)
{
	int rc;
	int fd;
	uint64_t idx;
	struct llapi_layout *layout;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T13FILE);

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	/* Only setting OST index for stripe 0 is supported for now. */
	errno = 0;
	rc = llapi_layout_ost_index_set(layout, 1, 1);
	ck_assert_msg(rc == -1 && errno == EOPNOTSUPP, "rc = %d, errno = %d",
		rc, errno);

	/* invalid OST index */
	errno = 0;
	rc = llapi_layout_ost_index_set(layout, 0, LLAPI_LAYOUT_INVALID);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	errno = 0;
	rc = llapi_layout_ost_index_set(layout, 0, -1);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL layout */
	errno = 0;
	rc = llapi_layout_ost_index_set(NULL, 0, 1);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	errno = 0;
	rc = llapi_layout_ost_index_get(NULL, 0, &idx);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* NULL index */
	errno = 0;
	rc = llapi_layout_ost_index_get(layout, 0, NULL);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* Layout not read from file so has no OST data. */
	errno = 0;
	rc = llapi_layout_stripe_count_set(layout, T13_STRIPE_COUNT);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_ost_index_get(layout, 0, &idx);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	/* n greater than stripe count*/
	rc = unlink(path);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	rc = llapi_layout_stripe_count_set(layout, T13_STRIPE_COUNT);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	fd = llapi_layout_file_create(path, 0, 0644, layout);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	llapi_layout_free(layout);

	layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	errno = 0;
	rc = llapi_layout_ost_index_get(layout, T13_STRIPE_COUNT + 1, &idx);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	llapi_layout_free(layout);
}
END_TEST

/* Verify llapi_layout_file_create() returns errors as expected */
#define T14_DESC	"llapi_layout_file_create error handling"
START_TEST(test14)
{
	int rc;
	struct llapi_layout *layout = llapi_layout_alloc(0);

	/* NULL path */
	errno = 0;
	rc = llapi_layout_file_create(NULL, 0, 0, layout);
	ck_assert_msg(rc == -1 && errno == EINVAL, "rc = %d, errno = %d", rc, errno);

	llapi_layout_free(layout);
}
END_TEST

/* Can't change striping attributes of existing file. */
#define T15FILE			"t15"
#define T15_STRIPE_COUNT	2
#define T15_DESC	"Can't change striping attributes of existing file"
START_TEST(test15)
{
	int rc;
	int fd;
	uint64_t count;
	struct llapi_layout *layout;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T15FILE);

	rc = unlink(path);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	rc = llapi_layout_stripe_count_set(layout, T15_STRIPE_COUNT);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	errno = 0;
	fd = llapi_layout_file_create(path, 0, 0640, layout);
	ck_assert_msg(fd >= 0, "fd = %d, errno = %d", fd, errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	rc = llapi_layout_stripe_count_set(layout, T15_STRIPE_COUNT - 1);
	errno = 0;
	fd = llapi_layout_file_open(path, 0, 0640, layout);
	ck_assert_msg(fd >= 0, "fd = %d, errno = %d", fd, errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	llapi_layout_free(layout);

	layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0 && count == T15_STRIPE_COUNT,
		"rc = %d, %"PRIu64" != %d", rc, count, T15_STRIPE_COUNT);
	llapi_layout_free(layout);
}
END_TEST

/* Default stripe attributes are applied as expected. */
#define T16FILE		"t16"
#define T16_DESC	"Default stripe attributes are applied as expected"
START_TEST(test16)
{
	int		rc;
	int		fd;
	struct llapi_layout	*deflayout;
	struct llapi_layout	*filelayout;
	char		path[PATH_MAX];
	uint64_t	fsize;
	uint64_t	fcount;
	uint64_t	dsize;
	uint64_t	dcount;

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T16FILE);

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	deflayout = llapi_layout_get_by_path(lustre_dir, LAYOUT_GET_EXPECTED);
	ck_assert_msg(deflayout != NULL, "errno = %d", errno);
	rc = llapi_layout_stripe_size_get(deflayout, &dsize);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_stripe_count_get(deflayout, &dcount);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	/* First, with a default struct llapi_layout */
	filelayout = llapi_layout_alloc(0);
	ck_assert_msg(filelayout != NULL, "errno = %d", errno);

	fd = llapi_layout_file_create(path, 0, 0640, filelayout);
	ck_assert_msg(fd >= 0, "errno = %d", errno);

	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	llapi_layout_free(filelayout);

	filelayout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(filelayout != NULL, "errno = %d", errno);

	rc = llapi_layout_stripe_count_get(filelayout, &fcount);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(fcount == dcount, "%"PRIu64" != %"PRIu64, fcount, dcount);

	rc = llapi_layout_stripe_size_get(filelayout, &fsize);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(fsize == dsize, "%"PRIu64" != %"PRIu64, fsize, dsize);

	/* NULL layout also implies default layout */
	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	fd = llapi_layout_file_create(path, 0, 0640, filelayout);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	llapi_layout_free(filelayout);
	filelayout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(filelayout != NULL, "errno = %d", errno);

	rc = llapi_layout_stripe_count_get(filelayout, &fcount);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_stripe_size_get(filelayout, &fsize);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(fcount == dcount, "%"PRIu64" != %"PRIu64, fcount, dcount);
	ck_assert_msg(fsize == dsize, "%"PRIu64" != %"PRIu64, fsize, dsize);

	llapi_layout_free(filelayout);
	llapi_layout_free(deflayout);
}
END_TEST

#if 0
/* Setting stripe count to LLAPI_LAYOUT_WIDE uses all available OSTs. */
#define T17FILE		"t17"
#define T17_DESC	"LLAPI_LAYOUT_WIDE is honored"
START_TEST(test17)
{
	int rc;
	int fd;
	int osts_all;
	uint64_t osts_layout;
	struct llapi_layout *layout;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T17FILE);

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);
	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	rc = llapi_layout_stripe_count_set(layout, LLAPI_LAYOUT_WIDE);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	fd = llapi_layout_file_create(path, 0, 0640, layout);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	llapi_layout_free(layout);

	/* Get number of available OSTs */
	fd = open(path, O_RDONLY);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = llapi_lov_get_uuids(fd, NULL, &osts_all);
	ck_assert_msg(rc == 0, "rc = %d, errno = %d", rc, errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	rc = llapi_layout_stripe_count_get(layout, &osts_layout);
	ck_assert_msg(osts_layout == osts_all, "%"PRIu64" != %d", osts_layout,
		osts_all);

	llapi_layout_free(layout);
}
END_TEST
#endif

/* Setting pool with "fsname.pool" notation. */
#define T18FILE		"t18"
#define T18_DESC	"Setting pool with fsname.pool notation"
START_TEST(test18)
{
	int rc;
	int fd;
	struct llapi_layout *layout = llapi_layout_alloc(0);
	char path[PATH_MAX];
	char pool[LOV_MAXPOOLNAME*2 + 1];
	char mypool[LOV_MAXPOOLNAME + 1] = { '\0' };

	snprintf(pool, sizeof(pool), "lustre.%s", poolname);

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T18FILE);

	ck_assert_msg(layout != NULL, "errno = %d", errno);

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	rc = llapi_layout_pool_name_set(layout, pool);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	rc = llapi_layout_pool_name_get(layout, mypool, sizeof(mypool));
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = strcmp(mypool, poolname);
	ck_assert_msg(rc == 0, "%s != %s", mypool, poolname);
	fd = llapi_layout_file_create(path, 0, 0640, layout);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	llapi_layout_free(layout);

	layout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	rc = llapi_layout_pool_name_get(layout, mypool, sizeof(mypool));
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = strcmp(mypool, poolname);
	ck_assert_msg(rc == 0, "%s != %s", mypool, poolname);
	llapi_layout_free(layout);
}
END_TEST

#define T19_DESC	"Maximum length pool name is NULL-terminated"
START_TEST(test19)
{
	struct llapi_layout *layout;
	char *name = "0123456789abcde";
	char mypool[LOV_MAXPOOLNAME + 1] = { '\0' };
	int rc;

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);
	rc = llapi_layout_pool_name_set(layout, name);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_pool_name_get(layout, mypool, sizeof(mypool));
	ck_assert_msg(strlen(name) == strlen(mypool), "name = %s, str = %s", name,
		      mypool);
	llapi_layout_free(layout);
}
END_TEST

#define T20FILE		"t20"
#define T20_DESC	"LLAPI_LAYOUT_DEFAULT is honored"
START_TEST(test20)
{
	int		rc;
	int		fd;
	struct llapi_layout	*deflayout;
	struct llapi_layout	*filelayout;
	char		path[PATH_MAX];
	uint64_t	fsize;
	uint64_t	fcount;
	uint64_t	dsize;
	uint64_t	dcount;

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T20FILE);

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	filelayout = llapi_layout_alloc(0);
	ck_assert_msg(filelayout != NULL, "errno = %d", errno);

	rc = llapi_layout_stripe_size_set(filelayout, LLAPI_LAYOUT_DEFAULT);
	ck_assert_msg(rc == 0, "rc = %d, errno = %d", rc, errno);

	rc = llapi_layout_stripe_count_set(filelayout, LLAPI_LAYOUT_DEFAULT);
	ck_assert_msg(rc == 0, "rc = %d, errno = %d", rc, errno);

	fd = llapi_layout_file_create(path, 0, 0640, filelayout);
	ck_assert_msg(fd >= 0, "errno = %d", errno);

	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	llapi_layout_free(filelayout);

	deflayout = llapi_layout_get_by_path(lustre_dir, LAYOUT_GET_EXPECTED);
	ck_assert_msg(deflayout != NULL, "errno = %d", errno);

	filelayout = llapi_layout_get_by_path(path, 0);
	ck_assert_msg(filelayout != NULL, "errno = %d", errno);

	rc = llapi_layout_stripe_count_get(filelayout, &fcount);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_stripe_count_get(deflayout, &dcount);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(fcount == dcount, "%"PRIu64" != %"PRIu64, fcount, dcount);

	rc = llapi_layout_stripe_size_get(filelayout, &fsize);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	rc = llapi_layout_stripe_size_get(deflayout, &dsize);
	ck_assert_msg(rc == 0, "errno = %d", errno);
	ck_assert_msg(fsize == dsize, "%"PRIu64" != %"PRIu64, fsize, dsize);

	llapi_layout_free(filelayout);
	llapi_layout_free(deflayout);
}
END_TEST

#define T21_DESC	"llapi_layout_file_create fails for non-Lustre file"
START_TEST(test21)
{
	struct llapi_layout *layout;
	char template[PATH_MAX];
	int fd;
	int rc;

	snprintf(template, sizeof(template), "%s/XXXXXX", P_tmpdir);
	fd = mkstemp(template);
	ck_assert_msg(fd >= 0, "template = %s, errno = %d", template, errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", fd);
	rc = unlink(template);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	layout = llapi_layout_alloc(0);
	ck_assert_msg(layout != NULL, "errno = %d", errno);

	fd = llapi_layout_file_create(template, 0, 0640, layout);
	ck_assert_msg(fd == -1 && errno == ENOTTY,
		"fd = %d, errno = %d, template = %s", fd, errno, template);
	llapi_layout_free(layout);
}
END_TEST

#define T22FILE		"t22"
#define T22_DESC	"llapi_layout_file_create applied mode correctly"
START_TEST(test22)
{
	int		rc;
	int		fd;
	char		path[PATH_MAX];
	struct stat	st;
	mode_t		mode_in = 0640;
	mode_t		mode_out;
	mode_t		umask_orig;

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T22FILE);

	rc = unlink(path);
	ck_assert_msg(rc == 0 || errno == ENOENT, "errno = %d", errno);

	umask_orig = umask(S_IWGRP | S_IWOTH);

	fd = llapi_layout_file_create(path, 0, mode_in, NULL);
	ck_assert_msg(fd >= 0, "errno = %d", errno);

	(void) umask(umask_orig);

	rc = fstat(fd, &st);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", fd);

	mode_out = st.st_mode & ~S_IFMT;
	ck_assert_msg(mode_in == mode_out, "%o != %o", mode_in, mode_out);
}
END_TEST

#define T23_DESC	"llapi_layout_get_by_path fails for non-Lustre file"
START_TEST(test23)
{
	struct llapi_layout *layout;
	char template[PATH_MAX];
	int fd;
	int rc;

	snprintf(template, sizeof(template), "%s/XXXXXX", P_tmpdir);
	fd = mkstemp(template);
	ck_assert_msg(fd >= 0, "template = %s, errno = %d", template, errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", fd);

	layout = llapi_layout_get_by_path(template, 0);
	ck_assert_msg(layout == NULL && errno == ENOTTY,
		      "errno = %d, template = %s", errno, template);

	rc = unlink(template);
	ck_assert_msg(rc == 0, "errno = %d", errno);
}
END_TEST

/* llapi_layout_get_by_path(path, LAYOUT_GET_EXPECTED) returns expected layout
 * for file with unspecified layout. */
#define T24FILE		"t24"
#define T24_DESC	"LAYOUT_GET_EXPECTED works with existing file"
START_TEST(test24)
{
	int fd;
	int rc;
	struct llapi_layout *layout;
	uint64_t count;
	uint64_t size;
	uint64_t pattern;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", lustre_dir, T24FILE);

	rc = unlink(path);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	fd = open(path, O_CREAT, 0640);
	ck_assert_msg(fd >= 0, "errno = %d", errno);
	rc = close(fd);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	layout = llapi_layout_get_by_path(path, LAYOUT_GET_EXPECTED);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(count != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(size != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	rc = llapi_layout_pattern_get(layout, &pattern);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(pattern != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	llapi_layout_free(layout);
}
END_TEST

#if 0
/* llapi_layout_get_by_path(path, LAYOUT_GET_EXPECTED) returns expected layout
 * for directory with unspecified layout. */
#define T25DIR		"d25"
#define T25_DESC	"LAYOUT_GET_EXPECTED works with directory"
START_TEST(test25)
{
	int rc;
	struct llapi_layout *layout;
	uint64_t count;
	uint64_t size;
	uint64_t pattern;
	char dir[PATH_MAX];

	snprintf(dir, sizeof(dir), "%s/%s", lustre_dir, T25DIR);

	rc = rmdir(dir);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	rc = mkdir(dir, 0750);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	layout = llapi_layout_get_by_path(dir, LAYOUT_GET_EXPECTED);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(count != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(size != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	rc = llapi_layout_pattern_get(layout, &pattern);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(pattern != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	llapi_layout_free(layout);
}
END_TEST

/* llapi_layout_get_by_path(path, LAYOUT_GET_EXPECTED) correctly combines
 * specified attributes of parent directory with attributes filesystem root. */
#define T26DIR		"d26"
#define T26_DESC	"LAYOUT_GET_EXPECTED partially specified parent"
#define T26_STRIPE_SIZE	(1048576 * 4)
START_TEST(test26)
{
	int rc;
	struct llapi_layout *layout;
	const char *lfs = getenv("LFS");
	uint64_t count;
	uint64_t size;
	uint64_t pattern;
	char dir[PATH_MAX];
	char cmd[4096];

	snprintf(dir, sizeof(dir), "%s/%s", lustre_dir, T26DIR);
	rc = rmdir(dir);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	rc = mkdir(dir, 0750);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	if (lfs == NULL)
		lfs = "/usr/bin/lfs";

	snprintf(cmd, sizeof(cmd), "%s setstripe -s %d %s", lfs,
		 T26_STRIPE_SIZE, dir);
	rc = system(cmd);
	ck_assert_msg(rc == 0, "system(%s): exit status %d", cmd, WEXITSTATUS(rc));

	layout = llapi_layout_get_by_path(dir, LAYOUT_GET_EXPECTED);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(count != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(size == T26_STRIPE_SIZE, "size = %"PRIu64, size);

	rc = llapi_layout_pattern_get(layout, &pattern);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(pattern != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	llapi_layout_free(layout);
}
END_TEST

/* llapi_layout_get_by_path(path, LAYOUT_GET_EXPECTED) work with
 * non existing file. */
#define T27DIR		"d27"
#define T27_DESC	"LAYOUT_GET_EXPECTED with non existing file"
#define T27_STRIPE_SIZE	(1048576 * 3)
START_TEST(test27)
{
	int rc;
	struct llapi_layout *layout;
	const char *lfs = getenv("LFS");
	uint64_t count;
	uint64_t size;
	uint64_t pattern;
	char dirpath[PATH_MAX];
	char filepath[PATH_MAX];
	char cmd[4096];

	snprintf(dirpath, sizeof(dirpath), "%s/%s", lustre_dir, T27DIR);
	snprintf(filepath, sizeof(filepath), "%s/nonesuch", dirpath);

	rc = rmdir(dirpath);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	rc = mkdir(dirpath, 0750);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	if (lfs == NULL)
		lfs = "/usr/bin/lfs";

	snprintf(cmd, sizeof(cmd), "%s setstripe -s %d %s", lfs,
		 T27_STRIPE_SIZE, dirpath);
	rc = system(cmd);
	ck_assert_msg(rc == 0, "system(%s): exit status %d", cmd, WEXITSTATUS(rc));

	layout = llapi_layout_get_by_path(filepath, LAYOUT_GET_EXPECTED);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(count != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	rc = llapi_layout_stripe_size_get(layout, &size);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(size == T27_STRIPE_SIZE, "size = %"PRIu64, size);

	rc = llapi_layout_pattern_get(layout, &pattern);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(pattern != LLAPI_LAYOUT_DEFAULT, "expected literal value");

	llapi_layout_free(layout);
}
END_TEST
#endif

/* llapi_layout_stripe_count_get returns LLAPI_LAYOUT_WIDE for a directory
 * with a stripe_count of -1. */
#define T28DIR		"d28"
#define T28_DESC	"LLAPI_LAYOUT_WIDE returned as expected"
START_TEST(test28)
{
	int rc;
	struct llapi_layout *layout;
	const char *lfs = getenv("LFS");
	uint64_t count;
	char dirpath[PATH_MAX];
	char cmd[4096];

	snprintf(dirpath, sizeof(dirpath), "%s/%s", lustre_dir, T28DIR);

	rc = rmdir(dirpath);
	ck_assert_msg(rc >= 0 || errno == ENOENT, "errno = %d", errno);
	rc = mkdir(dirpath, 0750);
	ck_assert_msg(rc == 0, "errno = %d", errno);

	if (lfs == NULL)
		lfs = "/usr/bin/lfs";

	snprintf(cmd, sizeof(cmd), "%s setstripe -c -1 %s", lfs, dirpath);
	rc = system(cmd);
	ck_assert_msg(rc == 0, "system(%s): exit status %d", cmd, WEXITSTATUS(rc));

	layout = llapi_layout_get_by_path(dirpath, 0);
	ck_assert_msg(layout != NULL, "errno = %d\n", errno);

	rc = llapi_layout_stripe_count_get(layout, &count);
	ck_assert_msg(rc == 0, "errno = %d\n", errno);
	ck_assert_msg(count == LLAPI_LAYOUT_WIDE, "count = %"PRIu64"\n", count);

	llapi_layout_free(layout);
}
END_TEST

#define ADD_TEST(name) {			\
	tc = tcase_create(#name);		\
	tcase_add_test(tc, name);		\
	suite_add_tcase(s, tc);			\
}

int main(int argc, char *argv[])
{
	int rc = 0;
	int opt;
	int number_failed;
	Suite *s;
	TCase *tc;
	SRunner *sr;

	llapi_msg_set_level(LLAPI_MSG_OFF);

	while ((opt = getopt(argc, argv, "d:o:p:")) != -1) {
		switch (opt) {
		case 'd':
			lustre_dir = optarg;
			break;
		case 'p':
			poolname = optarg;
			break;
		case 'o':
			num_osts = atoi(optarg);
			break;
		case '?':
		default:
			fprintf(stderr, "Unknown option '%c'\n", optopt);
			fprintf(stderr, "Usage: %s [-d lustre_dir] [-p pool_name] [-o num_osts]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (lustre_dir == NULL)
		lustre_dir = "/mnt/lustre";

	if (poolname == NULL)
		poolname = "mypool";

	if (num_osts == -1)
		num_osts = 2;

	rc = llapi_open_fs(lustre_dir, &lfsh);
	if (rc != 0) {
		fprintf(stderr, "Error: '%s' is not a Lustre filesystem\n",
			lustre_dir);
		return EXIT_FAILURE;
	}

	s = suite_create("LAYOUT");
	ADD_TEST(test0);
	ADD_TEST(test1);
	ADD_TEST(test2);
	ADD_TEST(test3);
	ADD_TEST(test4);
	ADD_TEST(test5);
	ADD_TEST(test6);
	ADD_TEST(test7);
	ADD_TEST(test8);
	ADD_TEST(test9);
	ADD_TEST(test10);
	ADD_TEST(test11);
	ADD_TEST(test12);
	ADD_TEST(test13);
	ADD_TEST(test14);
	ADD_TEST(test15);
	ADD_TEST(test16);
#if 0
	ADD_TEST(test17);
#endif
	ADD_TEST(test18);
	ADD_TEST(test19);
	ADD_TEST(test20);
	ADD_TEST(test21);
	ADD_TEST(test22);
	ADD_TEST(test23);
	ADD_TEST(test24);
#if 0
	ADD_TEST(test25);
	ADD_TEST(test26);
	ADD_TEST(test27);
#endif
	ADD_TEST(test28);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	llapi_close_fs(lfsh);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}