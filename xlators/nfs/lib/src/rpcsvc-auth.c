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

#include "rpcsvc.h"
#include "logging.h"
#include "dict.h"

extern rpcsvc_auth_t *
nfs_rpcsvc_auth_null_init (rpcsvc_t *svc, dict_t *options);

extern rpcsvc_auth_t *
nfs_rpcsvc_auth_unix_init (rpcsvc_t *svc, dict_t *options);

int
nfs_rpcsvc_auth_add_initer (struct list_head *list, char *idfier,
                            rpcsvc_auth_initer_t init)
{
        struct rpcsvc_auth_list         *new = NULL;

        if ((!list) || (!init) || (!idfier))
                return -1;

        new = GF_CALLOC (1, sizeof (*new), gf_common_mt_rpcsvc_auth_list);
        if (!new) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Memory allocation failed");
                return -1;
        }

        new->init = init;
        strcpy (new->name, idfier);
        INIT_LIST_HEAD (&new->authlist);
        list_add_tail (&new->authlist, list);
        return 0;
}



int
nfs_rpcsvc_auth_add_initers (rpcsvc_t *svc)
{
        int     ret = -1;

        ret = nfs_rpcsvc_auth_add_initer (&svc->authschemes, "auth-null",
                                          (rpcsvc_auth_initer_t)
                                          nfs_rpcsvc_auth_null_init);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add AUTH_NULL");
                goto err;
        }

        ret = nfs_rpcsvc_auth_add_initer (&svc->authschemes, "auth-unix",
                                          (rpcsvc_auth_initer_t)
                                          nfs_rpcsvc_auth_unix_init);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add AUTH_UNIX");
                goto err;
        }

err:
        return ret;
}


int
nfs_rpcsvc_auth_init_auth (rpcsvc_t *svc, dict_t *options,
                           struct rpcsvc_auth_list *authitem)
{
        int             ret = -1;

        if ((!svc) || (!options) || (!authitem))
                return -1;

        if (!authitem->init) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "No init function defined");
                ret = -1;
                goto err;
        }

        authitem->auth = authitem->init (svc, options);
        if (!authitem->auth) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Registration of auth failed:"
                        " %s", authitem->name);
                ret = -1;
                goto err;
        }

        authitem->enable = 1;
        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Authentication enabled: %s",
                authitem->auth->authname);

        ret = 0;
err:
        return ret;
}


int
nfs_rpcsvc_auth_init_auths (rpcsvc_t *svc, dict_t *options)
{
        int                     ret = -1;
        struct rpcsvc_auth_list *auth = NULL;
        struct rpcsvc_auth_list *tmp = NULL;

        if (!svc)
                return -1;

        if (list_empty (&svc->authschemes)) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "No authentication!");
                ret = 0;
                goto err;
        }

        /* If auth null and sys are not disabled by the user, we must enable
         * it by default. This is a globally default rule, the user is still
         * allowed to disable the two for particular subvolumes.
         */
        if (!dict_get (options, "rpc-auth.auth-null")) {
                ret = dict_set_dynstr (options, "rpc-auth.auth-null", "on");
                if (ret < 0) {
                        gf_log (GF_RPCSVC, GF_LOG_ERROR,
                                "Failed to set dict value.");
                        goto err;
                }
        }

        if (!dict_get (options, "rpc-auth.auth-unix")) {
                ret = dict_set_dynstr (options, "rpc-auth.auth-unix", "on");
                if (ret < 0) {
                        gf_log (GF_RPCSVC, GF_LOG_ERROR,
                                "Failed to set dict value.");
                        goto err;
                }
        }

        list_for_each_entry_safe (auth, tmp, &svc->authschemes, authlist) {
                ret = nfs_rpcsvc_auth_init_auth (svc, options, auth);
                if (ret == -1)
                        goto err;
        }

        ret = 0;
err:
        return ret;

}

int
nfs_rpcsvc_auth_init (rpcsvc_t *svc, dict_t *options)
{
        int             ret = -1;

        if ((!svc) || (!options))
                return -1;

        ret = nfs_rpcsvc_auth_add_initers (svc);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to add initers");
                goto out;
        }

        ret = nfs_rpcsvc_auth_init_auths (svc, options);
        if (ret == -1) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Failed to init auth schemes");
                goto out;
        }

out:
        return ret;
}


rpcsvc_auth_t *
__nfs_rpcsvc_auth_get_handler (rpcsvc_request_t *req)
{
        struct rpcsvc_auth_list *auth = NULL;
        struct rpcsvc_auth_list *tmp = NULL;
        rpcsvc_t                *svc = NULL;

        if (!req)
                return NULL;

        svc = nfs_rpcsvc_request_service (req);
        if (list_empty (&svc->authschemes)) {
                gf_log (GF_RPCSVC, GF_LOG_WARNING, "No authentication!");
                goto err;
        }

        list_for_each_entry_safe (auth, tmp, &svc->authschemes, authlist) {
                if (!auth->enable)
                        continue;
                if (auth->auth->authnum == req->cred.flavour)
                        goto err;

        }

        auth = NULL;
err:
        if (auth)
                return auth->auth;
        else
                return NULL;
}

