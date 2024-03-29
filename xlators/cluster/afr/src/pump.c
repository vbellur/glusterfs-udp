/*
   Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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
#include <sys/time.h>
#include <stdlib.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "afr-common.c"
#include "defaults.c"

static int
pump_mark_start_pending (xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        pump_priv->pump_start_pending = 1;

        return 0;
}

static int
is_pump_start_pending (xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        return (pump_priv->pump_start_pending);
}

static int
pump_remove_start_pending (xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        pump_priv->pump_start_pending = 0;

        return 0;
}

static pump_state_t
pump_get_state ()
{
        xlator_t *this = NULL;
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        pump_state_t ret;

        this = THIS;
        priv = this->private;
        pump_priv = priv->pump_private;

        LOCK (&pump_priv->pump_state_lock);
        {
                ret = pump_priv->pump_state;
        }
        UNLOCK (&pump_priv->pump_state_lock);

        return ret;
}

int
pump_change_state (xlator_t *this, pump_state_t state)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        pump_state_t state_old;
        pump_state_t state_new;


        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (pump_priv);

        LOCK (&pump_priv->pump_state_lock);
        {
                state_old = pump_priv->pump_state;
                state_new = state;

                pump_priv->pump_state = state;

        }
        UNLOCK (&pump_priv->pump_state_lock);

        gf_log (this->name, GF_LOG_DEBUG,
                "Pump changing state from %d to %d",
                state_old,
                state_new);

        return  0;
}

static int
pump_set_resume_path (xlator_t *this, const char *path)
{
        int ret = 0;

        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (pump_priv);

        LOCK (&pump_priv->resume_path_lock);
        {
                pump_priv->resume_path = strdup (path);
                if (!pump_priv->resume_path)
                        ret = -1;
        }
        UNLOCK (&pump_priv->resume_path_lock);

        return ret;
}

static void
build_child_loc (loc_t *parent, loc_t *child, char *path, char *name)
{
        child->path = path;
        child->name = name;

        child->parent = inode_ref (parent->inode);
        child->inode = inode_new (parent->inode->table);
}

static char *
build_file_path (loc_t *loc, gf_dirent_t *entry)
{
        xlator_t *this = NULL;
        char *file_path = NULL;
        int pathlen = 0;
        int total_size = 0;

        this = THIS;

        pathlen = STRLEN_0 (loc->path);

        if (IS_ROOT_PATH (loc->path)) {
                total_size = pathlen + entry->d_len;
                file_path = GF_CALLOC (1, total_size, gf_afr_mt_char);
                if (!file_path) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        return NULL;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "constructing file path of size=%d"
                        "pathlen=%d, d_len=%d",
                        total_size, pathlen,
                        entry->d_len);

                snprintf(file_path, total_size, "%s%s", loc->path, entry->d_name);

        } else {
                total_size = pathlen + entry->d_len + 1; /* for the extra '/' in the path */
                file_path = GF_CALLOC (1, total_size + 1, gf_afr_mt_char);
                if (!file_path) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        return NULL;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "constructing file path of size=%d"
                        "pathlen=%d, d_len=%d",
                        total_size, pathlen,
                        entry->d_len);

                snprintf(file_path, total_size, "%s/%s", loc->path, entry->d_name);
        }

        gf_log (this->name, GF_LOG_TRACE,
                "path=%s and d_name=%s", loc->path, entry->d_name);
        gf_log (this->name, GF_LOG_TRACE,
                "constructed file_path=%s of size=%d", file_path, total_size);

        return file_path;
}

static int
pump_save_path (xlator_t *this, const char *path)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;
        pump_state_t state;
        dict_t *dict = NULL;
        loc_t  loc = {0};
        int dict_ret = 0;
        int ret = -1;

        state = pump_get_state ();
        if (state == PUMP_STATE_RESUME)
                return 0;

        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (priv->root_inode);

        build_root_loc (priv->root_inode, &loc);

        dict = dict_new ();
        dict_ret = dict_set_str (dict, PUMP_PATH, (char *)path);

        ret = syncop_setxattr (PUMP_SOURCE_CHILD (this), &loc, dict, 0);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setxattr failed - could not save path=%s", path);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "setxattr succeeded - saved path=%s", path);
                gf_log (this->name, GF_LOG_DEBUG,
                        "Saving path for status info");
        }

        dict_unref (dict);

        return 0;
}

static int
pump_check_and_update_status (xlator_t *this)
{
        pump_state_t state;
        int ret = -1;

        state = pump_get_state ();

        switch (state) {

        case PUMP_STATE_RESUME:
        case PUMP_STATE_RUNNING:
        {
                ret = 0;
                break;
        }
        case PUMP_STATE_PAUSE:
        {
                ret = -1;
                break;
        }
        case PUMP_STATE_ABORT:
        {
                pump_save_path (this, "/");
                ret = -1;
                break;
        }
        default:
        {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unknown pump state");
                ret = -1;
                break;
        }

        }

        return ret;
}

static const char *
pump_get_resume_path (xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        const char *resume_path = NULL;

        priv = this->private;
        pump_priv = priv->pump_private;

        resume_path = pump_priv->resume_path;

        return resume_path;
}

