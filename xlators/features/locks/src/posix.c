/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "locks.h"
#include "common.h"
#include "statedump.h"

#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif /* LLONG_MAX */

/* Forward declarations */


void do_blocked_rw (pl_inode_t *);
static int __rw_allowable (pl_inode_t *, posix_lock_t *, glusterfs_fop_t);

struct _truncate_ops {
        loc_t  loc;
        fd_t  *fd;
        off_t  offset;
        enum {TRUNCATE, FTRUNCATE} op;
};

static pl_fdctx_t *
pl_new_fdctx ()
{
        pl_fdctx_t *fdctx = NULL;

        fdctx = GF_CALLOC (1, sizeof (*fdctx),
                           gf_locks_mt_pl_fdctx_t);
        GF_VALIDATE_OR_GOTO (POSIX_LOCKS, fdctx, out);

        INIT_LIST_HEAD (&fdctx->locks_list);

out:
        return fdctx;
}

static pl_fdctx_t *
pl_check_n_create_fdctx (xlator_t *this, fd_t *fd)
{
        int         ret   = 0;
        uint64_t    tmp   = 0;
        pl_fdctx_t *fdctx = NULL;

        GF_VALIDATE_OR_GOTO (POSIX_LOCKS, this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                ret = __fd_ctx_get (fd, this, &tmp);
                if ((ret != 0) || (tmp == 0)) {
                        fdctx = pl_new_fdctx ();
                        if (fdctx == NULL) {
                                goto unlock;
                        }
                }

                ret = __fd_ctx_set (fd, this, (uint64_t)(long)fdctx);
                if (ret != 0) {
                        GF_FREE (fdctx);
                        fdctx = NULL;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to set fd ctx");
                }
        }
unlock:
        UNLOCK (&fd->lock);

out:
        return fdctx;
}

int
pl_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf)
{
        struct _truncate_ops *local = NULL;

        local = frame->local;

        if (local->op == TRUNCATE)
                loc_wipe (&local->loc);

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
        return 0;
}


static int
truncate_allowed (pl_inode_t *pl_inode,
                  void *transport, pid_t client_pid,
                  uint64_t owner, off_t offset)
{
        posix_lock_t *l = NULL;
        posix_lock_t  region = {.list = {0, }, };
        int           ret = 1;

        region.fl_start   = offset;
        region.fl_end     = LLONG_MAX;
        region.transport  = transport;
        region.client_pid = client_pid;
        region.owner      = owner;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry (l, &pl_inode->ext_list, list) {
                        if (!l->blocked
                            && locks_overlap (&region, l)
                            && !same_owner (&region, l)) {
                                ret = 0;
                                gf_log (POSIX_LOCKS, GF_LOG_TRACE, "Truncate "
                                        "allowed");
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        return ret;
}


static int
truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        posix_locks_private_t *priv = NULL;
        struct _truncate_ops  *local = NULL;
        inode_t               *inode = NULL;
        pl_inode_t            *pl_inode = NULL;


        priv = this->private;
        local = frame->local;

        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "got error (errno=%d, stderror=%s) from child",
                        op_errno, strerror (op_errno));
                goto unwind;
        }

        if (local->op == TRUNCATE)
                inode = local->loc.inode;
        else
                inode = local->fd->inode;

        pl_inode = pl_inode_get (this, inode);
        if (!pl_inode) {
                op_ret   = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        if (priv->mandatory
            && pl_inode->mandatory
            && !truncate_allowed (pl_inode, frame->root->trans,
                                  frame->root->pid, frame->root->lk_owner,
                                  local->offset)) {
                op_ret   = -1;
                op_errno = EAGAIN;
                goto unwind;
        }

        switch (local->op) {
        case TRUNCATE:
                STACK_WIND (frame, pl_truncate_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->truncate,
                            &local->loc, local->offset);
                break;
        case FTRUNCATE:
                STACK_WIND (frame, pl_truncate_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->ftruncate,
                            local->fd, local->offset);
                break;
        }

        return 0;

unwind:
        gf_log (this->name, GF_LOG_ERROR, "truncate failed with ret: %d, "
                "error: %s", op_ret, strerror (op_errno));
        if (local->op == TRUNCATE)
                loc_wipe (&local->loc);

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, buf, NULL);
        return 0;
}


