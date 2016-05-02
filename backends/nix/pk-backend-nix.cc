/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Matthew Bauer <mjbauer95@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed i3n the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <sstream>

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>

#include <pk-backend.h>
#include <pk-backend-job.h>

#include <nix/nixexpr.hh>
#include <nix/shared.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>
#include <nix/derivations.hh>
#include <nix/store-api.hh>
#include <nix/get-drvs.hh>
#include <nix/names.hh>
#include <nix/profiles.hh>
#include <nix/globals.hh>
#include <nix/common-opts.hh>

using namespace nix;

typedef struct {
	std::shared_ptr<EvalState> state;
	Settings settings;
	DrvInfos drvs;
} PkBackendNixPrivate;

typedef struct {
} PkBackendNixJobData;

static PkBackendNixPrivate* priv;

static Path
nix_get_home_dir ()
{
  Path homeDir (getEnv("HOME", ""));
  if (homeDir == "") throw Error("HOME environment variable not set");
  return homeDir;
}

// #include <nix/user-env.hh>
// from user-env.hh
DrvInfos nix_query_installed(EvalState & state, const Path & userEnv)
{
  DrvInfos elems;
  Path manifestFile = userEnv + "/manifest.nix";
  if (pathExists(manifestFile)) {
    Value v;
    state.evalFile(manifestFile, v);
    Bindings & bindings(*state.allocBindings(0));
    getDerivations(state, v, "", bindings, elems, false);
  }
  return elems;
}

bool nix_create_user_env(EvalState & state, DrvInfos & elems,
												 const Path & profile, bool keepDerivations,
												 const string & lockToken)
{
  /* Build the components in the user environment, if they don't
     exist already. */
  PathSet drvsToBuild;
  for (auto & i : elems)
    if (i.queryDrvPath() != "")
      drvsToBuild.insert(i.queryDrvPath());

  state.store->buildPaths(drvsToBuild, state.repair ? bmRepair : bmNormal);

  /* Construct the whole top level derivation. */
  PathSet references;
  Value manifest;
  state.mkList(manifest, elems.size());
  unsigned int n = 0;
  for (auto & i : elems) {
  	/* Create a pseudo-derivation containing the name, system,
     	output paths, and optionally the derivation path, as well
     	as the meta attributes. */
  	Path drvPath = keepDerivations ? i.queryDrvPath() : "";

    Value & v(*state.allocValue());
    manifest.listElems()[n++] = &v;
    state.mkAttrs(v, 16);

    mkString(*state.allocAttr(v, state.sType), "derivation");
    mkString(*state.allocAttr(v, state.sName), i.name);
    if (!i.system.empty())
      mkString(*state.allocAttr(v, state.sSystem), i.system);
    mkString(*state.allocAttr(v, state.sOutPath), i.queryOutPath());
    if (drvPath != "")
      mkString(*state.allocAttr(v, state.sDrvPath), i.queryDrvPath());

  	// Copy each output meant for installation.
  	DrvInfo::Outputs outputs = i.queryOutputs();
  	Value & vOutputs = *state.allocAttr(v, state.sOutputs);
  	state.mkList(vOutputs, outputs.size());
  	unsigned int m = 0;
  	for (auto & j : outputs) {
      mkString(*(vOutputs.listElems()[m++] = state.allocValue()), j.first);
      Value & vOutputs = *state.allocAttr(v, state.symbols.create(j.first));
      state.mkAttrs(vOutputs, 2);
      mkString(*state.allocAttr(vOutputs, state.sOutPath), j.second);

      /* This is only necessary when installing store paths, e.g.,
         `nix-env -i /nix/store/abcd...-foo'. */
      state.store->addTempRoot(j.second);
      state.store->ensurePath(j.second);

      references.insert(j.second);
    }

    // Copy the meta attributes.
    Value & vMeta = *state.allocAttr(v, state.sMeta);
    state.mkAttrs(vMeta, 16);
    StringSet metaNames = i.queryMetaNames();
    for (auto & j : metaNames) {
      Value * v = i.queryMeta(j);
      if (!v) continue;
      vMeta.attrs->push_back(Attr(state.symbols.create(j), v));
    }
    vMeta.attrs->sort();
    v.attrs->sort();

    if (drvPath != "") references.insert(drvPath);
  }

  /* Also write a copy of the list of user environment elements to
     the store; we need it for future modifications of the
     environment. */
  Path manifestFile = state.store->addTextToStore("env-manifest.nix",
      (format("%1%") % manifest).str(), references);

  /* Get the environment builder expression. */
  Value envBuilder;
  state.evalFile(state.findFile("nix/buildenv.nix"), envBuilder);

  /* Construct a Nix expression that calls the user environment
     builder with the manifest as argument. */
  Value args, topLevel;
  state.mkAttrs(args, 3);
  mkString(*state.allocAttr(args, state.symbols.create("manifest")),
      manifestFile, singleton<PathSet>(manifestFile));
  args.attrs->push_back(Attr(state.symbols.create("derivations"), &manifest));
  args.attrs->sort();
  mkApp(topLevel, envBuilder, args);

  /* Evaluate it. */
  state.forceValue(topLevel);
  PathSet context;
  Attr & aDrvPath(*topLevel.attrs->find(state.sDrvPath));
  Path topLevelDrv = state.coerceToPath(aDrvPath.pos ? *(aDrvPath.pos) : noPos, *(aDrvPath.value), context);
  Attr & aOutPath(*topLevel.attrs->find(state.sOutPath));
  Path topLevelOut = state.coerceToPath(aOutPath.pos ? *(aOutPath.pos) : noPos, *(aOutPath.value), context);

  /* Realise the resulting store expression. */
  state.store->buildPaths(singleton<PathSet>(topLevelDrv), state.repair ? bmRepair : bmNormal);

  /* Switch the current user environment to the output path. */
  PathLocks lock;
  lockProfile(lock, profile);

  Path lockTokenCur = optimisticLockProfile(profile);
  if (lockToken != lockTokenCur) {
    return false;
  }

  Path generation = createGeneration(state.store, profile, topLevelOut);
  switchLink(profile, generation);

  return true;
}

