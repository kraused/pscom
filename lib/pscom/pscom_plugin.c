/*
 * ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author:	Jens Hauke <hauke@par-tec.com>
 */

#include "pscom_priv.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "pscom_env.h"

LIST_HEAD(pscom_plugins);

static
char *strtoupper(char *name)
{
	while (*name) {
		*name = toupper(*name);
		name++;
	}
	return name;
}


static
unsigned int pscom_plugin_uprio(const char *arch)
{
	char env_name[100];
	unsigned res;
#define ENV_EX_UNSET ((unsigned)~0U)
	static int env_extoll_initialized = 0;
	static unsigned env_extoll;
	static unsigned env_velo;

	strcpy(env_name, ENV_ARCH_PREFIX);
	strcat(env_name, arch);
	strtoupper(env_name);

	res = 1;
	if (strcmp(arch, "elan") == 0 ||
	    strcmp(arch, "ofed") == 0) {
		/* default of ELAN is 'off'. mpiexec will switch
		   it on, after setting up the elan environment.*/
		/* ToDo: Check for ELAN environment variables inside
		   elan plugin! And remove this if. */
		/* default for ofed is 'off'. Until ofed support
		   resends for lost messages. */
		res = 0;
	}
	if ((strcmp(env_name, ENV_ARCH_NEW_SHM) == 0) &&
	    !getenv(ENV_ARCH_NEW_SHM) && getenv(ENV_ARCH_OLD_SHM)) {
		/* old style shm var */
		pscom_env_get_uint(&res, ENV_ARCH_OLD_SHM);
	} else if ((strcmp(env_name, ENV_ARCH_NEW_P4S) == 0) &&
		   !getenv(ENV_ARCH_NEW_P4S) && getenv(ENV_ARCH_OLD_P4S)) {
		/* old style p4s var */
		pscom_env_get_uint(&res, ENV_ARCH_OLD_P4S);
	} else if ((strcmp(env_name, ENV_ARCH_PREFIX "EXTOLL") == 0) ||
		   (strcmp(env_name, ENV_ARCH_PREFIX "VELO") == 0)) {
		/* Extoll rma or velo? */
		if (!env_extoll_initialized) {
			env_extoll_initialized = 1;

			env_velo = ENV_UINT_AUTO;
			pscom_env_get_uint(&env_velo, ENV_ARCH_PREFIX "VELO");

			env_extoll = (env_velo == 0 || env_velo == ENV_UINT_AUTO) ? 1 : 0;
			pscom_env_get_uint(&env_extoll, ENV_ARCH_PREFIX "EXTOLL");

			if (env_velo == ENV_UINT_AUTO) {
				env_velo = env_extoll ? 0 : 1;
			}
			if (env_extoll && env_velo) {
				DPRINT(1, "'" ENV_ARCH_PREFIX "VELO' and '"
				       ENV_ARCH_PREFIX "EXTOLL' are mutually exclusive! Disabling '"
				       ENV_ARCH_PREFIX "VELO'");
				env_velo = 0;
			}
		}
		if ((strcmp(env_name, ENV_ARCH_PREFIX "EXTOLL") == 0)) {
			res = env_extoll;
		} else {
			res = env_velo;
		}
	} else {
		pscom_env_get_uint(&res, env_name);
	}
	return res;
}


static
void pscom_plugin_register(pscom_plugin_t *plugin, unsigned int user_prio)
{
	if (!user_prio) {
		DPRINT(2, "Arch %s is disabled", plugin->name);
		return; // disabled arch
	}
	plugin->user_prio = user_prio;

	if (pscom_plugin_by_name(plugin->name)) {
		DPRINT(2, "Arch %s already registered", plugin->name);
		return; // disabled arch
	}

	pscom_plugin_t *tmpp = pscom_plugin_by_archid(plugin->arch_id);
	if (tmpp) {
		DPRINT(2, "Arch id %d already registered (registered:%s, disabled:%s)",
		       plugin->arch_id, tmpp->name, plugin->name);
		return; // disabled arch
	}


	DPRINT(2, "Register arch %s with priority %02d.%02d",
	       plugin->name, plugin->user_prio, plugin->priority);

	struct list_head *pos, *inc;
	inc = &pscom_plugins;
	list_for_each(pos, &pscom_plugins) {
		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);

		if ((p->user_prio < plugin->user_prio) ||
		    ((p->user_prio == plugin->user_prio) &&
		     (p->priority < plugin->priority))) {
			inc = pos;
			break;
		}
	}

	list_add_tail(&plugin->next, inc);

	// Debug:
