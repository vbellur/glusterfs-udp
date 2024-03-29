/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "byte-order.h"

#define layout_base_size (sizeof (dht_layout_t))

#define layout_entry_size (sizeof ((dht_layout_t *)NULL)->list[0])

#define layout_size(cnt) (layout_base_size + (cnt * layout_entry_size))


dht_layout_t *
dht_layout_new (xlator_t *this, int cnt)
{
        dht_layout_t *layout = NULL;
        dht_conf_t   *conf = NULL;


        conf = this->private;

        layout = GF_CALLOC (1, layout_size (cnt),
                            gf_dht_mt_dht_layout_t);
        if (!layout) {
                goto out;
        }

        layout->type = DHT_HASH_TYPE_DM;
        layout->cnt = cnt;
        if (conf)
                layout->gen = conf->gen;

        layout->ref = 1;
out:
        return layout;
}


dht_layout_t *
dht_layout_get (xlator_t *this, inode_t *inode)
{
        dht_conf_t   *conf = NULL;
        uint64_t      layout_int = 0;
        dht_layout_t *layout = NULL;
        int           ret    = -1;

        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&conf->layout_lock);
        {
                ret = inode_ctx_get (inode, this, &layout_int);
                if (ret == 0) {
                        layout = (dht_layout_t *) (unsigned long) layout_int;
                        layout->ref++;
                }
        }
        UNLOCK (&conf->layout_lock);

out:
        return layout;
}


int
dht_layout_set (xlator_t *this, inode_t *inode, dht_layout_t *layout)
{
        dht_conf_t   *conf = NULL;
        int           oldret = -1;
        int           ret = 0;
        dht_layout_t *old_layout;
        uint64_t      old_layout_int;

        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&conf->layout_lock);
        {
                oldret = inode_ctx_get (inode, this, &old_layout_int);

                layout->ref++;
                ret = inode_ctx_put (inode, this, (uint64_t) (unsigned long)
                                     layout);
        }
        UNLOCK (&conf->layout_lock);

        if (oldret == 0) {
                old_layout = (dht_layout_t *) (unsigned long) old_layout_int;
                dht_layout_unref (this, old_layout);
        }

out:
        return ret;
}


void
dht_layout_unref (xlator_t *this, dht_layout_t *layout)
{
        dht_conf_t  *conf = NULL;
        int          ref = 0;

        if (layout->preset || !this->private)
                return;

        conf = this->private;

        LOCK (&conf->layout_lock);
        {
                ref = --layout->ref;
        }
        UNLOCK (&conf->layout_lock);

        if (!ref)
                GF_FREE (layout);
}


dht_layout_t *
dht_layout_ref (xlator_t *this, dht_layout_t *layout)
{
        dht_conf_t  *conf = NULL;

        if (layout->preset || !this->private)
                return layout;

        conf = this->private;
        LOCK (&conf->layout_lock);
        {
                layout->ref++;
        }
        UNLOCK (&conf->layout_lock);

        return layout;
}


xlator_t *
dht_layout_search (xlator_t *this, dht_layout_t *layout, const char *name)
{
        uint32_t   hash = 0;
        xlator_t  *subvol = NULL;
        int        i = 0;
        int        ret = 0;


        ret = dht_hash_compute (layout->type, name, &hash);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "hash computation failed for type=%d name=%s",
                        layout->type, name);
                goto out;
        }

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].start <= hash
                    && layout->list[i].stop >= hash) {
                        subvol = layout->list[i].xlator;
                        break;
                }
        }

        if (!subvol) {
                gf_log (this->name, GF_LOG_INFO,
                        "no subvolume for hash (value) = %u", hash);
        }

out:
        return subvol;
}


dht_layout_t *
dht_layout_for_subvol (xlator_t *this, xlator_t *subvol)
{
        dht_conf_t   *conf = NULL;
        dht_layout_t *layout = NULL;
        int           i = 0;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == subvol) {
                        layout = conf->file_layouts[i];
                        break;
                }
        }

out:
        return layout;
}


