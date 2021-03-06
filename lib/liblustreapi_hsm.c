/*
 * LGPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * (C) Copyright 2012 Commissariat a l'energie atomique et aux energies
 *     alternatives
 *
 * Copyright (c) 2013, 2014, Intel Corporation.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 or (at your discretion) any later version.
 * (LGPL) version 2.1 accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * LGPL HEADER END
 */
/*
 * lustre/utils/liblustreapi_hsm.c
 *
 * lustreapi library for hsm calls
 *
 * Author: Aurelien Degremont <aurelien.degremont@cea.fr>
 * Author: JC Lafoucriere <jacques-charles.lafoucriere@cea.fr>
 * Author: Thomas Leibovici <thomas.leibovici@cea.fr>
 * Author: Henri Doreau <henri.doreau@cea.fr>
 */

/**
 * @file
 * @brief Interfaces to HSM functions
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <fnmatch.h>
#include <glob.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include <lustre/lustre.h>

#include "internal.h"

/****** HSM Copytool API ********/
struct lus_hsm_ct_handle {
	const struct lus_fs_handle *lfsh;
	int			 channel_rfd;
	__u32			 archives;

	/* HSM action list. The kernel comm (kuc_msglen) is limited to
	 * 64k. It's small enough to reserve it here. */
	unsigned char            hal[65536];
};

struct lus_hsm_action_handle {
	int					 data_fd;
	const struct lus_hsm_ct_handle		*ct_priv;
	struct hsm_copy				 copy;
	struct stat				 stat;
};

enum ct_progress_type {
	CT_START	= 0,
	CT_RUNNING	= 50,
	CT_FINISH	= 100,
	CT_CANCEL	= 150,
	CT_ERROR	= 175
};

enum ct_event {
	CT_REGISTER		= 1,
	CT_UNREGISTER		= 2,
	CT_ARCHIVE_START	= HSMA_ARCHIVE,
	CT_ARCHIVE_RUNNING	= HSMA_ARCHIVE + CT_RUNNING,
	CT_ARCHIVE_FINISH	= HSMA_ARCHIVE + CT_FINISH,
	CT_ARCHIVE_CANCEL	= HSMA_ARCHIVE + CT_CANCEL,
	CT_ARCHIVE_ERROR	= HSMA_ARCHIVE + CT_ERROR,
	CT_RESTORE_START	= HSMA_RESTORE,
	CT_RESTORE_RUNNING	= HSMA_RESTORE + CT_RUNNING,
	CT_RESTORE_FINISH	= HSMA_RESTORE + CT_FINISH,
	CT_RESTORE_CANCEL	= HSMA_RESTORE + CT_CANCEL,
	CT_RESTORE_ERROR	= HSMA_RESTORE + CT_ERROR,
	CT_REMOVE_START		= HSMA_REMOVE,
	CT_REMOVE_RUNNING	= HSMA_REMOVE + CT_RUNNING,
	CT_REMOVE_FINISH	= HSMA_REMOVE + CT_FINISH,
	CT_REMOVE_CANCEL	= HSMA_REMOVE + CT_CANCEL,
	CT_REMOVE_ERROR		= HSMA_REMOVE + CT_ERROR,
	CT_EVENT_MAX
};

/* Open a communication channel with the kernel to retrieve HSM
 * events. Return 0 on success, or -1 on error. */
static int open_hsm_comm(struct lus_hsm_ct_handle *ct)
{
	struct lustre_kernelcomm channel;
	int pipefd[2];
	int rc;

	rc = pipe(pipefd);
	if (rc == -1)
		return -errno;

	/* Set the reader as non-blocking, so it can be poll'ed or
	 * select'ed. */
	rc = fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
	if (rc == -1) {
		rc = -errno;
		goto out_err;
	}

	/* Storing archive(s) in lk_data; see mdc_ioc_hsm_ct_start */
	channel.lk_rfd = 0;
	channel.lk_wfd = pipefd[1];
	channel.lk_uid = getpid();
	channel.lk_group = KUC_GRP_HSM;
	channel.lk_data = ct->archives;
	channel.lk_flags = 0;

	rc = ioctl(ct->lfsh->mount_fd, LL_IOC_HSM_CT_START, &channel);
	if (rc == -1) {
		rc = -errno;
		goto out_err;
	}

	/* The application reads and the kernel writes. */
	ct->channel_rfd = pipefd[0];
	close(pipefd[1]);

	return 0;

out_err:
	close(pipefd[0]);
	close(pipefd[1]);

	return rc;
}