static Path
nix_get_def_expr_path ()
{
  return nix_get_home_dir() + "/.nix-defexpr";
}

static PathSet
nix_get_paths (PkBackendNixPrivate* priv, gchar** package_ids)
{
	PathSet paths;

	for (; *package_ids != NULL; package_ids++)
	{
		Path path = *package_ids;
		if (!priv->state->store->isValidPath(path))
			continue;
		paths.insert(path);
	}

	return paths;
}

static Path
nix_get_profile ()
{
	Path profileLink = nix_get_home_dir() + "/.nix-profile";
	return pathExists(profileLink)
		? absPath(readLink(profileLink), dirOf(profileLink))
		: canonPath(settings.nixStateDir + "/profiles/default");
}

static DrvInfos
nix_get_drvs (PkBackendNixPrivate priv, PkBitfield filters)
{
	auto installedDrvs = nix_query_installed(*priv.state, nix_get_profile());
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		return installedDrvs;

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED));
		// TODO

	return priv.drvs;
}

static bool
nix_filter_drv (DrvInfo drv, PkBackendNixPrivate* priv, PkBitfield filters) {
	if (drv.hasFailed())
	{
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_VISIBLE))
			return FALSE;
	} else {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_VISIBLE))
			return FALSE;
	}

	if (drv.system == priv->settings.thisSystem)
	{
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH))
			return FALSE;
	} else {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH))
			return FALSE;
	}

	auto platforms = drv.queryMeta("platforms");
	if (platforms->isList())
	{
		bool hasPlatform = FALSE;
		for (auto i = platforms->listElems();
				 i != platforms->listElems() + platforms->listSize();
				 i++) {
			if ((*i)->type == tString && (*i)->string.s == priv->settings.thisSystem)
			{
				hasPlatform = FALSE;
				break;
			}
    }
		if (hasPlatform)
		{
			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SUPPORTED))
				return FALSE;
		} else {
			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED))
				return FALSE;
		}
	}

	if (priv->state->store->isValidPath (drv.queryOutPath()))
	{
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DOWNLOADED))
			return FALSE;
	} else {
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DOWNLOADED))
			return FALSE;
	}

	// auto license = drv.queryMeta("license");
	// if (license.type == tAttrs)
	// {
	// 	Bindings::iterator _free = license.find(priv->state->symbols.create("free"));
	// 	if (_free != license.end() && _free->type == tBool) {
	// 		if (_free->boolean)
	// 		{
	// 			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE))
	// 				return FALSE;
	// 		} else {
	// 			if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE))
	// 				return FALSE;
	// 		}
	// 	}
	// }

	return TRUE;
}

