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


%token VOLUME_BEGIN VOLUME_END OPTION NEWLINE SUBVOLUME ID WHITESPACE COMMENT TYPE STRING_TOK

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "xlator.h"
#include "graph-utils.h"
#include "logging.h"

static int new_volume (char *name);
static int volume_type (char *type);
static int volume_option (char *key, char *value);
static int volume_sub (char *sub);
static int volume_end (void);
static void sub_error (void);
static void type_error (void);
static void option_error (void);

#define YYSTYPE char *
#define GF_CMD_BUFFER_LEN (8 * GF_UNIT_KB)

int yyerror (const char *);
int yylex ();
%}


%%
VOLUMES: VOLUME | VOLUMES VOLUME;

VOLUME: VOLUME_HEADER VOLUME_DATA VOLUME_FOOTER;
VOLUME_HEADER: VOLUME_BEGIN WORD {if (new_volume ($2) == -1) { YYABORT; }};
VOLUME_FOOTER: VOLUME_END {if (volume_end () == -1) { YYABORT; }};

VOLUME_DATA: TYPE_LINE OPTIONS_LINE SUBVOLUME_LINE OPTIONS_LINE |
              TYPE_LINE SUBVOLUME_LINE OPTIONS_LINE |
              TYPE_LINE OPTIONS_LINE SUBVOLUME_LINE |
              TYPE_LINE SUBVOLUME_LINE |
              TYPE_LINE OPTIONS_LINE |
              OPTIONS_LINE SUBVOLUME_LINE OPTIONS_LINE | /* error case */
              OPTIONS_LINE;  /* error case */

TYPE_LINE: TYPE WORD {if (volume_type ($2) == -1) { YYABORT; }} | TYPE { type_error(); YYABORT; };

SUBVOLUME_LINE: SUBVOLUME WORDS | SUBVOLUME { sub_error (); YYABORT; };

OPTIONS_LINE: OPTION_LINE | OPTIONS_LINE OPTION_LINE;

OPTION_LINE: OPTION WORD WORD {if (volume_option ($2, $3) == -1) { YYABORT; }} |
	     OPTION WORD { option_error (); YYABORT; } |
	     OPTION { option_error (); YYABORT; };

WORDS: WORD {if (volume_sub ($1) == -1) {YYABORT; }} | WORDS WORD { if (volume_sub ($2) == -1) { YYABORT; }};
WORD: ID | STRING_TOK ;
%%

xlator_t *curr;
glusterfs_graph_t *construct;


static void
type_error (void)
{
        extern int yylineno;

        gf_log ("parser", GF_LOG_ERROR,
                "Volume %s, before line %d: Please specify volume type",
                curr->name, yylineno);
        return;
}


static void
sub_error (void)
{
        extern int yylineno;

        gf_log ("parser", GF_LOG_ERROR,
                "Volume %s, before line %d: Please specify subvolumes",
                curr->name, yylineno);
        return;
}


static void
option_error (void)
{
        extern int yylineno;

        gf_log ("parser", GF_LOG_ERROR,
                "Volume %s, before line %d: Please specify "
                "option <key> <value>",
                curr->name, yylineno);
        return;
}


static int
new_volume (char *name)
{
        extern int   yylineno;
        xlator_t    *trav = NULL;
        int          ret = 0;

        if (!name) {
                gf_log ("parser", GF_LOG_DEBUG,
			"Invalid argument name: '%s'", name);
                ret = -1;
                goto out;
        }

        if (curr) {
                gf_log ("parser", GF_LOG_ERROR,
                        "new volume (%s) defintion in line %d unexpected",
                        name, yylineno);
                ret = -1;
                goto out;
        }

        curr = (void *) GF_CALLOC (1, sizeof (*curr),
                                   gf_common_mt_xlator_t);

        if (!curr) {
                gf_log ("parser", GF_LOG_ERROR, "Out of memory");
                ret = -1;
                goto out;
        }

        trav = construct->first;

        while (trav) {
                if (!strcmp (name, trav->name)) {
                        gf_log ("parser", GF_LOG_ERROR,
				"Line %d: volume '%s' defined again",
                                yylineno, name);
                        ret = -1;
                        goto out;
                }
                trav = trav->next;
        }

        curr->name = gf_strdup (name);
        if (!curr->name) {
                GF_FREE (curr);
                ret = -1;
                goto out;
        }

        curr->options = get_new_dict ();

        if (!curr->options) {
                GF_FREE (curr->name);
                GF_FREE (curr);
                ret = -1;
                goto out;
        }

        curr->next = construct->first;
        if (curr->next)
                curr->next->prev = curr;

        curr->graph = construct;

        construct->first = curr;

        construct->xl_count++;

        gf_log ("parser", GF_LOG_TRACE, "New node for '%s'", name);

out:
        GF_FREE (name);

        return ret;
}


