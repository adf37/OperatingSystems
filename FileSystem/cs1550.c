/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;
cs1550_root_directory root;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long)) 

struct cs1550_disk_block
{
	//The next disk block, if needed. This is the next pointer in the linked 
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*Gets the directory entry in a given directory at the passed in position in .disk
Returns -1 on failure and 0 on success*/
static int get_dir(cs1550_directory_entry *dir, int pos){
	int r_value = -1;
	FILE *file = fopen(".disk", "r");
	if (file == NULL){
		return r_value; //failure, couldn't open file
	}
	int seek = fseek(file, sizeof(cs1550_directory_entry)*pos, SEEK_SET);
	if (seek == -1){
		return r_value;
	}
	int read = fread(dir, sizeof(cs1550_directory_entry), 1, file);
	if (read == 1){
		r_value = 0;
	}
	fclose(file);
	return r_value;
}

//Uses the get_dir method to find the correct directory entry and position
static int find_directory(char *dir, cs1550_root_directory *root){
	cs1550_directory_entry temp_dir;
	//int pos = 0;
	int r_value = -1;
	int i = 0;
	//printf("Directories total in find_directory: %i\n", root->nDirectories);
	for (i=0; i<root->nDirectories; i++){
		//printf("Check point\n");
		if ((get_dir(&temp_dir, i) == 0) && (r_value == -1)){
			//printf("Dir: %s\tDname:%s\n", dir, root->directories[i].dname);
			if (strcmp(dir, root->directories[i].dname) == 0){
				r_value = i;
				break;
			}
		}
	}
	return r_value; //returns position on success, -1 on failure
}
//Updates the directory entry specified at the position most likely with new contents then the one intially created
//Writes new directory entry to .disk
static int write_dir_pos(cs1550_directory_entry *entry, int position){
	int res = -1;
	//int i=0;
	cs1550_directory_entry temp;
	if (get_dir(&temp, position) == 0){
		FILE *f = fopen(".disk", "r+");
		fseek(f, sizeof(cs1550_directory_entry)*position, SEEK_SET);
		fwrite(entry, sizeof(cs1550_directory_entry), 1, f);
		fclose(f);
		res = 0;
	}
	//printf("Res in WDP: %i\n", res);
	return res;
}
//Reads a block from disk at a given position
//returns -1 on failure and 0 on success
static int read_block(cs1550_disk_block *block, int pos){
	int r_value = -1;
	FILE *f = fopen(".disk", "r");
	if (f== NULL){
		return r_value;
	}
	int seek = fseek(f, sizeof(cs1550_disk_block)*pos, SEEK_SET);
	if (seek == -1){
		return r_value;
	}
	int read = fread(block, sizeof(cs1550_disk_block), 1, f);
	if (read == 1){
		r_value = 0;
	}
	fclose(f);
	return r_value;
	
}
//Writes a block to the disk at the given position
//Returns -1 on failure and 0 on success
static int write_block(cs1550_disk_block *block, int pos){
	int r_value = -1;
	FILE *f = fopen(".disk", "r+");
	if (f == NULL){
		return r_value;
	}
	int seek = fseek(f, sizeof(cs1550_disk_block)*pos, SEEK_SET);
	if (seek == -1){
		return r_value;
	}
	int write = fwrite(block, sizeof(cs1550_disk_block), 1, f);
	if (write == 1){
		r_value = 0;
	}
	fclose(f);
	return r_value;
}
/*Transfers data from the block to a buffer
Returns the remaining amount of data remaining on success
and returns 0 if nothing was left to transfer
*/
static int block_buffer(cs1550_disk_block *block, char *buffer, int pos, int remaining){
	while (remaining>0){
		if (pos>MAX_DATA_IN_BLOCK){
			return remaining;
		}
		else{
			*buffer = block->data[pos];
			buffer++;
			remaining--;
			pos++;
		}
	}
	return remaining;
}
/*Transfers data from the buffer to a section of data on the block
Returns the amount of data remaining on success and 0 if there is no data remaining
*/
static int buffer_block(cs1550_disk_block *block, const char *buffer, int position, int remaining){
	while(remaining>0){
		if(position > MAX_DATA_IN_BLOCK){
			return remaining;
		}
		else{
			block->data[position] = *buffer;
			buffer++;
			remaining--;
			position++;
		}
	}
	return remaining;
}
//Allocates a new block 
//Used when creating a new file in mknod
//Returns position of new block on success and -1 on failure
static int allocate(void){
	int bposition = -1;
	int new_position;
	int seek, read, write;
	FILE *f = fopen(".disk", "r+");
	if (f == NULL){
		return -1;
	}
	seek = fseek(f, -sizeof(cs1550_disk_block), SEEK_END);
	if (seek == -1){
		return -1;
	}
	read = fread(&bposition, sizeof(int), 1, f);
	if (read == -1){
		return -1;
	}
	new_position = bposition +1;
	write = fwrite(&new_position, 1, sizeof(int), f);
	if (write == -1){
		return -1;
	}
	fclose(f);
	return new_position;
}
//Deallocates a block of memory from the disk
//Returns new block position on success and -1 on failure
static int deallocate(void){
	int bposition = -1;
	int new_position;
	int seek, read, write;
	FILE *f = fopen(".disk", "r+");
	if (f==NULL){
		return -1;
	}
	seek = fseek(f, -sizeof(cs1550_disk_block), SEEK_END);
	if (seek == -1){
		return -1;
	}
	read = fread(&bposition, sizeof(int), 1, f);
	if (read == -1){
		return -1;
	}
	new_position = bposition - 1;
	write = fwrite(&new_position, 1, sizeof(int), f);
	if (write == -1){
		return -1;
	}
	fclose(f);
	return new_position;
}

/* NEEDS TO BE DONE BY APRIL 12TH
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
	Looks up the input path to determine if it is a directory or a file. If it is a 
	directory, return the appropriate permissions. If it is a file, returns the appropiate
	permissions along with size. 
	Returns 0 on success and -ENOENT if the file is not found
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = -ENOENT; //return value
	cs1550_directory_entry current;
	//cs1550_root_directory root;
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_FILENAME+1];
	int pos; //position in directory
	memset(stbuf, 0, sizeof(struct stat));
   
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		res = 0;
	} else { //not the root directory
		memset(directory, 0, MAX_FILENAME+1);
		memset(filename, 0, MAX_FILENAME+1);
		memset(extension, 0, MAX_FILENAME+1);
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		pos = find_directory(directory, &root); //find the position of directory
		if (pos != -1){ //no errors in finding_directory
			get_dir(&current, pos); //get directory at that position
			if (directory != NULL && (filename[0] == '\0')){ //Check if subdirectory
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				res = 0; //no error
			}
			else{ //check if name is a regular file
				int i=0;
				for (i=0; i<current.nFiles; i++){
					if (strcmp(current.files[i].fname, filename) == 0){ //check if file names matches
						if (strcmp(current.files[i].fext, extension) == 0){ //check if extensions match
							stbuf->st_mode = S_IFREG | 0666; 
							stbuf->st_nlink = 1; //file links
							stbuf->st_size = current.files[i].fsize; //set size to current files size
							res = 0; // no error
							break;
						}
					}
				}
			}
		}
		//else{ //else return path doesn't exist
			//printf("Doesn't exist error\n");
			//res = -ENOENT;
		//}
	}
	return res;
}

/* NEEDS TO BE DONE BY APRIL 12TH
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 Looks up an input path, ensuring it is a directory then lists the contents
 Returns 0 on success and -ENOENT if the directory is not valid/found
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{	
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;
	int r_value = 0, pos =0;
	char buffer[50];
	cs1550_directory_entry current;
	//cs1550_root_directory root;
	
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_FILENAME+1];
	memset(directory, 0, MAX_FILENAME+1);
	memset(filename, 0, MAX_FILENAME+1);
	memset(extension, 0, MAX_EXTENSION+1);
	
	if (strcmp(path, "/") != 0){ //not just the root 
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		pos = find_directory(directory, &root); //get position of directory
		if ((directory != NULL) && (pos != -1)){ //if there is a directory
			get_dir(&current, pos); //get the correct directory
			filler(buf, ".", NULL, 0); //fill the buffer
			filler(buf, "..", NULL, 0); //fill the buffer
			int i = 0;
			for (i=0; i<current.nFiles; i++){ //if it has files write that out to the buffer
				if (strlen(current.files[i].fext)>0){ //if there is an extension right filename.extension to buffer
					sprintf(buffer, "%s.%s", current.files[i].fname, current.files[i].fext);
				}
				else{ //no extension, fill buffer with filename
					sprintf(buffer, "%s", current.files[i].fname);
				}
				filler(buf, buffer, NULL, 0);
			}
			r_value = 0;
		}
		else{
			r_value = -ENOENT;
		}
	}
	else{ //is just the root so write out sub-directories
		int i = 0;
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
			int c = 0;
			for (c = 0; c<root.nDirectories; c++){ 
				filler(buf, root.directories[i].dname, NULL, 0); //fills buffers with names of directories
			}
		r_value = 0;
	}
	return 0;

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
}

/* NEEDS TO BE DONE BY APRIL 12TH
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 Adds a new directory to the root level and updates the disk
 Returns 0 on success and one of three error codes on failure
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_FILENAME+1];
	int r_value = 0, res=0;
	cs1550_directory_entry temp;
	//cs1550_root_directory root;
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if (directory == NULL || directory[0] == '\0'){ //if the directory is not specified
		r_value = -EPERM;
	}
	else{
		if (find_directory(directory, &root) == -1){ //if directory doesn't already exist
		//printf("Directory doesn't already exist\n");
			if (strlen(directory)<= MAX_FILENAME){ //if filename is short enough
				temp.nFiles = 0;
				cs1550_directory_entry check;
				int i=0;
				while(get_dir(&check, i) != 0){ 
					//printf("Check point #1\n");
					if (check.nFiles<=0){
						//printf("check point\n");
						strcpy(root.directories[i].dname, directory);
						root.nDirectories += 1;
						FILE *f = fopen(".disk", "r+");
						fseek(f, sizeof(cs1550_directory_entry)*i, SEEK_SET);
						fwrite(&temp, sizeof(cs1550_directory_entry), 1, f); //write out to the disk the new directory entry
						fclose(f);
						res = 1;
						r_value = 0;
						//printf("Completed code segment\n");
					}
					i++;
				}
				if (res == 0){
					//printf("Check point #2\n");
					//printf("Root directory at 0: %s\n", root.directories[0].dname);
					strcpy(root.directories[0].dname, directory);
					root.nDirectories += 1;
					//printf("Directories total= %i\n", root.nDirectories);
					//printf("Root directory at 0: %s\n", root.directories[0].dname);
					//printf("Directory: %s\n", directory);
					FILE *f = fopen(".disk", "a");
					fwrite(&temp, sizeof(cs1550_directory_entry), 1, f);
					res = 1;
					fclose(f);
					r_value = 0;
					//printf("Completed check 2\n");
				}
			}
			else{ //error: name too long
				r_value = -ENAMETOOLONG;
			}
		}
		else{ //directory already exists
			r_value = -EEXIST;
		}
	}
	//printf("R-value: %i\n", r_value);
	return r_value;
}

/* 
 * Removes a directory. DO NOT MODIFY
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
	
    return 0;
}

/* NEEDS TO BE DONE BY APRIL 19TH
 * Does the actual creation of a file. Mode and dev can be ignored.
	Creates a new file under the current directory and allocates a block of memory 
	for the new file on the bitmap
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_FILENAME+1];
	memset(directory, 0, MAX_FILENAME+1);
	memset(filename, 0, MAX_FILENAME+1);
	memset(extension, 0, MAX_EXTENSION+1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	int r_value = 0, res = -1;
	int pos = 0;
	if (directory != NULL){ //directory specified
		pos = find_directory(directory, &root); //get position of directory
		//printf("Directory %s pos: %i\n", directory, pos);
		if (strlen(filename) > MAX_FILENAME){ //filename is too long
			r_value = -ENAMETOOLONG;			
		}
		else if (extension != NULL && strlen(extension)> MAX_EXTENSION){ //extension name is too long
			r_value = -ENAMETOOLONG;
		}
		else{
			cs1550_directory_entry current;
			get_dir(&current, pos);
			//////////////////////////get-file-position function
			int i=0;
			for (i=0; i<current.nFiles; i++){ //look through searching if file exists
				if (directory != NULL && (strcmp(filename, current.files[i].fname) == 0) && extension == NULL){
					res = i;
				}
				else if (directory != NULL && extension != NULL && (strcmp(filename, current.files[i].fname) == 0) && (strcmp(current.files[i].fext, extension) == 0)){
					res = i;
				}
			}		
			//////////////////////
			//printf("Res: %i\n", res);
			if (res == -1){ //file doesn't already exist
				strcpy(current.files[current.nFiles].fname, filename); //copy filename to end of array of files
				if (extension != NULL){ //file has an extension
					//printf("Extension not null\n");
					strcpy(current.files[current.nFiles].fext, extension); //copy extension
				}
				else{
					//printf("Extension is null\n");
					strcpy(current.files[current.nFiles].fext, "\0");
				}
				current.files[current.nFiles].nStartBlock = allocate(); //allocate a block for the new file
				//printf("Start block %ld\n", current.files[current.nFiles].nStartBlock);
				if (current.files[current.nFiles].nStartBlock == -1){ //space was not available error
					return -1; 
				}
				current.files[current.nFiles].fsize = 0;
				current.nFiles += 1;
				//printf("nFiles: %i\n", current.nFiles);
				write_dir_pos(&current, pos); //update directory with new file now allocated
				r_value = 0;
			}
			else{
				//printf("Reached EEXIST\n");
				r_value = -EEXIST; //file already exists
			}
		}
	}
	else{
		r_value = -EPERM; //file is trying to be created in the root directory
	}
	return r_value;
}

/* NEEDS TO BE DONE BY APRIL 19TH
 * Deletes a file
	Finds the directory specified and searches for the given file in that directory
	If found removes the file from the directory entry's files and updates the directory
	Returns 0 on success and -1 on failure
 */
static int cs1550_unlink(const char *path)
{
    (void) path;
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_FILENAME+1];
	memset(directory, 0, MAX_FILENAME+1);
	memset(filename, 0, MAX_FILENAME+1);
	memset(extension, 0, MAX_EXTENSION+1);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	cs1550_directory_entry current;
	int r_value = 0;
	int pos = find_directory(directory, &root); //get position of the directory in the root
	//printf("Directory %s filename %s extension %s\n", directory, filename, extension);
	//printf("Pos: %i\n", pos);
	if (pos == -1){ //directory wasn't found
		r_value = -ENOENT;
	}
	get_dir(&current, pos); //get the directory contents
	if (strcmp(directory, filename) == 0){ //directory and filename are the same
		r_value = -EISDIR; //this is a directory
	}
	else{
		if (filename != NULL){ //if a file was specified
		//printf("filename not null\n");
			int i=0;
			int res = -1;
			//printf("nfiles: %i\n", current.nFiles);
			for (i=0; i<current.nFiles; i++){ //search for file in directory
				if (directory != NULL && (strcmp(filename, current.files[i].fname) == 0) && extension == NULL){
					res = i;
				}
				else if (directory != NULL && extension != NULL && (strcmp(filename, current.files[i].fname) == 0) && (strcmp(current.files[i].fext, extension) == 0)){
					res = i;
				}
			}
			//printf("res %i\n", res);
			if (res != -1){ //file was found
				//printf("\nCheck point\n");
				get_dir(&current, pos);
				if (current.nFiles == 0){ 
					return -1; //no files in the directory to delete 
				}
				else{
					current.files[res].nStartBlock = deallocate(); //deallocate the block taken up by the file
					current.nFiles--;
					current.files[res].fsize = -1;
					write_dir_pos(&current, pos); //update the directory
				}
			}
			else{
				r_value = -ENOENT; //file not found
			}
		}
		else{
			return -ENOENT; //filename was null
		}
	}

