/*Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "libxlator.h"
#include "common-utils.h"
#include "byte-order.h"
#include "marker-quota.h"
#include "marker-quota-helper.h"

int
mq_loc_copy (loc_t *dst, loc_t *src)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", dst, out);
        GF_VALIDATE_OR_GOTO ("marker", src, out);

        if (src->inode == NULL ||
            src->path == NULL) {
                gf_log ("marker", GF_LOG_WARNING,
                        "src loc is not valid");
                goto out;
        }

        ret = loc_copy (dst, src);
out:
        return ret;
}

int32_t
mq_get_local_err (quota_local_t *local,
                  int32_t *val)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", local, out);
        GF_VALIDATE_OR_GOTO ("marker", val, out);

        LOCK (&local->lock);
        {
                *val = local->err;
        }
        UNLOCK (&local->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_get_ctx_updation_status (quota_inode_ctx_t *ctx,
                            gf_boolean_t *status)
{
        int32_t   ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        LOCK (&ctx->lock);
        {
                *status = ctx->updation_status;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}


int32_t
mq_set_ctx_updation_status (quota_inode_ctx_t *ctx,
                            gf_boolean_t status)
{
        int32_t   ret = -1;

        if (ctx == NULL)
                goto out;

        LOCK (&ctx->lock);
        {
                ctx->updation_status = status;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}

int32_t
mq_test_and_set_ctx_updation_status (quota_inode_ctx_t *ctx,
                                     gf_boolean_t *status)
{
        int32_t         ret     = -1;
        gf_boolean_t    temp    = _gf_false;

        GF_VALIDATE_OR_GOTO ("marker", ctx, out);
        GF_VALIDATE_OR_GOTO ("marker", status, out);

        LOCK (&ctx->lock);
        {
                temp = *status;
                *status = ctx->updation_status;
                ctx->updation_status = temp;
        }
        UNLOCK (&ctx->lock);

        ret = 0;
out:
        return ret;
}

void
mq_assign_lk_owner (xlator_t *this, call_frame_t *frame)
{
        marker_conf_t *conf     = NULL;
        uint64_t       lk_owner = 0;

        conf = this->private;

        LOCK (&conf->lock);
        {
                if (++conf->quota_lk_owner == 0) {
                        ++conf->quota_lk_owner;
                }

                lk_owner = conf->quota_lk_owner;
        }
        UNLOCK (&conf->lock);

        frame->root->lk_owner = lk_owner;

        return;
}


int32_t
loc_fill_from_name (xlator_t *this, loc_t *newloc, loc_t *oldloc,
                    uint64_t ino, char *name)
{
        int32_t   ret  = -1;
        int32_t   len  = 0;
        char     *path = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", newloc, out);
        GF_VALIDATE_OR_GOTO ("marker", oldloc, out);
        GF_VALIDATE_OR_GOTO ("marker", name, out);

        newloc->ino = ino;

        newloc->inode = inode_new (oldloc->inode->table);

        if (!newloc->inode) {
                ret = -1;
                goto out;
        }

        newloc->parent = inode_ref (oldloc->inode);

        len = strlen (oldloc->path);

        if (oldloc->path [len - 1] == '/')
                ret = gf_asprintf ((char **) &path, "%s%s",
                                   oldloc->path, name);
        else
                ret = gf_asprintf ((char **) &path, "%s/%s",
                                   oldloc->path, name);

        if (ret < 0)
                goto out;

        newloc->path = path;

        newloc->name = strrchr (newloc->path, '/');

        if (newloc->name)
                newloc->name++;

        gf_log (this->name, GF_LOG_DEBUG, "path = %s name =%s",
                newloc->path, newloc->name);
out:
        return ret;
}

int32_t
dirty_inode_updation_done (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno)
{
        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}

int32_t
release_lock_on_dirty_inode (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno)
{
        struct gf_flock   lock  = {0, };
        quota_local_t    *local = NULL;
        loc_t             loc = {0, };
	int               ret = -1;

        local = frame->local;

        if (op_ret == -1) {
                local->err = -1;

                dirty_inode_updation_done (frame, NULL, this, 0, 0);

                return 0;
        }

        if (op_ret == 0)
                local->ctx->dirty = 0;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        ret = loc_copy (&loc, &local->loc);
	if (ret == -1) {
                local->err = -1;
                frame->local = NULL;
                dirty_inode_updation_done (frame, NULL, this, 0, 0);
                return 0;
        }

        if (local->loc.inode == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Inode is NULL, so can't stackwind.");
                goto out;
        }

        STACK_WIND (frame,
                    dirty_inode_updation_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &loc, F_SETLKW, &lock);

        loc_wipe (&loc);

        return 0;
out:
        dirty_inode_updation_done (frame, NULL, this, -1, 0);

        return 0;
}

int32_t
mark_inode_undirty (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        int32_t        ret     = -1;
        int64_t       *size    = NULL;
        dict_t        *newdict = NULL;
        quota_local_t *local   = NULL;
        marker_conf_t *priv    = NULL;

        local = (quota_local_t *) frame->local;

        if (op_ret == -1)
                goto err;

        priv = (marker_conf_t *) this->private;

        if (!dict)
                goto wind;

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (ret)
                goto wind;

        LOCK (&local->ctx->lock);
        {
                local->ctx->size = ntoh64 (*size);
        }
        UNLOCK (&local->ctx->lock);

wind:
        newdict = dict_new ();
        if (!newdict)
                goto err;

        ret = dict_set_int8 (newdict, QUOTA_DIRTY_KEY, 0);
        if (ret)
                goto err;

        STACK_WIND (frame, release_lock_on_dirty_inode,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    &local->loc, newdict, 0);
        ret = 0;

err:
        if (op_ret == -1 || ret == -1) {
                local->err = -1;

                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
update_size_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        int32_t          ret      = -1;
        dict_t          *new_dict = NULL;
        int64_t         *size     = NULL;
        int64_t         *delta    = NULL;
        quota_local_t   *local    = NULL;
        marker_conf_t   *priv     = NULL;

        local = frame->local;

        if (op_ret == -1)
                goto err;

        priv = this->private;

        if (dict == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Dict is null while updating the size xattr %s",
                        local->loc.path?local->loc.path:"");
                goto err;
        }

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (!size) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to get the size, %s",
                        local->loc.path?local->loc.path:"");
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (delta, int64_t, ret, err);

        *delta = hton64 (local->sum - ntoh64 (*size));

        gf_log (this->name, GF_LOG_DEBUG, "calculated size = %"PRId64", "
                "original size = %"PRIu64
                " path = %s diff = %"PRIu64, local->sum, ntoh64 (*size),
                local->loc.path, ntoh64 (*delta));

        new_dict = dict_new ();
        if (!new_dict);

        ret = dict_set_bin (new_dict, QUOTA_SIZE_KEY, delta, 8);
        if (ret)
                goto err;

        STACK_WIND (frame, mark_inode_undirty, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, &local->loc,
                    GF_XATTROP_ADD_ARRAY64, new_dict);

        ret = 0;

err:
        if (op_ret == -1 || ret == -1) {
                local->err = -1;

                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);
        }

        if (new_dict)
                dict_unref (new_dict);

        return 0;
}

int32_t
mq_test_and_set_local_err(quota_local_t *local,
                          int32_t *val)
{
        int     tmp = 0;
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", local, out);
        GF_VALIDATE_OR_GOTO ("marker", val, out);

        LOCK (&local->lock);
        {
                tmp = local->err;
                local->err = *val;
                *val = tmp;
        }
        UNLOCK (&local->lock);

        ret = 0;
out:
        return ret;
}

int32_t
get_dirty_inode_size (call_frame_t *frame, xlator_t *this)
{
        int32_t        ret   = -1;
        dict_t        *dict  = NULL;
        quota_local_t *local = NULL;
        marker_conf_t *priv  = NULL;

        local = (quota_local_t *) frame->local;

        priv = (marker_conf_t *) this->private;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto err;
        }

        ret = dict_set_int64 (dict, QUOTA_SIZE_KEY, 0);
        if (ret)
                goto err;

        STACK_WIND (frame, update_size_xattr, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->loc, dict);
        ret =0;

err:
        if (ret) {
                local->err = -1;

                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);
        }

        if (dict)
                dict_unref (dict);

        return 0;
}

int32_t
get_child_contribution (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno,
                        inode_t *inode,
                        struct iatt *buf,
                        dict_t *dict,
                        struct iatt *postparent)
{
        int32_t        ret                = -1;
        int32_t        val                = 0;
        char           contri_key [512]   = {0, };
        int64_t       *contri             = NULL;
        quota_local_t *local              = NULL;

        local = frame->local;

        frame->local = NULL;

        QUOTA_STACK_DESTROY (frame, this);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s",
                        strerror (op_errno));
                val = -2;
                if (!mq_test_and_set_local_err (local, &val) &&
                    val != -2)
                        release_lock_on_dirty_inode (local->frame, NULL, this, 0, 0);

                goto exit;
        }

        ret = mq_get_local_err (local, &val);
        if (!ret && val == -2)
                goto exit;

        GET_CONTRI_KEY (contri_key, local->loc.inode->gfid, ret);
        if (ret < 0)
                goto out;

        if (!dict)
                goto out;

        if (dict_get_bin (dict, contri_key, (void **) &contri) == 0)
                local->sum += ntoh64 (*contri);

out:
        LOCK (&local->lock);
        {
                val = --local->dentry_child_count;
        }
        UNLOCK (&local->lock);

        if (val == 0) {
                quota_dirty_inode_readdir (local->frame, NULL, this,
                                                   0, 0, NULL);
        }
        quota_local_unref (this, local);

        return 0;
exit:
        quota_local_unref (this, local);
        return 0;
}

int32_t
quota_readdir_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   gf_dirent_t *entries)
{
        char           contri_key [512]   = {0, };
        int32_t        ret                = 0;
        int32_t        val                = 0;
        off_t          offset             = 0;
        int32_t        count              = 0;
        dict_t        *dict               = NULL;
        quota_local_t *local              = NULL;
        gf_dirent_t   *entry              = NULL;
        call_frame_t  *newframe           = NULL;
        loc_t          loc                = {0, };

        local = quota_local_ref (frame->local);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "readdir failed %s", strerror (op_errno));
                local->err = -1;

                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);

                goto end;
        } else if (op_ret == 0) {
                get_dirty_inode_size (frame, this);

                goto end;
        }

        local->dentry_child_count =  0;

        list_for_each_entry (entry, (&entries->list), list) {
                gf_log (this->name, GF_LOG_DEBUG, "entry  = %s", entry->d_name);

                if ((!strcmp (entry->d_name, ".")) || (!strcmp (entry->d_name,
                                                                ".."))) {
                        gf_log (this->name, GF_LOG_DEBUG, "entry  = %s",
                                entry->d_name);
                        continue;
                }

                offset = entry->d_off;
                count++;
        }

        if (count == 0) {
                get_dirty_inode_size (frame, this);
                goto end;

        }

        local->frame = frame;

        LOCK (&local->lock);
        {
                local->dentry_child_count = count;
                local->d_off = offset;
        }
        UNLOCK (&local->lock);


        list_for_each_entry (entry, (&entries->list), list) {
                gf_log (this->name, GF_LOG_DEBUG, "entry  = %s", entry->d_name);

                if ((!strcmp (entry->d_name, ".")) || (!strcmp (entry->d_name,
                                                                ".."))) {
                        gf_log (this->name, GF_LOG_DEBUG, "entry  = %s",
                                entry->d_name);
                        continue;
                }

                ret = loc_fill_from_name (this, &loc, &local->loc,
                                          entry->d_ino, entry->d_name);
                if (ret < 0)
                        goto out;

                ret = 0;

                LOCK (&local->lock);
                {
                        if (local->err != -2) {
                                newframe = copy_frame (frame);
                                if (!newframe) {
                                        ret = -1;
                                }
                        } else
                                ret = -1;
                }
                UNLOCK (&local->lock);

                if (ret == -1)
                        goto out;

                newframe->local = quota_local_ref (local);

                dict = dict_new ();
                if (!dict) {
                        ret = -1;
                        goto out;
                }

                GET_CONTRI_KEY (contri_key, local->loc.inode->gfid, ret);
                if (ret < 0)
                        goto out;

                ret = dict_set_int64 (dict, contri_key, 0);
                if (ret)
                        goto out;

                STACK_WIND (newframe,
                            get_child_contribution,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup,
                            &loc, dict);

                offset = entry->d_off;

                loc_wipe (&loc);

                newframe = NULL;

        out:
                if (dict) {
                        dict_unref (dict);
                        dict = NULL;
                }

                if (ret) {
                        val = -2;
                        mq_test_and_set_local_err (local, &val);

                        if (newframe) {
                                newframe->local = NULL;
                                quota_local_unref(this, local);
                                QUOTA_STACK_DESTROY (newframe, this);
                        }

                        break;
                }
        }

        if (ret && val != -2) {
                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);
        }
end:
        quota_local_unref (this, local);

        return 0;
}

int32_t
quota_dirty_inode_readdir (call_frame_t *frame,
                           void *cookie,
                           xlator_t *this,
                           int32_t op_ret,
                           int32_t op_errno,
                           fd_t *fd)
{
        quota_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                local->err = -1;
                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);
                return 0;
        }

        if (local->fd == NULL)
                local->fd = fd_ref (fd);

        STACK_WIND (frame,
                    quota_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    local->fd, READDIR_BUF, local->d_off);

        return 0;
}

int32_t
check_if_still_dirty (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      inode_t *inode,
                      struct iatt *buf,
                      dict_t *dict,
                      struct iatt *postparent)
{
        int8_t           dirty          = -1;
        int32_t          ret            = -1;
        fd_t            *fd             = NULL;
        quota_local_t   *local          = NULL;
        marker_conf_t   *priv           = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get "
                        "the dirty xattr for %s", local->loc.path);
                goto err;
        }

        priv = this->private;

        if (!dict) {
                ret = -1;
                goto err;
        }

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret)
                goto err;

        //the inode is not dirty anymore
        if (dirty == 0) {
                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);

                return 0;
        }

        fd = fd_create (local->loc.inode, frame->root->pid);

        local->d_off = 0;

        STACK_WIND(frame,
                   quota_dirty_inode_readdir,
                   FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->opendir,
                   &local->loc, fd);

        ret = 0;

err:
        if (op_ret == -1 || ret == -1) {
                local->err = -1;
                release_lock_on_dirty_inode (frame, NULL, this, 0, 0);
        }

        if (fd != NULL) {
                fd_unref (fd);
        }

        return 0;
}

int32_t
get_dirty_xattr (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        int32_t        ret       = -1;
        dict_t        *xattr_req = NULL;
        quota_local_t *local     = NULL;
        marker_conf_t *priv      = NULL;

        if (op_ret == -1) {
                dirty_inode_updation_done (frame, NULL, this, 0, 0);
                return 0;
        }

        priv = (marker_conf_t *) this->private;

        local = frame->local;

        xattr_req = dict_new ();
        if (xattr_req == NULL) {
                ret = -1;
                goto err;
        }

        ret = dict_set_int8 (xattr_req, QUOTA_DIRTY_KEY, 0);
        if (ret)
                goto err;

        STACK_WIND (frame,
                    check_if_still_dirty,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    &local->loc,
                    xattr_req);
        ret = 0;

err:
        if (ret) {
                local->err = -1;
                release_lock_on_dirty_inode(frame, NULL, this, 0, 0);
        }

        if (xattr_req)
                dict_unref (xattr_req);

        return 0;
}

/* return 1 when dirty updation started
 * 0 other wise
 */