static int
pump_update_resume_state (xlator_t *this, const char *path)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        pump_state_t state;
        const char *resume_path = NULL;

        priv = this->private;
        pump_priv = priv->pump_private;

        state = pump_get_state ();

        if (state == PUMP_STATE_RESUME) {
                resume_path = pump_get_resume_path (this);
                if (strcmp (resume_path, "/") == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Reached the resume path (/). Proceeding to change state"
                                " to running");
                        pump_change_state (this, PUMP_STATE_RUNNING);
                } else if (strcmp (resume_path, path) == 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Reached the resume path. Proceeding to change state"
                                " to running");
                        pump_change_state (this, PUMP_STATE_RUNNING);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Not yet hit the resume path:res-path=%s,path=%s",
                                resume_path, path);
                }
        }

        return 0;
}

static gf_boolean_t
is_pump_traversal_allowed (xlator_t *this, const char *path)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        pump_state_t state;
        const char *resume_path = NULL;
        gf_boolean_t ret = _gf_true;

        priv = this->private;
        pump_priv = priv->pump_private;

        state = pump_get_state ();

        if (state == PUMP_STATE_RESUME) {
                resume_path = pump_get_resume_path (this);
                if (strstr (resume_path, path)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "On the right path to resumption path");
                        ret = _gf_true;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Not the right path to resuming=> ignoring traverse");
                        ret = _gf_false;
                }
        }

        return ret;
}

static int
pump_save_file_stats (xlator_t *this, const char *path)
{
        afr_private_t  *priv        = NULL;
        pump_private_t *pump_priv   = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;

        LOCK (&pump_priv->resume_path_lock);
        {
                pump_priv->number_files_pumped++;

                strncpy (pump_priv->current_file, path,
                         PATH_MAX);
        }
        UNLOCK (&pump_priv->resume_path_lock);

        return 0;
}

static int
gf_pump_traverse_directory (loc_t *loc)
{
        xlator_t *this = NULL;
        afr_private_t *priv = NULL;
        fd_t     *fd   = NULL;

        off_t       offset   = 0;
        loc_t       entry_loc;
        gf_dirent_t *entry = NULL;
        gf_dirent_t *tmp = NULL;
        gf_dirent_t entries;

	struct iatt iatt, parent;
	dict_t *xattr_rsp;

        int source = 0;

        char *file_path = NULL;
        int ret = 0;
        gf_boolean_t is_directory_empty = _gf_true;

        INIT_LIST_HEAD (&entries.list);
        this = THIS;
        priv = this->private;

        GF_ASSERT (loc->inode);

	fd = fd_create (loc->inode, PUMP_PID);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to create fd for %s", loc->path);
                goto out;
        }

        ret = syncop_opendir (priv->children[source], loc, fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "opendir failed on %s", loc->path);
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "pump opendir on %s returned=%d",
                loc->path, ret);

        while (syncop_readdirp (priv->children[source], fd, 131072, offset, &entries)) {

                if (list_empty (&entries.list)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "no more entries in directory");
                        goto out;
                }

                list_for_each_entry_safe (entry, tmp, &entries.list, list) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "found readdir entry=%s", entry->d_name);

                        file_path = build_file_path (loc, entry);
                        if (!file_path) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "file path construction failed");
                                goto out;
                        }

                        build_child_loc (loc, &entry_loc, file_path, entry->d_name);

                        if (!IS_ENTRY_CWD (entry->d_name) &&
                                           !IS_ENTRY_PARENT (entry->d_name)) {

                                    is_directory_empty = _gf_false;
                                    ret = syncop_lookup (this, &entry_loc, NULL,
                                                         &iatt, &xattr_rsp, &parent);

                                    entry_loc.ino = iatt.ia_ino;
                                    entry_loc.inode->ino = iatt.ia_ino;
                                    memcpy (entry_loc.inode->gfid, iatt.ia_gfid, 16);

                                    gf_log (this->name, GF_LOG_DEBUG,
                                            "lookup %s => %"PRId64,
                                            entry_loc.path,
                                            iatt.ia_ino);

                                    ret = syncop_lookup (this, &entry_loc, NULL,
                                                         &iatt, &xattr_rsp, &parent);


                                    gf_log (this->name, GF_LOG_DEBUG,
                                            "second lookup ret=%d: %s => %"PRId64,
                                            ret,
                                            entry_loc.path,
                                            iatt.ia_ino);

                                    pump_update_resume_state (this, entry_loc.path);

                                    pump_save_path (this, entry_loc.path);
                                    pump_save_file_stats (this, entry_loc.path);

                                    ret = pump_check_and_update_status (this);
                                    if (ret < 0) {
                                            gf_log (this->name, GF_LOG_DEBUG,
                                                    "Pump beginning to exit out");
                                            goto out;
                                    }

                                    gf_log (this->name, GF_LOG_TRACE,
                                            "type of file=%d, IFDIR=%d",
                                            iatt.ia_type, IA_IFDIR);

                                    if (IA_ISDIR (iatt.ia_type)) {
                                            if (is_pump_traversal_allowed (this, entry_loc.path)) {
                                                    gf_log (this->name, GF_LOG_TRACE,
                                                            "entering dir=%s",
                                                            entry->d_name);
                                                    gf_pump_traverse_directory (&entry_loc);
                                            }
                                    }
                            }
                        offset = entry->d_off;
                        loc_wipe (&entry_loc);
                }

                gf_dirent_free (&entries);
                gf_log (this->name, GF_LOG_TRACE,
                        "offset incremented to %d",
                        (int32_t ) offset);

        }

        if (is_directory_empty && IS_ROOT_PATH (loc->path)) {
               pump_change_state (this, PUMP_STATE_RUNNING);
               gf_log (this->name, GF_LOG_INFO, "Empty source brick. "
                                "Nothing to be done.");
        }

