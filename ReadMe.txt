######### Steps to install Minix Image for File Recovery System ##############

Unzip FileRecoverSystem.zip

1.	If you want to use boot image then
	a.	Copy the files in 3.2.0 directory to /boot/minix_latest/ directory in your minix machine.
	b.	Reboot the minix machine.

(or)

2.	If you want to compile enhanced minix from scratch
	a.	 Goto kernel/ directory
	b.	unistd.h ? Added syscall declarations. Copy the file (or the changes) to /usr/include/ and /usr/src/include
	c.	callnr.h ? Added syscall numbers. Copy the file (or the changes) to /usr/include/minix and /usr/src/include/minix
	d.	vfsif.h ? vfs specific. Copy the file (or the changes) to /usr/include/minix and /usr/src/include/minix
	e.	vfs/proto.h ? Copy the file (or the changes) to /usr/src/servers/vfs
	f.	vfs/table.c ? Added syscalls in call vector in VFS. Copy the file (or the changes) to /usr/src/servers/vfs
	g.	vfs/request.c ? For communication with MFS. Copy the file (or the changes) to /usr/src/servers/vfs
	h.	vfs/myvfslink.c ? Added syscalls in call vector in VFS. Copy the file (or the changes) to /usr/src/servers/vfs
	i.	vfs/Makefile ? Add the changes to /usr/src/servers/vfs
	j.	mfs/proto.h ?  Copy the file (or the changes) to /usr/src/servers/mfs
	k.	mfs/table.c ? Added syscalls in call vector in MFS. Copy the file (or the changes) to /usr/src/servers/mfs
	l.	mfs/mymfslink.c ? Added syscalls in call vector in MFS. Copy the file (or the changes) to /usr/src/servers/mfs
	m.	mfs/Makefile ? Add the changes to /usr/src/servers/mfs

************************
COMPILATION:
************************

1.	Kernel
	a.	goto /usr/src/tools
		make hdboot
		reboot
2.	User tools
	a.	Copy damagetool to some directory in minix machine.
		i.	cd damagetool
		ii.	make
	b.	Copy recovertool to some directory in minix machine.
		i.	cd recovertool
		ii.	make
		
***************************
STEPS TO RUN THE PROGRAM:
***************************

Damagetool
1.	goto damagetool directory
	a.	./damageFileSysteTool
2.	When the program is executed you will see below options. Pick appropriate options.
	# ./damageFileSystemTool
	DAMAGE FILE SYSTEM TOOL – please select your choice
	0.	Delete the file without damaging the folder
	1.	Damage the inode bit Map by removing the file.
	2.	Currupt the Directory file completely.
	3.	Damage the inode time to damage the inode bit map.
	4.	Damage the zone bit map.
	5.	Damage the Directory file by corrupting its inode completely
	6.	Exit
	Enter your choice -> 