int32_t
update_dirty_inode (xlator_t *this,
                    loc_t *loc,
                    quota_inode_ctx_t *ctx,
                    inode_contribution_t *contribution)
{
        int32_t          ret        = -1;
        quota_local_t   *local      = NULL;
        gf_boolean_t    status     = _gf_false;
        struct gf_flock  lock       = {0, };
        call_frame_t    *frame      = NULL;

        ret = mq_get_ctx_updation_status (ctx, &status);
        if (ret == -1 || status == _gf_true) {
                ret = 0;
                goto out;
        }

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL) {
                ret = -1;
                goto out;
        }

        mq_assign_lk_owner (this, frame);

        local = quota_local_new ();
        if (local == NULL)
                goto fr_destroy;

        frame->local = local;
        ret = mq_loc_copy (&local->loc, loc);
        if (ret < 0)
                goto fr_destroy;

        local->ctx = ctx;

        local->contri = contribution;

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        if (local->loc.inode == NULL) {
                ret = -1;
                gf_log (this->name, GF_LOG_WARNING,
                        "Inode is NULL, so can't stackwind.");
                goto fr_destroy;
        }

        STACK_WIND (frame,
                    get_dirty_xattr,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->loc, F_SETLKW, &lock);
        return 1;

fr_destroy:
        QUOTA_STACK_DESTROY (frame, this);