    return r_value;
}

/* NEEDS TO BE DONE BY APRIL 19TH 
 * Read size bytes from file into buf starting from offset
 Returns size of read on success and on failure returns -EISDIR
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;
	int r_value = -1;
	
	int temp_offset = offset; //make a copy of offset
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];
	cs1550_directory_entry temp_dir;
	cs1550_disk_block temp_block;
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if (offset > size || size <= 0){ //offset is greater than size so throw error
		return -1;
	}
	if (strcmp(directory, filename) == 0){ //path is a directory
		return -EISDIR;
	}
	int data = size - offset; //amount of data that will need to be read
	//printf("data: %i\n", data);
	
	//printf("Directory %s Filename %s Extension %s\n", directory, filename, extension);
	if (directory != NULL){
		if (strlen(filename) < MAX_FILENAME){
			if ((extension == NULL || (extension != NULL && strlen(extension) <= MAX_EXTENSION))){
				int dir_pos = find_directory(directory, &root); //get directory's position
				if (dir_pos == -1){ //couldn't find directory
					r_value = -1;
				}
				int dir_return = get_dir(&temp_dir, dir_pos); //get the directory
				if (dir_return == -1){ //couldn't get the directory
					r_value = -1;
				}
				int i=0;
				int file_return = -1;
				for (i=0; i<temp_dir.nFiles; i++){ //find file in directory
					if (directory != NULL && (strcmp(filename, temp_dir.files[i].fname) == 0) && extension == NULL){
						file_return = i;
					}
					else if (directory != NULL && extension != NULL && (strcmp(filename, temp_dir.files[i].fname) == 0) && (strcmp(temp_dir.files[i].fext, extension) == 0)){
						file_return = i;
					}
				}
				//printf("Read: file return %i\n", file_return);
				if (file_return != -1){ //if file was found
					//printf("File was found and fsize is: %zi\n", temp_dir.files[file_return].fsize);
					if (temp_dir.files[file_return].fsize == 0){ //file's size is 0 so just return now
						return 0;
					}
					int num_block = temp_dir.files[file_return].nStartBlock; //get starting block of our file
					//printf("num_block in read: %i\n", num_block);
					while (offset<size){
						int rblock = read_block(&temp_block, num_block); //read block from bitmap starting at num_block
						//printf("Read block rblock: %i\n", rblock);
						if (temp_offset >= MAX_DATA_IN_BLOCK){ //outside of 512MB
							num_block = temp_block.nNextBlock; //get the next block allocated
							temp_offset = temp_offset - MAX_DATA_IN_BLOCK; //keep subtracting until temp_offset is less than 512MB
						}
						else{
							int buf_return = block_buffer(&temp_block, buf, temp_offset, data); //transfer data from block to buffer
							//printf("buf_return: %i\n", buf_return);
							temp_offset =0;
							if (buf_return == 0){ //success
								break;
							}
							else{
								num_block = temp_block.nNextBlock; //get next block 
								offset += MAX_DATA_IN_BLOCK;
							}
						}
					}
					r_value = size; //set return value to size on success
					//printf("Size in read: %zi\n", size);
				}
			}
		}
	}	
	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error

	//size = 0;

	return r_value;
}

/* NEEDS TO BE DONE BY APRIL 19TH
 * Write size bytes from buf into file starting from offset
 Returns size on success and -EFBIG if the offset is beyond the file size
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;
	int temp_offset = offset; //temporary hold on offset value
	int r_value = 0;
	char directory[MAX_FILENAME+1];
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];
	cs1550_directory_entry temp_dir;
	cs1550_disk_block tBlock;
	int read, write;
	memset(&tBlock, 0, sizeof(tBlock));
	if ((offset > size) || (size <= 0)){ //offset was bigger than size, error
		return -EFBIG;
	}
	int data = size - offset;
	//printf("Data in write: %i\n", data);
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	//printf("In write Directory %s filename %s extension %s\n", directory, filename, extension);
	if(directory != NULL){ 
		if ((filename != NULL) && (filename[0] != '\0')){
			if (extension == NULL || (extension != NULL && (strlen(extension)<= MAX_EXTENSION))){
					int pos = find_directory(directory, &root); //get the position of our directory
					if (pos == -1){ //failed to find the directory
						return -1;
					}
					int dir_return = get_dir(&temp_dir, pos); //get our directory at that position
					if (dir_return == -1){ //error in getting directory contents
						return -1;
					}
					int i=0;
					int file_return = -1;
					//get our file position
					for (i=0; i<temp_dir.nFiles; i++){
						if (directory != NULL && (strcmp(filename, temp_dir.files[i].fname) == 0) && extension == NULL){
							file_return = i;
						}
						else if (directory != NULL && extension != NULL && (strcmp(filename, temp_dir.files[i].fname) == 0) && (strcmp(temp_dir.files[i].fext, extension) == 0)){
							file_return = i;
						}
					}
					//printf("Write file return %i\n", file_return);
					if (file_return != -1){ //file was found
					//printf("Size in write: %zi\n", size);
						if (size == 0){ //size of file is 0 so just return
							return 0;
						}
						//size was greater than zero so proceed
						int num_block = temp_dir.files[file_return].nStartBlock; //get start block of our data
						//printf("Write: Start Block %i\n", num_block);
						//printf("num_block in write: %i\n", num_block);
						temp_dir.files[file_return].fsize = size; //copy size of write to file's size
						write_dir_pos(&temp_dir, pos); //write our updated directory to the disk
						//printf("Temp offset %i\n", temp_offset);
						while (temp_offset >= MAX_DATA_IN_BLOCK){ //offset is outside of max data in block
							num_block = tBlock.nNextBlock; //get next block
							temp_offset -= MAX_DATA_IN_BLOCK; //subtract to lower offset
							read = read_block(&tBlock, num_block); //read next block
						}
						while (offset <= size){ //offset is less than or equal to size so proceed
							if (temp_offset > MAX_DATA_IN_BLOCK){ 
								//printf("\nReached\n");
								num_block = tBlock.nNextBlock;
								temp_offset -= MAX_DATA_IN_BLOCK;
							}
							else{
								int buf_return = buffer_block(&tBlock, buf, temp_offset, data); //transfer data from buffer to block
								if (buf_return != 0 && (tBlock.nNextBlock <= 0)){
									tBlock.nNextBlock = allocate(); //allocate a new block in our bitmap for the next block we need
								}
								write = write_block(&tBlock, num_block); //write our block to the bitmap
								//printf("Write to block: %i\n", write);
								temp_offset = 0;
								if (buf_return == 0){ //success
									break;
								}
								else{
									num_block = tBlock.nNextBlock; //get next block in list
									offset += MAX_DATA_IN_BLOCK; 
									read = read_block(&tBlock, num_block); //read block from the bitmap
									buf += MAX_DATA_IN_BLOCK;
								}
							}
						}
						r_value = size; //success so set our return value as the size
						//printf("Size in write: %zi\n", size);
					}
			}
		}else{
			r_value = -EISDIR; //tried to work with a directory and not a file
		}
		
	}

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//set size (should be same as input) and return, or error

	return r_value;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