int
dht_layouts_init (xlator_t *this, dht_conf_t *conf)
{
        dht_layout_t *layout = NULL;
        int           i = 0;
        int           ret = -1;

        if (!conf)
                goto out;

        conf->file_layouts = GF_CALLOC (conf->subvolume_cnt,
                                        sizeof (dht_layout_t *),
                                        gf_dht_mt_dht_layout_t);
        if (!conf->file_layouts) {
                goto out;
        }

        for (i = 0; i < conf->subvolume_cnt; i++) {
                layout = dht_layout_new (this, 1);

                if (!layout) {
                        goto out;
                }

                layout->preset = 1;

                layout->list[0].xlator = conf->subvolumes[i];

                conf->file_layouts[i] = layout;
        }

        ret = 0;
out:
        return ret;
}


int
dht_disk_layout_extract (xlator_t *this, dht_layout_t *layout,
                         int pos, int32_t **disk_layout_p)
{
        int      ret = -1;
        int32_t *disk_layout = NULL;

        disk_layout = GF_CALLOC (5, sizeof (int),
                                 gf_dht_mt_int32_t);
        if (!disk_layout) {
                goto out;
        }

        disk_layout[0] = hton32 (1);
        disk_layout[1] = hton32 (layout->type);
        disk_layout[2] = hton32 (layout->list[pos].start);
        disk_layout[3] = hton32 (layout->list[pos].stop);

        if (disk_layout_p)
                *disk_layout_p = disk_layout;
        ret = 0;

out:
        return ret;
}


int
dht_disk_layout_merge (xlator_t *this, dht_layout_t *layout,
                       int pos, void *disk_layout_raw)
{
        int      cnt = 0;
        int      type = 0;
        int      start_off = 0;
        int      stop_off = 0;
        int      disk_layout[4];

        /* TODO: assert disk_layout_ptr is of required length */

        memcpy (disk_layout, disk_layout_raw, sizeof (disk_layout));

        cnt  = ntoh32 (disk_layout[0]);
        if (cnt != 1) {
                gf_log (this->name, GF_LOG_INFO,
                        "disk layout has invalid count %d", cnt);
                return -1;
        }

        /* TODO: assert type is compatible */
        type      = ntoh32 (disk_layout[1]);
        start_off = ntoh32 (disk_layout[2]);
        stop_off  = ntoh32 (disk_layout[3]);

        layout->list[pos].start = start_off;
        layout->list[pos].stop  = stop_off;

        gf_log (this->name, GF_LOG_TRACE,
                "merged to layout: %u - %u (type %d) from %s",
                start_off, stop_off, type,
                layout->list[pos].xlator->name);

        return 0;
}


int
dht_layout_merge (xlator_t *this, dht_layout_t *layout, xlator_t *subvol,
                  int op_ret, int op_errno, dict_t *xattr)
{
        int      i     = 0;
        int      ret   = -1;
        int      err   = -1;
        void    *disk_layout_raw = NULL;


        if (op_ret != 0) {
                err = op_errno;
        }

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].xlator == NULL) {
                        layout->list[i].err    = err;
                        layout->list[i].xlator = subvol;
                        break;
                }
        }

        if (op_ret != 0) {
                ret = 0;
                goto out;
        }

        if (xattr) {
                /* during lookup and not mkdir */
                ret = dict_get_ptr (xattr, "trusted.glusterfs.dht",
                                    &disk_layout_raw);
        }

        if (ret != 0) {
                layout->list[i].err = -1;
                gf_log (this->name, GF_LOG_TRACE,
                        "missing disk layout on %s. err = %d",
                        subvol->name, err);
                ret = 0;
                goto out;
        }

        ret = dht_disk_layout_merge (this, layout, i, disk_layout_raw);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "layout merge from subvolume %s failed",
                        subvol->name);
                goto out;
        }
        layout->list[i].err = 0;

out:
        return ret;
}


