#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/type.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/queue.h>
#include <minix/vfsif.h>
#include <minix/libminixfs.h>
#include <sys/cdefs.h>

/* Tables sizes */
#define V2_NR_DZONES       7	/* # direct zone numbers in a V2 inode */
#define V2_NR_TZONES      10	/* total # zone numbers in a V2 inode */

#define NR_INODES        512	/* # slots in "in core" inode table,
				 * should be more or less the same as
				 * NR_VNODES in vfs
				 */

#define INODE_HASH_LOG2   7     /* 2 based logarithm of the inode hash size */
#define INODE_HASH_SIZE   ((unsigned long)1<<INODE_HASH_LOG2)
#define INODE_HASH_MASK   (((unsigned long)1<<INODE_HASH_LOG2)-1)

/* Max. filename length */
#define MFS_NAME_MAX	 MFS_DIRSIZ

/* Declaration of the V2 inode as it is on the disk (not in core). */
typedef struct {		/* V2.x disk inode */
  u16_t d2_mode;		/* file type, protection, etc. */
  u16_t d2_nlinks;		/* how many links to this file. HACK! */
  i16_t d2_uid;			/* user id of the file's owner. */
  u16_t d2_gid;			/* group number HACK! */
  i32_t d2_size;		/* current file size in bytes */
  i32_t d2_atime;		/* when was file data last accessed */
  i32_t d2_mtime;		/* when was file data last changed */
  i32_t d2_ctime;		/* when was inode data last changed */
  zone_t d2_zone[V2_NR_TZONES];	/* block nums for direct, ind, and dbl ind */
} d2_inode;

/* Super block table.  The root file system and every mounted file system
 * has an entry here.  The entry holds information about the sizes of the bit
 * maps and inodes.  The s_ninodes field gives the number of inodes available
 * for files and directories, including the root directory.  Inode 0 is 
 * on the disk, but not used.  Thus s_ninodes = 4 means that 5 bits will be
 * used in the bit map, bit 0, which is always 1 and not used, and bits 1-4
 * for files and directories.  The disk layout is:
 *
 *    Item        # blocks
 *    boot block      1
 *    super block     1    (offset 1kB)
 *    inode map     s_imap_blocks
 *    zone map      s_zmap_blocks
 *    inodes        (s_ninodes + 'inodes per block' - 1)/'inodes per block'
 *    unused        whatever is needed to fill out the current zone
 *    data zones    (s_zones - s_firstdatazone) << s_log_zone_size
 *
 * A super_block slot is free if s_dev == NO_DEV. 
 */

EXTERN struct super_block {
  u32_t s_ninodes;		/* # usable inodes on the minor device */
  zone1_t  s_nzones;		/* total device size, including bit maps etc */
  short s_imap_blocks;		/* # of blocks used by inode bit map */
  short s_zmap_blocks;		/* # of blocks used by zone bit map */
  zone1_t s_firstdatazone_old;	/* number of first data zone (small) */
  short s_log_zone_size;	/* log2 of blocks/zone */
  unsigned short s_flags;	/* FS state flags */
  i32_t s_max_size;		/* maximum file size on this device */
  zone_t s_zones;		/* number of zones (replaces s_nzones in V2) */
  short s_magic;		/* magic number to recognize super-blocks */

  /* The following items are valid on disk only for V3 and above */

  short s_pad2;			/* try to avoid compiler-dependent padding */
  unsigned short s_block_size;	/* block size in bytes. */
  char s_disk_version;		/* filesystem format sub-version */

  /* The following items are only used when the super_block is in memory.
   * If this ever changes, i.e. more fields after s_disk_version has to go to
   * disk, update LAST_ONDISK_FIELD in super.c as that controls which part of the
   * struct is copied to and from disk.
   */
  
  /*struct inode *s_isup;*/	/* inode for root dir of mounted file sys */
  /*struct inode *s_imount;*/   /* inode mounted on */
  unsigned s_inodes_per_block;	/* precalculated from magic number */
  zone_t s_firstdatazone;	/* number of first data zone (big) */
  dev_t s_dev;			/* whose super block is this? */
  int s_rd_only;		/* set to 1 iff file sys mounted read only */
  int s_native;			/* set to 1 iff not byte swapped file system */
  int s_version;		/* file system version, zero means bad magic */
  int s_ndzones;		/* # direct zones in an inode */
  int s_nindirs;		/* # indirect zones per indirect block */
  bit_t s_isearch;		/* inodes below this bit number are in use */
  bit_t s_zsearch;		/* all zones below this bit number are in use*/
  char s_is_root;
} superblock;