static DrvInfo
nix_lookup_drv (PkBackendNixPrivate* priv, Path package_id)
{
	for (auto d : priv->drvs)
		if (d.queryOutPath() == package_id)
			return d;

	return DrvInfo(*(priv->state));
}

static DrvInfos
nix_lookup_drvs (PkBackendNixPrivate* priv, PathSet paths)
{
	DrvInfos drvs;

	for (auto path : paths)
	{
		DrvInfo drv = nix_lookup_drv(priv, path);
		drvs.push_back(drv);
	}

	return drvs;
}

void
pk_backend_initialize (GKeyFile* conf, PkBackend* backend)
{
	PkBackendNixPrivate priv;

	pk_backend_set_user_data (backend, &priv);

	initNix();
	initGC();

	priv.settings = settings;

	auto store = openStore();
	Strings searchPath;
	priv.state = std::shared_ptr<EvalState>(new EvalState(searchPath, store));

	std::map<string, string> autoArgs_;
	Bindings & autoArgs(*evalAutoArgs(*priv.state, autoArgs_));

	Value v;
	priv.state->evalFile(nix_get_def_expr_path(), v);
	getDerivations(*priv.state, v, "", autoArgs, priv.drvs, true);
}

void
pk_backend_destroy (PkBackend* backend)
{
	g_free (priv);
}

gboolean
pk_backend_supports_parallelization (PkBackend* backend)
{
	return TRUE;
}

const gchar *
pk_backend_get_description (PkBackend* backend)
{
	return "Nix";
}

const gchar *
pk_backend_get_author (PkBackend* backend)
{
	return "Matthew Bauer <mjbauer95@gmail.com>";
}

// TODO
PkBitfield
pk_backend_get_groups (PkBackend* backend)
{
	return pk_bitfield_from_enums (-1);
}

PkBitfield
pk_backend_get_filters (PkBackend* backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_NOT_INSTALLED,
		PK_FILTER_ENUM_FREE,
		PK_FILTER_ENUM_NOT_FREE,
		PK_FILTER_ENUM_VISIBLE,
		PK_FILTER_ENUM_NOT_VISIBLE,
		PK_FILTER_ENUM_SUPPORTED,
		PK_FILTER_ENUM_NOT_SUPPORTED,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_NOT_ARCH,
		PK_FILTER_ENUM_DOWNLOADED,
		PK_FILTER_ENUM_NOT_DOWNLOADED,
		-1);
}

PkBitfield
pk_backend_get_roles (PkBackend* backend)
{
	return pk_bitfield_from_enums (
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_GET_CATEGORIES,

		/* TODO:
			PK_ROLE_ENUM_DEPENDS_ON,
			PK_ROLE_ENUM_GET_FILES,
			PK_ROLE_ENUM_GET_REPO_LIST,
			PK_ROLE_ENUM_REQUIRED_BY,
			PK_ROLE_ENUM_GET_UPDATE_DETAIL,
			PK_ROLE_ENUM_GET_UPDATES,
			PK_ROLE_ENUM_INSTALL_FILES,
			PK_ROLE_ENUM_INSTALL_SIGNATURE,
			PK_ROLE_ENUM_REPO_ENABLE,
			PK_ROLE_ENUM_REPO_SET_DATA,
			PK_ROLE_ENUM_RESOLVE,
			PK_ROLE_ENUM_SEARCH_DETAILS,
			PK_ROLE_ENUM_SEARCH_FILE,
			PK_ROLE_ENUM_SEARCH_GROUP,
			PK_ROLE_ENUM_SEARCH_NAME,
			PK_ROLE_ENUM_UPDATE_PACKAGES,
			PK_ROLE_ENUM_WHAT_PROVIDES,
			PK_ROLE_ENUM_ACCEPT_EULA,
			PK_ROLE_ENUM_GET_DISTRO_UPGRADES,
			PK_ROLE_ENUM_GET_CATEGORIES,
			PK_ROLE_ENUM_GET_OLD_TRANSACTIONS,
			PK_ROLE_ENUM_REPAIR_SYSTEM,
			PK_ROLE_ENUM_GET_DETAILS_LOCAL,
			PK_ROLE_ENUM_GET_FILES_LOCAL,
			PK_ROLE_ENUM_REPO_REMOVE,
			PK_ROLE_ENUM_UPGRADE_SYSTEM,
		*/

		-1);
}