int
pl_truncate (call_frame_t *frame, xlator_t *this,
             loc_t *loc, off_t offset)
{
        struct _truncate_ops *local = NULL;

        local = GF_CALLOC (1, sizeof (struct _truncate_ops),
                           gf_locks_mt_truncate_ops);
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        local->op         = TRUNCATE;
        local->offset     = offset;
        loc_copy (&local->loc, loc);

        frame->local = local;

        STACK_WIND (frame, truncate_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);

        return 0;

unwind:
        gf_log (this->name, GF_LOG_ERROR, "truncate for %s failed with ret: %d, "
                "error: %s", loc->path, -1, strerror (ENOMEM));
        STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int
pl_ftruncate (call_frame_t *frame, xlator_t *this,
              fd_t *fd, off_t offset)
{
        struct _truncate_ops *local = NULL;

        local = GF_CALLOC (1, sizeof (struct _truncate_ops),
                           gf_locks_mt_truncate_ops);
        GF_VALIDATE_OR_GOTO (this->name, local, unwind);

        local->op         = FTRUNCATE;
        local->offset     = offset;
        local->fd         = fd;

        frame->local = local;

        STACK_WIND (frame, truncate_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd);
        return 0;

unwind:
        gf_log (this->name, GF_LOG_ERROR, "ftruncate failed with ret: %d, "
                "error: %s", -1, strerror (ENOMEM));
        STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}

static void
delete_locks_of_fd (xlator_t *this, pl_inode_t *pl_inode, fd_t *fd)
{
       posix_lock_t *tmp = NULL;
       posix_lock_t *l = NULL;

       struct list_head blocked_list;

       INIT_LIST_HEAD (&blocked_list);

       pthread_mutex_lock (&pl_inode->mutex);
       {

               list_for_each_entry_safe (l, tmp, &pl_inode->ext_list, list) {
                       if ((l->fd_num == fd_to_fdnum(fd))) {
                               if (l->blocked) {
                                       list_move_tail (&l->list, &blocked_list);
                                       continue;
                               }
                               __delete_lock (pl_inode, l);
                               __destroy_lock (l);
                       }
               }

       }
       pthread_mutex_unlock (&pl_inode->mutex);

       list_for_each_entry_safe (l, tmp, &blocked_list, list) {
               list_del_init(&l->list);
               STACK_UNWIND_STRICT (lk, l->frame, -1, EAGAIN, &l->user_flock);
               __destroy_lock (l);
       }

        grant_blocked_locks (this, pl_inode);

        do_blocked_rw (pl_inode);

}

static void
__delete_locks_of_owner (pl_inode_t *pl_inode,
                         void *transport, uint64_t owner)
{
        posix_lock_t *tmp = NULL;
        posix_lock_t *l = NULL;

        /* TODO: what if it is a blocked lock with pending l->frame */

        list_for_each_entry_safe (l, tmp, &pl_inode->ext_list, list) {
                if ((l->transport == transport)
                    && (l->owner == owner)) {
                        gf_log ("posix-locks", GF_LOG_TRACE,
                                " Flushing lock"
                                "%s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" state: %s",
                                l->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                l->client_pid,
                                l->owner,
                                l->user_flock.l_start,
                                l->user_flock.l_len,
                                l->blocked == 1 ? "Blocked" : "Active");

                        __delete_lock (pl_inode, l);
                        __destroy_lock (l);
                }
        }

        return;
}

int32_t
pl_opendir_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     fd_t *fd)
{
        pl_fdctx_t *fdctx = NULL;

        if (op_ret < 0)
                goto unwind;

        fdctx = pl_check_n_create_fdctx (this, fd);
        if (!fdctx) {
                op_errno = ENOMEM;
                op_ret   = -1;
                goto unwind;
        }

unwind:
        STACK_UNWIND_STRICT (opendir,
                             frame,
                             op_ret,
                             op_errno,
                             fd);
        return 0;
}

int32_t
pl_opendir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, fd_t *fd)
{
        STACK_WIND (frame,
                    pl_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc, fd);
        return 0;

}

int
pl_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);

        return 0;
}


int
pl_flush (call_frame_t *frame, xlator_t *this,
          fd_t *fd)
{
        pl_inode_t            *pl_inode = NULL;
        uint64_t              owner     = -1;

        owner = frame->root->lk_owner;

        pl_inode = pl_inode_get (this, fd->inode);

        if (!pl_inode) {
                gf_log (this->name, GF_LOG_DEBUG, "Could not get inode.");
                STACK_UNWIND_STRICT (flush, frame, -1, EBADFD);
                return 0;
        }

        pl_trace_flush (this, frame, fd);

        if (owner == 0) {
                /* Handle special case when protocol/server sets lk-owner to zero.
                 * This usually happens due to a client disconnection. Hence, free
                 * all locks opened with this fd.
                 */
                gf_log (this->name, GF_LOG_TRACE,
                        "Releasing all locks with fd %p", fd);
                delete_locks_of_fd (this, pl_inode, fd);
                goto wind;

        }
        pthread_mutex_lock (&pl_inode->mutex);
        {
                __delete_locks_of_owner (pl_inode, frame->root->trans,
                                         owner);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        grant_blocked_locks (this, pl_inode);

        do_blocked_rw (pl_inode);

wind:
        STACK_WIND (frame, pl_flush_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush, fd);
        return 0;
}


int
pl_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        pl_fdctx_t *fdctx = NULL;

        if (op_ret < 0)
                goto unwind;

        fdctx = pl_check_n_create_fdctx (this, fd);
        if (!fdctx) {
                op_errno = ENOMEM;
                op_ret   = -1;
                goto unwind;
        }

unwind:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);

        return 0;
}


int
pl_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        /* why isn't O_TRUNC being handled ? */
        STACK_WIND (frame, pl_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags & ~O_TRUNC, fd, wbflags);

        return 0;
}


int
pl_create_cbk (call_frame_t *frame, void *cookie,
               xlator_t *this, int32_t op_ret, int32_t op_errno,
               fd_t *fd, inode_t *inode, struct iatt *buf,
               struct iatt *preparent, struct iatt *postparent)
{
        pl_fdctx_t *fdctx = NULL;

        if (op_ret < 0)
                goto unwind;

        fdctx = pl_check_n_create_fdctx (this, fd);
        if (!fdctx) {
                op_errno = ENOMEM;
                op_ret   = -1;
                goto unwind;
        }

unwind:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);

        return 0;
}


int
pl_create (call_frame_t *frame, xlator_t *this,
           loc_t *loc, int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
        STACK_WIND (frame, pl_create_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->create,
                    loc, flags, mode, fd, params);
        return 0;
}


int
pl_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno,
              struct iovec *vector, int32_t count, struct iatt *stbuf,
              struct iobref *iobref)
{
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             vector, count, stbuf, iobref);

        return 0;
}

