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

#ifndef _ERROR_GEN_H
#define _ERROR_GEN_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define GF_FAILURE_DEFAULT 10

typedef struct {
        int enable[GF_FOP_MAXVALUE];
        int op_count;
        int failure_iter_no;
        char *error_no;
        gf_lock_t lock;
} eg_t;

typedef struct {
        int error_no_count;
	int error_no[20];
} sys_error_t;

#endif
