/* This file handles the MYLINK and MYUNLINK system calls.  It also deals with
 * deallocating the storage used by a file when the last MYUNLINK is done to a
 * file and the blocks must be returned to the free block pool.
 *
 * The entry points into this file are
 *   do_mylink:         perform the MYLINK system call
 *   do_myunlink:	      perform the MYUNLINK and RMDIR system calls
 */

#include "fs.h"
#include <sys/stat.h>
#include <string.h>
#include <minix/com.h>
#include <minix/callnr.h>
#include <minix/vfsif.h>
#include <dirent.h>
#include <assert.h>
#include "file.h"
#include "fproc.h"
#include "path.h"
#include "vnode.h"
#include "param.h"

/*===========================================================================*
 *				do_mylink					     *
 *===========================================================================*/
PUBLIC int do_mylink()
{
/* Perform the link(name1, name2) system call. */
  int r = OK;
  struct vnode *vp = NULL, *dirp = NULL;
  struct vmnt *vmp1 = NULL, *vmp2 = NULL;
  char fullpath[PATH_MAX];
  struct lookup resolve;

  lookup_init(&resolve, fullpath, PATH_NOFLAGS, &vmp1, &vp);
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_READ;

  /* See if 'name1' (file to be linked to) exists. */
  if (fetch_name(m_in.name1, m_in.name1_length, M1, fullpath) != OK)
	return(err_code);
  if ((vp = eat_path(&resolve, fp)) == NULL) return(err_code);

  /* Does the final directory of 'name2' exist? */
  lookup_init(&resolve, fullpath, PATH_NOFLAGS, &vmp2, &dirp);
  resolve.l_vmnt_lock = VMNT_READ;
  resolve.l_vnode_lock = VNODE_READ;
  if (fetch_name(m_in.name2, m_in.name2_length, M1, fullpath) != OK)
	r = err_code;
  else if ((dirp = last_dir(&resolve, fp)) == NULL)
	r = err_code;

  if (r != OK) {
	unlock_vnode(vp);
	unlock_vmnt(vmp1);
	put_vnode(vp);
	return(r);
  }

  /* Check for links across devices. */
  if (vp->v_fs_e != dirp->v_fs_e)
	r = EXDEV;
  else
	r = forbidden(fp, dirp, W_BIT | X_BIT);

  if (r == OK) {
 
	r = req_mylink(vp->v_fs_e, dirp->v_inode_nr, fullpath,
		     vp->v_inode_nr);
	//r = req_link(vp->v_fs_e, dirp->v_inode_nr, fullpath,
        //	     vp->v_inode_nr);
  }
  unlock_vnode(vp);
  unlock_vnode(dirp);
  if (vmp2 != NULL) unlock_vmnt(vmp2);
  unlock_vmnt(vmp1);
  put_vnode(vp);
  put_vnode(dirp);
  return(r);
}


/*===========================================================================*
 *				do_myunlink				     *
 *===========================================================================*/
PUBLIC int do_myunlink()
{
/* Perform the unlink(name) or rmdir(name) system call. The code for these two
 * is almost the same.  They differ only in some condition testing.  Unlink()
 * may be used by the superuser to do dangerous things; rmdir() may not.
 */
  struct vnode *dirp, *vp;
  struct vmnt *vmp, *vmp2;
  int r;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  int dmg_type = m_in.m1_i2;

  lookup_init(&resolve, fullpath, PATH_RET_SYMLINK, &vmp, &dirp);
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_READ;

  /* Get the last directory in the path. */
  if (fetch_name(m_in.name, m_in.name_length, M3, fullpath) != OK)
	return(err_code);

  if ((dirp = last_dir(&resolve, fp)) == NULL) return(err_code);
  assert(vmp != NULL);

  /* Make sure that the object is a directory */
  if ((dirp->v_mode & I_TYPE) != I_DIRECTORY) {
	unlock_vnode(dirp);
	unlock_vmnt(vmp);
	put_vnode(dirp);
	return(ENOTDIR);
  }

  /* The caller must have both search and execute permission */
  if ((r = forbidden(fp, dirp, X_BIT | W_BIT)) != OK) {
	unlock_vnode(dirp);
	unlock_vmnt(vmp);
	put_vnode(dirp);
	return(r);
  }

  /* Also, if the sticky bit is set, only the owner of the file or a privileged
     user is allowed to unlink */
  if ((dirp->v_mode & S_ISVTX) == S_ISVTX) {
	/* Look up inode of file to unlink to retrieve owner */
	resolve.l_flags = PATH_RET_SYMLINK;
	resolve.l_vmp = &vmp2;	/* Shouldn't actually get locked */
	resolve.l_vmnt_lock = VMNT_READ;
	resolve.l_vnode = &vp;
	resolve.l_vnode_lock = VNODE_READ;
	vp = advance(dirp, &resolve, fp);
	assert(vmp2 == NULL);
	if (vp != NULL) {
		if (vp->v_uid != fp->fp_effuid && fp->fp_effuid != SU_UID)
			r = EPERM;
		unlock_vnode(vp);
		put_vnode(vp);
	} else
		r = err_code;
	if (r != OK) {
		unlock_vnode(dirp);
		unlock_vmnt(vmp);
		put_vnode(dirp);
		return(r);
	}
  }

  assert(vmp != NULL);
  tll_upgrade(&vmp->m_lock);

  if (call_nr == MYUNLINK) { 
	  //r = req_unlink(dirp->v_fs_e, dirp->v_inode_nr, fullpath);
	  r = req_myunlink(dirp->v_fs_e, dirp->v_inode_nr, fullpath, dmg_type);
  } 
  else
	  r = req_rmdir(dirp->v_fs_e, dirp->v_inode_nr, fullpath);
  unlock_vnode(dirp);
  unlock_vmnt(vmp);
  put_vnode(dirp);
  return(r);
}

