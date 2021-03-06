/*
 * libdpkg - Debian packaging suite library routines
 * dpkg-db.h - declarations for in-core package database management
 *
 * Copyright © 1994,1995 Ian Jackson <ian@chiark.greenend.org.uk>
 * Copyright © 2000,2001 Wichert Akkerman
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBDPKG_DPKG_DB_H
#define LIBDPKG_DPKG_DB_H

#include <sys/types.h>

#include <stdbool.h>
#include <stdio.h>

#include <dpkg/macros.h>
#include <dpkg/varbuf.h>

DPKG_BEGIN_DECLS

struct versionrevision {
  unsigned long epoch;
  const char *version;
  const char *revision;
};  

enum deptype {
  dep_suggests,
  dep_recommends,
  dep_depends,
  dep_predepends,
  dep_breaks,
  dep_conflicts,
  dep_provides,
  dep_replaces,
  dep_enhances
};

enum depverrel {
  dvrf_earlier=      0001,
  dvrf_later=        0002,
  dvrf_strict=       0010,
  dvrf_orequal=      0020,
  dvrf_builtup=      0100,
  dvr_none=          0200,
  dvr_earlierequal=  dvrf_builtup | dvrf_earlier | dvrf_orequal,
  dvr_earlierstrict= dvrf_builtup | dvrf_earlier | dvrf_strict,
  dvr_laterequal=    dvrf_builtup | dvrf_later   | dvrf_orequal,
  dvr_laterstrict=   dvrf_builtup | dvrf_later   | dvrf_strict,
  dvr_exact=         0400
};

struct dependency {
  struct pkginfo *up;
  struct dependency *next;
  struct deppossi *list;
  enum deptype type;
};

struct deppossi {
  struct dependency *up;
  struct pkginfo *ed;
  struct deppossi *next, *nextrev, *backrev;
  struct versionrevision version;
  enum depverrel verrel;
  int cyclebreak;
};

struct arbitraryfield {
  struct arbitraryfield *next;
  const char *name;
  const char *value;
};

struct conffile {
  struct conffile *next;
  const char *name;
  const char *hash;
  int obsolete;
};

struct filedetails {
  struct filedetails *next;
  const char *name;
  const char *msdosname;
  const char *size;
  const char *md5sum;
};

struct pkginfoperfile { /* pif */
  int valid;
  struct dependency *depends;
  struct deppossi *depended;
  int essential; /* The `essential' flag, 1=yes, 0=no (absent) */
  const char *description;
  const char *maintainer;
  const char *source;
  const char *architecture;
  const char *installedsize;
  const char *origin;
  const char *bugs;
  struct versionrevision version;
  struct conffile *conffiles;
  struct arbitraryfield *arbs;
};

struct trigpend {
  /* Node indicates that parent's Triggers-Pending mentions name. */
  /* NB that these nodes do double duty: after they're removed from
   * a package's trigpend list, references may be preserved by the
   * trigger cycle checker (see trigproc.c).
   */
  struct trigpend *next;
  const char *name;
};

struct trigaw {
  /* Node indicates that aw's Triggers-Awaited mentions pend. */
  struct pkginfo *aw, *pend;
  struct trigaw *nextsamepend;
  struct {
    struct trigaw *next, *back;
  } sameaw;
};

struct perpackagestate; /* dselect and dpkg have different versions of this */

struct pkginfo { /* pig */
  struct pkginfo *next;
  const char *name;
  enum pkgwant {
    want_unknown, want_install, want_hold, want_deinstall, want_purge,
    want_sentinel /* Not allowed except as special sentinel value
                     in some places */
  } want;
  enum pkgeflag {
    eflag_ok		= 0,
    eflag_reinstreq	= 1,
  } eflag; /* Bitmask. */
  enum pkgstatus {
    stat_notinstalled,
    stat_configfiles,
    stat_halfinstalled,
    stat_unpacked,
    stat_halfconfigured,
    stat_triggersawaited,
    stat_triggerspending,
    stat_installed
  } status;
  enum pkgpriority {
    pri_required,
    pri_important,
    pri_standard,
    pri_optional,
    pri_extra,
    pri_other, pri_unknown, pri_unset=-1
  } priority;
  const char *otherpriority;
  const char *section;
  struct versionrevision configversion;
  struct filedetails *files;
  struct pkginfoperfile installed;
  struct pkginfoperfile available;
  struct perpackagestate *clientdata;
  enum { white, gray, black } color;  /* used during cycle detection */