int
pl_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf)
{
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


void
do_blocked_rw (pl_inode_t *pl_inode)
{
        struct list_head  wind_list;
        pl_rw_req_t      *rw = NULL;
        pl_rw_req_t      *tmp = NULL;

        INIT_LIST_HEAD (&wind_list);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (rw, tmp, &pl_inode->rw_list, list) {
                        if (__rw_allowable (pl_inode, &rw->region,
                                            rw->stub->fop)) {
                                list_del_init (&rw->list);
                                list_add_tail (&rw->list, &wind_list);
                        }
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (rw, tmp, &wind_list, list) {
                list_del_init (&rw->list);
                call_resume (rw->stub);
                GF_FREE (rw);
        }

        return;
}


static int
__rw_allowable (pl_inode_t *pl_inode, posix_lock_t *region,
                glusterfs_fop_t op)
{
        posix_lock_t *l = NULL;
        int           ret = 1;

        list_for_each_entry (l, &pl_inode->ext_list, list) {
                if (locks_overlap (l, region) && !same_owner (l, region)) {
                        if ((op == GF_FOP_READ) && (l->fl_type != F_WRLCK))
                                continue;
                        ret = 0;
                        break;
                }
        }

        return ret;
}


int
pl_readv_cont (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t offset)
{
        STACK_WIND (frame, pl_readv_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                    fd, size, offset);

        return 0;
}


int
pl_readv (call_frame_t *frame, xlator_t *this,
          fd_t *fd, size_t size, off_t offset)
{
        posix_locks_private_t *priv = NULL;
        pl_inode_t            *pl_inode = NULL;
        pl_rw_req_t           *rw = NULL;
        posix_lock_t           region = {.list = {0, }, };
        int                    op_ret = 0;
        int                    op_errno = 0;
        char                   wind_needed = 1;


        priv = this->private;
        pl_inode = pl_inode_get (this, fd->inode);

        if (priv->mandatory && pl_inode->mandatory) {
                region.fl_start   = offset;
                region.fl_end     = offset + size - 1;
                region.transport  = frame->root->trans;
                region.fd_num     = fd_to_fdnum(fd);
                region.client_pid = frame->root->pid;
                region.owner      = frame->root->lk_owner;

                pthread_mutex_lock (&pl_inode->mutex);
                {
                        wind_needed = __rw_allowable (pl_inode, &region,
                                                      GF_FOP_READ);
                        if (wind_needed) {
                                goto unlock;
                        }

                        if (fd->flags & O_NONBLOCK) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "returning EAGAIN as fd is O_NONBLOCK");
                                op_errno = EAGAIN;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw = GF_CALLOC (1, sizeof (*rw),
                                        gf_locks_mt_pl_rw_req_t);
                        if (!rw) {
                                op_errno = ENOMEM;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw->stub = fop_readv_stub (frame, pl_readv_cont,
                                                   fd, size, offset);
                        if (!rw->stub) {
                                op_errno = ENOMEM;
                                op_ret = -1;
                                GF_FREE (rw);
                                goto unlock;
                        }

                        rw->region = region;

                        list_add_tail (&rw->list, &pl_inode->rw_list);
                }
        unlock:
                pthread_mutex_unlock (&pl_inode->mutex);
        }


        if (wind_needed) {
                STACK_WIND (frame, pl_readv_cbk,
                            FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                            fd, size, offset);
        }

        if (op_ret == -1)
                STACK_UNWIND_STRICT (readv, frame, -1, op_errno,
                                     NULL, 0, NULL, NULL);

        return 0;
}


int
pl_writev_cont (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int count, off_t offset,
                struct iobref *iobref)
{
        STACK_WIND (frame, pl_writev_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
                    fd, vector, count, offset, iobref);

        return 0;
}


int
pl_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
           struct iovec *vector, int32_t count, off_t offset,
           struct iobref *iobref)
{
        posix_locks_private_t *priv = NULL;
        pl_inode_t            *pl_inode = NULL;
        pl_rw_req_t           *rw = NULL;
        posix_lock_t           region = {.list = {0, }, };
        int                    op_ret = 0;
        int                    op_errno = 0;
        char                   wind_needed = 1;


        priv = this->private;
        pl_inode = pl_inode_get (this, fd->inode);

        if (priv->mandatory && pl_inode->mandatory) {
                region.fl_start   = offset;
                region.fl_end     = offset + iov_length (vector, count) - 1;
                region.transport  = frame->root->trans;
                region.fd_num     = fd_to_fdnum(fd);
                region.client_pid = frame->root->pid;
                region.owner      = frame->root->lk_owner;

                pthread_mutex_lock (&pl_inode->mutex);
                {
                        wind_needed = __rw_allowable (pl_inode, &region,
                                                      GF_FOP_WRITE);
                        if (wind_needed)
                                goto unlock;

                        if (fd->flags & O_NONBLOCK) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "returning EAGAIN because fd is "
                                        "O_NONBLOCK");
                                op_errno = EAGAIN;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw = GF_CALLOC (1, sizeof (*rw),
                                        gf_locks_mt_pl_rw_req_t);
                        if (!rw) {
                                op_errno = ENOMEM;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw->stub = fop_writev_stub (frame, pl_writev_cont,
                                                    fd, vector, count, offset,
                                                    iobref);
                        if (!rw->stub) {
                                op_errno = ENOMEM;
                                op_ret = -1;
                                GF_FREE (rw);
                                goto unlock;
                        }

                        rw->region = region;

                        list_add_tail (&rw->list, &pl_inode->rw_list);
                }
        unlock:
                pthread_mutex_unlock (&pl_inode->mutex);
        }


        if (wind_needed)
                STACK_WIND (frame, pl_writev_cbk,
                            FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
                            fd, vector, count, offset, iobref);

        if (op_ret == -1)
                STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);

        return 0;
}