static int
volume_type (char *type)
{
        extern int   yylineno;
        int32_t      ret = 0;

        if (!type) {
                gf_log ("parser", GF_LOG_DEBUG, "Invalid argument type");
                ret = -1;
                goto out;
        }

        ret = xlator_set_type (curr, type);
        if (ret) {
                gf_log ("parser", GF_LOG_ERROR,
                        "Volume '%s', line %d: type '%s' is not valid or "
			"not found on this machine",
                        curr->name, yylineno, type);
                ret = -1;
                goto out;
        }

        gf_log ("parser", GF_LOG_TRACE, "Type:%s:%s", curr->name, type);

out:
        GF_FREE (type);

        return 0;
}


static int
volume_option (char *key, char *value)
{
        extern int  yylineno;
        int         ret = 0;
        char       *set_value = NULL;

        if (!key || !value){
                gf_log ("parser", GF_LOG_ERROR, "Invalid argument");
                ret = -1;
                goto out;
        }

        set_value = gf_strdup (value);
	ret = dict_set_dynstr (curr->options, key, set_value);

        if (ret == 1) {
                gf_log ("parser", GF_LOG_ERROR,
                        "Volume '%s', line %d: duplicate entry "
			"('option %s') present",
                        curr->name, yylineno, key);
                ret = -1;
                goto out;
        }

        gf_log ("parser", GF_LOG_TRACE, "Option:%s:%s:%s",
                curr->name, key, value);

out:
        GF_FREE (key);
        GF_FREE (value);

        return 0;
}


static int
volume_sub (char *sub)
{
        extern int       yylineno;
        xlator_t        *trav = NULL;
        int              ret = 0;

        if (!sub) {
                gf_log ("parser", GF_LOG_ERROR, "Invalid subvolumes argument");
                ret = -1;
                goto out;
        }

        trav = construct->first;

        while (trav) {
                if (!strcmp (sub,  trav->name))
                        break;
                trav = trav->next;
        }

        if (!trav) {
                gf_log ("parser", GF_LOG_ERROR,
                        "Volume '%s', line %d: subvolume '%s' is not defined "
			"prior to usage",
                        curr->name, yylineno, sub);
                ret = -1;
                goto out;
        }

        if (trav == curr) {
                gf_log ("parser", GF_LOG_ERROR,
                        "Volume '%s', line %d: has '%s' itself as subvolume",
                        curr->name, yylineno, sub);
                ret = -1;
                goto out;
        }

	ret = glusterfs_xlator_link (curr, trav);
	if (ret) {
                gf_log ("parser", GF_LOG_ERROR, "Out of memory");
                ret = -1;
                goto out;
        }

        gf_log ("parser", GF_LOG_TRACE, "child:%s->%s", curr->name, sub);

out:
        GF_FREE (sub);

        return 0;
}


static int
volume_end (void)
{
        if (!curr->fops) {
                gf_log ("parser", GF_LOG_ERROR,
                        "\"type\" not specified for volume %s", curr->name);
                return -1;
        }
        gf_log ("parser", GF_LOG_TRACE, "end:%s", curr->name);

        curr = NULL;
        return 0;
}


int
yywrap ()
{
        return 1;
}


int
yyerror (const char *str)
{
        extern char  *yytext;
        extern int    yylineno;

        if (curr && curr->name && yytext) {
                if (!strcmp (yytext, "volume")) {
                        gf_log ("parser", GF_LOG_ERROR,
                                "'end-volume' not defined for volume '%s'",
				curr->name);
                } else if (!strcmp (yytext, "type")) {
                        gf_log ("parser", GF_LOG_ERROR,
                                "line %d: duplicate 'type' defined for "
				"volume '%s'",
                                yylineno, curr->name);
                } else if (!strcmp (yytext, "subvolumes")) {
                        gf_log ("parser", GF_LOG_ERROR,
                                "line %d: duplicate 'subvolumes' defined for "
				"volume '%s'",
                                yylineno, curr->name);
                } else if (curr) {
                        gf_log ("parser", GF_LOG_ERROR,
                                "syntax error: line %d (volume '%s'): \"%s\""
				"\nallowed tokens are 'volume', 'type', "
				"'subvolumes', 'option', 'end-volume'()",
                                yylineno, curr->name,
				yytext);
                } else {
                        gf_log ("parser", GF_LOG_ERROR,
                                "syntax error: line %d (just after volume "
				"'%s'): \"%s\"\n(%s)",
                                yylineno, curr->name,
				yytext,
                                "allowed tokens are 'volume', 'type', "
				"'subvolumes', 'option', 'end-volume'");
                }
        } else {
                gf_log ("parser", GF_LOG_ERROR,
                        "syntax error in line %d: \"%s\" \n"
                        "(allowed tokens are 'volume', 'type', "
			"'subvolumes', 'option', 'end-volume')\n",
                        yylineno, yytext);
        }

        return -1;
}