/* Clsoe the HSM connection opened by open_hsm_conn. */
static void close_hsm_comm(struct lus_hsm_ct_handle *ct)
{
	struct lustre_kernelcomm channel = {
		.lk_group = KUC_GRP_HSM,
		.lk_flags = LK_FLG_STOP,
	};

	if (ct->channel_rfd != -1) {
		/* Tell the kernel to stop sending us messages */
		ioctl(ct->lfsh->mount_fd, LL_IOC_HSM_CT_START, &channel);

		close(ct->channel_rfd);
		ct->channel_rfd = -1;
	}
}

/* Get a message from HSM. Return 0 on success and set hal and
 * hal_len. Return a negative errno on error. The caller is expected
 * to handle -EWOULDBLOCK. */
static int get_hsm_comm(struct lus_hsm_ct_handle *ct,
			const struct hsm_action_list **hal,
			size_t *hal_len)
{
	int rc;
	struct kuc_hdr header;
	size_t data_len;

	/* Read until we get a valid message or an error occurs. */
	while (1) {
		/* Get header */
		rc = read(ct->channel_rfd, &header, sizeof(header));
		if (rc == -1) {
			rc = -errno;
			break;
		}

		if (rc != sizeof(struct kuc_hdr)) {
			/* Not enough data. We're hosed. */
			rc = -ENODATA;
			break;
		}

		if (header.kuc_magic != KUC_MAGIC) {
			log_msg(LUS_LOG_ERROR, 0,
				"Bad magic received from kernel (%08x instead of %08x)",
				header.kuc_magic, KUC_MAGIC);
			rc = -EPROTO;
			break;
		}

		if (header.kuc_msglen < sizeof(header)) {
			log_msg(LUS_LOG_ERROR, 0,
				"Invalid data length (%08x < %08zx)",
				header.kuc_msglen, sizeof(header));
			rc = -EPROTO;
			break;
		}

		/* Get message data */
		data_len = header.kuc_msglen - sizeof(header);
		rc = read(ct->channel_rfd, ct->hal, data_len);
		if (rc != data_len) {
			/* Not enough data. We're hosed. */
			rc = -EPROTO;
			break;
		}

		/* Only accept HSM messages. */
		if (header.kuc_transport == KUC_TRANSPORT_HSM ||
		    header.kuc_transport == KUC_TRANSPORT_GENERIC) {

			switch (header.kuc_msgtype) {
			case KUC_MSG_SHUTDOWN:
				rc = -ESHUTDOWN;
				break;
			case HMT_ACTION_LIST:
				*hal = (struct hsm_action_list *)ct->hal;
				*hal_len = data_len;
				return 0;
			}
		}

		/* Something else. Ignore the message and try again. */
	}

	return rc;
}


/**
 * Register a copytool
 *
 * \param[in]   lfsh      An opened Lustre fs opaque handle
 * \param[in]   archive_count  Number of valid archive IDs in \a archives
 * \param[in]   archives  Which archive numbers this copytool is
 *			  responsible for
 * \param[out]  priv	  Opaque private control structure
 *
 * \retval 0 on success.
 * \retval negative errno on error, with priv set to NULL.
 */
int lus_hsm_copytool_register(const struct lus_fs_handle *lfsh,
			      unsigned int archive_count, int *archives,
			      struct lus_hsm_ct_handle **priv)
{
	struct lus_hsm_ct_handle *ct;
	int rc;

	*priv = NULL;

	if (archive_count > 0 && archives == NULL)
		return -EINVAL;

	if (archive_count > LL_HSM_MAX_ARCHIVE) {
		log_msg(LUS_LOG_ERROR, 0,
			"%d requested when maximum of %zu archives supported",
			archive_count, LL_HSM_MAX_ARCHIVE);
		return -EINVAL;
	}