static int
__fd_has_locks (pl_inode_t *pl_inode, fd_t *fd)
{
        int           found = 0;
        posix_lock_t *l     = NULL;

        list_for_each_entry (l, &pl_inode->ext_list, list) {
                if ((l->fd_num == fd_to_fdnum(fd))) {
                        found = 1;
                        break;
                }
        }

        return found;
}

static posix_lock_t *
lock_dup (posix_lock_t *lock)
{
        posix_lock_t *new_lock = NULL;

        new_lock = new_posix_lock (&lock->user_flock, lock->transport,
                                   lock->client_pid, lock->owner,
                                   (fd_t *)lock->fd_num);
        return new_lock;
}

static int
__dup_locks_to_fdctx (pl_inode_t *pl_inode, fd_t *fd,
                      pl_fdctx_t *fdctx)
{
        posix_lock_t *l        = NULL;
        posix_lock_t *duplock = NULL;
        int ret = 0;

        list_for_each_entry (l, &pl_inode->ext_list, list) {
                if ((l->fd_num == fd_to_fdnum(fd))) {
                        duplock = lock_dup (l);
                        if (!duplock) {
                                ret = -1;
                                break;
                        }

                        list_add_tail (&duplock->list, &fdctx->locks_list);
                }
        }

        return ret;
}

static int
__copy_locks_to_fdctx (pl_inode_t *pl_inode, fd_t *fd,
                      pl_fdctx_t *fdctx)
{
        int ret = 0;

        ret = __dup_locks_to_fdctx (pl_inode, fd, fdctx);
        if (ret)
                goto out;

out:
        return ret;

}

static void
pl_mark_eol_lock (posix_lock_t *lock)
{
        lock->user_flock.l_type = GF_LK_EOL;
        return;
}

static posix_lock_t *
__get_next_fdctx_lock (pl_fdctx_t *fdctx)
{
        posix_lock_t *lock = NULL;

        GF_ASSERT (fdctx);

        if (list_empty (&fdctx->locks_list)) {
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "fdctx lock list empty");
                goto out;
        }

        lock = list_entry (fdctx->locks_list.next, typeof (*lock),
                           list);

        GF_ASSERT (lock);

        list_del_init (&lock->list);

out:
        return lock;
}

static int
__set_next_lock_fd (pl_fdctx_t *fdctx, posix_lock_t *reqlock)
{
        posix_lock_t *lock  = NULL;
        int           ret   = 0;

        GF_ASSERT (fdctx);

        lock = __get_next_fdctx_lock (fdctx);
        if (!lock) {
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "marking EOL in reqlock");
                pl_mark_eol_lock (reqlock);
                goto out;
        }

        reqlock->user_flock  = lock->user_flock;
        reqlock->fl_start    = lock->fl_start;
        reqlock->fl_type     = lock->fl_type;
        reqlock->fl_end      = lock->fl_end;
        reqlock->owner       = lock->owner;

out:
        if (lock)
                __destroy_lock (lock);

        return ret;
}

static int
pl_getlk_fd (xlator_t *this, pl_inode_t *pl_inode,
             fd_t *fd, posix_lock_t *reqlock)
{
        uint64_t    tmp   = 0;
        pl_fdctx_t *fdctx = NULL;
        int         ret   = 0;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                if (!__fd_has_locks (pl_inode, fd)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "fd=%p has no active locks", fd);
                        ret = 0;
                        goto unlock;
                }

                gf_log (this->name, GF_LOG_DEBUG,
                        "There are active locks on fd");

                ret = fd_ctx_get (fd, this, &tmp);
                fdctx = (pl_fdctx_t *)(long) tmp;

                if (list_empty (&fdctx->locks_list)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "no fdctx -> copying all locks on fd");

                        ret = __copy_locks_to_fdctx (pl_inode, fd, fdctx);
                        if (ret) {
                                goto unlock;
                        }

                        ret = __set_next_lock_fd (fdctx, reqlock);

                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "fdctx present -> returning the next lock");
                        ret = __set_next_lock_fd (fdctx, reqlock);
                        if (ret) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "could not get next lock of fd");
                                goto unlock;
                        }
                }
        }

unlock:
        pthread_mutex_unlock (&pl_inode->mutex);
        return ret;

}