out:
        return 0;

}

void
build_root_loc (inode_t *inode, loc_t *loc)
{
        loc->path = "/";
        loc->name = "";
        loc->inode = inode;
        loc->ino = 1;
        loc->inode->ino = 1;
        memset (loc->inode->gfid, 0, 16);
        loc->inode->gfid[15] = 1;

}

static int
pump_update_resume_path (xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        const char *resume_path = NULL;

        priv = this->private;
        pump_priv = priv->pump_private;

        resume_path = pump_get_resume_path (this);

        if (resume_path) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Found a path to resume from: %s",
                        resume_path);

        }else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Did not find a path=> setting to '/'");
                pump_set_resume_path (this, "/");
        }

        pump_change_state (this, PUMP_STATE_RESUME);

        return 0;
}

static int
pump_complete_migration (xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;
        dict_t *dict = NULL;
        pump_state_t state;
        loc_t  loc = {0};
        int dict_ret = 0;
        int ret = -1;

        priv = this->private;
        pump_priv = priv->pump_private;

        GF_ASSERT (priv->root_inode);

        build_root_loc (priv->root_inode, &loc);

        dict = dict_new ();

        state = pump_get_state ();
        if (state == PUMP_STATE_RUNNING) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Pump finished pumping");

                pump_priv->pump_finished = _gf_true;

                dict_ret = dict_set_str (dict, PUMP_SOURCE_COMPLETE, "jargon");

                ret = syncop_setxattr (PUMP_SOURCE_CHILD (this), &loc, dict, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "setxattr failed - while  notifying source complete");
                }
                dict_ret = dict_set_str (dict, PUMP_SINK_COMPLETE, "jargon");

                ret = syncop_setxattr (PUMP_SINK_CHILD (this), &loc, dict, 0);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "setxattr failed - while notifying sink complete");
                }

                pump_save_path (this, "/");
        }

        return 0;
}

static int
pump_set_root_gfid (dict_t *dict)
{
        uuid_t gfid;
        int ret = 0;

        memset (gfid, 0, 16);
        gfid[15] = 1;

        ret = afr_set_dict_gfid (dict, gfid);

        return ret;
}

static int
pump_lookup_sink (loc_t *loc)
{
        xlator_t *this = NULL;
	struct iatt iatt, parent;
	dict_t *xattr_rsp;
        dict_t *xattr_req = NULL;
        int ret = 0;

        this = THIS;

        xattr_req = dict_new ();

        ret = pump_set_root_gfid (xattr_req);
        if (ret)
                goto out;

        ret = syncop_lookup (PUMP_SINK_CHILD (this), loc,
                             xattr_req, &iatt, &xattr_rsp, &parent);

        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Lookup on sink child failed");
                goto out;
        }

out:
        if (xattr_req)
                dict_unref (xattr_req);

        return ret;
}

static int
pump_task (void *data)
{
	xlator_t *this = NULL;
        afr_private_t *priv = NULL;


        loc_t loc = {0};
	struct iatt iatt, parent;
	dict_t *xattr_rsp = NULL;
        dict_t *xattr_req = NULL;

        int ret = -1;

        this = THIS;
        priv = this->private;

        GF_ASSERT (priv->root_inode);

        build_root_loc (priv->root_inode, &loc);
        xattr_req = dict_new ();
        if (!xattr_req) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        pump_set_root_gfid (xattr_req);
        ret = syncop_lookup (this, &loc, xattr_req,
                             &iatt, &xattr_rsp, &parent);

        gf_log (this->name, GF_LOG_TRACE,
                "lookup: ino=%"PRId64", path=%s",
                loc.ino,
                loc.path);

        ret = pump_check_and_update_status (this);
        if (ret < 0) {
                goto out;
        }

        pump_update_resume_path (this);

        pump_set_root_gfid (xattr_req);
        ret = pump_lookup_sink (&loc);
        if (ret) {
                pump_update_resume_path (this);
                goto out;
        }

        gf_pump_traverse_directory (&loc);

        pump_complete_migration (this);
out:
        if (xattr_req)
                dict_unref (xattr_req);

	return 0;
}


static int
pump_task_completion (int ret, void *data)
{
        xlator_t *this = NULL;
        call_frame_t *frame = NULL;
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        this = THIS;

        frame = (call_frame_t *) data;

        priv = this->private;
        pump_priv = priv->pump_private;

        inode_unref (priv->root_inode);

        gf_log (this->name, GF_LOG_DEBUG,
                "Pump xlator exiting");
	return 0;
}

int
pump_start (call_frame_t *pump_frame, xlator_t *this)
{
	afr_private_t *priv = NULL;
	pump_private_t *pump_priv = NULL;

	int ret = -1;

	priv = this->private;
        pump_priv = priv->pump_private;

        if (!pump_frame->root->lk_owner)
                pump_frame->root->lk_owner = PUMP_LK_OWNER;

	ret = synctask_new (pump_priv->env, pump_task,
                            pump_task_completion,
                            pump_frame);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "starting pump failed");
                pump_change_state (this, PUMP_STATE_ABORT);
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "setting pump as started");

        priv->use_afr_in_pump = 1;
out:
	return ret;
}

static int
pump_start_synctask (xlator_t *this)
{
        call_frame_t *frame = NULL;
        int ret = 0;

        frame = create_frame (this, this->ctx->pool);
        if (!frame) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        pump_change_state (this, PUMP_STATE_RUNNING);

        ret = pump_start (frame, this);

out:
        return ret;
}

