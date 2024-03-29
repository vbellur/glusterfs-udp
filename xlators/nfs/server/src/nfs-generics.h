/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _NFS_GENERICS_H_
#define _NFS_GENERICS_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "nfs.h"
#include "xlator.h"
#include "nfs-fops.h"
#include "nfs-inodes.h"

struct nfs_direntcache {
        gf_dirent_t entries;            /* Head of list of cached dirents. */
        gf_dirent_t *next;              /* Pointer to the next entry that
                                         * should be sent by readdir */
        uint64_t prev_off;              /* Offset where the next read will
                                         * happen.
                                         */
};

/* WE're trying to abstract the fops interface from the NFS xlator so that
 * different NFS versions can simply call a standard interface and have fop
 * interface dependent functions be handled internally.
 * This structure is part of such an  abstraction. The fops layer stores any
 * state is requires in the fd. For eg, the dirent cache for a directory fd_t.
 */
typedef struct nfs_fop_fdcontext {
        pthread_mutex_t         lock;
        size_t                  dirent_bufsize;
        off_t                   offset;
        struct nfs_direntcache  *dcache;
        xlator_t                *dirvol;
} nfs_fdctx_t;

extern int
nfs_fstat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           fop_stat_cbk_t cbk, void *local);

extern int
nfs_readdirp (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *dirfd,
              size_t bufsize, off_t offset, fop_readdir_cbk_t cbk, void *local);


extern int
nfs_lookup (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_lookup_cbk_t cbk, void *local);

extern int
nfs_create (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            int flags, mode_t mode, fop_create_cbk_t cbk, void *local);

extern int
nfs_flush (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           fop_flush_cbk_t cbk, void *local);

extern int
nfs_mkdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
           mode_t mode, fop_mkdir_cbk_t cbk, void *local);

extern int
nfs_truncate (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
              off_t offset, fop_truncate_cbk_t cbk, void *local);

extern int
nfs_read (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd, size_t size,
          off_t offset, fop_readv_cbk_t cbk, void *local);

extern int
nfs_fsync (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           int32_t datasync, fop_fsync_cbk_t cbk, void *local);

extern int
nfs_write (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           struct iobuf *srciob, struct iovec *vector, int32_t count,
           off_t offset, fop_writev_cbk_t cbk, void *local);

extern int
nfs_open (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
          int32_t flags, fop_open_cbk_t cbk, void *local);

extern int
nfs_rename (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
            loc_t *newloc, fop_rename_cbk_t cbk, void *local);

extern int
nfs_link (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
          loc_t *newloc, fop_link_cbk_t cbk, void *local);

extern int
nfs_unlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_unlink_cbk_t cbk, void *local);

extern int
nfs_rmdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
           fop_rmdir_cbk_t cbk, void *local);

extern int
nfs_mknod (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
           mode_t mode, dev_t dev, fop_mknod_cbk_t cbk, void *local);

extern int
nfs_readlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *linkloc,
              fop_readlink_cbk_t cbk, void *local);

extern int
nfs_setattr (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
             struct iatt *buf, int32_t valid, fop_setattr_cbk_t cbk,
             void *local);

extern int
nfs_statfs (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_statfs_cbk_t cbk, void *local);

extern int
nfs_stat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
          fop_stat_cbk_t cbk, void *local);

extern int
nfs_symlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, char *target,
             loc_t *linkloc, fop_symlink_cbk_t cbk, void *local);

/* Synchronous equivalents */

extern call_stub_t *
nfs_open_sync (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
               int32_t flags);

extern call_stub_t *
nfs_write_sync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, struct iobuf *srciob,
                struct iovec *vec, int count, off_t offset);

extern call_stub_t *
nfs_read_sync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, size_t size,
               off_t offset);

extern int
nfs_opendir (xlator_t *nfsx, xlator_t *fopxl, nfs_user_t *nfu, loc_t *pathloc,
             fop_opendir_cbk_t cbk, void *local);

extern int
nfs_access (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            int32_t accesstest, fop_access_cbk_t cbk, void *local);
#endif