int
pl_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd, struct gf_flock *flock)
{
        void                  *transport = NULL;
        pid_t                  client_pid = 0;
        uint64_t               owner      = 0;
        pl_inode_t            *pl_inode = NULL;
        int                    op_ret = 0;
        int                    op_errno = 0;
        int                    can_block = 0;
        posix_lock_t          *reqlock = NULL;
        posix_lock_t          *conf = NULL;
        int                    ret = 0;

        transport  = frame->root->trans;
        client_pid = frame->root->pid;
        owner      = frame->root->lk_owner;

        if ((flock->l_start < 0) || (flock->l_len < 0)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        pl_inode = pl_inode_get (this, fd->inode);
        if (!pl_inode) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        reqlock = new_posix_lock (flock, transport, client_pid,
                                  owner, fd);

        if (!reqlock) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        pl_trace_in (this, frame, fd, NULL, cmd, flock, NULL);

        switch (cmd) {

        case F_RESLK_LCKW:
                can_block = 1;

                /* fall through */
        case F_RESLK_LCK:
                memcpy (&reqlock->user_flock, flock, sizeof (struct gf_flock));
                reqlock->frame = frame;
                reqlock->this = this;

                ret = pl_reserve_setlk (this, pl_inode, reqlock,
                                        can_block);
                if (ret < 0) {
                        if (can_block)
                                goto out;

                        op_ret = -1;
                        op_errno = -ret;
                        __destroy_lock (reqlock);
                        goto unwind;
                }
                /* Finally a getlk and return the call */
                conf = pl_getlk (pl_inode, reqlock);
                if (conf)
                        posix_lock_to_flock (conf, flock);
                break;

        case F_RESLK_UNLCK:
                reqlock->frame = frame;
                reqlock->this = this;
                ret = pl_reserve_unlock (this, pl_inode, reqlock);
                if (ret < 0) {
                        op_ret = -1;
                        op_errno = -ret;
                }
                __destroy_lock (reqlock);
                goto unwind;

                break;

        case F_GETLK_FD:
                reqlock->frame = frame;
                reqlock->this = this;
                ret = pl_verify_reservelk (this, pl_inode, reqlock, can_block);
                GF_ASSERT (ret >= 0);

                ret = pl_getlk_fd (this, pl_inode, fd, reqlock);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "getting locks on fd failed");
                        op_ret = -1;
                        op_errno = ENOLCK;
                        goto unwind;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "Replying with a lock on fd for healing");

                posix_lock_to_flock (reqlock, flock);
                __destroy_lock (reqlock);

                break;

#if F_GETLK != F_GETLK64
        case F_GETLK64:
#endif
        case F_GETLK:
                conf = pl_getlk (pl_inode, reqlock);
                posix_lock_to_flock (conf, flock);
                __destroy_lock (reqlock);

                break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:
                can_block = 1;
                reqlock->frame  = frame;
                reqlock->this   = this;

                /* fall through */

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
                memcpy (&reqlock->user_flock, flock, sizeof (struct gf_flock));
                ret = pl_verify_reservelk (this, pl_inode, reqlock, can_block);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "Lock blocked due to conflicting reserve lock");
                        goto out;
                }
                ret = pl_setlk (this, pl_inode, reqlock,
                                can_block);

                if (ret == -1) {
                        if ((can_block) && (F_UNLCK != flock->l_type)) {
                                pl_trace_block (this, frame, fd, NULL, cmd, flock, NULL);
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_DEBUG, "returning EAGAIN");
                        op_ret = -1;
                        op_errno = EAGAIN;
                        __destroy_lock (reqlock);
                }
        }

unwind:
        pl_trace_out (this, frame, fd, NULL, cmd, flock, op_ret, op_errno, NULL);
        pl_update_refkeeper (this, fd->inode);
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, flock);
out:
        return 0;
}


/* TODO: this function just logs, no action required?? */
int
pl_forget (xlator_t *this,
           inode_t *inode)
{
        pl_inode_t   *pl_inode = NULL;

        posix_lock_t *ext_tmp = NULL;
        posix_lock_t *ext_l   = NULL;
        struct list_head posixlks_released;

        pl_inode_lock_t *ino_tmp = NULL;
        pl_inode_lock_t *ino_l   = NULL;
        struct list_head inodelks_released;

        pl_rw_req_t *rw_tmp = NULL;
        pl_rw_req_t *rw_req = NULL;

        pl_entry_lock_t *entry_tmp = NULL;
        pl_entry_lock_t *entry_l   = NULL;
        struct list_head entrylks_released;

        pl_dom_list_t *dom = NULL;
        pl_dom_list_t *dom_tmp = NULL;

        INIT_LIST_HEAD (&posixlks_released);
        INIT_LIST_HEAD (&inodelks_released);
        INIT_LIST_HEAD (&entrylks_released);

        pl_inode = pl_inode_get (this, inode);

        pthread_mutex_lock (&pl_inode->mutex);
        {

                if (!list_empty (&pl_inode->rw_list)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Pending R/W requests found, releasing.");

                        list_for_each_entry_safe (rw_req, rw_tmp, &pl_inode->rw_list,
                                                  list) {

                                list_del (&rw_req->list);
                                GF_FREE (rw_req);
                        }
                }

                if (!list_empty (&pl_inode->ext_list)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Pending fcntl locks found, releasing.");

                        list_for_each_entry_safe (ext_l, ext_tmp, &pl_inode->ext_list,
                                                  list) {

                                __delete_lock (pl_inode, ext_l);
                                if (ext_l->blocked) {
                                        list_add_tail (&ext_l->list, &posixlks_released);
                                        continue;
                                }
                                __destroy_lock (ext_l);
                        }
                }


                list_for_each_entry_safe (dom, dom_tmp, &pl_inode->dom_list, inode_list) {

                        if (!list_empty (&dom->inodelk_list)) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Pending inode locks found, releasing.");

                                list_for_each_entry_safe (ino_l, ino_tmp, &dom->inodelk_list, list) {
                                        __delete_inode_lock (ino_l);
                                        __destroy_inode_lock (ino_l);
                                }

                                list_splice_init (&dom->blocked_inodelks, &inodelks_released);


                        }
                        if (!list_empty (&dom->entrylk_list)) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Pending entry locks found, releasing.");

                                list_for_each_entry_safe (entry_l, entry_tmp, &dom->entrylk_list, domain_list) {
                                        list_del_init (&entry_l->domain_list);

                                        if (entry_l->basename)
                                                GF_FREE ((char *)entry_l->basename);
                                        GF_FREE (entry_l);
                                }

                                list_splice_init (&dom->blocked_entrylks, &entrylks_released);
                        }

                        list_del (&dom->inode_list);
                        gf_log ("posix-locks", GF_LOG_TRACE,
                                " Cleaning up domain: %s", dom->domain);
                        GF_FREE ((char *)(dom->domain));
                        GF_FREE (dom);
                }

        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (ext_l, ext_tmp, &posixlks_released, list) {

                STACK_UNWIND_STRICT (lk, ext_l->frame, -1, 0, &ext_l->user_flock);
                __destroy_lock (ext_l);
        }

        list_for_each_entry_safe (ino_l, ino_tmp, &inodelks_released, blocked_locks) {

                STACK_UNWIND_STRICT (inodelk, ino_l->frame, -1, 0);
                __destroy_inode_lock (ino_l);
        }

        list_for_each_entry_safe (entry_l, entry_tmp, &entrylks_released, blocked_locks) {

                STACK_UNWIND_STRICT (entrylk, entry_l->frame, -1, 0);
                if (entry_l->basename)
                        GF_FREE ((char *)entry_l->basename);
                GF_FREE (entry_l);

        }

        GF_FREE (pl_inode);

        return 0;
}