gchar **
pk_backend_get_mime_types (PkBackend* backend)
{
	const gchar* mime_types[] = { "application/nix-package", NULL };
	return g_strdupv ((gchar **) mime_types);
}

void
pk_nix_run (PkBackendJob *job,
						PkStatusEnum status,
						PkBackendJobThreadFunc func,
						gpointer data)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	g_return_if_fail (func != NULL);

	pk_backend_job_set_allow_cancel (job, TRUE);
	pk_backend_job_set_status (job, status);
	pk_backend_job_thread_create (job, func, data, NULL);
}

void
pk_nix_error_emit (PkBackendJob* job, GError* error)
{
	PkErrorEnum code = PK_ERROR_ENUM_UNKNOWN;
	g_return_if_fail (error != NULL);
	pk_backend_job_error_code (job, code, "%s", error->message);
}

gboolean
pk_nix_finish (PkBackendJob* job, GError* error)
{
	if (error != NULL) {
		pk_nix_error_emit (job, error);
		return FALSE;
	}

	return TRUE;
}

static void
pk_backend_get_details_thread_package (PkBackendJob* job,
																			 PkBackendNixPrivate* priv,
																			 Path path)
{
	DrvInfo drv = nix_lookup_drv (priv, path);

	// auto license = "unknown";
	// auto licenseMeta = drv.queryMeta("license");
	// if (licenseMeta->type == tAttrs)
	// {
	// 	Bindings::iterator fullName = licenseMeta->find(priv->state->symbols.create("fullName"));
	// 	if (fullName != licenseMeta.end() && fullName->value->type == tString)
	// 		license = fullName->value->string.s.c_str();
	// }

	auto info = priv->state->store->queryPathInfo(drv.queryOutPath());

	pk_backend_job_details (
		job,
		path.c_str(),
		drv.queryMetaString("description").c_str(),
		"unknown",
		PK_GROUP_ENUM_UNKNOWN, // TODO
		drv.queryMetaString("longDescription").c_str(),
		drv.queryMetaString("homepage").c_str(),
		info.narSize
	);
}

static void
pk_backend_get_details_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	g_autoptr(GError) error = NULL;

	PathSet paths = nix_get_paths(priv, (gchar**) p);

	for (auto path : paths) {
		if (pk_backend_job_is_cancelled (job))
			break;

		pk_backend_get_details_thread_package (job, priv, path);
	}

	pk_nix_finish (job, error);
}

void
pk_backend_get_details (PkBackend* backend, PkBackendJob* job, gchar** packages)
{
	pk_nix_run (job, PK_STATUS_ENUM_QUERY, pk_backend_get_details_thread, packages);
}

static void
pk_backend_install_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	PkBitfield flags;
	gchar** package_ids;
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(t^a&s)", &flags, &package_ids);

	PathSet paths = nix_get_paths(priv, package_ids);
	// should provide updates to status
	priv->state->store->buildPaths(paths);

	DrvInfos newElems = nix_lookup_drvs(priv, paths);

	StringSet newNames;
	for (auto & i : newElems)
		newNames.insert(DrvName(i.name).name);

	Path profile = nix_get_profile();

	PkBackendNixPrivate _priv = *priv;

	// should provide updates to status
	while (true) {
		if (pk_backend_job_is_cancelled (job))
			break;

		string lockToken = optimisticLockProfile(profile);
		DrvInfos allElems (newElems);

		/* Add in the already installed derivations, unless they have
			 the same name as a to-be-installed element. */
		DrvInfos installedElems = nix_query_installed(*_priv.state, profile);

		for (auto & i : installedElems) {
			DrvName drvName(i.name);
			if (newNames.find(drvName.name) != newNames.end())
				continue;
			else
				allElems.push_back(i);
		}

		if (nix_create_user_env(*_priv.state, allElems, profile, false, lockToken))
			break;
	}

	pk_nix_finish (job, error);
}