out:

        return 0;
}


int32_t
quota_inode_creation_done (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno)
{
        if (frame == NULL)
                return 0;

        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}


int32_t
quota_xattr_creation_release_lock (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int32_t op_ret,
                                   int32_t op_errno)
{
        struct gf_flock  lock  = {0, };
        quota_local_t   *local = NULL;

        local = frame->local;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    quota_inode_creation_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->loc,
                    F_SETLKW, &lock);

        return 0;
}


int32_t
create_dirty_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        int32_t          ret       = -1;
        dict_t          *newdict   = NULL;
        quota_local_t   *local     = NULL;
        marker_conf_t   *priv      = NULL;

        if (op_ret < 0) {
                goto err;
        }

        local = frame->local;

        priv = (marker_conf_t *) this->private;

        if (local->loc.inode->ia_type == IA_IFDIR) {
                newdict = dict_new ();
                if (!newdict) {
                        goto err;
                }

                ret = dict_set_int8 (newdict, QUOTA_DIRTY_KEY, 0);
                if (ret == -1) {
                        goto err;
                }

                STACK_WIND (frame, quota_xattr_creation_release_lock,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            &local->loc, newdict, 0);
        } else {
                quota_xattr_creation_release_lock (frame, NULL, this, 0, 0);
        }

        ret = 0;

err:
        if (ret < 0) {
                quota_xattr_creation_release_lock (frame, NULL, this, 0, 0);
        }

        if (newdict != NULL)
                dict_unref (newdict);

        return 0;
}