int
pl_release (xlator_t *this, fd_t *fd)
{
        pl_inode_t *pl_inode     = NULL;
        uint64_t    tmp_pl_inode = 0;
        int         ret          = -1;
        uint64_t    tmp          = 0;
        pl_fdctx_t *fdctx        = NULL;

        if (fd == NULL) {
                goto out;
        }

        ret = inode_ctx_get (fd->inode, this, &tmp_pl_inode);
        if (ret != 0)
                goto out;

        pl_inode = (pl_inode_t *)(long)tmp_pl_inode;

        pl_trace_release (this, fd);

        gf_log (this->name, GF_LOG_TRACE,
                "Releasing all locks with fd %p", fd);

        delete_locks_of_fd (this, pl_inode, fd);
        pl_update_refkeeper (this, fd->inode);

        ret = fd_ctx_del (fd, this, &tmp);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Could not get fdctx");
                goto out;
        }

        fdctx = (pl_fdctx_t *)(long)tmp;

        GF_FREE (fdctx);
out:
        return ret;
}

int
pl_releasedir (xlator_t *this, fd_t *fd)
{
        int         ret          = -1;
        uint64_t    tmp          = 0;
        pl_fdctx_t *fdctx        = NULL;

        if (fd == NULL) {
                goto out;
        }

        ret = fd_ctx_del (fd, this, &tmp);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Could not get fdctx");
                goto out;
        }

        fdctx = (pl_fdctx_t *)(long)tmp;

        GF_FREE (fdctx);
out:
        return ret;
}

static int32_t
__get_posixlk_count (xlator_t *this, pl_inode_t *pl_inode)
{
        posix_lock_t *lock   = NULL;
        int32_t       count  = 0;

        list_for_each_entry (lock, &pl_inode->ext_list, list) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                " XATTR DEBUG"
                                "%s (pid=%d) (lk-owner=%"PRIu64") %"PRId64" - %"PRId64" state: %s",
                                lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                lock->client_pid,
                                lock->owner,
                                lock->user_flock.l_start,
                                lock->user_flock.l_len,
                                lock->blocked == 1 ? "Blocked" : "Active");

                count++;
        }

        return count;
}

int32_t
get_posixlk_count (xlator_t *this, inode_t *inode)
{
        pl_inode_t   *pl_inode = NULL;
        uint64_t      tmp_pl_inode = 0;
        int           ret      = 0;
        int32_t       count    = 0;

        ret = inode_ctx_get (inode, this, &tmp_pl_inode);
        if (ret != 0) {
                goto out;
        }

        pl_inode = (pl_inode_t *)(long) tmp_pl_inode;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                count =__get_posixlk_count (this, pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

out:
        return count;
}

void
pl_entrylk_xattr_fill (xlator_t *this, inode_t *inode,
                       dict_t *dict)
{
        int32_t     count = 0;
        int         ret   = -1;

        count = get_entrylk_count (this, inode);
        ret = dict_set_int32 (dict, GLUSTERFS_ENTRYLK_COUNT, count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " dict_set failed on key %s", GLUSTERFS_ENTRYLK_COUNT);
        }

}

void
pl_inodelk_xattr_fill (xlator_t *this, inode_t *inode,
                       dict_t *dict)
{
        int32_t     count = 0;
        int         ret   = -1;

        count = get_inodelk_count (this, inode);
        ret = dict_set_int32 (dict, GLUSTERFS_INODELK_COUNT, count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " dict_set failed on key %s", GLUSTERFS_INODELK_COUNT);
        }

}

void
pl_posixlk_xattr_fill (xlator_t *this, inode_t *inode,
                       dict_t *dict)
{
        int32_t     count = 0;
        int         ret   = -1;

        count = get_posixlk_count (this, inode);
        ret = dict_set_int32 (dict, GLUSTERFS_POSIXLK_COUNT, count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " dict_set failed on key %s", GLUSTERFS_POSIXLK_COUNT);
        }

}

int32_t
pl_lookup_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    inode_t *inode,
                    struct iatt *buf,
                    dict_t *dict,
                    struct iatt *postparent)
{
        pl_local_t *local = NULL;

        GF_VALIDATE_OR_GOTO (this->name, frame->local, out);

        if (op_ret) {
                goto out;
        }

        local = frame->local;

        if (local->entrylk_count_req)
                pl_entrylk_xattr_fill (this, inode, dict);
        if (local->inodelk_count_req)
                pl_inodelk_xattr_fill (this, inode, dict);
        if (local->posixlk_count_req)
                pl_posixlk_xattr_fill (this, inode, dict);