  struct {
    /* ->aw == this */
    struct trigaw *head, *tail;
  } trigaw;

  /* ->pend == this, non-NULL for us when Triggers-Pending. */
  struct trigaw *othertrigaw_head;
  struct trigpend *trigpend_head;
};

/*** from lock.c ***/

void lockdatabase(const char *admindir);
void unlockdatabase(void);

/*** from dbmodify.c ***/

enum modstatdb_rw {
  /* Those marked with \*s*\ are possible returns from modstatdb_init. */
  msdbrw_readonly/*s*/, msdbrw_needsuperuserlockonly/*s*/,
  msdbrw_writeifposs,
  msdbrw_write/*s*/, msdbrw_needsuperuser,
  /* Now some optional flags: */
  msdbrw_flagsmask= ~077,
  /* flags start at 0100 */
  msdbrw_noavail= 0100,
};

enum modstatdb_rw modstatdb_init(const char *admindir, enum modstatdb_rw reqrwflags);
void modstatdb_note(struct pkginfo *pkg);
void modstatdb_note_ifwrite(struct pkginfo *pkg);
void modstatdb_checkpoint(void);
void modstatdb_shutdown(void);

/* Initialised by modstatdb_init. */
extern char *statusfile, *availablefile;
extern char *triggersdir, *triggersfilefile, *triggersnewfilefile;

const char *pkgadminfile(struct pkginfo *pkg, const char *whichfile);

/*** from trigdeferred.l ***/

enum trigdef_updateflags {
  tduf_nolockok =           001,
  tduf_write =              002,
  tduf_nolock =             003,
  /* Should not be set unless _write is. */
  tduf_writeifempty =       010,
  tduf_writeifenoent =      020,
};

struct trigdefmeths {
  void (*trig_begin)(const char *trig);
  void (*package)(const char *awname);
  void (*trig_end)(void);
};

extern const struct trigdefmeths *trigdef;
extern FILE *trig_new_deferred;

/* Return values:
 *  -1  Lock ENOENT with O_CREAT (directory does not exist)
 *  -2  Unincorp empty, tduf_writeifempty unset
 *  -3  Unincorp ENOENT, tduf_writeifenoent unset
 *   1  Unincorp ENOENT, tduf_writeifenoent set   } caller must call
 *   2  ok                                        }  trigdef_update_done!
 */
int trigdef_update_start(enum trigdef_updateflags uf, const char *admindir);

int trigdef_yylex(void);
void trigdef_process_done(void);

/*** hooks for more sophisticated processing in dpkg proper ***/

/* We do things like this so we can get most of the trigger tracking
 * in dpkg-query, dselect, and so on, but avoid the transitional
 * processing and deferred trigproc queue management other than when
 * we're actually doing real package management work. */

struct trigfileint {
  struct pkginfo *pkg;
  struct filenamenode *fnn;
  struct trigfileint *samefile_next;
  struct {
    struct trigfileint *next, *back;
  } inoverall;
};

struct trig_hooks {
 /* The first two are normally NULL.
  * If non-NULL, we're dpkg proper and we might need to invent trigger
  * activations as the first run of a triggers-supporting dpkg.
  */
  void (*enqueue_deferred)(struct pkginfo *pend);
  void (*transitional_activate)(enum modstatdb_rw cstatus);

  struct filenamenode *(*namenode_find)(const char *filename, int nonew);
  struct trigfileint **(*namenode_interested)(struct filenamenode *fnn);

  /* Returns a pointer from nfmalloc. */
  const char *(*namenode_name)(struct filenamenode *fnn);
};

extern struct trig_hooks trigh;

#define TRIGHOOKS_DEFINE_NAMENODE_ACCESSORS				 \
  static struct trigfileint **th_nn_interested(struct filenamenode *fnn) \
    { return &fnn->trig_interested; }					 \
  static const char *th_nn_name(struct filenamenode *fnn)		 \
    { return fnn->name; }

/*** from triglib.c ***/

void trig_file_activate_byname(const char *trig, struct pkginfo *aw);
void trig_file_activate(struct filenamenode *trig, struct pkginfo *aw);

int trig_note_pend_core(struct pkginfo *pend, const char *trig /*not copied!*/);
int trig_note_pend(struct pkginfo *pend, const char *trig /*not copied!*/);
int trig_note_aw(struct pkginfo *pend, struct pkginfo *aw);
void trig_clear_awaiters(struct pkginfo *notpend);