int32_t
quota_create_xattr (xlator_t *this, call_frame_t *frame)
{
        int32_t               ret       = 0;
        int64_t              *value     = NULL;
        int64_t              *size      = NULL;
        dict_t               *dict      = NULL;
        char                  key[512]  = {0, };
        quota_local_t        *local     = NULL;
        marker_conf_t        *priv      = NULL;
        quota_inode_ctx_t    *ctx       = NULL;
        inode_contribution_t *contri    = NULL;

        if (frame == NULL || this == NULL)
                return 0;

        local = frame->local;

        priv = (marker_conf_t *) this->private;

        ret = quota_inode_ctx_get (local->loc.inode, this, &ctx);
        if (ret < 0) {
                ctx = quota_inode_ctx_new (local->loc.inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota_inode_ctx_new failed");
                        ret = -1;
                        goto out;
                }
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        if (local->loc.inode->ia_type == IA_IFDIR) {
                QUOTA_ALLOC_OR_GOTO (size, int64_t, ret, err);
                ret = dict_set_bin (dict, QUOTA_SIZE_KEY, size, 8);
                if (ret < 0)
                        goto free_size;
        }

        if (strcmp (local->loc.path, "/") != 0) {
                contri = add_new_contribution_node (this, ctx, &local->loc);
                if (contri == NULL)
                        goto err;

                QUOTA_ALLOC_OR_GOTO (value, int64_t, ret, err);
                GET_CONTRI_KEY (key, local->loc.parent->gfid, ret);

                ret = dict_set_bin (dict, key, value, 8);
                if (ret < 0)
                        goto free_value;
        }

        STACK_WIND (frame, create_dirty_xattr, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, &local->loc,
                    GF_XATTROP_ADD_ARRAY64, dict);
        ret = 0;

free_size:
        if (ret < 0) {
                GF_FREE (size);
        }

free_value:
        if (ret < 0) {
                GF_FREE (value);
        }

err:
        dict_unref (dict);

out:
        if (ret < 0) {
                quota_xattr_creation_release_lock (frame, NULL, this, 0, 0);
        }

        return 0;
}


int32_t
quota_check_n_set_inode_xattr (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *buf, dict_t *dict,
                               struct iatt *postparent)
{
        quota_local_t        *local           = NULL;
        int64_t              *size            = NULL, *contri = NULL;
        int8_t                dirty           = 0;
        marker_conf_t        *priv            = NULL;
        int32_t               ret             = 0;
        char                  contri_key[512] = {0, };

        if (op_ret < 0) {
                goto out;
        }

        local = frame->local;
        priv = this->private;

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (ret < 0)
                goto create_xattr;

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret < 0)
                goto create_xattr;

        //check contribution xattr if not root
        if (strcmp (local->loc.path, "/") != 0) {
                GET_CONTRI_KEY (contri_key, local->loc.parent->gfid, ret);
                if (ret < 0)
                        goto out;

                ret = dict_get_bin (dict, contri_key, (void **) &contri);
                if (ret < 0)
                        goto create_xattr;
        }

out:
        quota_xattr_creation_release_lock (frame, NULL, this, 0, 0);
        return 0;

create_xattr:
        quota_create_xattr (this, frame);
        return 0;
}


int32_t
quota_get_xattr (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno)
{
        dict_t        *xattr_req = NULL;
        quota_local_t *local     = NULL;
        int32_t        ret       = 0;

        if (op_ret < 0) {
                goto lock_err;
        }

        local = frame->local;

        xattr_req = dict_new ();
        if (xattr_req == NULL) {
                goto err;
        }

        ret = quota_req_xattr (this, &local->loc, xattr_req);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "cannot request xattr");
                goto err;
        }

        STACK_WIND (frame, quota_check_n_set_inode_xattr, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->loc, xattr_req);

        dict_unref (xattr_req);

        return 0;

err:
        quota_xattr_creation_release_lock (frame, NULL, this, 0, 0);

        if (xattr_req)
                dict_unref (xattr_req);
        return 0;

lock_err:
        quota_inode_creation_done (frame, NULL, this, 0, 0);
        return 0;
}


int32_t
quota_set_inode_xattr (xlator_t *this, loc_t *loc)
{
        struct gf_flock  lock  = {0, };
        quota_local_t   *local = NULL;
        int32_t          ret   = 0;
        call_frame_t    *frame = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto err;
        }

        local = quota_local_new ();
        if (local == NULL) {
                goto err;
        }

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret < 0) {
                goto err;
        }

        frame->local = local;

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        STACK_WIND (frame,
                    quota_get_xattr,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->loc, F_SETLKW, &lock);

        return 0;

err:
        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}


int32_t
get_parent_inode_local (xlator_t *this, quota_local_t *local)
{
        int32_t            ret = -1;
        quota_inode_ctx_t *ctx = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", local, out);

        loc_wipe (&local->loc);

        ret = mq_loc_copy (&local->loc, &local->parent_loc);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "loc copy failed");
                goto out;
        }

        loc_wipe (&local->parent_loc);

        ret = quota_inode_loc_fill (NULL, local->loc.parent,
                                    &local->parent_loc);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "failed to build parent loc of %s",
                        local->loc.path);
                goto out;
        }

        ret = quota_inode_ctx_get (local->loc.inode, this, &ctx);
        if (ret < 0) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "inode ctx get failed");
                goto out;
        }

        local->ctx = ctx;

        if (list_empty (&ctx->contribution_head)) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                        "contribution node list is empty which "
                        "is an error");
                goto out;
        }

        local->contri = (inode_contribution_t *) ctx->contribution_head.next;

        ret = 0;
out:
        return ret;
}


int32_t
xattr_updation_done (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     dict_t *dict)
{
        QUOTA_STACK_DESTROY (frame, this);
        return 0;
}


int32_t
quota_inodelk_cbk (call_frame_t *frame, void *cookie,
                   xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        int32_t         ret    = 0;
        gf_boolean_t    status = _gf_false;
        quota_local_t  *local  = NULL;

        local = frame->local;

        if (op_ret == -1 || local->err) {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "unlocking on path (%s) failed (%s)",
                                local->parent_loc.path, strerror (op_errno));
                }

                xattr_updation_done (frame, NULL, this, 0, 0, NULL);

                return 0;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "inodelk released on %s", local->parent_loc.path);

        if ((strcmp (local->parent_loc.path, "/") == 0)
            || (local->delta == 0)) {
                xattr_updation_done (frame, NULL, this, 0, 0, NULL);
        } else {
                ret = get_parent_inode_local (this, local);
                if (ret < 0) {
                        xattr_updation_done (frame, NULL, this, 0, 0, NULL);
                        goto out;
                }
                status = _gf_true;

                ret = mq_test_and_set_ctx_updation_status (local->ctx, &status);
                if (ret == 0 && status == _gf_false) {
                        get_lock_on_parent (frame, this);
                } else {
                        xattr_updation_done (frame, NULL, this, 0, 0, NULL);
                }
        }