void
dht_layout_entry_swap (dht_layout_t *layout, int i, int j)
{
        uint32_t  start_swap = 0;
        uint32_t  stop_swap = 0;
        xlator_t *xlator_swap = 0;
        int       err_swap = 0;

        start_swap  = layout->list[i].start;
        stop_swap   = layout->list[i].stop;
        xlator_swap = layout->list[i].xlator;
        err_swap    = layout->list[i].err;

        layout->list[i].start  = layout->list[j].start;
        layout->list[i].stop   = layout->list[j].stop;
        layout->list[i].xlator = layout->list[j].xlator;
        layout->list[i].err    = layout->list[j].err;

        layout->list[j].start  = start_swap;
        layout->list[j].stop   = stop_swap;
        layout->list[j].xlator = xlator_swap;
        layout->list[j].err    = err_swap;
}

int64_t
dht_layout_entry_cmp_volname (dht_layout_t *layout, int i, int j)
{
        return (strcmp (layout->list[i].xlator->name,
                        layout->list[j].xlator->name));
}

int64_t
dht_layout_entry_cmp (dht_layout_t *layout, int i, int j)
{
        int64_t diff = 0;

        if (layout->list[i].err || layout->list[j].err)
                diff = layout->list[i].err - layout->list[j].err;
        else
                diff = (int64_t) layout->list[i].start
                        - (int64_t) layout->list[j].start;

        return diff;
}


int
dht_layout_sort (dht_layout_t *layout)
{
        int       i = 0;
        int       j = 0;
        int64_t   ret = 0;

        /* TODO: O(n^2) -- bad bad */

        for (i = 0; i < layout->cnt - 1; i++) {
                for (j = i + 1; j < layout->cnt; j++) {
                        ret = dht_layout_entry_cmp (layout, i, j);
                        if (ret > 0)
                                dht_layout_entry_swap (layout, i, j);
                }
        }

        return 0;
}

int
dht_layout_sort_volname (dht_layout_t *layout)
{
        int       i = 0;
        int       j = 0;
        int64_t   ret = 0;

        /* TODO: O(n^2) -- bad bad */

        for (i = 0; i < layout->cnt - 1; i++) {
                for (j = i + 1; j < layout->cnt; j++) {
                        ret = dht_layout_entry_cmp_volname (layout, i, j);
                        if (ret > 0)
                                dht_layout_entry_swap (layout, i, j);
                }
        }

        return 0;
}


int
dht_layout_anomalies (xlator_t *this, loc_t *loc, dht_layout_t *layout,
                      uint32_t *holes_p, uint32_t *overlaps_p,
                      uint32_t *missing_p, uint32_t *down_p, uint32_t *misc_p)
{
        uint32_t    overlaps = 0;
        uint32_t    missing  = 0;
        uint32_t    down     = 0;
        uint32_t    misc     = 0;
        uint32_t    hole_cnt = 0;
        uint32_t    overlap_cnt = 0;
        int         i = 0;
        int         ret = 0;
        uint32_t    prev_stop = 0;
        uint32_t    last_stop = 0;
        char        is_virgin = 1;

        /* TODO: explain WTF is happening */

        last_stop = layout->list[0].start - 1;
        prev_stop = last_stop;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err) {
                        switch (layout->list[i].err) {
                        case -1:
                        case ENOENT:
                                missing++;
                                break;
                        case ENOTCONN:
                                down++;
                                break;
                        case ENOSPC:
                                down++;
                                break;
                        default:
                                misc++;
                        }
                        continue;
                }

                is_virgin = 0;

                if ((prev_stop + 1) < layout->list[i].start) {
                        hole_cnt++;
                }

                if ((prev_stop + 1) > layout->list[i].start) {
                        overlap_cnt++;
                        overlaps += ((prev_stop + 1) - layout->list[i].start);
                }
                prev_stop = layout->list[i].stop;
        }

        if ((last_stop - prev_stop) || is_virgin)
                hole_cnt++;

        if (holes_p)
                *holes_p = hole_cnt;

        if (overlaps_p)
                *overlaps_p = overlap_cnt;

        if (missing_p)
                *missing_p = missing;

        if (down_p)
                *down_p = down;

        if (misc_p)
                *misc_p = misc;

        return ret;
}