#define IMAP		0	/* operating on the inode bit map */
#define ZMAP		1	/* operating on the zone bit map */

/* s_flags contents; undefined flags are guaranteed to be zero on disk
 * (not counting future versions of mfs setting them!)
 */
#define MFSFLAG_CLEAN	(1L << 0) /* 0: dirty; 1: FS was unmounted cleanly */

/* Future compatability (or at least, graceful failure):
 * if any of these bits are on, and the MFS 
 * implementation doesn't understand them, do not mount
 * the FS.
 */
#define MFSFLAG_MANDATORY_MASK 0xff00

/* Maximum Minix MFS on-disk directory filename.
 * MFS uses 'struct direct' to write and parse 
 * directory entries, so this can't be changed
 * without breaking filesystems.
 */
#define MFS_DIRSIZ	60

struct direct {
  uint32_t mfs_d_ino;
  char mfs_d_name[MFS_DIRSIZ];
} __packed;

#include <minix/fslib.h>

/* The type of sizeof may be (unsigned) long.  Use the following macro for
 * taking the sizes of small objects so that there are no surprises like
 * (small) long constants being passed to routines expecting an int.
 */
#define usizeof(t) ((unsigned) sizeof(t))

/* File system types. */
#define SUPER_MAGIC   0x137F	/* magic number contained in super-block */
#define SUPER_REV     0x7F13	/* magic # when 68000 disk read on PC or vv */
#define SUPER_V2      0x2468	/* magic # for V2 file systems */
#define SUPER_V2_REV  0x6824	/* V2 magic written on PC, read on 68K or vv */
#define SUPER_V3      0x4d5a	/* magic # for V3 file systems */

#define V2		   2	/* version number of V2 file systems */ 
#define V3		   3	/* version number of V3 file systems */ 

/* Miscellaneous constants */
#define SU_UID 	 ((uid_t) 0)	/* super_user's uid_t */

#define NO_BIT   ((bit_t) 0)	/* returned by alloc_bit() to signal failure */

#define LOOK_UP            0 /* tells search_dir to lookup string */
#define ENTER              1 /* tells search_dir to make dir entry */
#define DELETE             2 /* tells search_dir to delete entry */
#define IS_EMPTY           3 /* tells search_dir to ret. OK or ENOTEMPTY */  

/* write_map() args */
#define WMAP_FREE	(1 << 0)

#define IGN_PERM	0
#define CHK_PERM	1

#define IN_CLEAN        0	/* in-block inode and memory copies identical */
#define IN_DIRTY        1	/* in-block inode and memory copies differ */
#define ATIME            002	/* set if atime field needs updating */
#define CTIME            004	/* set if ctime field needs updating */
#define MTIME            010	/* set if mtime field needs updating */

#define BYTE_SWAP          0	/* tells conv2/conv4 to swap bytes */

#define END_OF_FILE   (-104)	/* eof detected */

#define ROOT_INODE   ((ino_t) 1)	/* inode number for root directory */
#define BOOT_BLOCK  ((block_t) 0)	/* block number of boot block */
#define SUPER_BLOCK_BYTES  (1024)	/* bytes offset */
#define START_BLOCK ((block_t) 2)	/* first block of FS (not counting SB) */

#define DIR_ENTRY_SIZE       usizeof (struct direct)  /* # bytes/dir entry   */
#define NR_DIR_ENTRIES(b)   ((b)/DIR_ENTRY_SIZE)  /* # dir entries/blk   */
#define SUPER_SIZE      usizeof (struct super_block)  /* super_block size    */

