The following project is a student made filesystem.

FUSE (http://fuse.sourceforge.net/) is a Linux kernel extension that allows for a user space
program to provide the implementations for the various file-related syscalls. This extension was
used to support this project. 

The File system is a two level directory system, with the following restrictions/simplifications:
1. The root directory "\" will only contain other subdirectories, and no regular files
2. The subdirectories will only contain regular files, and no subdirectories of their own
3. All files will be full access (i.e. chmod 0666), with permissions mainly ignored
4. Many file attributes such as creation and modification time will not be accurately stored
5. Files cannot be truncated