out:
        return 0;
}


//now release lock on the parent inode
int32_t
quota_release_parent_lock (call_frame_t *frame, void *cookie,
                           xlator_t *this, int32_t op_ret,
                           int32_t op_errno)
{
        int32_t            ret      = 0;
        quota_local_t     *local    = NULL;
        quota_inode_ctx_t *ctx      = NULL;
        struct gf_flock    lock     = {0, };

        local = frame->local;

        if (local->err != 0) {
                gf_log_callingfn (this->name,
                                  (local->err == ENOENT) ? GF_LOG_DEBUG
                                  : GF_LOG_WARNING,
                                  "An operation during quota updation "
                                  "of path (%s) failed (%s)", local->loc.path,
                                  strerror (local->err));
        }

        ret = quota_inode_ctx_get (local->parent_loc.inode, this, &ctx);
        if (ret < 0)
                goto wind;

        LOCK (&ctx->lock);
        {
                ctx->dirty = 0;
        }
        UNLOCK (&ctx->lock);

        if (local->parent_loc.inode == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Invalid parent inode.");
                goto err;
        }

wind:
        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    quota_inodelk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc,
                    F_SETLKW, &lock);

        return 0;
err:
        xattr_updation_done (frame, NULL, this,
                             0, 0 , NULL);
        return 0;
}


int32_t
quota_mark_undirty (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    dict_t *dict)
{
        int32_t            ret          = -1;
        int64_t           *size         = NULL;
        dict_t            *newdict      = NULL;
        quota_local_t     *local        = NULL;
        quota_inode_ctx_t *ctx          = NULL;
        marker_conf_t     *priv         = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, (op_errno == ENOENT) ? GF_LOG_DEBUG
                        : GF_LOG_WARNING, "cannot update size of path (%s)(%s)",
                        local->parent_loc.path, strerror (op_errno));
                local->err = op_errno;
                goto err;
        }

        priv = this->private;

        //update the size of the parent inode
        if (dict != NULL) {
                ret = quota_inode_ctx_get (local->parent_loc.inode, this, &ctx);
                if (ret < 0) {
                        op_errno = EINVAL;
                        goto err;
                }

                ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
                if (ret < 0) {
                        op_errno = EINVAL;
                        goto err;
                }

                LOCK (&ctx->lock);
                {
                        if (size)
                                ctx->size = ntoh64 (*size);
                        gf_log (this->name, GF_LOG_DEBUG, "%s %"PRId64,
                                local->parent_loc.path, ctx->size);
                }
                UNLOCK (&ctx->lock);
        }

        newdict = dict_new ();
        if (!newdict) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = dict_set_int8 (newdict, QUOTA_DIRTY_KEY, 0);
        if (ret < 0) {
                op_errno = -ret;
                goto err;
        }

        STACK_WIND (frame, quota_release_parent_lock,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    &local->parent_loc, newdict, 0);

        ret = 0;
err:
        if ((op_ret == -1) || (ret < 0)) {
                local->err = op_errno;

                quota_release_parent_lock (frame, NULL, this, 0, 0);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}


int32_t
quota_update_parent_size (call_frame_t *frame,
                          void *cookie,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno,
                          dict_t *dict)
{
        int64_t             *size       = NULL;
        int32_t              ret        = -1;
        dict_t              *newdict    = NULL;
        marker_conf_t       *priv       = NULL;
        quota_local_t       *local      = NULL;
        quota_inode_ctx_t   *ctx        = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "xattrop call on path (%s) failed: %s",
                        local->loc.path, strerror (op_errno));

                goto err;
        }

        LOCK (&local->contri->lock);
        {
                local->contri->contribution += local->delta;
        }
        UNLOCK (&local->contri->lock);

        gf_log (this->name, GF_LOG_DEBUG, "%s %"PRId64 "%"PRId64,
                local->loc.path, local->ctx->size,
                local->contri->contribution);

        priv = this->private;

        if (dict == NULL) {
                op_errno = EINVAL;
                goto err;
        }

        ret = quota_inode_ctx_get (local->parent_loc.inode, this, &ctx);
        if (ret < 0) {
                op_errno = EINVAL;
                goto err;
        }

        newdict = dict_new ();
        if (!newdict) {
                ret = -1;
                op_errno = EINVAL;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (size, int64_t, ret, err);

        *size = hton64 (local->delta);

        ret = dict_set_bin (newdict, QUOTA_SIZE_KEY, size, 8);
        if (ret < 0) {
                op_errno = -ret;
                goto err;
        }

        STACK_WIND (frame,
                    quota_mark_undirty,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    &local->parent_loc,
                    GF_XATTROP_ADD_ARRAY64,
                    newdict);
        ret = 0;
err:
        if (op_ret == -1 || ret < 0) {
                local->err = op_errno;
                quota_release_parent_lock (frame, NULL, this, 0, 0);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
quota_update_inode_contribution (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, inode_t *inode,
                                 struct iatt *buf, dict_t *dict,
                                 struct iatt *postparent)
{
        int32_t               ret              = -1;
        int64_t              *size             = NULL, size_int = 0;
        int64_t               contri_int       = 0;
        int64_t              *contri           = NULL;
        int64_t              *delta            = NULL;
        char                  contri_key [512] = {0, };
        dict_t               *newdict          = NULL;
        quota_local_t        *local            = NULL;
        quota_inode_ctx_t    *ctx              = NULL;
        marker_conf_t        *priv             = NULL;
        inode_contribution_t *contribution     = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "failed to get size and contribution with %s error",
                        strerror (op_errno));
                goto err;
        }

        priv = this->private;

        ctx = local->ctx;
        contribution = local->contri;

        //prepare to update size & contribution of the inode
        GET_CONTRI_KEY (contri_key, contribution->gfid, ret);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto err;
        }

        LOCK (&ctx->lock);
        {
                if (local->loc.inode->ia_type == IA_IFDIR ) {
                        ret = dict_get_bin (dict, QUOTA_SIZE_KEY,
                                            (void **) &size);
                        if (ret < 0) {
                                op_errno = EINVAL;
                                goto unlock;
                        }

                        ctx->size = ntoh64 (*size);
                } else
                        ctx->size = buf->ia_blocks * 512;

                size_int = ctx->size;
        }