	ct = calloc(1, sizeof(*ct));
	if (ct == NULL)
		return -ENOMEM;

	ct->lfsh = lfsh;
	ct->channel_rfd = -1;

	/* no archives specified means "match all". */
	ct->archives = 0;
	for (rc = 0; rc < archive_count; rc++) {
		if ((archives[rc] > LL_HSM_MAX_ARCHIVE) || (archives[rc] < 0)) {
			log_msg(LUS_LOG_ERROR, 0,
				"%d requested when archive id [0 - %zu] is supported",
				archives[rc], LL_HSM_MAX_ARCHIVE);
			rc = -EINVAL;
			goto out_err;
		}
		/* in the list we have an all archive wildcard
		 * so move to all archives mode
		 */
		if (archives[rc] == 0) {
			ct->archives = 0;
			archive_count = 0;
			break;
		}
		ct->archives |= (1 << (archives[rc] - 1));
	}

	rc = open_hsm_comm(ct);
	if (rc < 0) {
		log_msg(LUS_LOG_ERROR, rc,
			"cannot start copytool on '%s'", lfsh->mount_path);
		goto out_err;
	}

	*priv = ct;

	return 0;

out_err:
	if (ct->channel_rfd != -1)
		close(ct->channel_rfd);
	free(ct);

	return rc;
}

/**
 * Deregister a copytool
 *
 * \param[in,out]  priv    Copytool handle acquired at registration.
 *
 * \retval 0 on success.
 * \retval a future version of the library might return a negative
 *         errno.
 *
 * Note: under Linux, until lus_hsm_copytool_unregister is called
 * (or the program is killed), the libcfs module will be referenced
 * and unremovable, even after Lustre services stop.
 */
int lus_hsm_copytool_unregister(struct lus_hsm_ct_handle **priv)
{
	if (*priv) {
		close_hsm_comm(*priv);
		free(*priv);
		*priv = NULL;
	}

	return 0;
}

/**
 * Returns a file descriptor to poll/select on.
 *
 * \param   ct    Opaque private control structure
 *
 * \retval  -EINVAL on error
 * \retval  the file descriptor for reading HSM events from the kernel
 */
int lus_hsm_copytool_get_fd(const struct lus_hsm_ct_handle *ct)
{
	return ct->channel_rfd;
}

/**
 * Wait for the next hsm_action_list
 *
 * \param[in]  ct        Opaque private control structure
 * \param[out] halh      Action list handle, will be allocated here
 * \param[out] msgsize   Number of bytes in the message, will be set here
 *
 * \retval  0 when a valid message is received; halh and msgsize are set
 * \retval  a negative errno on error
 *
 * Note: The application must not call lus_hsm_copytool_recv until
 * it has cleared the data in ct->kuch from the previous call.
 */
int lus_hsm_copytool_recv(struct lus_hsm_ct_handle *ct,
			  const struct hsm_action_list **halh,
			  size_t *msgsize)
{
	int rc;

repeat:
	rc = get_hsm_comm(ct, halh, msgsize);
	if (rc < 0)
		goto out_err;

	/* TODO: the following should go away. We should trust Lustre
	 * to not send bad messages. */

	/* Check that we have registered for this archive number.
	 * If 0 registered, we serve any archive. */
	if (ct->archives &&
	    ((1 << ((*halh)->hal_archive_id - 1)) & ct->archives) == 0) {
		log_msg(LUS_LOG_INFO, 0,
			"This copytool does not service archive #%d,"
			" ignoring this request."
			" Mask of served archive is 0x%.8X",
			(*halh)->hal_archive_id, ct->archives);

		goto repeat;
	}

	return 0;

out_err:
	*halh = NULL;
	*msgsize = 0;
	return rc;
}

/**
 * Create the destination volatile file for a restore operation.
 *
 * \param hcp         Private copyaction handle.
 * \param mdt_index   MDT index where to create the volatile file.
 * \param open_flags  Volatile file creation flags.
 *
 * \return 0 on success.
 */
