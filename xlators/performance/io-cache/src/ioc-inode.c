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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "io-cache.h"
#include "ioc-mem-types.h"

extern int ioc_log2_page_size;

/*
 * str_to_ptr - convert a string to pointer
 * @string: string
 *
 */
void *
str_to_ptr (char *string)
{
        void *ptr = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", string, out);

        ptr = (void *)strtoul (string, NULL, 16);

out:
        return ptr;
}


/*
 * ptr_to_str - convert a pointer to string
 * @ptr: pointer
 *
 */
char *
ptr_to_str (void *ptr)
{
        int   ret = 0;
        char *str = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", ptr, out);

        ret = gf_asprintf (&str, "%p", ptr);
        if (-1 == ret) {
                gf_log ("io-cache", GF_LOG_WARNING,
                        "asprintf failed while converting ptr to str");
                str = NULL;
                goto out;
        }

out:
        return str;
}


void
ioc_inode_wakeup (call_frame_t *frame, ioc_inode_t *ioc_inode,
                  struct iatt *stbuf)
{
        ioc_waitq_t *waiter            = NULL, *waited = NULL;
        ioc_waitq_t *page_waitq        = NULL;
        int8_t       cache_still_valid = 1;
        ioc_local_t *local             = NULL;
        int8_t       need_fault        = 0;
        ioc_page_t  *waiter_page       = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", frame, out);

        local = frame->local;
        GF_VALIDATE_OR_GOTO (frame->this->name, local, out);

        if (ioc_inode == NULL) {
                local->op_ret = -1;
                local->op_errno = EINVAL;
                gf_log (frame->this->name, GF_LOG_WARNING, "ioc_inode is NULL");
                goto out;
        }

        ioc_inode_lock (ioc_inode);
        {
                waiter = ioc_inode->waitq;
                ioc_inode->waitq = NULL;
        }
        ioc_inode_unlock (ioc_inode);

        if (stbuf)
                cache_still_valid = ioc_cache_still_valid (ioc_inode, stbuf);
        else
                cache_still_valid = 0;

        if (!waiter) {
                gf_log (frame->this->name, GF_LOG_WARNING,
                        "cache validate called without any "
                        "page waiting to be validated");
        }

        while (waiter) {
                waiter_page = waiter->data;
                page_waitq = NULL;

                if (waiter_page) {
                        if (cache_still_valid) {
                                /* cache valid, wake up page */
                                ioc_inode_lock (ioc_inode);
                                {
                                        page_waitq =
                                                __ioc_page_wakeup (waiter_page);
                                }
                                ioc_inode_unlock (ioc_inode);

                                if (page_waitq)
                                        ioc_waitq_return (page_waitq);
                        } else {
                                /* cache invalid, generate page fault and set
                                 * page->ready = 0, to avoid double faults
                                 */
                                ioc_inode_lock (ioc_inode);
                                {
                                        if (waiter_page->ready) {
                                                waiter_page->ready = 0;
                                                need_fault = 1;
                                        } else {
                                                gf_log (frame->this->name,
                                                        GF_LOG_TRACE,
                                                        "validate frame(%p) is "
                                                        "waiting for "
                                                        "in-transit page = %p",
                                                        frame, waiter_page);
                                        }
                                }
                                ioc_inode_unlock (ioc_inode);

                                if (need_fault) {
                                        need_fault = 0;
                                        ioc_page_fault (ioc_inode, frame,
                                                        local->fd,
                                                        waiter_page->offset);
                                }
                        }
                }

                waited = waiter;
                waiter = waiter->next;

                waited->data = NULL;
                GF_FREE (waited);
        }

out:
        return;
}


/*
 * ioc_inode_update - create a new ioc_inode_t structure and add it to
 *                    the table table. fill in the fields which are derived
 *                    from inode_t corresponding to the file
 *
 * @table: io-table structure
 * @inode: inode structure
 *
 * not for external reference
 */
ioc_inode_t *
ioc_inode_update (ioc_table_t *table, inode_t *inode, uint32_t weight)
{
        ioc_inode_t     *ioc_inode   = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", table, out);

        ioc_inode = GF_CALLOC (1, sizeof (ioc_inode_t), gf_ioc_mt_ioc_inode_t);
        if (ioc_inode == NULL) {
                goto out;
        }

        ioc_inode->table = table;
        INIT_LIST_HEAD (&ioc_inode->cache.page_lru);
        pthread_mutex_init (&ioc_inode->inode_lock, NULL);
        ioc_inode->weight = weight;

        ioc_table_lock (table);
        {
                table->inode_count++;
                list_add (&ioc_inode->inode_list, &table->inodes);
                list_add_tail (&ioc_inode->inode_lru,
                               &table->inode_lru[weight]);
        }
        ioc_table_unlock (table);

        gf_log (table->xl->name, GF_LOG_TRACE,
                "adding to inode_lru[%d]", weight);

out:
        return ioc_inode;
}


/*
 * ioc_inode_destroy - destroy an ioc_inode_t object.
 *
 * @inode: inode to destroy
 *
 * to be called only from ioc_forget.
 */
void
ioc_inode_destroy (ioc_inode_t *ioc_inode)
{
        ioc_table_t *table = NULL;

        GF_VALIDATE_OR_GOTO ("io-cache", ioc_inode, out);

        table = ioc_inode->table;

        ioc_table_lock (table);
        {
                table->inode_count--;
                list_del (&ioc_inode->inode_list);
                list_del (&ioc_inode->inode_lru);
        }
        ioc_table_unlock (table);

        ioc_inode_flush (ioc_inode);
        rbthash_table_destroy (ioc_inode->cache.page_table);

        pthread_mutex_destroy (&ioc_inode->inode_lock);
        GF_FREE (ioc_inode);
out:
        return;
}