unlock:
        UNLOCK  (&ctx->lock);

        if (ret < 0) {
                goto err;
        }

        ret = dict_get_bin (dict, contri_key, (void **) &contri);

        LOCK (&contribution->lock);
        {
                if (ret < 0)
                        contribution->contribution = 0;
                else
                        contribution->contribution = ntoh64 (*contri);

                contri_int = contribution->contribution;
        }
        UNLOCK (&contribution->lock);

        gf_log (this->name, GF_LOG_DEBUG, "%s %"PRId64 "%"PRId64,
                local->loc.path, size_int, contri_int);

        local->delta = size_int - contri_int;

        if (local->delta == 0) {
                quota_mark_undirty (frame, NULL, this, 0, 0, NULL);
                return 0;
        }

        newdict = dict_new ();
        if (newdict == NULL) {
                ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (delta, int64_t, ret, err);

        *delta = hton64 (local->delta);

        ret = dict_set_bin (newdict, contri_key, delta, 8);
        if (ret < 0) {
                op_errno = -ret;
                ret = -1;
                goto err;
        }

        STACK_WIND (frame,
                    quota_update_parent_size,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    &local->loc,
                    GF_XATTROP_ADD_ARRAY64,
                    newdict);
        ret = 0;

err:
        if (op_ret == -1 || ret < 0) {
                local->err = op_errno;

                quota_release_parent_lock (frame, NULL, this, 0, 0);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
quota_fetch_child_size_and_contri (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int32_t op_ret,
                                   int32_t op_errno)
{
        int32_t            ret              = -1;
        char               contri_key [512] = {0, };
        dict_t            *newdict          = NULL;
        quota_local_t     *local            = NULL;
        marker_conf_t     *priv             = NULL;
        quota_inode_ctx_t *ctx              = NULL;

        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, (op_errno == ENOENT) ? GF_LOG_DEBUG
                        : GF_LOG_WARNING,
                        "couldn't mark inode corresponding to path (%s) dirty "
                        "(%s)", local->parent_loc.path, strerror (op_errno));
                goto err;
        }

        VALIDATE_OR_GOTO (local->ctx, err);
        VALIDATE_OR_GOTO (local->contri, err);

        gf_log (this->name, GF_LOG_DEBUG, "%s marked dirty",
                local->parent_loc.path);

        priv = this->private;

        //update parent ctx
        ret = quota_inode_ctx_get (local->parent_loc.inode, this, &ctx);
        if (ret == -1) {
                op_errno = EINVAL;
                goto err;
        }

        LOCK (&ctx->lock);
        {
                ctx->dirty = 1;
        }
        UNLOCK (&ctx->lock);

        newdict = dict_new ();
        if (newdict == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        if (local->loc.inode->ia_type == IA_IFDIR) {
                ret = dict_set_int64 (newdict, QUOTA_SIZE_KEY, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dict_set failed.");
                        goto err;
                }
        }

        GET_CONTRI_KEY (contri_key, local->contri->gfid, ret);
        if (ret < 0) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = dict_set_int64 (newdict, contri_key, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dict_set failed.");
                goto err;
        }

        mq_set_ctx_updation_status (local->ctx, _gf_false);

        STACK_WIND (frame, quota_update_inode_contribution, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->loc, newdict);

        ret = 0;

err:
        if (op_ret == -1 || ret < 0) {
                local->err = op_errno;

                mq_set_ctx_updation_status (local->ctx, _gf_false);

                quota_release_parent_lock (frame, NULL, this, 0, 0);
        }

        if (newdict)
                dict_unref (newdict);

        return 0;
}

int32_t
quota_markdirty (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        int32_t        ret      = -1;
        dict_t        *dict     = NULL;
        quota_local_t *local    = NULL;
        marker_conf_t *priv     = NULL;

        local = frame->local;

        if (op_ret == -1){
                gf_log (this->name, (op_errno == ENOENT) ? GF_LOG_DEBUG
                        : GF_LOG_WARNING,
                        "lock setting failed on %s (%s)",
                        local->parent_loc.path, strerror (op_errno));

                local->err = op_errno;

                mq_set_ctx_updation_status (local->ctx, _gf_false);

                quota_inodelk_cbk (frame, NULL, this, 0, 0);

                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "inodelk succeeded on  %s", local->parent_loc.path);

        priv = this->private;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto err;
        }

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, 1);
        if (ret == -1)
                goto err;

        STACK_WIND (frame, quota_fetch_child_size_and_contri,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    &local->parent_loc, dict, 0);

        ret = 0;
err:
        if (ret == -1) {
                local->err = 1;

                mq_set_ctx_updation_status (local->ctx, _gf_false);

                quota_release_parent_lock (frame, NULL, this, 0, 0);
        }

        if (dict)
                dict_unref (dict);

        return 0;
}


int32_t
get_lock_on_parent (call_frame_t *frame, xlator_t *this)
{
        struct gf_flock  lock  = {0, };
        quota_local_t   *local = NULL;

        GF_VALIDATE_OR_GOTO ("marker", frame, fr_destroy);

        local = frame->local;
        gf_log (this->name, GF_LOG_DEBUG, "taking lock on %s",
                local->parent_loc.path);

        if (local->parent_loc.inode == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "parent inode is not valid, aborting "
                        "transaction.");
                goto fr_destroy;
        }

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        STACK_WIND (frame,
                    quota_markdirty,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc, F_SETLKW, &lock);

        return 0;

fr_destroy:
        QUOTA_STACK_DESTROY (frame, this);

        return -1;
}


int
start_quota_txn (xlator_t *this, loc_t *loc,
                 quota_inode_ctx_t *ctx,
                 inode_contribution_t *contri)
{
        int32_t        ret      = -1;
        call_frame_t  *frame    = NULL;
        quota_local_t *local    = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL)
                goto err;

        mq_assign_lk_owner (this, frame);

        local = quota_local_new ();
        if (local == NULL)
                goto fr_destroy;

        frame->local = local;

        ret = mq_loc_copy (&local->loc, loc);
        if (ret < 0)
                goto fr_destroy;

        ret = quota_inode_loc_fill (NULL, local->loc.parent,
                                    &local->parent_loc);
        if (ret < 0)
                goto fr_destroy;

        local->ctx = ctx;
        local->contri = contri;

        ret = get_lock_on_parent (frame, this);
        if (ret == -1)
                goto err;

        return 0;

fr_destroy:
        QUOTA_STACK_DESTROY (frame, this);
err:
        mq_set_ctx_updation_status (ctx, _gf_false);

        return -1;
}