int32_t
pump_cmd_start_setxattr_cbk (call_frame_t *frame,
                             void *cookie,
                             xlator_t *this,
                             int32_t op_ret,
                             int32_t op_errno)

{
        call_frame_t *prev = NULL;
        afr_local_t *local = NULL;
        int ret = 0;

        local = frame->local;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not initiate destination "
                        "brick connect");
                ret = op_ret;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Successfully initiated destination "
                "brick connect");

        pump_mark_start_pending (this);

        /* send the PARENT_UP as pump is ready now */
        prev = cookie;
        if (prev && prev->this)
                prev->this->notify (prev->this, GF_EVENT_PARENT_UP, this);

out:
        local->op_ret = ret;
        pump_command_reply (frame, this);

        return 0;
}

static int
pump_initiate_sink_connect (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local     = NULL;
        afr_private_t *priv      = NULL;
        dict_t        *dict      = NULL;
        data_t        *data      = NULL;
        char          *clnt_cmd = NULL;
        loc_t loc = {0};

        int ret = 0;

        priv  = this->private;
        local = frame->local;

        GF_ASSERT (priv->root_inode);

        build_root_loc (priv->root_inode, &loc);

        data = data_ref (dict_get (local->dict, PUMP_CMD_START));
        if (!data) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not get destination brick value");
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        clnt_cmd = GF_CALLOC (1, data->len+1, gf_common_mt_char);
        if (!clnt_cmd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto out;
        }

        memcpy (clnt_cmd, data->data, data->len);
        clnt_cmd[data->len] = '\0';
        gf_log (this->name, GF_LOG_DEBUG, "Got destination brick %s\n",
                        clnt_cmd);

        ret = dict_set_dynstr (dict, CLIENT_CMD_CONNECT, clnt_cmd);
        if (ret < 0) {
                GF_FREE (clnt_cmd);
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not inititiate destination brick "
                        "connect");
                goto out;
        }

	STACK_WIND (frame,
		    pump_cmd_start_setxattr_cbk,
		    PUMP_SINK_CHILD(this),
		    PUMP_SINK_CHILD(this)->fops->setxattr,
		    &loc,
		    dict,
		    0);

        ret = 0;

        dict_unref (dict);
out:
        if (data)
                data_unref (data);
        return ret;
}

static int
is_pump_aborted (xlator_t *this)
{
        pump_state_t state;

        state = pump_get_state ();

        return ((state == PUMP_STATE_ABORT));
}

int32_t
pump_cmd_start_getxattr_cbk (call_frame_t *frame,
                             void *cookie,
                             xlator_t *this,
                             int32_t op_ret,
                             int32_t op_errno,
                             dict_t *dict)
{
        afr_local_t *local = NULL;
        char *path = NULL;

        pump_state_t state;
        int ret = 0;
        int need_unwind = 0;
        int dict_ret = -1;

        local = frame->local;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "getxattr failed - changing pump "
                        "state to RUNNING with '/'");
                path = "/";
                ret = op_ret;
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "getxattr succeeded");

                dict_ret =  dict_get_str (dict, PUMP_PATH, &path);
                if (dict_ret < 0)
                        path = "/";
        }

        state = pump_get_state ();
        if ((state == PUMP_STATE_RUNNING) ||
            (state == PUMP_STATE_RESUME)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Pump is already started");
                ret = -1;
                goto out;
        }

        pump_set_resume_path (this, path);

        if (is_pump_aborted (this))
                /* We're re-starting pump afresh */
                ret = pump_initiate_sink_connect (frame, this);
        else {
                /* We're re-starting pump from a previous
                   pause */
                gf_log (this->name, GF_LOG_DEBUG,
                        "about to start synctask");
                ret = pump_start_synctask (this);
                need_unwind = 1;
        }

out:
        if ((ret < 0) || (need_unwind == 1)) {
                local->op_ret = ret;
                pump_command_reply (frame, this);
        }
	return 0;
}

int
pump_execute_status (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        pump_private_t *pump_priv = NULL;

        uint64_t number_files = 0;

        char filename[PATH_MAX];
        char *dict_str = NULL;

        int32_t op_ret = 0;
        int32_t op_errno = 0;

        dict_t *dict = NULL;
        int ret = -1;

        priv = this->private;
        pump_priv = priv->pump_private;

        LOCK (&pump_priv->resume_path_lock);
        {
                number_files  = pump_priv->number_files_pumped;
                strncpy (filename, pump_priv->current_file, PATH_MAX);
        }
        UNLOCK (&pump_priv->resume_path_lock);

        dict_str     = GF_CALLOC (1, PATH_MAX + 256, gf_afr_mt_char);
        if (!dict_str) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        if (pump_priv->pump_finished) {
        snprintf (dict_str, PATH_MAX + 256, "Number of files migrated = %"PRIu64"        Migration complete ",
                  number_files);
        } else {
        snprintf (dict_str, PATH_MAX + 256, "Number of files migrated = %"PRIu64"       Current file= %s ",
                  number_files, filename);
        }

        dict = dict_new ();

        ret = dict_set_dynptr (dict, PUMP_CMD_STATUS, dict_str, PATH_MAX + 256);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_set_dynptr returned negative value");
        }

        op_ret = 0;

out:

        AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);

        dict_unref (dict);

        return 0;
}

int
pump_execute_pause (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        pump_change_state (this, PUMP_STATE_PAUSE);

        local->op_ret = 0;
        pump_command_reply (frame, this);

        return 0;
}