static int
execute_cmd (char *cmd, char **result, size_t size)
{
	FILE       *fpp = NULL;
	int         i = 0;
        int         status = 0;
	int         character = 0;
	char       *buf = *result;

	fpp = popen (cmd, "r");
	if (!fpp) {
		gf_log ("parser", GF_LOG_ERROR, "%s: failed to popen", cmd);
		return -1;
	}

	while ((character = fgetc (fpp)) != EOF) {
		if (i == size) {
			size *= 2;
			buf = *result = GF_REALLOC (*result, size);
                }

		buf[i++] = character;
	}

	if (i > 0) {
		i--;
		buf[i] = '\0';
	}

	status = pclose (fpp);
	if (status == -1 || !WIFEXITED (status) ||
	    ((WEXITSTATUS (status)) != 0)) {
		i = -1;
		buf[0] = '\0';
	}

	return i;
}


static int
preprocess (FILE *srcfp, FILE *dstfp)
{
	int     ret = 0;
        int     i = 0;
	char   *cmd = NULL;
        char   *result = NULL;
	size_t  cmd_buf_size = GF_CMD_BUFFER_LEN;
	char    escaped = 0;
        char    in_backtick = 0;
	int     line = 1;
        int     column = 0;
        int     backtick_line = 0;
        int     backtick_column = 0;
        int     character = 0;


	fseek (srcfp, 0L, SEEK_SET);
	fseek (dstfp, 0L, SEEK_SET);

	cmd = GF_CALLOC (cmd_buf_size, 1,
                         gf_common_mt_char);
        if (cmd == NULL) {
                gf_log ("parser", GF_LOG_ERROR, "Out of memory");
                return -1;
        }

	result = GF_CALLOC (cmd_buf_size * 2, 1,
                            gf_common_mt_char);
        if (result == NULL) {
                GF_FREE (cmd);
                gf_log ("parser", GF_LOG_ERROR, "Out of memory");
                return -1;
        }

	while ((character = fgetc (srcfp)) != EOF) {
		if ((character == '`') && !escaped) {
			if (in_backtick) {
				cmd[i] = '\0';
				result[0] = '\0';

				ret = execute_cmd (cmd, &result,
                                                   2 * cmd_buf_size);
				if (ret < 0) {
					ret = -1;
					goto out;
				}
				fwrite (result, ret, 1, dstfp);
			} else {
				i = 0;
				cmd[i] = '\0';

				backtick_column = column;
				backtick_line = line;
			}

			in_backtick = !in_backtick;
		} else {
			if (in_backtick) {
				if (i == cmd_buf_size) {
					cmd_buf_size *= 2;
					cmd = GF_REALLOC (cmd, cmd_buf_size);
                                        if (cmd == NULL) {
                                                return -1;
                                        }

					result = GF_REALLOC (result,
                                                             2 * cmd_buf_size);
                                        if (result == NULL) {
                                                GF_FREE (cmd);
                                                return -1;
                                        }
                                }

				cmd[i++] = character;
                        } else {
				fputc (character, dstfp);
                        }
                }

		if (character == '\\') {
			escaped = !escaped;
		} else {
			escaped = 0;
                }

		if (character == '\n') {
			line++;
			column = 0;
		} else {
			column++;
		}
        }

	if (in_backtick) {
		gf_log ("parser", GF_LOG_ERROR,
			"Unterminated backtick in volume specfication file at line (%d), column (%d).",
			line, column);
                ret = -1;
	}

out:
	fseek (srcfp, 0L, SEEK_SET);
	fseek (dstfp, 0L, SEEK_SET);
	GF_FREE (cmd);
	GF_FREE (result);

	return ret;
}


extern FILE *yyin;

glusterfs_graph_t *
glusterfs_graph_new ()
{
        glusterfs_graph_t *graph = NULL;

        graph = GF_CALLOC (1, sizeof (*graph),
                           gf_common_mt_glusterfs_graph_t);
        if (!graph)
                return NULL;

        INIT_LIST_HEAD (&graph->list);

        gettimeofday (&graph->dob, NULL);

        return graph;
}


glusterfs_graph_t *
glusterfs_graph_construct (FILE *fp)
{
        int                ret = 0;
        glusterfs_graph_t *graph = NULL;
	FILE              *tmp_file = NULL;

        graph = glusterfs_graph_new ();
        if (!graph)
                return NULL;

	tmp_file = tmpfile ();

	if (tmp_file == NULL) {
		gf_log ("parser", GF_LOG_ERROR,
			"cannot create temparory file");

                glusterfs_graph_destroy (graph);
		return NULL;
	}

	ret = preprocess (fp, tmp_file);
	if (ret < 0) {
		gf_log ("parser", GF_LOG_ERROR,
			"parsing of backticks failed");

                glusterfs_graph_destroy (graph);
		fclose (tmp_file);
		return NULL;
	}

        yyin = tmp_file;

        construct = graph;

        ret = yyparse ();

        construct = NULL;

	fclose (tmp_file);

        if (ret == 1) {
                gf_log ("parser", GF_LOG_DEBUG,
			"parsing of volfile failed, please review it "
			"once more");

                glusterfs_graph_destroy (graph);
                return NULL;
        }

        return graph;
}