#define FS_BITMAP_CHUNKS(b) ((b)/usizeof (bitchunk_t))/* # map chunks/blk   */
#define FS_BITCHUNK_BITS		(usizeof(bitchunk_t) * CHAR_BIT)
#define FS_BITS_PER_BLOCK(b)	(FS_BITMAP_CHUNKS(b) * FS_BITCHUNK_BITS)

/* Derived sizes pertaining to the V2 file system. */
#define V2_ZONE_NUM_SIZE            usizeof (zone_t)  /* # bytes in V2 zone  */
#define V2_INODE_SIZE             usizeof (d2_inode)  /* bytes in V2 dsk ino */
#define V2_INDIRECTS(b)   ((b)/V2_ZONE_NUM_SIZE)  /* # zones/indir block */
#define V2_INODES_PER_BLOCK(b) ((b)/V2_INODE_SIZE)/* # V2 dsk inodes/blk */

#define NUL(str,l,m) mfs_nul_f(__FILE__,__LINE__,(str), (l), (m))

/* Inode table.  This table holds inodes that are currently in use.  In some
 * cases they have been opened by an open() or creat() system call, in other
 * cases the file system itself needs the inode for one reason or another,
 * such as to search a directory for a path name.
 * The first part of the struct holds fields that are present on the
 * disk; the second part holds fields not present on the disk.
 *
 * Updates:
 * 2007-01-06: jfdsmit@gmail.com added i_zsearch
 */

EXTERN struct inode {
  u16_t i_mode;		/* file type, protection, etc. */
  u16_t i_nlinks;		/* how many links to this file */
  u16_t i_uid;			/* user id of the file's owner */
  u16_t i_gid;			/* group number */
  i32_t i_size;			/* current file size in bytes */
  u32_t i_atime;		/* time of last access (V2 only) */
  u32_t i_mtime;		/* when was file data last changed */
  u32_t i_ctime;		/* when was inode itself changed (V2 only)*/
  u32_t i_zone[V2_NR_TZONES]; /* zone numbers for direct, ind, and dbl ind */
  
  /* The following items are not present on the disk. */
  dev_t i_dev;			/* which device is the inode on */
  ino_t i_num;			/* inode number on its (minor) device */
  int i_count;			/* # times inode used; 0 means slot is free */
  unsigned int i_ndzones;	/* # direct zones (Vx_NR_DZONES) */
  unsigned int i_nindirs;	/* # indirect zones per indirect block */
  struct super_block *i_sp;	/* pointer to super block for inode's device */
  char i_dirt;			/* CLEAN or DIRTY */
  zone_t i_zsearch;		/* where to start search for new zones */
  off_t i_last_dpos;		/* where to start dentry search */
  
  char i_mountpoint;		/* true if mounted on */

  char i_seek;			/* set on LSEEK, cleared on READ/WRITE */
  char i_update;		/* the ATIME, CTIME, and MTIME bits are here */

  LIST_ENTRY(inode) i_hash;     /* hash list */
  TAILQ_ENTRY(inode) i_unused;  /* free and unused list */
  
} inode[NR_INODES];

/* list of unused/free inodes */ 
EXTERN TAILQ_HEAD(unused_inodes_t, inode)  unused_inodes;

/* inode hashtable */
EXTERN LIST_HEAD(inodelist, inode)         hash_inodes[INODE_HASH_SIZE];

EXTERN unsigned int inode_cache_hit;
EXTERN unsigned int inode_cache_miss;


/* Field values.  Note that CLEAN and DIRTY are defined in "const.h" */
#define NO_SEEK            0	/* i_seek = NO_SEEK if last op was not SEEK */
#define ISEEK              1	/* i_seek = ISEEK if last op was SEEK */

#define IN_MARKCLEAN(i) i->i_dirt = IN_CLEAN
#define IN_MARKDIRTY(i) do { if(i->i_sp->s_rd_only) { printf("%s:%d: dirty inode on rofs ", __FILE__, __LINE__); util_stacktrace(); } else { i->i_dirt = IN_DIRTY; } } while(0)

#define IN_ISCLEAN(i) i->i_dirt == IN_CLEAN
#define IN_ISDIRTY(i) i->i_dirt == IN_DIRTY

#undef N_DATA