int
pump_execute_start (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t   *local = NULL;

        int ret = 0;
        loc_t loc = {0};

        priv = this->private;
        local = frame->local;

        if (!priv->root_inode) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Pump xlator cannot be started without an initial "
                        "lookup");
                ret = -1;
                goto out;
        }

        GF_ASSERT (priv->root_inode);

        build_root_loc (priv->root_inode, &loc);

	STACK_WIND (frame,
		    pump_cmd_start_getxattr_cbk,
		    PUMP_SOURCE_CHILD(this),
		    PUMP_SOURCE_CHILD(this)->fops->getxattr,
		    &loc,
		    PUMP_PATH);

        ret = 0;

out:
        if (ret < 0) {
                local->op_ret = ret;
                pump_command_reply (frame, this);
        }

	return 0;
}

int
pump_execute_abort (call_frame_t *frame, xlator_t *this)
{
        afr_private_t  *priv      = NULL;
        pump_private_t *pump_priv = NULL;
        afr_local_t    *local     = NULL;

        priv      = this->private;
        pump_priv = priv->pump_private;
        local     = frame->local;

        pump_change_state (this, PUMP_STATE_ABORT);

        LOCK (&pump_priv->resume_path_lock);
        {
                pump_priv->number_files_pumped = 0;
                pump_priv->current_file[0] = '\0';
        }
        UNLOCK (&pump_priv->resume_path_lock);

        local->op_ret = 0;
        pump_command_reply (frame, this);

        return 0;
}

gf_boolean_t
pump_command_status (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, PUMP_CMD_STATUS, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump status command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - status");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_pause (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, PUMP_CMD_PAUSE, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump pause command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - pause");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_abort (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, PUMP_CMD_ABORT, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump abort command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - abort");
        ret = _gf_true;

out:
        return ret;

}

gf_boolean_t
pump_command_start (xlator_t *this, dict_t *dict)
{
        char *cmd = NULL;
        int dict_ret = -1;
        int ret = _gf_true;

        dict_ret = dict_get_str (dict, PUMP_CMD_START, &cmd);
        if (dict_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Not a pump start command");
                ret = _gf_false;
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Hit a pump command - start");
        ret = _gf_true;

out:
        return ret;

}

struct _xattr_key {
        char *key;
        struct list_head list;
};

static void
__gather_xattr_keys (dict_t *dict, char *key, data_t *value,
                     void *data)
{
        struct list_head *  list  = data;
        struct _xattr_key * xkey  = NULL;

        if (!strncmp (key, AFR_XATTR_PREFIX,
                      strlen (AFR_XATTR_PREFIX))) {

                xkey = GF_CALLOC (1, sizeof (*xkey), gf_afr_mt_xattr_key);
                if (!xkey)
                        return;

                xkey->key = key;
                INIT_LIST_HEAD (&xkey->list);

                list_add_tail (&xkey->list, list);
        }
}

static void
__filter_xattrs (dict_t *dict)
{
        struct list_head keys;

        struct _xattr_key *key;
        struct _xattr_key *tmp;

        INIT_LIST_HEAD (&keys);

        dict_foreach (dict, __gather_xattr_keys,
                      (void *) &keys);

        list_for_each_entry_safe (key, tmp, &keys, list) {
                dict_del (dict, key->key);

                list_del_init (&key->list);

                GF_FREE (key);
        }
}

int32_t
pump_getxattr_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  dict_t *dict)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;
	xlator_t **     children = NULL;

	int unwind     = 1;
	int last_tried = -1;
	int this_try = -1;
        int read_child = -1;

	priv     = this->private;
	children = priv->children;

	local = frame->local;

        read_child = (long) cookie;

	if (op_ret == -1) {
        retry:
		last_tried = local->cont.getxattr.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try = ++local->cont.getxattr.last_tried;

                if (this_try == read_child) {
                        goto retry;
                }

		unwind = 0;
		STACK_WIND_COOKIE (frame, pump_getxattr_cbk,
				   (void *) (long) read_child,
				   children[this_try],
				   children[this_try]->fops->getxattr,
				   &local->loc,
				   local->cont.getxattr.name);
	}

out:
	if (unwind) {
                if (op_ret >= 0 && dict)
                        __filter_xattrs (dict);

		AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);
	}

	return 0;
}

int32_t
pump_getxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *name)
{
	afr_private_t *   priv       = NULL;
	xlator_t **       children   = NULL;
	int               call_child = 0;
	afr_local_t     * local      = NULL;

        int               read_child = -1;

	int32_t op_ret   = -1;
	int32_t op_errno = 0;


	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	VALIDATE_OR_GOTO (priv->children, out);

	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	frame->local = local;

        if (name) {
                if (!strncmp (name, AFR_XATTR_PREFIX,
                              strlen (AFR_XATTR_PREFIX))) {

                        op_errno = ENODATA;
                        goto out;
                }

                if (!strcmp (name, PUMP_CMD_STATUS)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Hit pump command - status");
                        pump_execute_status (frame, this);
                        op_ret = 0;
                        goto out;
                }
        }

        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_getxattr_cbk,
                            FIRST_CHILD (this),
                            (FIRST_CHILD (this))->fops->getxattr,
                            loc, name);
                return 0;
        }

        read_child = afr_read_child (this, loc->inode);

        if (read_child >= 0) {
                call_child = read_child;

                local->cont.getxattr.last_tried = -1;
        } else {
                call_child = afr_first_up_child (priv);

                if (call_child == -1) {
                        op_errno = ENOTCONN;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "no child is up");
                        goto out;
                }

                local->cont.getxattr.last_tried = call_child;
        }

	loc_copy (&local->loc, loc);
	if (name)
	  local->cont.getxattr.name       = gf_strdup (name);

	STACK_WIND_COOKIE (frame, pump_getxattr_cbk,
			   (void *) (long) call_child,
			   children[call_child], children[call_child]->fops->getxattr,
			   loc, name);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, NULL);
	}
	return 0;
}