static int create_restore_volatile(struct lus_hsm_action_handle *hcp,
				   int mdt_index, int open_flags)
{
	int			 rc;
	int			 fd;
	const struct lus_fs_handle *lfsh = hcp->ct_priv->lfsh;
	const char		*mnt = lfsh->mount_path;
	struct hsm_action_item	*hai = &hcp->copy.hc_hai;
	lustre_fid		parent_fid;

	/* TODO: original version would create volatile in root fs if
	 * that failed. Is it correct? We should fail because that is
	 * not correct. */
	rc = lus_fid2parent(hcp->ct_priv->lfsh, &hai->hai_fid, 0,
			      &parent_fid, NULL, 0);
	if (rc < 0) {
		log_msg(LUS_LOG_ERROR, rc,
			"cannot get parent fid to restore "DFID" using '%s'",
			PFID(&hai->hai_fid), mnt);
		return rc;
	}

	fd = lus_create_volatile_by_fid(lfsh, &parent_fid, mdt_index,
					open_flags, S_IRUSR | S_IWUSR, NULL);
	if (fd < 0)
		return fd;

	rc = fchown(fd, hcp->stat.st_uid, hcp->stat.st_gid);
	if (rc < 0)
		goto err_cleanup;

	rc = lus_fd2fid(fd, &hai->hai_dfid);
	if (rc < 0)
		goto err_cleanup;

	hcp->data_fd = fd;

	return 0;

err_cleanup:
	hcp->data_fd = -1;
	close(fd);

	return rc;
}

/**
 * Start processing an HSM action.
 * Should be called by copytools just before starting handling a request.
 * It could be skipped if copytool only want to directly report an error,
 * \see lus_hsm_action_end().
 *
 * \param[out] phcp     Opaque action handle to be passed to
 *                      lus_hsm_action_progress and lus_hsm_action_end.
 * \param[in] ct        Copytool handle acquired at registration.
 * \param[in] hai       The hsm_action_item describing the request.
 * \param[in] restore_mdt_index   On restore: MDT index where to create
 *                      the volatile file. Use -1 for default.
 * \param[in] restore_open_flags  On restore: volatile file creation mode. Use
 *                      O_LOV_DELAY_CREATE to manually set the LOVEA
 *                      afterwards.
 * \param[in] is_error  Whether this call is just to report an error.
 *
 * \return 0 on success.
 */
int lus_hsm_action_begin(struct lus_hsm_action_handle **phcp,
			 const struct lus_hsm_ct_handle *ct,
			 const struct hsm_action_item *hai,
			 int restore_mdt_index, int restore_open_flags,
			 bool is_error)
{
	struct lus_hsm_action_handle *hcp;
	int rc;

	hcp = calloc(1, sizeof(*hcp));
	if (hcp == NULL) {
		rc = -ENOMEM;
		goto err_out;
	}

	hcp->data_fd = -1;
	hcp->ct_priv = ct;
	hcp->copy.hc_hai = *hai;
	hcp->copy.hc_hai.hai_len = sizeof(*hai);

	if (is_error)
		goto ok_out;

	if (hai->hai_action == HSMA_RESTORE) {
		rc = lus_mdt_stat_by_fid(ct->lfsh, &hai->hai_fid, &hcp->stat);
		if (rc < 0) {
			log_msg(LUS_LOG_ERROR, rc,
				"cannot get metadata attributes of "DFID
				" in '%s'",
				PFID(&hai->hai_fid), ct->lfsh->mount_path);
			goto err_out;
		}

		rc = create_restore_volatile(hcp, restore_mdt_index,
					     restore_open_flags);
		if (rc < 0)
			goto err_out;
	}

	rc = ioctl(ct->lfsh->mount_fd, LL_IOC_HSM_COPY_START, &hcp->copy);
	if (rc < 0) {
		rc = -errno;
		goto err_out;
	}

ok_out:
	*phcp = hcp;
	return 0;

err_out:
	if (hcp) {
		if (hcp->data_fd != -1)
			close(hcp->data_fd);

		free(hcp);
	}

	*phcp = NULL;

	return rc;
}

