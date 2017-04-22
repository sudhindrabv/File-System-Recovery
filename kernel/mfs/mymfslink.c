#include "fs.h"
#include <sys/stat.h>
#include <string.h>
#include <minix/com.h>
#include "buf.h"
#include "inode.h"
#include "super.h"
#include <minix/vfsif.h>

#define SAME 1000


FORWARD _PROTOTYPE( int remove_dir, (struct inode *rldirp,
			struct inode *rip, char dir_name[MFS_NAME_MAX])	);
FORWARD _PROTOTYPE( int unlink_file, (struct inode *dirp,
			struct inode *rip, char file_name[MFS_NAME_MAX])	);
FORWARD _PROTOTYPE( int damage_unlink_file, (struct inode *dirp,
			struct inode *rip, char file_name[MFS_NAME_MAX], int dmg_type)	);

/* Args to zerozone_half() */
#define FIRST_HALF	0
#define LAST_HALF	1


/*===========================================================================*
 *				fs_mylink 				     *
 *===========================================================================*/
PUBLIC int fs_mylink()
{
/* Perform the link(name1, name2) system call. */

  struct inode *ip, *rip;
  register int r;
  char string[MFS_NAME_MAX];
  struct inode *new_ip;
  phys_bytes len;

  len = min( (unsigned) fs_m_in.REQ_PATH_LEN, sizeof(string));
  /* Copy the link name's last component */
  r = sys_safecopyfrom(VFS_PROC_NR, (cp_grant_id_t) fs_m_in.REQ_GRANT,
  		       (vir_bytes) 0, (vir_bytes) string, (size_t) len, D);
  if (r != OK) return r;
  NUL(string, len, sizeof(string));
  
  /* Temporarily open the file. */
  if( (rip = get_inode(fs_dev, (ino_t) fs_m_in.REQ_INODE_NR)) == NULL)
	  return(EINVAL);
  
  /* Check to see if the file has maximum number of links already. */
  r = OK;
  if(rip->i_nlinks >= LINK_MAX)
	  r = EMLINK;

  /* Only super_user may link to directories. */
  if(r == OK)
	  if( (rip->i_mode & I_TYPE) == I_DIRECTORY && caller_uid != SU_UID) 
		  r = EPERM;

  /* If error with 'name', return the inode. */
  if (r != OK) {
	  put_inode(rip);
	  return(r);
  }

  /* Temporarily open the last dir */
  if( (ip = get_inode(fs_dev, (ino_t) fs_m_in.REQ_DIR_INO)) == NULL) {
	put_inode(rip);
	return(EINVAL);
  }

  if (ip->i_nlinks == NO_LINK) {	/* Dir does not actually exist */
  	put_inode(rip);
	put_inode(ip);
  	return(ENOENT);
  }

  /* If 'name2' exists in full (even if no space) set 'r' to error. */
  if((new_ip = advance(ip, string, IGN_PERM)) == NULL) {
	  r = err_code;
	  if(r == ENOENT)
		  r = OK;
  } else {
	  put_inode(new_ip);
	  r = EEXIST;
  }
  
  /* Try to link. */
  if(r == OK)
	  r = search_dir(ip, string, &rip->i_num, ENTER, IGN_PERM);

  /* If success, register the linking. */
  if(r == OK) {
	  rip->i_nlinks++;
	  rip->i_update |= CTIME;
	  IN_MARKDIRTY(rip);
  }
  
  /* Done.  Release both inodes. */
  put_inode(rip);
  put_inode(ip);
  return(r);
}


/*===========================================================================*
 *				fs_myunlink				     *
 *===========================================================================*/
PUBLIC int fs_myunlink()
{
/* Perform the unlink(name) or rmdir(name) system call. The code for these two
 * is almost the same.  They differ only in some condition testing.  Unlink()
 * may be used by the superuser to do dangerous things; rmdir() may not.
 */
  register struct inode *rip;
  struct inode *rldirp;
  int r;
  char string[MFS_NAME_MAX];
  phys_bytes len;
  int dmg_type;

  dmg_type = (unsigned) fs_m_in.REQ_DMG_TYPE;
  /* Copy the last component */
  len = min( (unsigned) fs_m_in.REQ_PATH_LEN, sizeof(string));
  r = sys_safecopyfrom(VFS_PROC_NR, (cp_grant_id_t) fs_m_in.REQ_GRANT,
  		       (vir_bytes) 0, (vir_bytes) string, (size_t) len, D);
  if (r != OK) return r;
  NUL(string, len, sizeof(string));
  
  /* Temporarily open the dir. */
  if( (rldirp = get_inode(fs_dev, (ino_t) fs_m_in.REQ_INODE_NR)) == NULL)
	  return(EINVAL);
  
  /* The last directory exists.  Does the file also exist? */
  rip = advance(rldirp, string, IGN_PERM);
  r = err_code;

  /* If error, return inode. */
  if(r != OK) {
	  /* Mount point? */
  	if (r == EENTERMOUNT || r == ELEAVEMOUNT) {
  	  	put_inode(rip);
  		r = EBUSY;
  	}
	put_inode(rldirp);
	return(r);
  }
  
  if(rip->i_sp->s_rd_only) {
  	r = EROFS;
  }  else if(fs_m_in.m_type == REQ_MYUNLINK) {
  /* Now test if the call is allowed, separately for unlink() and rmdir(). */
	  /* Only the su may unlink directories, but the su can unlink any
	   * dir.*/
	  if( (rip->i_mode & I_TYPE) == I_DIRECTORY) r = EPERM;

	  /* Actually try to unlink the file; fails if parent is mode 0 etc. */
	  if (r == OK) r = damage_unlink_file(rldirp, rip, string, dmg_type);
  } else {
	  r = remove_dir(rldirp, rip, string); /* call is RMDIR */
  }

  /* If unlink was possible, it has been done, otherwise it has not. */
  put_inode(rip);
  put_inode(rldirp);
  return(r);
}