unsigned int fs_version = 2, block_size = 0;

#define BITSHIFT	  5	/* = log2(#bits(int)) */

#define MAXPRINT	  80	/* max. number of error lines in chkmap */
#define CINDIR		128	/* number of indirect zno's read at a time */
#define CDIRECT		  1	/* number of dir entries read at a time */

#define INODES_PER_BLOCK V2_INODES_PER_BLOCK(block_size)
#define INODE_SIZE ((int) V2_INODE_SIZE)
#define WORDS_PER_BLOCK (block_size / (int) sizeof(bitchunk_t))
#define MAX_ZONES (V2_NR_DZONES+V2_INDIRECTS(block_size)+(long)V2_INDIRECTS(block_size)*V2_INDIRECTS(block_size))
#define NR_INDIRECTS V2_INDIRECTS(block_size)

/* Macros for handling bitmaps.  Now bit_t is long, these are bulky and the
 * type demotions produce a lot of lint.  The explicit demotion in POWEROFBIT
 * is for efficiency and assumes 2's complement ints.  Lint should be clever
 * enough not to warn about it since BITMASK is small, but isn't.  (It would
 * be easier to get right if bit_t was was unsigned (long) since then there
 * would be no danger from wierd sign representations.  Lint doesn't know
 * we only use non-negative bit numbers.) There will usually be an implicit
 * demotion when WORDOFBIT is used as an array index.  This should be safe
 * since memory for bitmaps will run out first.
 */
#define BITMASK		((1 << BITSHIFT) - 1)
#define WORDOFBIT(b)	((b) >> BITSHIFT)
#define POWEROFBIT(b)	(1 << ((int) (b) & BITMASK))
#define setbit(w, b)	(w[WORDOFBIT(b)] |= POWEROFBIT(b))
#define clrbit(w, b)	(w[WORDOFBIT(b)] &= ~POWEROFBIT(b))
#define bitset(w, b)	(w[WORDOFBIT(b)] & POWEROFBIT(b))

#define ZONE_CT 	360	/* default zones  (when making file system) */
#define INODE_CT	 95	/* default inodes (when making file system) */

static struct super_block sb;

#define STICKY_BIT	01000	/* not defined anywhere else */

/* Ztob gives the block address of a zone
 * btoa64 gives the byte address of a block
 */
#define ztob(z)		((block_t) (z) << sb.s_log_zone_size)
#define btoa64(b)	((u64_t)(b) * block_size)
#define SCALE		((int) ztob(1))	/* # blocks in a zone */
#define FIRST		((zone_t) sb.s_firstdatazone)	/* as the name says */

/* # blocks of each type */
#define N_IMAP		(sb.s_imap_blocks)
#define N_ZMAP		(sb.s_zmap_blocks)
#define N_ILIST		((sb.s_ninodes+INODES_PER_BLOCK-1) / INODES_PER_BLOCK)
#define N_DATA		(sb.s_zones - FIRST)

/* Block address of each type */
#define OFFSET_SUPER_BLOCK	SUPER_BLOCK_BYTES
#define BLK_IMAP	2
#define BLK_ZMAP	(BLK_IMAP  + N_IMAP)
#define BLK_ILIST	(BLK_ZMAP  + N_ZMAP)
#define BLK_FIRST	ztob(FIRST)
#define ZONE_SIZE	((int) ztob(block_size))
#define NLEVEL		(V2_NR_TZONES - V2_NR_DZONES + 1)

/* Byte address of a zone */
#define INDCHUNK	((int) (CINDIR * V2_ZONE_NUM_SIZE))
#define DIRCHUNK	((int) (CDIRECT * DIR_ENTRY_SIZE))

#define OK  0
#define NOK 1

char *prog, *device;		/* program name, device name */
int firstcnterr;		/* is this the first inode ref cnt error? */
bitchunk_t *imap, *spec_imap;	/* inode bit maps */
bitchunk_t *zmap, *spec_zmap;	/* zone bit maps */
bitchunk_t *dirmap;		/* directory (inode) bit map */
char *rwbuf;			/* one block buffer cache */
block_t thisblk;		/* block in buffer cache */
char *nullbuf;	/* null buffer */
nlink_t *count;			/* inode count */
int changed;			/* has the diskette been written to? */
struct stack {
  struct direct *st_dir;
  struct stack *st_next;
  char st_presence;
} *ftop;