int
dht_layout_normalize (xlator_t *this, loc_t *loc, dht_layout_t *layout)
{
        int          ret   = 0;
        int          i = 0;
        uint32_t     holes = 0;
        uint32_t     overlaps = 0;
        uint32_t     missing = 0;
        uint32_t     down = 0;
        uint32_t     misc = 0;


        ret = dht_layout_sort (layout);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "sort failed?! how the ....");
                goto out;
        }

        ret = dht_layout_anomalies (this, loc, layout,
                                    &holes, &overlaps,
                                    &missing, &down, &misc);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "error while finding anomalies in %s -- not good news",
                        loc->path);
                goto out;
        }

        if (holes || overlaps) {
                if (missing == layout->cnt) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "directory %s looked up first time",
                                loc->path);
                } else {
                        gf_log (this->name, GF_LOG_INFO,
                                "found anomalies in %s. holes=%d overlaps=%d",
                                loc->path, holes, overlaps);
                }
                ret = 1;
        }

        for (i = 0; i < layout->cnt; i++) {
                /* TODO During DHT selfheal rewrite (almost) find a better place to
                 * detect this - probably in dht_layout_anomalies()
                 */
                if (layout->list[i].err > 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "path=%s err=%s on subvol=%s",
                                loc->path, strerror (layout->list[i].err),
                                (layout->list[i].xlator ?
                                 layout->list[i].xlator->name : "<>"));
                        if (layout->list[i].err == ENOENT)
                                ret = 1;
                }
        }

out:
        return ret;
}


int
dht_layout_dir_mismatch (xlator_t *this, dht_layout_t *layout, xlator_t *subvol,
                         loc_t *loc, dict_t *xattr)
{
        int       idx = 0;
        int       pos = -1;
        int       ret = 0;
        int       err = 0;
        int       dict_ret = 0;
        int32_t   disk_layout[4];
        void     *disk_layout_raw = NULL;
        int32_t   count = -1;
        uint32_t  start_off = -1;
        uint32_t  stop_off = -1;


        for (idx = 0; idx < layout->cnt; idx++) {
                if (layout->list[idx].xlator == subvol) {
                        pos = idx;
                        break;
                }
        }

        if (pos == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s - no layout info for subvolume %s",
                        loc->path, subvol->name);
                ret = 1;
                goto out;
        }

        err = layout->list[pos].err;

        if (!xattr) {
                if (err == 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "%s - xattr dictionary is NULL",
                                loc->path);
                        ret = -1;
                }
                goto out;
        }

        dict_ret = dict_get_ptr (xattr, "trusted.glusterfs.dht",
                                 &disk_layout_raw);

        if (dict_ret < 0) {
                if (err == 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "%s - disk layout missing", loc->path);
                        ret = -1;
                }
                goto out;
        }

        memcpy (disk_layout, disk_layout_raw, sizeof (disk_layout));

        count  = ntoh32 (disk_layout[0]);
        if (count != 1) {
                gf_log (this->name, GF_LOG_INFO,
                        "%s - disk layout has invalid count %d",
                        loc->path, count);
                ret = -1;
                goto out;
        }

        start_off = ntoh32 (disk_layout[2]);
        stop_off  = ntoh32 (disk_layout[3]);

        if ((layout->list[pos].start != start_off)
            || (layout->list[pos].stop != stop_off)) {
                gf_log (this->name, GF_LOG_INFO,
                        "subvol: %s; inode layout - %"PRIu32" - %"PRIu32"; "
                        "disk layout - %"PRIu32" - %"PRIu32,
                        layout->list[pos].xlator->name,
                        layout->list[pos].start, layout->list[pos].stop,
                        start_off, stop_off);
                ret = 1;
        } else {
                ret = 0;
        }
out:
        return ret;
}


int
dht_layout_preset (xlator_t *this, xlator_t *subvol, inode_t *inode)
{
        dht_layout_t *layout = NULL;
        int           ret = -1;
        dht_conf_t   *conf = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        layout = dht_layout_for_subvol (this, subvol);
        if (!layout) {
                gf_log (this->name, GF_LOG_INFO,
                        "no pre-set layout for subvolume %s",
                        subvol ? subvol->name : "<nil>");
                ret = -1;
                goto out;
        }

        LOCK (&conf->layout_lock);
        {
                inode_ctx_put (inode, this, (uint64_t)(long)layout);
        }
        UNLOCK (&conf->layout_lock);

        ret = 0;
out:
        return ret;
}