static int
afr_setxattr_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		AFR_STACK_UNWIND (setxattr, main_frame,
                                  local->op_ret, local->op_errno)
	}
	return 0;
}

static int
afr_setxattr_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int need_unwind = 0;

	local = frame->local;
	priv = this->private;

	LOCK (&frame->lock);
	{
		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->child_count) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		local->transaction.unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}

	return 0;
}

static int
afr_setxattr_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count = -1;
	int i = 0;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_setxattr_wind_cbk,
					   (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->setxattr,
					   &local->loc,
					   local->cont.setxattr.dict,
					   local->cont.setxattr.flags);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


static int
afr_setxattr_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t * local = frame->local;

	local->transaction.unwind (frame, this);

	AFR_STACK_DESTROY (frame);

	return 0;
}

int32_t
pump_setxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int
pump_command_reply (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0)
                gf_log (this->name, GF_LOG_INFO,
                        "Command failed");
        else
                gf_log (this->name, GF_LOG_INFO,
                        "Command succeeded");

        dict_unref (local->dict);

        AFR_STACK_UNWIND (setxattr,
                          frame,
                          local->op_ret,
                          local->op_errno);

        return 0;
}

int
pump_parse_command (call_frame_t *frame, xlator_t *this,
                    afr_local_t *local, dict_t *dict)
{

        int ret = -1;

        if (pump_command_start (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_start (frame, this);

        } else if (pump_command_pause (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_pause (frame, this);

        } else if (pump_command_abort (this, dict)) {
                frame->local = local;
                local->dict = dict_ref (dict);
                ret = pump_execute_abort (frame, this);
        }
        return ret;
}

int
pump_setxattr (call_frame_t *frame, xlator_t *this,
               loc_t *loc, dict_t *dict, int32_t flags)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;
	call_frame_t   *transaction_frame = NULL;

	int ret = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

        ret = pump_parse_command (frame, this,
                                  local, dict);
        if (ret >= 0) {
                op_ret = 0;
                goto out;
        }

        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_setxattr_cbk,
                            FIRST_CHILD (this),
                            (FIRST_CHILD (this))->fops->setxattr,
                            loc, dict, flags);
                return 0;
        }

	transaction_frame = copy_frame (frame);
	if (!transaction_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
	}

	transaction_frame->local = local;

	local->op_ret = -1;

	local->cont.setxattr.dict  = dict_ref (dict);
	local->cont.setxattr.flags = flags;

	local->transaction.fop    = afr_setxattr_wind;
	local->transaction.done   = afr_setxattr_done;
	local->transaction.unwind = afr_setxattr_unwind;

	loc_copy (&local->loc, loc);

	local->transaction.main_frame = frame;
	local->transaction.start   = LLONG_MAX - 1;
	local->transaction.len     = 0;

	afr_transaction (transaction_frame, this, AFR_METADATA_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (transaction_frame)
			AFR_STACK_DESTROY (transaction_frame);
		AFR_STACK_UNWIND (setxattr, frame, op_ret, op_errno);
	}

	return 0;
}

/* Defaults */
static int32_t
pump_lookup (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             dict_t *xattr_req)
{
	afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_lookup_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup,
                            loc,
                            xattr_req);
                return 0;
        }

        afr_lookup (frame, this, loc, xattr_req);
        return 0;
}


static int32_t
pump_truncate (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               off_t offset)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_truncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate,
                            loc,
                            offset);
                return 0;
        }

        afr_truncate (frame, this, loc, offset);
        return 0;
}


static int32_t
pump_ftruncate (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd,
                off_t offset)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_ftruncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate,
                            fd,
                            offset);
                return 0;
        }

        afr_ftruncate (frame, this, fd, offset);
        return 0;
}




int
pump_mknod (call_frame_t *frame, xlator_t *this,
            loc_t *loc, mode_t mode, dev_t rdev, dict_t *parms)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_mknod_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mknod,
                            loc, mode, rdev, parms);
                return 0;
        }
        afr_mknod (frame, this, loc, mode, rdev, parms);
        return 0;

}



int
pump_mkdir (call_frame_t *frame, xlator_t *this,
            loc_t *loc, mode_t mode, dict_t *params)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_mkdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->mkdir,
                            loc, mode, params);
                return 0;
        }
        afr_mkdir (frame, this, loc, mode, params);
        return 0;

}


static int32_t
pump_unlink (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_unlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            loc);
                return 0;
        }
        afr_unlink (frame, this, loc);
        return 0;

}


static int
pump_rmdir (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int flags)
{
        afr_private_t *priv  = NULL;

	priv = this->private;

        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_rmdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rmdir,
                            loc, flags);
                return 0;
        }

        afr_rmdir (frame, this, loc, flags);
        return 0;

}



int
pump_symlink (call_frame_t *frame, xlator_t *this,
              const char *linkpath, loc_t *loc, dict_t *params)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_symlink_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->symlink,
                            linkpath, loc, params);
                return 0;
        }
        afr_symlink (frame, this, linkpath, loc, params);
        return 0;

}