void trig_enqueue_awaited_pend(struct pkginfo *pend);
void trig_fixup_awaiters(enum modstatdb_rw cstatus);

void trig_file_interests_ensure(void);
void trig_file_interests_save(void);

void trig_cicb_interest_delete(const char *trig, void *user);
void trig_cicb_interest_add(const char *trig, void *user);
typedef void trig_parse_cicb(const char *trig, void *user);
void trig_parse_ci(const char *file, trig_parse_cicb *interest,
                   trig_parse_cicb *activate, void *user);

/* Called by process_archive. */
void trig_cicb_statuschange_activate(const char *trig, void *user);

void trig_incorporate(enum modstatdb_rw cstatus, const char *admindir);

const char *illegal_triggername(const char *p);

/*** from database.c ***/

struct pkginfo *findpackage(const char *name);
void blankpackage(struct pkginfo *pp);
void blankpackageperfile(struct pkginfoperfile *pifp);
void blankversion(struct versionrevision*);
bool informative(struct pkginfo *pkg, struct pkginfoperfile *info);
int countpackages(void);
void resetpackages(void);

struct pkgiterator *iterpkgstart(void);
struct pkginfo *iterpkgnext(struct pkgiterator*);
void iterpkgend(struct pkgiterator*);

void hashreport(FILE*);

/*** from parse.c ***/

enum parsedbflags {
  pdb_recordavailable   =001, /* Store in `available' in-core structures, not `status' */
  pdb_rejectstatus      =002, /* Throw up an error if `Status' encountered             */
  pdb_weakclassification=004, /* Ignore priority/section info if we already have any   */
  pdb_ignorefiles       =010, /* Ignore files info if we already have them             */
  pdb_ignoreolder       =020  /* Ignore packages with older versions already read      */
};

const char *illegal_packagename(const char *p, const char **ep);
int parsedb(const char *filename, enum parsedbflags, struct pkginfo **donep,
            FILE *warnto, int *warncount);
void copy_dependency_links(struct pkginfo *pkg,
                           struct dependency **updateme,
                           struct dependency *newdepends,
                           int available);

/*** from parsehelp.c ***/

struct namevalue {
  const char *name;
  int value, length;
};

extern const struct namevalue booleaninfos[];
extern const struct namevalue priorityinfos[];
extern const struct namevalue statusinfos[];
extern const struct namevalue eflaginfos[];
extern const struct namevalue wantinfos[];

int informativeversion(const struct versionrevision *version);

enum versiondisplayepochwhen { vdew_never, vdew_nonambig, vdew_always };
void varbufversion(struct varbuf*, const struct versionrevision*,
                   enum versiondisplayepochwhen);
const char *parseversion(struct versionrevision *rversion, const char*);
const char *versiondescribe(const struct versionrevision*,
                            enum versiondisplayepochwhen);

/*** from dump.c ***/

void writerecord(FILE*, const char*,
                 const struct pkginfo*, const struct pkginfoperfile*);

void writedb(const char *filename, int available, int mustsync);

void varbufrecord(struct varbuf*, const struct pkginfo*, const struct pkginfoperfile*);
void varbufdependency(struct varbuf *vb, struct dependency *dep);
  /* NB THE VARBUF MUST HAVE BEEN INITIALISED AND WILL NOT BE NULL-TERMINATED */

/*** from vercmp.c ***/

bool versionsatisfied(struct pkginfoperfile *it, struct deppossi *against);
bool versionsatisfied3(const struct versionrevision *it,
                       const struct versionrevision *ref,
                       enum depverrel verrel);
int versioncompare(const struct versionrevision *version,
                   const struct versionrevision *refversion);
bool epochsdiffer(const struct versionrevision *a,
                  const struct versionrevision *b);

/*** from nfmalloc.c ***/
void *nfmalloc(size_t);
char *nfstrsave(const char*);
char *nfstrnsave(const char*, size_t);
void nffreeall(void);

/*** from showpkg.c ***/

struct lstitem;

struct lstitem* parseformat(const char* fmt);
void freeformat(struct lstitem* head);
void show1package(const struct lstitem* head, struct pkginfo *pkg);

DPKG_END_DECLS

#endif /* LIBDPKG_DPKG_DB_H */
