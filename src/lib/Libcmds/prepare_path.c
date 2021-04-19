/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

/**
 * @file	perpare_path.c
 * @brief
 *	Prepare a full path name to give to the server.
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <netdb.h>
#include <sys/stat.h>
#include "cmds.h"
#include "pbs_ifl.h"
#include "net_connect.h"



/**
 * @brief
 *	validate if hname is local host
 *
 * @param[in] hname - host name
 *
 * @return	int
 * @retval	0	success
 * @retval	1	error
 *
 */
int
is_local_host(char *hname)
{
	char hname_full[PBS_MAXSERVERNAME+1];
	char cname_short[PBS_MAXSERVERNAME+1];
	char cname_full[PBS_MAXSERVERNAME+1];

	if (gethostname(cname_short, PBS_MAXSERVERNAME) != 0)
		return (0);
	/*
	 * Compare with "locahost" and "localhost.localdomain".
	 */
#ifdef WIN32
	if (stricmp(hname, cname_short) == 0 ||
		stricmp(hname, LOCALHOST_SHORTNAME) == 0 ||
		stricmp(hname, LOCALHOST_FULLNAME)== 0)
		return (1);
#else
	if (strcmp(hname, cname_short) == 0 ||
		strcmp(hname, LOCALHOST_SHORTNAME) == 0 ||
		strcmp(hname, LOCALHOST_FULLNAME)== 0)
		return (1);
#endif
	if (get_fullhostname(cname_short, cname_full, PBS_MAXSERVERNAME) != 0)
		return (0);

	if (get_fullhostname(hname, hname_full, PBS_MAXSERVERNAME) != 0)
		return (0);

	if (strcmp(hname_full, cname_full) == 0)
		return (1);

	return (0);
}


/**
 * @brief
 *	parses path and prepares complete path name
 *
 * @param[in]      path_in - the path name provided as input to be parsed
 * @param[out]     path_out - contains final parsed and prepared path, must
 *                            be at least MAXPATHLEN+1 bytes.
 *
 * @return int
 * @retval  0 - success in parsing
 * @retval  nonzero - error encountered in parsing
 */