        frame->local = NULL;

        if (local != NULL)
                GF_FREE (local);

out:
        STACK_UNWIND_STRICT (
                     lookup,
                     frame,
                      op_ret,
                      op_errno,
                      inode,
                      buf,
                      dict,
                      postparent);
        return 0;
}

int32_t
pl_lookup (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                dict_t *xattr_req)
{
        pl_local_t *local  = NULL;
        int         ret    = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        local = GF_CALLOC (1, sizeof (*local), gf_locks_mt_pl_local_t);
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        if (xattr_req) {
                if (dict_get (xattr_req, GLUSTERFS_ENTRYLK_COUNT))
                        local->entrylk_count_req = 1;
                if (dict_get (xattr_req, GLUSTERFS_INODELK_COUNT))
                        local->inodelk_count_req = 1;
                if (dict_get (xattr_req, GLUSTERFS_POSIXLK_COUNT))
                        local->posixlk_count_req = 1;
        }

        frame->local = local;

        STACK_WIND (frame,
                    pl_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc,
                    xattr_req);
        ret = 0;
out:
        if (ret == -1)
                STACK_UNWIND_STRICT (lookup, frame, -1, 0, NULL, NULL, NULL, NULL);

        return 0;
}

void
pl_dump_lock (char *str, int size, struct gf_flock *flock,
              uint64_t owner, void *trans)
{
        char *type_str = NULL;

        switch (flock->l_type) {
        case F_RDLCK:
                type_str = "READ";
                break;
        case F_WRLCK:
                type_str = "WRITE";
                break;
        case F_UNLCK:
                type_str = "UNLOCK";
                break;
        default:
                type_str = "UNKNOWN";
                break;
        }

        snprintf (str, size, "type=%s, start=%llu, len=%llu, pid=%llu, lk-owner=%llu, transport=%p",
                  type_str, (unsigned long long) flock->l_start,
                  (unsigned long long) flock->l_len,
                  (unsigned long long) flock->l_pid,
                  (unsigned long long) owner,
                  trans);


}

void
__dump_entrylks (pl_inode_t *pl_inode)
{
        pl_dom_list_t   *dom  = NULL;
        pl_entry_lock_t *lock = NULL;
        int             count = 0;
        char            key[GF_DUMP_MAX_BUF_LEN];

        char tmp[256];

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {

                count = 0;

                gf_proc_dump_build_key(key,
                                       "xlator.feature.locks.lock-dump.domain",
                                       "domain");
                gf_proc_dump_write(key, "%s", dom->domain);

                list_for_each_entry (lock, &dom->entrylk_list, domain_list) {

                        gf_proc_dump_build_key(key,
                                               "xlator.feature.locks.lock-dump.domain.entrylk",
                                               "entrylk[%d](ACTIVE)",count );
                        snprintf (tmp, 256," %s on %s owner=%llu, transport=%p",
                                  lock->type == ENTRYLK_RDLCK ? "ENTRYLK_RDLCK" :
                                  "ENTRYLK_WRLCK", lock->basename,
                                  (unsigned long long) lock->owner,
                                  lock->trans);

                        gf_proc_dump_write(key, tmp);

                        count++;
                }

                list_for_each_entry (lock, &dom->blocked_entrylks, blocked_locks) {

                        gf_proc_dump_build_key(key,
                                               "xlator.feature.locks.lock-dump.domain.entrylk",
                                               "entrylk[%d](BLOCKED)",count );
                        snprintf (tmp, 256," %s on %s state = Blocked",
                                  lock->type == ENTRYLK_RDLCK ? "ENTRYLK_RDLCK" :
                                  "ENTRYLK_WRLCK", lock->basename);

                        gf_proc_dump_write(key, tmp);

                        count++;
                }

        }

}