/**
 * Terminate an HSM action processing.
 * Should be called by copytools just having finished handling the request.
 *
 * \param[in,out]  phcp    Handle returned by lus_hsm_action_begin.
 * \param[in]      he      The final range of copied data (for copy actions).
 * \param[in]      errval  The status code of the operation.
 * \param[in]      hp_flags   The flags about the termination status
 *                         (HP_FLAG_RETRY if the error is retryable).
 * \return 0 on success.
 */
int lus_hsm_action_end(struct lus_hsm_action_handle **phcp,
		       const struct hsm_extent *he, int hp_flags, int errval)
{
	struct lus_hsm_action_handle *hcp = *phcp;
	struct hsm_action_item *hai = &hcp->copy.hc_hai;
	int rc;

	if (hai->hai_action == HSMA_RESTORE && errval == 0) {
		struct timeval tv[2];

		/* Set {a,m}time of volatile file to that of original. */
		tv[0].tv_sec = hcp->stat.st_atime;
		tv[0].tv_usec = 0;
		tv[1].tv_sec = hcp->stat.st_mtime;
		tv[1].tv_usec = 0;
		if (futimes(hcp->data_fd, tv) < 0) {
			errval = -errno;
			goto end;
		}

		rc = fsync(hcp->data_fd);
		if (rc < 0) {
			errval = -errno;
			goto end;
		}
	}

end:
	/* In some cases, like restore, 2 FIDs are used.
	 * Set the right FID to use here. */
	if (hai->hai_action == HSMA_ARCHIVE || hai->hai_action == HSMA_RESTORE)
		hai->hai_fid = hai->hai_dfid;

	/* Fill the last missing data that will be needed by
	 * kernel to send a hsm_progress. */
	hcp->copy.hc_flags = hp_flags;
	hcp->copy.hc_errval = abs(errval);

	hcp->copy.hc_hai.hai_extent = *he;

	rc = ioctl(hcp->ct_priv->lfsh->mount_fd, LL_IOC_HSM_COPY_END,
		   &hcp->copy);
	if (rc)
		rc = -errno;

	if (hcp->data_fd >= 0)
		close(hcp->data_fd);

	free(hcp);
	*phcp = NULL;

	return rc;
}

/**
 * Notify a progress in processing an HSM action.
 *
 * \param[in,out]  hcp       handle returned by lus_hsm_action_begin.
 * \param[in]      he        the range of copied data (for copy actions).
 * \param[in]      total     the expected total of copied data
 *                           (for copy actions).
 * \param[in]      hp_flags  HSM progress flags.
 *
 * \return 0 on success.
 */
int lus_hsm_action_progress(const struct lus_hsm_action_handle *hcp,
			    const struct hsm_extent *he, uint64_t total,
			    unsigned int hp_flags)
{
	int rc;
	struct hsm_progress hp;
	const struct hsm_action_item *hai = &hcp->copy.hc_hai;

	memset(&hp, 0, sizeof(hp));

	hp.hp_cookie = hai->hai_cookie;
	hp.hp_flags  = hp_flags;

	/* Progress is made on the data fid */
	hp.hp_fid = hai->hai_dfid;
	hp.hp_extent = *he;

	rc = ioctl(hcp->ct_priv->lfsh->mount_fd, LL_IOC_HSM_PROGRESS, &hp);
	if (rc < 0)
		rc = -errno;

	return rc;
}

/**
 * Get the fid of object to be used for copying data.
 *
 * \param[in]  hcp    handle returned by lus_hsm_action_begin.
 * \param[out] fid    Pointer to receive the data FID
 *
 * \retval 0 on success
 * \retval -EINVAL if the action is not a copy operation.
 */
int lus_hsm_action_get_dfid(const struct lus_hsm_action_handle *hcp,
			    struct lu_fid *fid)
{
	const struct hsm_action_item *hai = &hcp->copy.hc_hai;

	if (hai->hai_action != HSMA_RESTORE && hai->hai_action != HSMA_ARCHIVE)
		return -EINVAL;

	*fid = hai->hai_dfid;

	return 0;
}

