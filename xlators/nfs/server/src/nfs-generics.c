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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "string.h"

#include "inode.h"
#include "nfs.h"
#include "nfs-fops.h"
#include "nfs-inodes.h"
#include "nfs-generics.h"
#include "xlator.h"


int
nfs_fstat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           fop_stat_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!fd) || (!nfu))
                return ret;

        ret = nfs_fop_fstat (nfsx, xl, nfu, fd, cbk, local);
        return ret;
}

int
nfs_access (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            int32_t accesstest, fop_access_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_access (nfsx, xl, nfu, pathloc, accesstest, cbk, local);

        return ret;
}

int
nfs_stat (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
          fop_stat_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_stat (nfsx, xl, nfu, pathloc, cbk, local);

        return ret;
}


int
nfs_readdirp (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *dirfd,
              size_t bufsize, off_t offset, fop_readdir_cbk_t cbk, void *local)
{
        if ((!nfsx) || (!xl) || (!dirfd) || (!nfu))
                return -EFAULT;

        return nfs_fop_readdirp (nfsx, xl, nfu, dirfd, bufsize, offset, cbk,
                                 local);
}


int
nfs_lookup (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_lookup_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_lookup (nfsx, xl, nfu, pathloc, cbk, local);
        return ret;
}

int
nfs_create (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            int flags, mode_t mode, fop_create_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_create (nfsx, xl, nfu, pathloc, flags, mode, cbk,local);
        return ret;
}


int
nfs_flush (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           fop_flush_cbk_t cbk, void *local)
{
        return nfs_fop_flush (nfsx, xl, nfu, fd, cbk, local);
}



int
nfs_mkdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
           mode_t mode, fop_mkdir_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_mkdir (nfsx, xl, nfu, pathloc, mode, cbk, local);
        return ret;
}



int
nfs_truncate (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
              off_t offset, fop_truncate_cbk_t cbk, void *local)
{
        int     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_truncate (nfsx, xl, nfu, pathloc, offset, cbk, local);
        return ret;
}


int
nfs_read (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd, size_t size,
          off_t offset, fop_readv_cbk_t cbk, void *local)
{
        return nfs_fop_read (nfsx, xl, nfu, fd, size, offset, cbk, local);
}


int
nfs_fsync (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           int32_t datasync, fop_fsync_cbk_t cbk, void *local)
{
        return nfs_fop_fsync (nfsx, xl, nfu, fd, datasync, cbk, local);
}


int
nfs_write (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, fd_t *fd,
           struct iobuf *srciob, struct iovec *vector, int32_t count,
           off_t offset, fop_writev_cbk_t cbk, void *local)
{
        return nfs_fop_write (nfsx, xl, nfu, fd, srciob, vector, count, offset,
                              cbk, local);
}


int
nfs_open (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
          int32_t flags, fop_open_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_open (nfsx, xl, nfu, pathloc, flags, GF_OPEN_NOWB, cbk,
                              local);
        return ret;
}


int
nfs_rename (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
            loc_t *newloc, fop_rename_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!oldloc) || (!newloc) || (!nfu))
                return ret;

        ret = nfs_inode_rename (nfsx, xl, nfu, oldloc, newloc, cbk, local);
        return ret;
}


int
nfs_link (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
          loc_t *newloc, fop_link_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!oldloc) || (!newloc) || (!nfu))
                return ret;

        ret = nfs_inode_link (nfsx, xl, nfu, oldloc, newloc, cbk, local);
        return ret;
}


int
nfs_unlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_unlink_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_unlink (nfsx, xl, nfu, pathloc, cbk, local);
        return ret;
}


int
nfs_rmdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *path,
           fop_rmdir_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!path) || (!nfu))
                return ret;

        ret = nfs_inode_rmdir (nfsx, xl, nfu, path, cbk, local);
        return ret;
}


int
nfs_mknod (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
           mode_t mode, dev_t dev, fop_mknod_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_mknod (nfsx, xl, nfu, pathloc, mode, dev, cbk, local);
        return ret;
}


int
nfs_readlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *linkloc,
              fop_readlink_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!linkloc) || (!nfu))
                return ret;

        ret = nfs_fop_readlink (nfsx, xl, nfu, linkloc, NFS_PATH_MAX, cbk,
                                local);
        return ret;
}


int
nfs_symlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, char *target,
             loc_t *linkloc, fop_symlink_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!linkloc) || (!target) || (!nfu))
                return ret;

        ret = nfs_inode_symlink (nfsx, xl, nfu, target, linkloc, cbk, local);
        return ret;
}



int
nfs_setattr (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
             struct iatt *buf, int32_t valid, fop_setattr_cbk_t cbk,
             void *local)
{
        int     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_setattr (nfsx, xl, nfu, pathloc, buf, valid, cbk, local);
        return ret;
}


int
nfs_statfs (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_statfs_cbk_t cbk, void *local)
{
        int     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_statfs (nfsx, xl, nfu, pathloc, cbk, local);
        return ret;
}

int
nfs_opendir (xlator_t *nfsx, xlator_t *fopxl, nfs_user_t *nfu, loc_t *pathloc,
             fop_opendir_cbk_t cbk, void *local)
{
        if ((!nfsx) || (!fopxl) || (!pathloc) || (!nfu))
                return -EFAULT;

        return nfs_inode_opendir (nfsx, fopxl, nfu, pathloc, cbk, local);
}