rpcsvc_auth_t *
nfs_rpcsvc_auth_get_handler (rpcsvc_request_t *req)
{
        rpcsvc_auth_t           *auth = NULL;

        auth = __nfs_rpcsvc_auth_get_handler (req);
        if (auth)
                goto ret;

        gf_log (GF_RPCSVC, GF_LOG_TRACE, "No auth handler: %d",
                req->cred.flavour);

        /* The requested scheme was not available so fall back the to one
         * scheme that will always be present.
         */
        req->cred.flavour = AUTH_NULL;
        req->verf.flavour = AUTH_NULL;
        auth = __nfs_rpcsvc_auth_get_handler (req);
ret:
        return auth;
}


int
nfs_rpcsvc_auth_request_init (rpcsvc_request_t *req)
{
        int                     ret = -1;
        rpcsvc_auth_t           *auth = NULL;

        if (!req)
                return -1;

        auth = nfs_rpcsvc_auth_get_handler (req);
        if (!auth)
                goto err;
        ret = 0;
        gf_log (GF_RPCSVC, GF_LOG_TRACE, "Auth handler: %s", auth->authname);
        if (!auth->authops->request_init)
                ret = auth->authops->request_init (req, auth->authprivate);

err:
        return ret;
}


int
nfs_rpcsvc_authenticate (rpcsvc_request_t *req)
{
        int                     ret = RPCSVC_AUTH_REJECT;
        rpcsvc_auth_t           *auth = NULL;
        int                     minauth = 0;

        if (!req)
                return ret;

        minauth = nfs_rpcsvc_request_prog_minauth (req);
        if (minauth > nfs_rpcsvc_request_cred_flavour (req)) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "Auth too weak");
                nfs_rpcsvc_request_set_autherr (req, AUTH_TOOWEAK);
                goto err;
        }

        auth = nfs_rpcsvc_auth_get_handler (req);
        if (!auth) {
                gf_log (GF_RPCSVC, GF_LOG_DEBUG, "No auth handler found");
                goto err;
        }

        if (auth->authops->authenticate)
                ret = auth->authops->authenticate (req, auth->authprivate);

err:
        return ret;
}


int
nfs_rpcsvc_auth_array (rpcsvc_t *svc, char *volname, int *autharr, int arrlen)
{
        int             count = 0;
        int             gen = RPCSVC_AUTH_REJECT;
        int             spec = RPCSVC_AUTH_REJECT;
        int             final = RPCSVC_AUTH_REJECT;
        char            *srchstr = NULL;
        char            *valstr = NULL;
        gf_boolean_t    boolval = _gf_false;
        int             ret = 0;

        struct rpcsvc_auth_list *auth = NULL;
        struct rpcsvc_auth_list *tmp = NULL;

        if ((!svc) || (!autharr) || (!volname))
                return -1;

        memset (autharr, 0, arrlen * sizeof(int));
        if (list_empty (&svc->authschemes)) {
                gf_log (GF_RPCSVC, GF_LOG_ERROR, "No authentication!");
                goto err;
        }

        list_for_each_entry_safe (auth, tmp, &svc->authschemes, authlist) {
                if (count >= arrlen)
                        break;

                gen = gf_asprintf (&srchstr, "rpc-auth.%s", auth->name);
                if (gen == -1) {
                        count = -1;
                        goto err;
                }

                gen = RPCSVC_AUTH_REJECT;
                if (dict_get (svc->options, srchstr)) {
                        ret = dict_get_str (svc->options, srchstr, &valstr);
                        if (ret == 0) {
                                ret = gf_string2boolean (valstr, &boolval);
                                if (ret == 0) {
                                        if (boolval == _gf_true)
                                                gen = RPCSVC_AUTH_ACCEPT;
                                } else
                                        gf_log (GF_RPCSVC, GF_LOG_ERROR, "Faile"
                                                "d to read auth val");
                        } else
                                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Faile"
                                        "d to read auth val");
                }

                GF_FREE (srchstr);
                spec = gf_asprintf (&srchstr, "rpc-auth.%s.%s", auth->name,
                                    volname);
                if (spec == -1) {
                        count = -1;
                        goto err;
                }

                spec = RPCSVC_AUTH_DONTCARE;
                if (dict_get (svc->options, srchstr)) {
                        ret = dict_get_str (svc->options, srchstr, &valstr);
                        if (ret == 0) {
                                ret = gf_string2boolean (valstr, &boolval);
                                if (ret == 0) {
                                        if (boolval == _gf_true)
                                                spec = RPCSVC_AUTH_ACCEPT;
                                        else
                                                spec = RPCSVC_AUTH_REJECT;
                                } else
                                        gf_log (GF_RPCSVC, GF_LOG_ERROR, "Faile"
                                                "d to read auth val");
                        } else
                                gf_log (GF_RPCSVC, GF_LOG_ERROR, "Faile"
                                        "d to read auth val");
                }

                GF_FREE (srchstr);
                final = nfs_rpcsvc_combine_gen_spec_volume_checks (gen, spec);
                if (final == RPCSVC_AUTH_ACCEPT) {
                        autharr[count] = auth->auth->authnum;
                        ++count;
                }
        }

err:
        return count;
}


gid_t *
nfs_rpcsvc_auth_unix_auxgids (rpcsvc_request_t *req, int *arrlen)
{
        if ((!req) || (!arrlen))
                return NULL;

        if (req->cred.flavour != AUTH_UNIX)
                return NULL;

        *arrlen = req->auxgidcount;
        if (*arrlen == 0)
                return NULL;

        return &req->auxgids[0];
}