static int32_t
pump_rename (call_frame_t *frame,
             xlator_t *this,
             loc_t *oldloc,
             loc_t *newloc)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_rename_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename,
                            oldloc, newloc);
                return 0;
        }
        afr_rename (frame, this, oldloc, newloc);
        return 0;

}


static int32_t
pump_link (call_frame_t *frame,
           xlator_t *this,
           loc_t *oldloc,
           loc_t *newloc)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_link_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->link,
                            oldloc, newloc);
                return 0;
        }
        afr_link (frame, this, oldloc, newloc);
        return 0;

}


static int32_t
pump_create (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int32_t flags, mode_t mode,
             fd_t *fd, dict_t *params)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame, default_create_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->create,
                            loc, flags, mode, fd, params);
                return 0;
        }
        afr_create (frame, this, loc, flags, mode, fd, params);
        return 0;

}


static int32_t
pump_open (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           int32_t flags, fd_t *fd,
           int32_t wbflags)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_open_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->open,
                            loc, flags, fd, wbflags);
                return 0;
        }
        afr_open (frame, this, loc, flags, fd, wbflags);
        return 0;

}


static int32_t
pump_writev (call_frame_t *frame,
             xlator_t *this,
             fd_t *fd,
             struct iovec *vector,
             int32_t count,
             off_t off,
             struct iobref *iobref)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_writev_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->writev,
                            fd,
                            vector,
                            count,
                            off,
                            iobref);
                return 0;
        }
        afr_writev (frame, this, fd, vector, count, off, iobref);
        return 0;

}


static int32_t
pump_flush (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_flush_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->flush,
                            fd);
                return 0;
        }
        afr_flush (frame, this, fd);
        return 0;

}


static int32_t
pump_fsync (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            int32_t flags)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fsync_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsync,
                            fd,
                            flags);
                return 0;
        }
        afr_fsync (frame, this, fd, flags);
        return 0;

}


static int32_t
pump_opendir (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc, fd_t *fd)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_opendir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->opendir,
                            loc, fd);
                return 0;
        }
        afr_opendir (frame, this, loc, fd);
        return 0;

}


static int32_t
pump_fsyncdir (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               int32_t flags)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fsyncdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsyncdir,
                            fd,
                            flags);
                return 0;
        }
        afr_fsyncdir (frame, this, fd, flags);
        return 0;

}


static int32_t
pump_xattrop (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              gf_xattrop_flags_t flags,
              dict_t *dict)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_xattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->xattrop,
                            loc,
                            flags,
                            dict);
                return 0;
        }
        afr_xattrop (frame, this, loc, flags, dict);
        return 0;

}

static int32_t
pump_fxattrop (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               gf_xattrop_flags_t flags,
               dict_t *dict)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fxattrop_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fxattrop,
                            fd,
                            flags,
                            dict);
                return 0;
        }
        afr_fxattrop (frame, this, fd, flags, dict);
        return 0;

}


static int32_t
pump_removexattr (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc,
                  const char *name)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_removexattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            loc,
                            name);
                return 0;
        }
        afr_removexattr (frame, this, loc, name);
        return 0;

}



static int32_t
pump_readdir (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              size_t size,
              off_t off)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_readdir_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdir,
                            fd, size, off);
                return 0;
        }
        afr_readdir (frame, this, fd, size, off);
        return 0;

}


static int32_t
pump_readdirp (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               size_t size,
               off_t off)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, off);
                return 0;
        }
        afr_readdirp (frame, this, fd, size, off);
        return 0;

}



static int32_t
pump_releasedir (xlator_t *this,
                 fd_t *fd)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (priv->use_afr_in_pump)
                afr_releasedir (this, fd);
	return 0;

}

static int32_t
pump_release (xlator_t *this,
              fd_t *fd)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (priv->use_afr_in_pump)
                afr_release (this, fd);
	return 0;

}


static int32_t
pump_setattr (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              struct iatt *stbuf,
              int32_t valid)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_setattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr,
                            loc, stbuf, valid);
                return 0;
        }
        afr_setattr (frame, this, loc, stbuf, valid);
        return 0;

}


static int32_t
pump_fsetattr (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               struct iatt *stbuf,
               int32_t valid)
{
        afr_private_t *priv  = NULL;
	priv = this->private;
        if (!priv->use_afr_in_pump) {
                STACK_WIND (frame,
                            default_fsetattr_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsetattr,
                            fd, stbuf, valid);
                return 0;
        }
        afr_fsetattr (frame, this, fd, stbuf, valid);
        return 0;

}


/* End of defaults */


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_afr_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

static int
is_xlator_pump_sink (xlator_t *child)
{
        return (child == PUMP_SINK_CHILD(THIS));
}

static int
is_xlator_pump_source (xlator_t *child)
{
        return (child == PUMP_SOURCE_CHILD(THIS));
}