/**
 * Get a file descriptor to be used for copying data. It's up to the
 * caller to close the FDs obtained from this function.
 *
 * \param[in]  hcp    handle returned by lus_hsm_action_begin.
 *
 * \retval a file descriptor on success.
 * \retval a negative errno on failure.
 * \retval -EINVAL if the action is not a copy operation.
 */
int lus_hsm_action_get_fd(const struct lus_hsm_action_handle *hcp)
{
	const struct hsm_action_item *hai = &hcp->copy.hc_hai;
	int fd;

	if (hai->hai_action == HSMA_ARCHIVE) {
		return lus_open_by_fid(hcp->ct_priv->lfsh, &hai->hai_dfid,
				       O_RDONLY | O_NOATIME |
				       O_NOFOLLOW | O_NONBLOCK);
	} else if (hai->hai_action == HSMA_RESTORE) {
		fd = dup(hcp->data_fd);
		return fd < 0 ? -errno : fd;
	} else {
		return -EINVAL;
	}
}

/**
 * Import an existing hsm-archived file into Lustre.
 *
 * \param dst      path to Lustre destination (e.g. /mnt/lustre/my/file).
 * \param archive  archive number.
 * \param st       struct stat buffer containing file ownership, perm, etc.
 * \param layout   file layout. Can be NULL to select a default one.
 *
 * \retval    open fd of the newly imported file on success
 * \retval    a negative errno
 */
int lus_hsm_import(const char *dst, int archive, const struct stat *st,
		   struct lus_layout *layout)
{
	struct hsm_user_import	 hui;
	int			 fd = -1;
	int			 rc = 0;
	struct lus_layout	*def_layout = NULL;

	/* We need a layout. */
	if (layout == NULL) {
		rc = lus_layout_alloc(0, &def_layout);

		if (rc) {
			log_msg(LUS_LOG_ERROR, -rc,
				"cannot allocate a new layout for import");
			return rc;
		}
		layout = def_layout;
	}

	if (lus_layout_pattern_set_flags(layout,
					 LLAPI_LAYOUT_RELEASED) != 0) {
		log_msg(LUS_LOG_ERROR, EINVAL,
			"invalid striping information for importing '%s'",
			dst);
		rc = -EINVAL;
		goto out;
	}

	fd = lus_layout_file_open(dst, O_CREAT | O_WRONLY, st->st_mode, layout);
	if (fd < 0) {
		log_msg(LUS_LOG_ERROR, fd,
			    "cannot create '%s' for import", dst);
		rc = fd;
		goto out;
	}

	hui.hui_uid = st->st_uid;
	hui.hui_gid = st->st_gid;
	hui.hui_mode = st->st_mode;
	hui.hui_size = st->st_size;
	hui.hui_archive_id = archive;
	hui.hui_atime = st->st_atime;
	hui.hui_atime_ns = st->st_atim.tv_nsec;
	hui.hui_mtime = st->st_mtime;
	hui.hui_mtime_ns = st->st_mtim.tv_nsec;
	rc = ioctl(fd, LL_IOC_HSM_IMPORT, &hui);
	if (rc != 0) {
		rc = -errno;
		log_msg(LUS_LOG_ERROR, rc, "cannot import '%s'", dst);
		unlink(dst);
		goto out;
	}

out:
	lus_layout_free(def_layout);

	if (rc) {
		if (fd >= 0)
			close(fd);
		return rc;
	} else {
		return fd;
	}
}

/**
 * Return the current HSM states and HSM requests related to a file.
 *
 * \param[in]   fd     Opened file descriptor of a Lustre file
 * \param[out]  hus    Structure allocated by caller, which will be filled
 *                     up with the current HSM file states.
 *
 * \retval 0 on success.
 * \retval negative errno on error.
 */
int lus_hsm_state_get_fd(int fd, struct hsm_user_state *hus)
{
	int rc;

	rc = ioctl(fd, LL_IOC_HSM_STATE_GET, hus);

	return rc ? -errno : 0;
}

/**
 * Return the current HSM states and HSM requests related to the file
 * pointed by \a path.
 *
 * \param[in]   path    Full file name
 * \param[out]  hus     Structure allocated by caller, which will be filled
 *                      up with the current HSM file states.
 *
 * \retval 0 on success.
 * \retval negative errno on error.
 */