void
pk_backend_install_packages (PkBackend* backend, PkBackendJob* job, PkBitfield transaction_flags, gchar** package_ids)
{
	pk_nix_run (job, PK_STATUS_ENUM_INSTALL, pk_backend_install_packages_thread, NULL);
}

static void
pk_backend_refresh_cache_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	g_autoptr(GError) error = NULL;

	// should provide updates to status
	// nix_load_priv (priv);

	pk_nix_finish (job, error);
}

void
pk_backend_refresh_cache (PkBackend* backend, PkBackendJob* job, gboolean force)
{
	pk_nix_run (job, PK_STATUS_ENUM_REFRESH_CACHE, pk_backend_refresh_cache_thread, NULL);
}

static void
pk_backend_remove_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	g_autoptr(GError) error = NULL;

	gboolean allow_deps, autoremove;
	gchar** package_ids;
	PkBitfield transaction_flags;
	g_variant_get (params, "(t^a&sbb)", &transaction_flags, &package_ids, &allow_deps, &autoremove);

	PathSet paths = nix_get_paths(priv, package_ids);

	Path profile = nix_get_profile();

	PkBackendNixPrivate _priv = *priv;

  while (true) {
		if (pk_backend_job_is_cancelled (job))
			break;

    string lockToken = optimisticLockProfile(profile);

    DrvInfos installedElems = nix_query_installed(*_priv.state, profile);
    DrvInfos newElems;

    for (auto & drv : installedElems) {
      bool found = false;

			for (auto & path : paths) {
				if (drv.queryOutPath() == path)
					found = true;
			}

			if (!found)
				newElems.push_back(drv);
		}

    if (nix_create_user_env(*_priv.state, newElems, profile, false, lockToken))
			break;
  }

	pk_nix_finish (job, error);
}

void
pk_backend_remove_packages (PkBackend* backend, PkBackendJob* job,
														PkBitfield transaction_flags, gchar** package_ids,
														gboolean allow_deps, gboolean autoremove)
{
	pk_nix_run (job, PK_STATUS_ENUM_REMOVE, pk_backend_remove_packages_thread, NULL);
}

static void
pk_backend_get_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	g_autoptr(GError) error = NULL;

	PkBitfield filters = *((PkBitfield*) p);

	for (auto drv : nix_get_drvs (*priv, filters))
	{
		if (pk_backend_job_is_cancelled (job))
			break;

		if (nix_filter_drv (drv, priv, filters))
			continue;

		auto info = PK_INFO_ENUM_AVAILABLE; // TODO

		pk_backend_job_package (
			job,
			info,
			drv.queryDrvPath().c_str(),
			drv.queryMetaString("description").c_str()
		);
	}

	pk_nix_finish (job, error);
}

void
pk_backend_get_packages (PkBackend* backend, PkBackendJob *job, PkBitfield filters)
{
	pk_nix_run (job, PK_STATUS_ENUM_QUERY, pk_backend_get_packages_thread, &filters);
}

static void
pk_backend_download_packages_thread (PkBackendJob* job, GVariant* params, gpointer p)
{
	PkBackend* backend = (PkBackend*) pk_backend_job_get_backend (job);
	PkBackendNixPrivate* priv = (PkBackendNixPrivate*) pk_backend_get_user_data (backend);
	g_autoptr(GError) error = NULL;

	gchar* directory;
	gchar** package_ids;
	g_variant_get (params, "(^a&ss)", &package_ids, &directory);

	PathSet paths = nix_get_paths(priv, package_ids);

	// should provide updates to status
	(*priv).state->store->buildPaths(paths);

	pk_nix_finish (job, error);
}

void
pk_backend_download_packages (PkBackend* backend, PkBackendJob* job,
	                            gchar **package_ids, const gchar	*directory)
{
	pk_nix_run (job, PK_STATUS_ENUM_DOWNLOAD, pk_backend_download_packages_thread, NULL);
}