/*===========================================================================*
 *				remove_dir				     *
 *===========================================================================*/
PRIVATE int remove_dir(rldirp, rip, dir_name)
struct inode *rldirp;		 	/* parent directory */
struct inode *rip;			/* directory to be removed */
char dir_name[MFS_NAME_MAX];		/* name of directory to be removed */
{
  /* A directory file has to be removed. Five conditions have to met:
   * 	- The file must be a directory
   *	- The directory must be empty (except for . and ..)
   *	- The final component of the path must not be . or ..
   *	- The directory must not be the root of a mounted file system (VFS)
   *	- The directory must not be anybody's root/working directory (VFS)
   */
  int r;

  /* search_dir checks that rip is a directory too. */
  if ((r = search_dir(rip, "", NULL, IS_EMPTY, IGN_PERM)) != OK)
  	return(r);

  if (strcmp(dir_name, ".") == 0 || strcmp(dir_name, "..") == 0)return(EINVAL);
  if (rip->i_num == ROOT_INODE) return(EBUSY); /* can't remove 'root' */
 
  /* Actually try to unlink the file; fails if parent is mode 0 etc. */
  if ((r = unlink_file(rldirp, rip, dir_name)) != OK) return r;

  /* Unlink . and .. from the dir. The super user can link and unlink any dir,
   * so don't make too many assumptions about them.
   */
  (void) unlink_file(rip, NULL, dot1);
  (void) unlink_file(rip, NULL, dot2);
  return(OK);
}


/*===========================================================================*
 *				unlink_file				     *
 *===========================================================================*/
PRIVATE int unlink_file(dirp, rip, file_name)
struct inode *dirp;		/* parent directory of file */
struct inode *rip;		/* inode of file, may be NULL too. */
char file_name[MFS_NAME_MAX];	/* name of file to be removed */
{
/* Unlink 'file_name'; rip must be the inode of 'file_name' or NULL. */

  ino_t numb;			/* inode number */
  int	r;

  /* If rip is not NULL, it is used to get faster access to the inode. */
  if (rip == NULL) {
  	/* Search for file in directory and try to get its inode. */
	err_code = search_dir(dirp, file_name, &numb, LOOK_UP, IGN_PERM);
	if (err_code == OK) rip = get_inode(dirp->i_dev, (int) numb);
	if (err_code != OK || rip == NULL) return(err_code);
  } else {
	dup_inode(rip);		/* inode will be returned with put_inode */
  }

  r = search_dir(dirp, file_name, NULL, DELETE, IGN_PERM);

  if (r == OK) {
	rip->i_nlinks--;	/* entry deleted from parent's dir */
	rip->i_update |= CTIME;
	IN_MARKDIRTY(rip);
  }

  put_inode(rip);
  return(r);
}


/*===========================================================================*
 *				damage_unlink_file				     *
 *===========================================================================*/
PRIVATE int damage_unlink_file(dirp, rip, file_name, dmg_type)
struct inode *dirp;		/* parent directory of file */
struct inode *rip;		/* inode of file, may be NULL too. */
char file_name[MFS_NAME_MAX];	/* name of file to be removed */
int dmg_type;		/* damage type */
{
/* Unlink 'file_name'; rip must be the inode of 'file_name' or NULL. */

  ino_t numb;			/* inode number */
  int	r;

  /* If rip is not NULL, it is used to get faster access to the inode. */
  if (rip == NULL) {
  	/* Search for file in directory and try to get its inode. */
	err_code = search_dir(dirp, file_name, &numb, LOOK_UP, IGN_PERM);
	if (err_code == OK) rip = get_inode(dirp->i_dev, (int) numb);
	if (err_code != OK || rip == NULL) return(err_code);
  } else {
	dup_inode(rip);		/* inode will be returned with put_inode */
  }

  switch (dmg_type) {
      default: case 0:
      r = search_dir(dirp, file_name, NULL, DELETE, IGN_PERM);
      rip->i_nlinks--;	/* entry deleted from parent's dir */
      rip->i_update |= CTIME;
      IN_MARKDIRTY(rip);
      put_inode(rip);
      break; 
      
      case 1:
      rip->i_nlinks--;	/* entry deleted from parent's dir */
      rip->i_update |= CTIME;
      IN_MARKDIRTY(rip);
      put_inode(rip);
      break; 
       
      case 2:
      r = search_dir(dirp, file_name, NULL, DELETE, IGN_PERM);
      rip->i_update |= CTIME;
      IN_MARKDIRTY(rip);
      put_inode(rip);
      break; 
      
      case 3:
      rip->i_nlinks++;	/* entry deleted from parent's dir */
      rip->i_atime = 0;	
      rip->i_mtime = 0;	
      rip->i_ctime = 0;	
      rip->i_update |= CTIME;
      IN_MARKDIRTY(rip);
      put_inode(rip);
      break; 
      
      case 4:
      dirp->i_atime = 0;	
      dirp->i_mtime = 0;	
      dirp->i_ctime = 0;	
      IN_MARKDIRTY(dirp);
      break; 
      
      case 5:
      dirp->i_nlinks++;	/* entry deleted from parent's dir */
      dirp->i_update |= CTIME;
      IN_MARKDIRTY(dirp);
      break; 
      
  }

  r = OK;

  return(r);
}