int lus_hsm_state_get(const char *path, struct hsm_user_state *hus)
{
	int fd;
	int rc;

	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	rc = lus_hsm_state_get_fd(fd, hus);

	close(fd);
	return rc;
}

/**
 * Set HSM states of file.
 *
 * Change the current HSM states for this file using the provided
 * bitmasks. The archive number will be updated to \a archive_id,
 * unless its value is zero.
 *
 * \param fd           Opened file descriptor of a Lustre file
 * \param setmask      Bitmask for flag to be set (enum hsm_states).
 * \param clearmask    Bitmask for flag to be cleared (enum hsm_states).
 * \param archive_id   Archive number identifier to use. 0 means no change.
 *
 * \retval 0 on success.
 * \retval a negative errno on error.
 */
int lus_hsm_state_set_fd(int fd, uint64_t setmask, uint64_t clearmask,
			 unsigned int archive_id)
{
	struct hsm_state_set hss;
	int rc;

	hss.hss_valid = HSS_SETMASK|HSS_CLEARMASK;
	hss.hss_setmask = setmask;
	hss.hss_clearmask = clearmask;

	/* Change archive_id if provided. We can only change
	 * to set something different than 0. */
	if (archive_id > 0) {
		hss.hss_valid |= HSS_ARCHIVE_ID;
		hss.hss_archive_id = archive_id;
	}
	rc = ioctl(fd, LL_IOC_HSM_STATE_SET, &hss);

	return rc ? -errno : 0;
}

/**
 * Set HSM states of file pointed by \a path.
 *
 * see lus_hsm_state_set_fd() for args use and return
 */
int lus_hsm_state_set(const char *path, uint64_t setmask, uint64_t clearmask,
		      unsigned int archive_id)
{
	int fd;
	int rc;

	fd = open(path, O_WRONLY | O_LOV_DELAY_CREATE | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	rc = lus_hsm_state_set_fd(fd, setmask, clearmask, archive_id);

	close(fd);
	return rc;
}

/**
 * Return the current HSM request related to file pointed by \a path.
 *
 * \param[in] path     Fullpath to the file to operate on.
 * \param[in,out] hca  Should be allocated by caller. Will be filled
 *		       with current file actions.
 *
 * \retval 0 on success.
 * \retval -errno on error.
 */
int lus_hsm_current_action(const char *path, struct hsm_current_action *hca)
{
	int fd;
	int rc;

	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	rc = ioctl(fd, LL_IOC_HSM_ACTION, hca);
	/* If error, save errno value */
	rc = rc ? -errno : 0;

	close(fd);
	return rc;
}

/**
 * Send a HSM request to Lustre.
 *
 * \param[in]  lfsh     An opened Lustre fs opaque handle
 * \param[in]  request  The request, of at least lus_hsm_user_request_len
 *                      bytes long.
 *
 * \return 0 on success, or a negative errno on error.
 */
int lus_hsm_request(const struct lus_fs_handle *lfsh,
		    const struct hsm_user_request *request)
{
	int rc;

	rc = ioctl(lfsh->mount_fd, LL_IOC_HSM_REQUEST, request);

	return rc ? -errno : 0;
}

/**
 * Returns the first action item after the action list header.
 *
 * \param[in]  hal    the action list
 *
 * \retval  a pointer to the first action item in the list
 */
const struct hsm_action_item *
lus_hsm_hai_first(const struct hsm_action_list *hal)
{
	const char *p;

	/* Find end of string. */
	p = hal->hal_fsname;
	while (*p)
		p++;

	/* Add 1 for the NUL character, plus 7 for the rounding to the
	 * 8 bytes boundary operation */
	p += 8;

	return (struct hsm_action_item *)((uintptr_t)p & ~7);
}

/**
 * Returns the next action item in the action list.
 *
 * \param[in]  hai    the previous action item
 *
 * \retval  a pointer to the next action item in the list
 */
const struct hsm_action_item *
lus_hsm_hai_next(const struct hsm_action_item *hai)
{
	return (struct hsm_action_item *)
		(((uintptr_t)hai + hai->hai_len + 7) & ~7);
}