int
initiate_quota_txn (xlator_t *this, loc_t *loc)
{
        int32_t               ret          = -1;
        gf_boolean_t          status       = _gf_false;
        quota_inode_ctx_t    *ctx          = NULL;
        inode_contribution_t *contribution = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = quota_inode_ctx_get (loc->inode, this, &ctx);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "inode ctx get failed, aborting quota txn");
                ret = -1;
                goto out;
        }

        contribution = get_contribution_node (loc->parent, ctx);
        if (contribution == NULL)
                goto out;

        /* To improve performance, donot start another transaction
         * if one is already in progress for same inode
         */
        status = _gf_true;

        ret = mq_test_and_set_ctx_updation_status (ctx, &status);
        if (ret < 0)
                goto out;

        if (status == _gf_false) {
                start_quota_txn (this, loc, ctx, contribution);
        }

        ret = 0;
out:
        return ret;
}


/* int32_t */
/* validate_inode_size_contribution (xlator_t *this, loc_t *loc, int64_t size, */
/*                                int64_t contribution) */
/* { */
/*   if (size != contribution) { */
/*     initiate_quota_txn (this, loc); */
/*   } */

/*   return 0; */
/* } */


int32_t
inspect_directory_xattr (xlator_t *this,
                         loc_t *loc,
                         dict_t *dict,
                         struct iatt buf)
{
        int32_t               ret                 = 0;
        int8_t                dirty               = -1;
        int64_t              *size                = NULL, size_int = 0;
        int64_t              *contri              = NULL, contri_int = 0;
        char                  contri_key [512]    = {0, };
        marker_conf_t        *priv                = NULL;
        gf_boolean_t          not_root            = _gf_false;
        quota_inode_ctx_t    *ctx                 = NULL;
        inode_contribution_t *contribution        = NULL;

        priv = this->private;

        ret = quota_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                ctx = quota_inode_ctx_new (loc->inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota_inode_ctx_new failed");
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **) &size);
        if (ret < 0)
                goto out;

        ret = dict_get_int8 (dict, QUOTA_DIRTY_KEY, &dirty);
        if (ret < 0)
                goto out;

        if (strcmp (loc->path, "/") != 0) {
                not_root = _gf_true;

                contribution = add_new_contribution_node (this, ctx, loc);
                if (contribution == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "cannot add a new contributio node");
                        goto out;
                }

                GET_CONTRI_KEY (contri_key, contribution->gfid, ret);
                if (ret < 0)
                        goto out;

                ret = dict_get_bin (dict, contri_key, (void **) &contri);
                if (ret < 0)
                        goto out;

                LOCK (&contribution->lock);
                {
                        contribution->contribution = ntoh64 (*contri);
                        contri_int = contribution->contribution;
                }
                UNLOCK (&contribution->lock);
        }

        LOCK (&ctx->lock);
        {
                ctx->size = ntoh64 (*size);
                ctx->dirty = dirty;
                size_int = ctx->size;
        }
        UNLOCK (&ctx->lock);

        gf_log (this->name, GF_LOG_DEBUG, "size=%"PRId64
                " contri=%"PRId64, size_int, contri_int);

        if (dirty) {
                ret = update_dirty_inode (this, loc, ctx, contribution);
        }

        if ((!dirty || ret == 0) && (not_root == _gf_true) &&
            (size_int != contri_int)) {
                initiate_quota_txn (this, loc);
        }

        ret = 0;
out:
        if (ret)
                quota_set_inode_xattr (this, loc);

        return 0;
}

int32_t
inspect_file_xattr (xlator_t *this,
                    loc_t *loc,
                    dict_t *dict,
                    struct iatt buf)
{
        int32_t               ret              = -1;
        uint64_t              contri_int       = 0, size = 0;
        int64_t              *contri_ptr       = NULL;
        char                  contri_key [512] = {0, };
        marker_conf_t        *priv             = NULL;
        quota_inode_ctx_t    *ctx              = NULL;
        inode_contribution_t *contribution     = NULL;

        priv = this->private;

        ret = quota_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0) {
                ctx = quota_inode_ctx_new (loc->inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "quota_inode_ctx_new failed");
                        ret = -1;
                        goto out;
                }
        }

        contribution = add_new_contribution_node (this, ctx, loc);
        if (contribution == NULL)
                goto out;

        LOCK (&ctx->lock);
        {
                ctx->size = 512 * buf.ia_blocks;
                size = ctx->size;
        }
        UNLOCK (&ctx->lock);

        list_for_each_entry (contribution, &ctx->contribution_head,
                             contri_list) {
                GET_CONTRI_KEY (contri_key, contribution->gfid, ret);
                if (ret < 0)
                        continue;

                ret = dict_get_bin (dict, contri_key, (void **) &contri_int);
                if (ret == 0) {
                        contri_ptr = (int64_t *)(unsigned long)contri_int;

                        LOCK (&contribution->lock);
                        {
                                contribution->contribution = ntoh64 (*contri_ptr);
                                contri_int = contribution->contribution;
                        }
                        UNLOCK (&contribution->lock);

                        gf_log (this->name, GF_LOG_DEBUG,
                                "size=%"PRId64 " contri=%"PRId64, size, contri_int);

                        if (size != contri_int) {
                                initiate_quota_txn (this, loc);
                        }
                } else
                        initiate_quota_txn (this, loc);
        }

out:
        return ret;
}

int32_t
quota_xattr_state (xlator_t *this,
                   loc_t *loc,
                   dict_t *dict,
                   struct iatt buf)
{
        if (buf.ia_type == IA_IFREG ||
            buf.ia_type == IA_IFLNK) {
                k ++;
                inspect_file_xattr (this, loc, dict, buf);
        } else if (buf.ia_type == IA_IFDIR)
                inspect_directory_xattr (this, loc, dict, buf);

        return 0;
}

int32_t
quota_req_xattr (xlator_t *this,
                 loc_t *loc,
                 dict_t *dict)
{
        int32_t               ret       = -1;
        marker_conf_t        *priv      = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", dict, out);

        priv = this->private;

        //if not "/" then request contribution
        if (strcmp (loc->path, "/") == 0)
                goto set_size;

        ret = dict_set_contribution (this, dict, loc);
        if (ret == -1)
                goto out;

set_size:
        ret = dict_set_uint64 (dict, QUOTA_SIZE_KEY, 0);
        if (ret < 0) {
                ret = -1;
                goto out;
        }

        ret = dict_set_int8 (dict, QUOTA_DIRTY_KEY, 0);
        if (ret < 0) {
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        return ret;
}


int32_t
quota_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        QUOTA_STACK_DESTROY (frame, this);

        return 0;
}

