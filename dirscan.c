/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002,2003,2004,2005
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * 
 **********************************************************************
 */

/* Traverse a directory tree and find maildirs, then list files in them. */

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>
#include "mairix.h"

struct msgpath_array *new_msgpath_array(void)/*{{{*/
{
  struct msgpath_array *result;
  result = new(struct msgpath_array);
  result->paths = NULL;
  result->type = NULL;
  result->n = 0;
  result->max = 0;
  return result;
}
/*}}}*/
void free_msgpath_array(struct msgpath_array *x)/*{{{*/
{
  int i;
  if (x->paths) {
    for (i=0; i<x->n; i++) {
      switch (x->type[i]) {
        case MTY_FILE:
          free(x->paths[i].src.mpf.path);
          break;
        case MTY_MBOX:
          break;
        case MTY_DEAD:
          break;
      }
    }
    free(x->type);
    free(x->paths);
  }
  free(x);
}
/*}}}*/
static void add_file_to_list(char *x, unsigned long mtime, size_t message_size, struct msgpath_array *arr) {/*{{{*/
  char *y = new_string(x);
  if (arr->n == arr->max) {
    arr->max += 1024;
    arr->paths = grow_array(struct msgpath,    arr->max, arr->paths);
    arr->type  = grow_array(enum message_type, arr->max, arr->type);
  }
  arr->type[arr->n] = MTY_FILE;
  arr->paths[arr->n].src.mpf.path = y;
  arr->paths[arr->n].src.mpf.mtime = mtime;
  arr->paths[arr->n].src.mpf.size = message_size;
  ++arr->n;
  return;
}
/*}}}*/
static void get_maildir_message_paths(char *folder, struct msgpath_array *arr)/*{{{*/
{
  char *subdir, *fname;
  int i;
  static char *subdirs[] = {"new", "cur"};
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int folder_len = strlen(folder);

  /* FIXME : just store mdir-rooted paths in array and have common prefix elsewhere. */
  
  subdir = new_array(char, folder_len + 6);
  fname = new_array(char, folder_len + 8 + NAME_MAX);
  for (i=0; i<2; i++) { 
    strcpy(subdir, folder);
    strcat(subdir, "/");
    strcat(subdir, subdirs[i]);
    d = opendir(subdir);
    if (d) {
      while ((de = readdir(d))) {
        /* TODO : Perhaps we ought to do some validation on the path here?
           i.e. check that the filename looks valid for a maildir message. */
        strcpy(fname, subdir);
        strcat(fname, "/");
        strcat(fname, de->d_name);
        if (stat(fname, &sb) >= 0) {
          if (S_ISREG(sb.st_mode)) {
            add_file_to_list(fname, sb.st_mtime, sb.st_size, arr);
          }
        }
      }
      closedir(d);
    }
  }
  free(subdir);
  free(fname);
  return;
}
/*}}}*/
int is_integer_string(const char *x)/*{{{*/
{
  const char *p;
  
  if (!*x) return 0; /* Must not be empty */
  p = x;
  while (*p) {
    if (!isdigit(*p)) return 0;
    p++;
  }
  return 1;
}
/*}}}*/
static void get_mh_message_paths(char *folder, struct msgpath_array *arr)/*{{{*/
{
  char *fname;
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int folder_len = strlen(folder);

  fname = new_array(char, folder_len + 8 + NAME_MAX);
  d = opendir(folder);
  if (d) {
    while ((de = readdir(d))) {
      strcpy(fname, folder);
      strcat(fname, "/");
      strcat(fname, de->d_name);
      if (stat(fname, &sb) >= 0) {
        if (S_ISREG(sb.st_mode)) {
          if (is_integer_string(de->d_name)) {
            add_file_to_list(fname, sb.st_mtime, sb.st_size, arr);
          }
        }
      }
    }
    closedir(d);
  }
  free(fname);
  return;
}
/*}}}*/
static int child_stat(const char *base, const char *child, struct stat *sb)/*{{{*/
{
  int result = 0;
  char *scratch;
  int len;

  len = strlen(base) + strlen(child) + 2;
  scratch = new_array(char, len);

  strcpy(scratch, base);
  strcat(scratch, "/");
  strcat(scratch, child);
  
  result = stat(scratch, sb);
  free(scratch);
  return result;
}
/*}}}*/
static int has_child_file(const char *base, const char *child)/*{{{*/
{
  int result = 0;
  int status;
  struct stat sb;
  
  status = child_stat(base, child, &sb);
  if ((status >= 0) && S_ISREG(sb.st_mode)) {
    result = 1;
  }

  return result;
}
/*}}}*/
static int has_child_dir(const char *base, const char *child)/*{{{*/
{
  int result = 0;
  int status;
  struct stat sb;
  
  status = child_stat(base, child, &sb);
  if ((status >= 0) && S_ISDIR(sb.st_mode)) {
    result = 1;
  }

  return result;
}
/*}}}*/
enum traverse_check scrutinize_maildir_entry(int parent_is_maildir, const char *de_name)/*{{{*/
{
  if (parent_is_maildir) {
    /* Process any subdirectory that's not part of this maildir itself. */
		if (!strcmp(de_name, "new") ||
				!strcmp(de_name, "cur") ||
				!strcmp(de_name, "tmp")) {
			return TRAV_IGNORE;
		} else {
			return TRAV_PROCESS;
		}
  } else {
    return TRAV_PROCESS;
  }
}
/*}}}*/
int filter_is_maildir(const char *path, const struct stat *sb)/*{{{*/
{
  if (S_ISDIR(sb->st_mode)) {
    if (has_child_dir(path, "new") &&
        has_child_dir(path, "tmp") &&
        has_child_dir(path, "cur")) {
      return 1;
    }
  }
  return 0;
}
/*}}}*/
struct traverse_methods maildir_traverse_methods = {/*{{{*/
  .filter = filter_is_maildir,
  .scrutinize = scrutinize_maildir_entry
};
/*}}}*/
enum traverse_check scrutinize_mh_entry(int parent_is_mh, const char *de_name)/*{{{*/
{
  /* Don't allow sub-folders within a folder */
  if (parent_is_mh) {
    return TRAV_FINISH;
  } else {
    return TRAV_PROCESS;
  }
}
/*}}}*/
int filter_is_mh(const char *path, const struct stat *sb)/*{{{*/
{
  int result = 0;
  if (S_ISDIR(sb->st_mode)) {
    if (has_child_file(path, ".xmhcache") ||
        has_child_file(path, ".mh_sequences")) {
      result = 1;
    }
  }
  return result;
}
/*}}}*/
struct traverse_methods mh_traverse_methods = {/*{{{*/
  .filter = filter_is_mh,
  .scrutinize = scrutinize_mh_entry
};
/*}}}*/
#if 0
static void scan_directory(char *folder_base, char *this_folder, enum folder_type ft, struct msgpath_array *arr)/*{{{*/
{
  DIR *d;
  struct dirent *de;
  struct stat sb;
  char *fname, *sname;
  char *name;
  int folder_base_len = strlen(folder_base);
  int this_folder_len = strlen(this_folder);

  name = new_array(char, folder_base_len + this_folder_len + 2);
  strcpy(name, folder_base);
  strcat(name, "/");
  strcat(name, this_folder);

  switch (ft) {
    case FT_MAILDIR:
      if (looks_like_maildir(folder_base, this_folder)) {
        get_maildir_message_paths(folder_base, this_folder, arr);
      }
      break;
    case FT_MH:
      get_mh_message_paths(folder_base, this_folder, arr);
      break;
    default:
      break;
  }
  
  fname = new_array(char, strlen(name) + 2 + NAME_MAX);
  sname = new_array(char, this_folder_len + 2 + NAME_MAX);

  d = opendir(name);
  if (d) {
    while ((de = readdir(d))) {
      if (!strcmp(de->d_name, ".") ||
          !strcmp(de->d_name, "..")) {
        continue;
      }

      strcpy(fname, name);
      strcat(fname, "/");
      strcat(fname, de->d_name);

      strcpy(sname, this_folder);
      strcat(sname, "/");
      strcat(sname, de->d_name);

      if (stat(fname, &sb) >= 0) {
        if (S_ISDIR(sb.st_mode)) {
          scan_directory(folder_base, sname, ft, arr);
        }
      }
    }
    closedir(d);
  }

  free(fname);
  free(sname);
  free(name);
  return;
}
/*}}}*/
#endif
static int message_compare(const void *a, const void *b)/*{{{*/
{
  /* FIXME : Is this a sensible way to do this with mbox messages in the picture? */
  struct msgpath *aa = (struct msgpath *) a;  
  struct msgpath *bb = (struct msgpath *) b;
  /* This should only get called on 'file' type messages - TBC! */
  return strcmp(aa->src.mpf.path, bb->src.mpf.path);
}
/*}}}*/
static void sort_message_list(struct msgpath_array *arr)/*{{{*/
{
  qsort(arr->paths, arr->n, sizeof(struct msgpath), message_compare);
}
/*}}}*/
/*{{{ void build_message_list */
void build_message_list(char *folder_base, char *folders, enum folder_type ft,
    struct msgpath_array *msgs,
    struct globber_array *omit_globs)
{
  char **raw_paths, **paths;
  int n_raw_paths, n_paths, i;

  split_on_colons(folders, &n_raw_paths, &raw_paths);
  switch (ft) {
    case FT_MAILDIR:
      glob_and_expand_paths(folder_base, raw_paths, n_raw_paths, &paths, &n_paths, &maildir_traverse_methods, omit_globs);
      for (i=0; i<n_paths; i++) {
        get_maildir_message_paths(paths[i], msgs);
      }
      break;
    case FT_MH:
      glob_and_expand_paths(folder_base, raw_paths, n_raw_paths, &paths, &n_paths, &mh_traverse_methods, omit_globs);
      for (i=0; i<n_paths; i++) {
        get_mh_message_paths(paths[i], msgs);
      }
      break;
    default:
      assert(0);
      break;
  }
      
  if (paths) free(paths);

  sort_message_list(msgs);
  return;
}
/*}}}*/

#ifdef TEST
int main (int argc, char **argv)
{
  int i;
  struct msgpath_array *arr;
  
  arr = build_message_list(".");

  for (i=0; i<arr->n; i++) {
    printf("%08lx %s\n", arr->paths[i].mtime, arr->paths[i].path);
  }

  free_msgpath_array(arr);
  
  return 0;
}
#endif