int
prepare_path(char *path_in, char *path_out)
{
	char *c = NULL;
	int have_fqdn = 0;
	/* Initialization with {'\0'} populates entire array */
	char host_name[PBS_MAXSERVERNAME + 1] = {'\0'};	/* short host name */
	int h_pos = 0;
	char path_name[MAXPATHLEN + 1] = {'\0'};
	size_t path_len;
	int p_pos = 0;
	char *host_given = NULL;
	struct stat statbuf = {0};
	dev_t dev = 0;
	ino_t ino = 0;

	if (!path_out)
		return 1;
	*path_out = '\0';
	if (!path_in)
		return 1;

	/* Begin the parse */
	for (c = path_in; *c; c++) {
		if (isspace(*c) == 0)
			break;
	}
	if (*c == '\0')
		return 1;

#ifdef WIN32
	/* Check for drive letter in Windows */
	if (!(isalpha(*c) && (*(c + 1) == ':')))
#endif
	{
		/* Looking for a hostname */
		if ((host_given = strchr(c, ':')) != NULL) {
			/* Capture the hostname portion */
			for (h_pos = 0; (h_pos < sizeof(host_name)); h_pos++, c++) {
				if (isalnum(*c) || (*c == '.') || (*c == '-')
#ifdef WIN32
					/* Underscores are legal in Windows */
					|| (*c == '_')
#endif
					) {
					host_name[h_pos] = *c;
				} else {
					break;
				}
			}
			if (*c != ':')
			{
				if (*c == '/') {
					/* There's a colon in the path */ 
					host_given = NULL;
					host_name[0] = '\0';
					for (c = path_in; *c; c++) {
						if (isspace(*c) == 0)
							break;
					}
				}
				else
					return 1;
			}
			else {
				/* Advance past the colon */
				c++;
			}
		}
	}

	/* Looking for a posix path */
	for (p_pos = 0; p_pos < sizeof(path_name); p_pos++, c++) {
		if (!isprint(*c))
			break;
		path_name[p_pos] = *c;
	}
	/* Should be at end of string */
	if (*c != '\0')
		return 1;

	path_len = strlen(path_name);
	if (path_len == 0 && strlen(host_name) == 0)
		return 1;

	/* appending a slash in the end to indicate that it is a directory */
	if ((path_name[path_len - 1] != '/') &&
	    (path_name[path_len - 1] != '\\') &&
	    (stat(path_name, &statbuf) == 0) &&
            S_ISDIR(statbuf.st_mode)) {
		if ((path_len + 1) < sizeof(path_name)) {
			strcat(path_name, "/");
			path_len++;
		}
	}

#ifdef WIN32
	if (IS_UNCPATH(path_name)) {
		/*
		 * given path is UNC path
		 * so just skip hostname
		 * as UNC path dose not require it
		 */
		host_given = NULL;
		host_name[0] = '\0';
	} else
#endif
	{
		/* get full host name */
		if (host_name[0] == '\0') {
			if (pbs_conf.pbs_output_host_name) {
				/* use the specified host for returning the file */
				snprintf(host_name, sizeof(host_name), "%s", pbs_conf.pbs_output_host_name);
				have_fqdn = 1;
			} else {
				if (gethostname(host_name, sizeof(host_name)) != 0)
					return 2;
				host_name[sizeof(host_name) - 1] = '\0';
			}
		}
		if (have_fqdn == 0) {
			char host_fqdn[PBS_MAXSERVERNAME + 1] = {'\0'};
			/* need to fully qualify the host name */
			if (get_fullhostname(host_name, host_fqdn, PBS_MAXSERVERNAME) != 0)
				return 2;
			strncpy(path_out, host_fqdn, MAXPATHLEN); /* FQ host name */
		} else {
			strncpy(path_out, host_name, MAXPATHLEN); /* "localhost" or pbs_output_host_name */
		}
		path_out[MAXPATHLEN - 1] = '\0';

		/* finish preparing complete host name */
		if (strlen(path_out) < MAXPATHLEN)
			strcat(path_out, ":");
	}

#ifdef WIN32
	if (path_name[0] != '/' && path_name[0] != '\\' &&
		host_given == NULL && strchr(path_name, ':') == NULL )
#else
	if (path_name[0] != '/' && host_given == NULL)
#endif
	{
		char cwd[MAXPATHLEN + 1] = {'\0'};

		c = getenv("PWD");		/* PWD carries a name that will cause */
		if (c != NULL) {		/* the NFS to mount */

			if (stat(c, &statbuf) < 0) {	/* can't stat PWD */
				c = NULL;
			} else {
				dev = statbuf.st_dev;
				ino = statbuf.st_ino;
				if (stat(".", &statbuf) < 0) {
					perror("prepare_path: cannot stat current directory:");
					*path_out = '\0';
					return (1);
				}
			}
			if (dev == statbuf.st_dev && ino == statbuf.st_ino) {
				snprintf(cwd, sizeof(cwd), "%s", c);
			} else {
				c = NULL;
			}
		}
		if (c == NULL) {
			c = getcwd(cwd, MAXPATHLEN);
			if (c == NULL) {
				perror("prepare_path: getcwd failed : ");
				*path_out = '\0';
				return (1);
			}
		}
#ifdef WIN32
		/* get UNC path (if available) if it is mapped drive */
		get_uncpath(cwd);
		if (IS_UNCPATH(cwd)) {
			strcpy(path_out, cwd);
			if (cwd[strlen(cwd)-1] != '\\')
				strcat(path_out, "\\");
		} else
#endif
		{
			strncat(path_out, cwd, (MAXPATHLEN + 1) - strlen(path_out));
			if (strlen(path_out) < MAXPATHLEN)
				strcat(path_out, "/");
		}
	}


#ifdef WIN32
	/* get UNC path (if available) if it is mapped drive */
	get_uncpath(path_name);
	if (IS_UNCPATH(path_name))
		strcpy(path_out, path_name);
	else {
		/*
		 * check whether given <path_name> is relative path
		 * without drive on localhost and <cwd> is not UNC path?
		 * if yes then do not append drive into <path_out>
		 * otherwise append drive into <path_out>
		 */
		if (is_local_host(host_name) &&
			(strchr(path_name, ':') == NULL) &&
			(path_out[strlen(path_out) - 1] != '/') &&
			(!IS_UNCPATH(path_out))) {

			char drivestr[3] = {'\0'};
			char drivestr_unc[MAXPATHLEN + 1] = {'\0'};

			drivestr[0] = _getdrive() + 'A' - 1;
			drivestr[1] = ':';
			drivestr[2] = '\0';
			/*
			 * check whether <drivestr> is mapped drive?
			 * by calling get_uncpath()
			 * if yes then remove <hostname> part from <path_out>
			 *
			 * This is the case when user submit job
			 * from mapped drive with relative path without drive
			 * in path ex. localhost:err or localhost:out
			 */
			snprintf(drivestr_unc, sizeof(drivestr_unc), "%s\\", drivestr);
			get_uncpath(drivestr_unc);
			if (IS_UNCPATH(drivestr_unc)) {
				strncpy(path_out, drivestr_unc, MAXPATHLEN);
			} else {
				strncat(path_out, drivestr, MAXPATHLEN - strlen(path_out));
			}
		}
		strncat(path_out, path_name, MAXPATHLEN - strlen(path_out));
	}
	fix_path(path_out, 1);
	strcpy(path_out, replace_space(path_out, "\\ "));
	path_out[MAXPATHLEN - 1] = '\0';
#else
	strncat(path_out, path_name, (MAXPATHLEN + 1) - strlen(path_out));
#endif

	return (0);
}