int32_t
quota_inode_remove_done (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno)
{
        int32_t        ret                = 0;
        char           contri_key [512]   = {0, };
        quota_local_t *local              = NULL;

        local = (quota_local_t *) frame->local;

        if (op_ret == -1 || local->err == -1) {
                quota_removexattr_cbk (frame, NULL, this, -1, 0);
                return 0;
        }

        frame->local = NULL;

        if (local->hl_count > 1) {
                GET_CONTRI_KEY (contri_key, local->contri->gfid, ret);

                STACK_WIND (frame, quota_removexattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            &local->loc, contri_key);
                ret = 0;
        } else {
                quota_removexattr_cbk (frame, NULL, this, 0, 0);
        }

        if (strcmp (local->parent_loc.path, "/") != 0) {
                ret = get_parent_inode_local (this, local);
                if (ret < 0)
                        goto out;

                start_quota_txn (this, &local->loc, local->ctx, local->contri);
        }
out:
        quota_local_unref (this, local);

        return 0;
}

int32_t
mq_inode_remove_done (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        int32_t            ret   = -1;
        struct gf_flock    lock  = {0, };
        quota_inode_ctx_t *ctx   = NULL;
        quota_local_t     *local = NULL;
        int64_t contribution = 0;

        local = frame->local;
        if (op_ret == -1)
                local->err = -1;

        ret = quota_inode_ctx_get (local->parent_loc.inode, this, &ctx);

        LOCK (&local->contri->lock);
        {
                contribution = local->contri->contribution;
        }
        UNLOCK (&local->contri->lock);

        if (contribution == local->size) {
                if (ret == 0) {
                        LOCK (&ctx->lock);
                        {
                                ctx->size -= contribution;
                        }
                        UNLOCK (&ctx->lock);

                        LOCK (&local->contri->lock);
                        {
                                local->contri->contribution = 0;
                        }
                        UNLOCK (&local->contri->lock);
                }
        }

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    quota_inode_remove_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc,
                    F_SETLKW, &lock);
        return 0;
}

int32_t
mq_reduce_parent_size_xattr (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        int32_t                  ret               = -1;
        int64_t                 *size              = NULL;
        dict_t                  *dict              = NULL;
        marker_conf_t           *priv              = NULL;
        quota_local_t           *local             = NULL;
        inode_contribution_t    *contribution      = NULL;

        local = frame->local;
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "inodelk set failed on %s", local->parent_loc.path);
                QUOTA_STACK_DESTROY (frame, this);
                return 0;
        }

        VALIDATE_OR_GOTO (local->contri, err);

        priv = this->private;

        contribution = local->contri;

        dict = dict_new ();
        if (dict == NULL) {
                ret = -1;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (size, int64_t, ret, err);

        *size = hton64 (-local->size);

        ret = dict_set_bin (dict, QUOTA_SIZE_KEY, size, 8);
        if (ret < 0)
                goto err;


        STACK_WIND (frame, mq_inode_remove_done, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, &local->parent_loc,
                    GF_XATTROP_ADD_ARRAY64, dict);
        dict_unref (dict);
        return 0;

err:
        local->err = 1;
        mq_inode_remove_done (frame, NULL, this, -1, 0, NULL);
        if (dict)
                dict_unref (dict);
        return 0;
}

int32_t
reduce_parent_size (xlator_t *this, loc_t *loc, int64_t contri)
{
        int32_t                  ret           = -1;
        struct gf_flock          lock          = {0,};
        call_frame_t            *frame         = NULL;
        marker_conf_t           *priv          = NULL;
        quota_local_t           *local         = NULL;
        quota_inode_ctx_t       *ctx           = NULL;
        inode_contribution_t    *contribution  = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);

        priv = this->private;

        ret = quota_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0)
                goto out;

        contribution = get_contribution_node (loc->parent, ctx);
        if (contribution == NULL)
                goto out;

        local = quota_local_new ();
        if (local == NULL) {
                ret = -1;
                goto out;
        }

        if (contri >= 0) {
                local->size = contri;
        } else {
                LOCK (&contribution->lock);
                {
                        local->size = contribution->contribution;
                }
                UNLOCK (&contribution->lock);
        }

        if (local->size == 0) {
                ret = 0;
                goto out;
        }

        ret = mq_loc_copy (&local->loc, loc);
        if (ret < 0)
                goto out;

        local->ctx = ctx;
        local->contri = contribution;

        ret = quota_inode_loc_fill (NULL, loc->parent, &local->parent_loc);
        if (ret < 0)
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        mq_assign_lk_owner (this, frame);

        frame->local = local;

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        if (local->parent_loc.inode == NULL) {
                ret = -1;
                gf_log (this->name, GF_LOG_WARNING,
                        "Inode is NULL, so can't stackwind.");
                goto out;
        }

        STACK_WIND (frame,
                    mq_reduce_parent_size_xattr,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc, F_SETLKW, &lock);
        local = NULL;
        ret = 0;

out:
        if (local != NULL)
                quota_local_unref (this, local);

        return ret;
}


int32_t
init_quota_priv (xlator_t *this)
{
        return 0;
}


int32_t
quota_rename_update_newpath (xlator_t *this, loc_t *loc)
{
        int32_t               ret          = -1;
        quota_inode_ctx_t    *ctx          = NULL;
        inode_contribution_t *contribution = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", loc, out);
        GF_VALIDATE_OR_GOTO ("marker", loc->inode, out);

        ret = quota_inode_ctx_get (loc->inode, this, &ctx);
        if (ret < 0)
                goto out;

        contribution = add_new_contribution_node (this, ctx, loc);
        if (contribution == NULL) {
                ret = -1;
                goto out;
        }

        initiate_quota_txn (this, loc);
out:
        return ret;
}

int32_t
quota_forget (xlator_t *this, quota_inode_ctx_t *ctx)
{
        inode_contribution_t *contri = NULL;
        inode_contribution_t *next   = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO ("marker", ctx, out);

        list_for_each_entry_safe (contri, next, &ctx->contribution_head,
                                  contri_list) {
                list_del (&contri->contri_list);
                GF_FREE (contri);
        }

        LOCK_DESTROY (&ctx->lock);
        GF_FREE (ctx);
out:
        return 0;
}