int32_t
notify (xlator_t *this, int32_t event,
	void *data, ...)
{
        int ret = -1;
        xlator_t *child_xl = NULL;

        child_xl = (xlator_t *) data;

        ret = afr_notify (this, event, data);

	switch (event) {
	case GF_EVENT_CHILD_DOWN:
                if (is_xlator_pump_source (child_xl))
                        pump_change_state (this, PUMP_STATE_ABORT);
                break;

        case GF_EVENT_CHILD_UP:
                if (is_xlator_pump_sink (child_xl))
                        if (is_pump_start_pending (this)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "about to start synctask");
                                ret = pump_start_synctask (this);
                                if (ret < 0)
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "Could not start pump "
                                                "synctask");
                                else
                                        pump_remove_start_pending (this);
                        }
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
	afr_private_t * priv        = NULL;
        pump_private_t *pump_priv = NULL;
	int             child_count = 0;
	xlator_list_t * trav        = NULL;
	int             i           = 0;
	int             ret         = -1;
	int             op_errno    = 0;

        int source_child = 0;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"pump translator needs a source and sink"
                        "subvolumes defined.");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"Volume is dangling.");
	}

	ALLOC_OR_GOTO (this->private, afr_private_t, out);

	priv = this->private;

        priv->read_child = source_child;
        priv->favorite_child = source_child;
        priv->background_self_heal_count = 0;

	priv->data_self_heal     = 1;
	priv->metadata_self_heal = 1;
	priv->entry_self_heal    = 1;

        priv->data_self_heal_algorithm = "";

        priv->data_self_heal_window_size = 16;

	priv->data_change_log     = 1;
	priv->metadata_change_log = 1;
	priv->entry_change_log    = 1;
        priv->use_afr_in_pump = 1;

	/* Locking options */

        /* Lock server count infact does not matter. Locks are held
           on all subvolumes, in this case being the source
           and the sink.
        */

	priv->data_lock_server_count = 2;
	priv->metadata_lock_server_count = 2;
	priv->entry_lock_server_count = 2;

	priv->strict_readdir = _gf_false;

        trav = this->children;
        while (trav) {
                child_count++;
                trav = trav->next;
        }

	priv->wait_count = 1;

        if (child_count != 2) {
                gf_log (this->name, GF_LOG_ERROR,
                        "There should be exactly 2 children - one source "
                        "and one sink");
                return -1;
        }
	priv->child_count = child_count;

	LOCK_INIT (&priv->lock);
        LOCK_INIT (&priv->read_child_lock);

	priv->child_up = GF_CALLOC (sizeof (unsigned char), child_count,
                                 gf_afr_mt_char);
	if (!priv->child_up) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_errno = ENOMEM;
		goto out;
	}

	priv->children = GF_CALLOC (sizeof (xlator_t *), child_count,
                                 gf_afr_mt_xlator_t);
	if (!priv->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		op_errno = ENOMEM;
		goto out;
	}

        priv->pending_key = GF_CALLOC (sizeof (*priv->pending_key),
                                       child_count,
                                       gf_afr_mt_char);
        if (!priv->pending_key) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_errno = ENOMEM;
                goto out;
        }

	trav = this->children;
	i = 0;
	while (i < child_count) {
		priv->children[i] = trav->xlator;

                ret = gf_asprintf (&priv->pending_key[i], "%s.%s", AFR_XATTR_PREFIX,
                                trav->xlator->name);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "asprintf failed to set pending key");
                        op_errno = ENOMEM;
                        goto out;
                }

		trav = trav->next;
		i++;
	}

        priv->first_lookup = 1;
        priv->root_inode = NULL;

        priv->last_event = GF_CALLOC (child_count, sizeof (*priv->last_event),
                                      gf_afr_mt_int32_t);
        if (!priv->last_event) {
                ret = -ENOMEM;
                goto out;
        }

	pump_priv = GF_CALLOC (1, sizeof (*pump_priv),
                            gf_afr_mt_pump_priv);
	if (!pump_priv) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory");
                op_errno = ENOMEM;
		goto out;
	}

        LOCK_INIT (&pump_priv->resume_path_lock);
        LOCK_INIT (&pump_priv->pump_state_lock);

        pump_priv->resume_path = GF_CALLOC (1, PATH_MAX,
                                            gf_afr_mt_char);
        if (!pump_priv->resume_path) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

	pump_priv->env = syncenv_new (0);
        if (!pump_priv->env) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not create new sync-environment");
                ret = -1;
                goto out;
        }

	priv->pump_private = pump_priv;

        pthread_mutex_init (&priv->mutex, NULL);
        INIT_LIST_HEAD (&priv->saved_fds);

        pump_change_state (this, PUMP_STATE_ABORT);

	ret = 0;
out:
	return ret;
}

int
fini (xlator_t *this)
{
	return 0;
}


struct xlator_fops fops = {
	.lookup      = pump_lookup,
	.open        = pump_open,
	.flush       = pump_flush,
	.fsync       = pump_fsync,
	.fsyncdir    = pump_fsyncdir,
	.xattrop     = pump_xattrop,
	.fxattrop    = pump_fxattrop,
        .getxattr    = pump_getxattr,

	/* inode write */
	.writev      = pump_writev,
	.truncate    = pump_truncate,
	.ftruncate   = pump_ftruncate,
	.setxattr    = pump_setxattr,
        .setattr     = pump_setattr,
	.fsetattr    = pump_fsetattr,
	.removexattr = pump_removexattr,

	/* dir read */
	.opendir     = pump_opendir,
	.readdir     = pump_readdir,
	.readdirp    = pump_readdirp,

	/* dir write */
	.create      = pump_create,
	.mknod       = pump_mknod,
	.mkdir       = pump_mkdir,
	.unlink      = pump_unlink,
	.rmdir       = pump_rmdir,
	.link        = pump_link,
	.symlink     = pump_symlink,
	.rename      = pump_rename,
};

struct xlator_dumpops dumpops = {
        .priv       = afr_priv_dump,
};


struct xlator_cbks cbks = {
	.release     = pump_release,
	.releasedir  = pump_releasedir,
};

struct volume_options options[] = {
	{ .key  = {NULL} },
};