int dev;			/* file descriptor of the device */

#define DOT	1
#define DOTDOT	2

/* Counters for each type of inode/zone. */
int nfreeinode, nregular, ndirectory, nblkspec, ncharspec, nbadinode;
int nsock, npipe, nsyml, ztype[NLEVEL];
long nfreezone;

int repair, notrepaired = 0, automatic, listing, listsuper;	/* flags */
int preen = 0, markdirty = 0;
int firstlist;			/* has the listing header been printed? */
unsigned part_offset;		/* sector offset for this partition */
char answer[] = "Answer questions with y or n.  Then hit RETURN";

int main(int argc, char **argv);
void initvars(void);
void fatal(char *s);
int eoln(int c);
int yes(char *question);
int atoo(char *s);
int input(char *buf, int size);
char *alloc(unsigned nelem, unsigned elsize);
void printname(char *s);
void printrec(struct stack *sp);
void printpath(int mode, int nlcr);
void devopen(void);
void devclose(void);
void devio(block_t bno, int dir);
void devread(long block, long offset, char *buf, int size);
void devwrite(long block, long offset, char *buf, int size);
void pr(char *fmt, int cnt, char *s, char *p);
void lpr(char *fmt, long cnt, char *s, char *p);
bit_t getnumber(char *s);
char **getlist(char ***argv, char *type);
void lsuper(void);
#define SUPER_GET	0
#define SUPER_PUT	1
void rw_super(int mode);
void chksuper(void);
void lsi(char **clist);
bitchunk_t *allocbitmap(int nblk);
void loadbitmap(bitchunk_t *bitmap, block_t bno, int nblk);
void dumpbitmap(bitchunk_t *bitmap, block_t bno, int nblk);
void fillbitmap(bitchunk_t *bitmap, bit_t lwb, bit_t upb, char
	**list);
void freebitmap(bitchunk_t *p);
void getbitmaps(void);
void putbitmaps(void);
void chkword(unsigned w1, unsigned w2, bit_t bit, char *type, int *n,
	int *report, bit_t);
void chkmap(bitchunk_t *cmap, bitchunk_t *dmap, bit_t bit, block_t
	blkno, int nblk, char *type);
void chkilist(void);
void getcount(void);
void counterror(ino_t ino);
void chkcount(void);
void freecount(void);
void printperm(mode_t mode, int shift, int special, int overlay);
void list(ino_t ino, d2_inode *ip);
int Remove(struct direct *dp);
void make_printable_name(char *dst, char *src, int n);
int chkdots(ino_t ino, off_t pos, struct direct *dp, ino_t exp);
int chkname(ino_t ino, struct direct *dp);
int chkentry(ino_t ino, off_t pos, struct direct *dp);
int chkdirzone(ino_t ino, d2_inode *ip, off_t pos, zone_t zno);
int chksymlinkzone(ino_t ino, d2_inode *ip, off_t pos, zone_t zno);
void errzone(char *mess, zone_t zno, int level, off_t pos);
int markzone(zone_t zno, int level, off_t pos);
int chkindzone(ino_t ino, d2_inode *ip, off_t *pos, zone_t zno, int
	level);
off_t jump(int level);
int zonechk(ino_t ino, d2_inode *ip, off_t *pos, zone_t zno, int level);
int chkzones(ino_t ino, d2_inode *ip, off_t *pos, zone_t *zlist, int
	len, int level);
int chkfile(ino_t ino, d2_inode *ip);
int chkdirectory(ino_t ino, d2_inode *ip);
int chklink(ino_t ino, d2_inode *ip);
int chkspecial(ino_t ino, d2_inode *ip);
int chkmode(ino_t ino, d2_inode *ip);
int chkinode(ino_t ino, d2_inode *ip);
int descendtree(struct direct *dp);
void chktree(void);
void printtotal(void);
void chkdev(char *f, char **clist, char **ilist, char **zlist);