void
dump_entrylks (pl_inode_t *pl_inode)
{
        pthread_mutex_lock (&pl_inode->mutex);
        {
                __dump_entrylks (pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

}

void
__dump_inodelks (pl_inode_t *pl_inode)
{
        pl_dom_list_t   *dom  = NULL;
        pl_inode_lock_t *lock = NULL;
        int             count = 0;
        char            key[GF_DUMP_MAX_BUF_LEN];

        char tmp[256];

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {

                count = 0;

                gf_proc_dump_build_key(key,
                                       "xlator.feature.locks.lock-dump.domain",
                                       "domain");
                gf_proc_dump_write(key, "%s", dom->domain);

                list_for_each_entry (lock, &dom->inodelk_list, list) {

                        gf_proc_dump_build_key(key,
                                               "xlator.feature.locks.lock-dump.domain.inodelk",
                                               "inodelk[%d](ACTIVE)",count );

                        pl_dump_lock (tmp, 256, &lock->user_flock,
                                      lock->owner, lock->transport);
                        gf_proc_dump_write(key, tmp);

                        count++;
                }

                list_for_each_entry (lock, &dom->blocked_inodelks, blocked_locks) {

                        gf_proc_dump_build_key(key,
                                               "xlator.feature.locks.lock-dump.domain.inodelk",
                                               "inodelk[%d](BLOCKED)",count );
                        pl_dump_lock (tmp, 256, &lock->user_flock,
                                      lock->owner, lock->transport);
                        gf_proc_dump_write(key, tmp);

                        count++;
                }

        }

}

void
dump_inodelks (pl_inode_t *pl_inode)
{
        pthread_mutex_lock (&pl_inode->mutex);
        {
                __dump_inodelks (pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

}

void
__dump_posixlks (pl_inode_t *pl_inode)
{
        posix_lock_t    *lock = NULL;
        int             count = 0;
        char            key[GF_DUMP_MAX_BUF_LEN];

        char tmp[256];

      list_for_each_entry (lock, &pl_inode->ext_list, list) {

              gf_proc_dump_build_key(key,
                                     "xlator.feature.locks.lock-dump.domain.posixlk",
                                     "posixlk[%d](%s)",
                                     count,
                                     lock->blocked ? "BLOCKED" : "ACTIVE");
              pl_dump_lock (tmp, 256, &lock->user_flock,
                            lock->owner, lock->transport);
              gf_proc_dump_write(key, tmp);

              count++;
        }



}

void
dump_posixlks (pl_inode_t *pl_inode)
{
        pthread_mutex_lock (&pl_inode->mutex);
        {
                __dump_posixlks (pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

}

int32_t
pl_dump_inode_priv (xlator_t *this, inode_t *inode)
{

        int             ret = -1;
        uint64_t        tmp_pl_inode = 0;
        pl_inode_t      *pl_inode = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN];

        int count      = 0;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_get (inode, this, &tmp_pl_inode);

        if (ret != 0)
                goto out;

        pl_inode = (pl_inode_t *)(long)tmp_pl_inode;

        if (!pl_inode) {
                ret = -1;
                goto out;
        }

        gf_proc_dump_build_key(key,
                               "xlator.feature.locks.inode",
                               "%ld.mandatory",inode->ino);
        gf_proc_dump_write(key, "%d", pl_inode->mandatory);


                count = get_entrylk_count (this, inode);
                gf_proc_dump_build_key(key,
                                       "xlator.feature.locks.entrylk-count",
                                       "%ld.entrylk-count", inode->ino);
                gf_proc_dump_write(key, "%d", count);

                dump_entrylks(pl_inode);

                count = get_inodelk_count (this, inode);
                gf_proc_dump_build_key(key,
                                       "xlator.feature.locks.inodelk-count",
                                       "%ld.inodelk-count", inode->ino);
                gf_proc_dump_write(key, "%d", count);

                dump_inodelks(pl_inode);

                count = get_posixlk_count (this, inode);
                gf_proc_dump_build_key(key,
                                       "xlator.feature.locks.posixlk-count",
                                       "%ld.posixlk-count", inode->ino);
                gf_proc_dump_write(key, "%d", count);

                dump_posixlks(pl_inode);


out:
        return ret;
}



/*
 * pl_dump_inode - inode dump function for posix locks
 *
 */
int
pl_dump_inode (xlator_t *this)
{

        GF_ASSERT (this);

        if (this->itable) {
                inode_table_dump(this->itable,
                                 "xlator.features.locks.inode_table");
        }

        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_locks_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

int
init (xlator_t *this)
{
        posix_locks_private_t *priv = NULL;
        xlator_list_t         *trav = NULL;
        data_t                *mandatory = NULL;
        data_t                *trace = NULL;
        int                   ret = -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "FATAL: posix-locks should have exactly one child");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling. Please check the volume file.");
        }

        trav = this->children;
        while (trav->xlator->children)
                trav = trav->xlator->children;

        if (strncmp ("storage/", trav->xlator->type, 8)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "'locks' translator is not loaded over a storage "
                        "translator");
                goto out;;
        }

        priv = GF_CALLOC (1, sizeof (*priv),
                          gf_locks_mt_posix_locks_private_t);

        mandatory = dict_get (this->options, "mandatory-locks");
        if (mandatory)
                gf_log (this->name, GF_LOG_WARNING,
                        "mandatory locks not supported in this minor release.");

        trace = dict_get (this->options, "trace");
        if (trace) {
                if (gf_string2boolean (trace->data,
                                       &priv->trace) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'trace' takes on only boolean values.");
                        goto out;
                }
        }

        this->private = priv;
        ret = 0;

out:
        if (ret) {
                if (priv)
                        GF_FREE (priv);
        }
        return ret;
}


int
fini (xlator_t *this)
{
        posix_locks_private_t *priv = NULL;

        priv = this->private;
        if (!priv)
                return 0;
        this->private = NULL;
        GF_FREE (priv);

        return 0;
}


int
pl_inodelk (call_frame_t *frame, xlator_t *this,
            const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *flock);

int
pl_finodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *flock);

int
pl_entrylk (call_frame_t *frame, xlator_t *this,
            const char *volume, loc_t *loc, const char *basename,
            entrylk_cmd cmd, entrylk_type type);

int
pl_fentrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, fd_t *fd, const char *basename,
             entrylk_cmd cmd, entrylk_type type);

struct xlator_fops fops = {
        .lookup      = pl_lookup,
        .create      = pl_create,
        .truncate    = pl_truncate,
        .ftruncate   = pl_ftruncate,
        .open        = pl_open,
        .readv       = pl_readv,
        .writev      = pl_writev,
        .lk          = pl_lk,
        .inodelk     = pl_inodelk,
        .finodelk    = pl_finodelk,
        .entrylk     = pl_entrylk,
        .fentrylk    = pl_fentrylk,
        .flush       = pl_flush,
        .opendir     = pl_opendir,
};

struct xlator_dumpops dumpops = {
        .inodectx    = pl_dump_inode_priv,
};

struct xlator_cbks cbks = {
        .forget      = pl_forget,
        .release     = pl_release,
        .releasedir  = pl_releasedir,
};


struct volume_options options[] = {
        { .key  = { "mandatory-locks", "mandatory" },
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key  = { "trace" },
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key = {NULL} },
};