//	list_for_each(pos, &pscom_plugins) {
//		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);
//		printf("%02d.%02d %s\n", p->user_prio, p->priority, p->name);
//	}
}


static
pscom_plugin_t *load_plugin_lib(char *lib)
{
	void *libh;
	char *errstr;

	libh = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);

	if (libh) {
		pscom_plugin_t *plugin = dlsym(libh, "pscom_plugin");

		if (plugin) {
			if (plugin->version == PSCOM_PLUGIN_VERSION) {
				DPRINT(2, "Using   %s", lib);
				// OK
				return plugin;
			} else {
				// Error
				DPRINT(1,
				       "Loading %s failed : Version mismatch (0x%04x != expected 0x%04x)",
				       lib, plugin->version, PSCOM_PLUGIN_VERSION);
			}
		} else {
			// Error
			DPRINT(1, "Loading %s failed : No symbol 'pscom_plugin'", lib);
		}
		// all errors
		dlclose(libh);

		return NULL;
	}

	errstr = dlerror();
	DPRINT(3, "Loading %s failed : %s", lib, errstr ? errstr : "unknown error");

	return NULL;
}


static
void pscom_plugin_load(const char *arch)
{
	unsigned int uprio = pscom_plugin_uprio(arch);
	if (!uprio) {
		DPRINT(2, "Arch %s is disabled", arch);
		return; // disabled arch
	}

	char *libdirs[] = {
		"",
		LIBDIR,
		NULL
	};
	unsigned cnt = 0;
	libdirs[0] = pscom.env.plugindir; // "" or environ

	char **ld_p;
	for (ld_p = libdirs; *ld_p; ld_p++) {
		char libpath[400];
		pscom_plugin_t *plugin;
		struct stat statbuf;

		snprintf(libpath, sizeof(libpath), "%slibpscom4%s.so",
			 *ld_p, arch);

		if ((*ld_p)[0] && stat(libpath, &statbuf) && errno == ENOENT) {
			continue;
		}
		cnt++;
		plugin = load_plugin_lib(libpath);

		if (plugin) {
			assert(strcmp(arch, plugin->name) == 0);

			pscom_plugin_register(plugin, uprio);
			break;
		}
	}
	if (!cnt) DPRINT(3, "libpscom4%s.so not available", arch);
}


pscom_plugin_t *pscom_plugin_by_archid(unsigned int arch_id)
{
	struct list_head *pos;

	list_for_each(pos, &pscom_plugins) {
		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);
		if (p->arch_id == arch_id) return p;
	}
	return NULL;
}


pscom_plugin_t *pscom_plugin_by_name(const char *arch)
{
	struct list_head *pos;

	list_for_each(pos, &pscom_plugins) {
		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);
		if (strcmp(arch, p->name) == 0) return p;
	}
	return NULL;
}


static
int plugins_loaded = 0;

void pscom_plugins_destroy(void)
{
	if (!plugins_loaded) return;
	plugins_loaded = 0;

	while (!list_empty(&pscom_plugins)) {
		pscom_plugin_t *p = list_entry(pscom_plugins.next, pscom_plugin_t, next);
		if (p->destroy) p->destroy();
		list_del(&p->next);
	}
}


void pscom_plugins_init(void)
{
	if (plugins_loaded) return;
	plugins_loaded = 1;

	pscom_plugin_register(&pscom_plugin_tcp, pscom_plugin_uprio("tcp"));
	pscom_plugin_register(&pscom_plugin_shm, pscom_plugin_uprio("shm"));
	pscom_plugin_register(&pscom_plugin_p4s, pscom_plugin_uprio("p4s"));

	// ToDo: Use file globbing!
	char *pls[] = {
		"psm",
		"openib",
		"ofed",
		"mvapi",
		"gm",
		"elan",
		"extoll",
		"velo",
		"dapl",
		NULL };
	char **tmp;

	for (tmp = pls; *tmp; tmp++) {
		pscom_plugin_load(*tmp);
	}

	struct list_head *pos;
	list_for_each(pos, &pscom_plugins) {
		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);
		if (p->init) p->init();
	}
}


void pscom_plugins_sock_init(pscom_sock_t *sock)
{
	pscom_plugins_init();

	struct list_head *pos;
	list_for_each(pos, &pscom_plugins) {
		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);
		if (p->sock_init) p->sock_init(sock);
	}
}


void pscom_plugins_sock_destroy(pscom_sock_t *sock)
{
	struct list_head *pos;
	list_for_each(pos, &pscom_plugins) {
		pscom_plugin_t *p = list_entry(pos, pscom_plugin_t, next);
		if (p->sock_destroy) p->sock_destroy(sock);
	}
}
