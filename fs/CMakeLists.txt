
add_kernel_module(
	NAME ext2fs
	SOURCES
		ext2/ext2.cpp
		ext2/inode.cpp
)

add_kernel_module(
	NAME tmpfs
	SOURCES
		tmpfs/dir.cpp
		tmpfs/tmpfs.cpp
		tmpfs/inode.cpp
		tmpfs/superblock.cpp
)

add_kernel_module(NAME console SOURCES console.cpp)
add_kernel_module(NAME tty SOURCES tty.cpp)
add_kernel_module(NAME pty SOURCES pty.cpp)
