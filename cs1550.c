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
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)

struct cs1550_disk_block
{
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

static void tokenPath(const char *path, char *directory, char *filename, char *extension) {
	strcpy(directory, "");
	strcpy(filename, "");
	strcpy(extension, "");

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
}

static struct cs1550_root_directory readDisk() {
	FILE *fp = fopen(".disk", "rb+");
	fseek(fp, 0, SEEK_SET);
	cs1550_root_directory root;
	root.nDirectories = 0;
	memset(&root.directories, 0, MAX_DIRS_IN_ROOT*sizeof(struct cs1550_directory));
	fread(&root, BLOCK_SIZE, 1, fp);
	fclose(fp);

	return root;
}

static cs1550_directory_entry readDirectory(long location) {
	FILE *fp = fopen(".disk", "rb+");
	fseek(fp, location, SEEK_SET);
	cs1550_directory_entry currDir;
	currDir.nFiles = 0;
	memset(&currDir.files, 0, MAX_FILES_IN_DIR*sizeof(struct cs1550_file_directory));
	fread(&currDir, BLOCK_SIZE, 1, fp);
	fclose(fp);

	return currDir;
}

static void readFile(char *buf, long location, size_t size) {
	FILE *fp = fopen(".disk", "rb");
	fseek(fp, location, SEEK_SET);
	fread(buf, size, 1, fp);
	fclose(fp);
}

static int isContainDir(char *directory) {
	cs1550_root_directory root = readDisk();
	int i;
	for(i = 0 ; i < MAX_DIRS_IN_ROOT ; i++) {
		if(strcmp(root.directories[i].dname, directory) == 0)
			return 1;
	}
	return 0;
}

static int isContainFile(char *directory, char *filename, char *extension) {
	cs1550_root_directory root = readDisk();
	int i;
	int j;
	if(strlen(filename) == 0) return 0;
	for(i = 0 ; i < MAX_DIRS_IN_ROOT ; i++) { //
		if(strcmp(root.directories[i].dname, directory) == 0) {
			struct cs1550_directory_entry currDir;
			currDir = readDirectory(root.directories[i].nStartBlock);
			for(j = 0 ; j < MAX_FILES_IN_DIR ; j++) { //
				if(strlen(extension) == 0) {
					if(strcmp(currDir.files[j].fname, filename) == 0)
						return 1;
					}
					else if (strcmp(currDir.files[j].fname, filename) == 0 && strcmp(currDir.files[j].fext, extension) == 0)
						return 1;
			}
		}
	}
	return 0;
}

static void writeMultiBlock(const void *block, size_t size, size_t location) {
	FILE *fp = fopen(".disk", "rb+");
	fseek(fp, location, SEEK_SET);
	fwrite(block, size, 1, fp);
	fclose(fp);
}

static void writeBlock(const void *block, size_t times, size_t location ) {
	FILE *fp = fopen(".disk", "rb+");
	fseek(fp, location, SEEK_SET);
	fwrite(block, BLOCK_SIZE, times, fp);
	fclose(fp);
}

static void writeBitmap(const void *block) {
	FILE *fp = fopen(".disk", "rb+");
	fseek(fp, -BLOCK_SIZE, SEEK_END);
	fwrite(block, BLOCK_SIZE, 1, fp);
	fclose(fp);
}

static void printBitmap(unsigned char *bitmap) {
	int i;
	int j;
	printf("output the bitmap ---------------------------\n");
	for(i = 0 ; i < BLOCK_SIZE ; i++) {
		for(j = 7 ; j >= 0 ; j--) {
			printf("%d",(bitmap[i] & 1<<j)!=0);
		}
	}
	printf("output the bitmap ---------------------------\n");
}

static void initializeDisk(unsigned char bitmap[BLOCK_SIZE]) {
	cs1550_root_directory root;
	root.nDirectories = 0;
	memset(&root.directories, 0, MAX_DIRS_IN_ROOT*sizeof(struct cs1550_directory));
	writeBlock(&root, 1, 0);
	bitmap[0] = 128;
	writeBitmap(bitmap);
}

static void setBit(unsigned char *bitmap, long location) {
	int indexOfLocation = location/BLOCK_SIZE;
	int bytes = indexOfLocation/8;
	int index = 7 - indexOfLocation%8;
	char currByte = bitmap[bytes] | 1<<index;
	bitmap[bytes] = currByte;
}

static void updateBitmap(long location) {
	int startReading = 0 - BLOCK_SIZE;
	FILE *fp = fopen(".disk", "rb");
	fseek(fp, startReading, SEEK_END);
	unsigned char bitmap[BLOCK_SIZE];
	fread(&bitmap, sizeof(char), BLOCK_SIZE, fp);
	fclose(fp);

	setBit(bitmap, location);
	writeBitmap(bitmap);
	// printBitmap(bitmap);
}

static int getBlockAddress() {
	int j;
	int i;
	int startReading = 0 - BLOCK_SIZE;
	FILE *fp = fopen(".disk", "rb");
	fseek(fp, startReading, SEEK_END);
	unsigned char bitmap[BLOCK_SIZE];
	fread(&bitmap, sizeof(char), BLOCK_SIZE, fp);

	if((bitmap[0] & 128) == 0) {
		initializeDisk(bitmap);
	}
	int bit;
	for(i = 0 ; i < BLOCK_SIZE ; i++) {
		for(j = 7 ; j >= 0 ; j--) {
			bit = ((bitmap[i] & 1<<j) != 0);
			if(!bit) break;
		}
		if(!bit) break;
	}
	fclose(fp);
	return (i*8 + (7-j)) * BLOCK_SIZE;
}

static size_t getFileSize(char* directory, char* filename, char* extension) {
	cs1550_root_directory root = readDisk();
	cs1550_directory_entry currDir;
	int i;
	for(i = 0 ; i < root.nDirectories ; i++) {
		if(strcmp(root.directories[i].dname, directory) == 0) {	// find the directory in root
			currDir = readDirectory(root.directories[i].nStartBlock);
			break;
		}
	}
	for(i = 0 ; i < currDir.nFiles ; i++) {
		if(strcmp(currDir.files[i].fname, filename) == 0) {
			if(strlen(extension) != 0) {
				if(strcmp(currDir.files[i].fext, extension) != 0)
					return -EPERM;
				}
			return currDir.files[i].fsize;
		}
	}
	return 0;
}
/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{

	// printf("---Call function getattr---\n");

	int res = 0;
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];

	tokenPath(path, directory, filename, extension);

	// printf("---Dir: %s File: %s Ext: %s---\n", directory, filename, extension);
	// printf("---Dir: %d File: %d Ext: %d---\n", (int)strlen(directory), (int)strlen(filename), (int)strlen(extension));

	memset(stbuf, 0, sizeof(struct stat));

	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		if(isContainDir(directory) == 1 && strlen(filename) == 0 && strlen(extension) == 0) {  //Check if name is subdirectory
			//Might want to return a structure with these fields
			stbuf->st_mode = S_IFDIR | 0755;
			stbuf->st_nlink = 2;
		} else if(isContainFile(directory, filename, extension))  {	//Check if name is a regular file
			//regular file, probably want to be read and write
			size_t size = getFileSize(directory, filename, extension);
			stbuf->st_mode = S_IFREG | 0666;
			stbuf->st_nlink = 1; //file links
			stbuf->st_size = size; //file size - make sure you replace with real size!
		} else {  //Else return that path doesn't exist
			res = -ENOENT;
		}
	}
	return res;
}

/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	// printf("---Call function readdir---\n");
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	struct cs1550_root_directory root = readDisk();
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	tokenPath(path, directory, filename, extension);

	//This line assumes we have no subdirectories, need to change
	// if (strcmp(path, "/") != 0)
	// 	return -ENOENT;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	if (strcmp(path, "/") == 0) {
		int i;
		for(i = 0 ; i < MAX_DIRS_IN_ROOT ; i++) {
			if(strcmp(root.directories[i].dname, "") != 0) {
				filler(buf, root.directories[i].dname, NULL, 0);
			}
		}
		return 0;
	} else {
		struct cs1550_directory dir;
		struct cs1550_directory_entry currDir;
		int i = 0;
		int check = 0;
		int j;

		for(i = 0 ; i < MAX_DIRS_IN_ROOT ; i++) {
			if(strcmp(directory, root.directories[i].dname) == 0) {
				dir = root.directories[i];
				check = 1;
				break;
			}
		}
		if(check) {
			currDir = readDirectory(dir.nStartBlock);
			for(j = 0 ; j < currDir.nFiles ; j++) {
				char result[MAX_FILENAME+1];
				strcpy(result, currDir.files[j].fname);
				if(strcmp(currDir.files[j].fext, "") != 0) {
					strcat(result, ".");
					strcat(result, currDir.files[j].fext);
				}
				filler(buf, result, NULL, 0);
			}
		}
	}
	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	// printf("---Call function mkdir---\n");
	(void) path;
	(void) mode;

	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];

	tokenPath(path, directory, filename, extension);

	if(strlen(directory) > MAX_FILENAME) {
		return -ENAMETOOLONG;
	}	else if (isContainDir(directory) == 1) {
		return -EEXIST;
	} else if (strlen(directory) == 0 || strlen(filename) != 0) {
		return -EPERM;
	} else {
		cs1550_root_directory root = readDisk();
		if(root.nDirectories < MAX_DIRS_IN_ROOT) {
			long blockAddress = getBlockAddress();
			if(blockAddress != -1) {
				strcpy(root.directories[root.nDirectories].dname, directory);
				root.directories[root.nDirectories].nStartBlock = (long)blockAddress;
				root.nDirectories = root.nDirectories + 1;
				cs1550_directory_entry newDirEntry;
				newDirEntry.nFiles = 0;
				updateBitmap(blockAddress);
				writeBlock(&root, 1, 0);
				writeBlock(&newDirEntry, 1, blockAddress);
			}
		} else {
			return -ENOSPC;
		}
	}
	return 0;
}

/*
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	// printf("---Call function mknod---\n");
	(void) mode;
	(void) dev;

	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];

	tokenPath(path, directory, filename, extension);

	if(strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION) {
		return -ENAMETOOLONG;
	}	else if (isContainFile(directory, filename, extension) == 1) {
		return -EEXIST;
	} else if (strlen(directory) == 0) {
		return -EPERM;
	} else {
		cs1550_root_directory root = readDisk();
		int i;
		cs1550_directory_entry currDir;
		long dirNStartBlock = -1;
		for(i = 0 ; i < MAX_DIRS_IN_ROOT ; i++) {
			if(strcmp(root.directories[i].dname, directory) == 0) {
				currDir = readDirectory(root.directories[i].nStartBlock);
				dirNStartBlock = root.directories[i].nStartBlock;
				break;
			}
		}
		long blockAddress = getBlockAddress();
		if(blockAddress != -1) {
			strcpy(currDir.files[currDir.nFiles].fname, filename);
			if(strlen(extension) > 0) strcpy(currDir.files[currDir.nFiles].fext, extension);
			currDir.files[currDir.nFiles].fsize = 0;
			currDir.files[currDir.nFiles].nStartBlock = blockAddress;
			currDir.nFiles = currDir.nFiles + 1;
			cs1550_disk_block file;
			strcpy(file.data, "");
			updateBitmap(blockAddress);
			writeBlock(&currDir, 1, dirNStartBlock);
			writeBlock(&file, 1, blockAddress);
		}
	}

	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	// printf("---Call function read---\n");
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error
	// size = 0;
	if(size <= 0) return -ENOENT;

	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];

	tokenPath(path, directory, filename, extension);

	if(strlen(directory) == 0) {
		return -EPERM;
	} else {
		if(strlen(filename) == 0) {
			return -EPERM;
		} else if(strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION) {
			return -ENAMETOOLONG;
		} else if (isContainDir(directory) == 0) {
			return -EPERM;
		} else if (isContainFile(directory, filename, extension)) {
			cs1550_root_directory root = readDisk();
			int i;
			cs1550_directory_entry currDir;
			long fileNStartBlock = -1;
			size_t fileSize = -1;
			for(i = 0 ; i < root.nDirectories ; i++) {
				if(strcmp(root.directories[i].dname, directory) == 0) {
					currDir = readDirectory(root.directories[i].nStartBlock);
					break;
				}
			}
			for(i = 0 ; i < currDir.nFiles ; i++) {
				if(strcmp(currDir.files[i].fname, filename) == 0) {
					if(strlen(extension) != 0) {
						if(strcmp(currDir.files[i].fext, extension) != 0)
							return -EPERM;
					}
					fileNStartBlock = currDir.files[i].nStartBlock;
					fileSize = currDir.files[i].fsize;
					break;
				}
			}
			if(offset > fileSize) return -ENOENT;
			readFile(buf, (long)offset+fileNStartBlock, size);
			return size;
		}
	}
	return 0;
}

/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	// printf("---Call function write---\n");
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//set size (should be same as input) and return, or error
	if(size <= 0) return -ENOENT;

	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];

	tokenPath(path, directory, filename, extension);

	if(strlen(directory) == 0) {
		return -EPERM;
	} else {
		if(strlen(filename) == 0) {
			return -EPERM;
		} else if(strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION) {
			return -ENAMETOOLONG;
		} else if (isContainDir(directory) == 0) {
			return -EPERM;
		} else if (isContainFile(directory, filename, extension)) {
			cs1550_root_directory root = readDisk();
			int i;
			cs1550_directory_entry currDir;
			long fileNStartBlock = -1;
			long dirNStartBlock = -1;
			size_t fileSize = -1;
			for(i = 0 ; i < root.nDirectories ; i++) {
				if(strcmp(root.directories[i].dname, directory) == 0) {	// find the directory in root
					currDir = readDirectory(root.directories[i].nStartBlock);
					dirNStartBlock = root.directories[i].nStartBlock;	// find the nStartBlock of the dir
					break;
				}
			}
			int blocks = 1;
			for(i = 0 ; i < currDir.nFiles ; i++) {
				if(strcmp(currDir.files[i].fname, filename) == 0) {
					if(strlen(extension) != 0) {
						if(strcmp(currDir.files[i].fext, extension) != 0)
							return -EPERM;
						}
					fileNStartBlock = currDir.files[i].nStartBlock;
					fileSize = currDir.files[i].fsize;
					break;
				}
			}
			if(offset > fileSize) return -ENOENT;
			if(offset + size > fileSize) {
				int currBlocks = fileSize / BLOCK_SIZE;
				if(currBlocks == 0) currBlocks = currBlocks+1;
				int currRest = fileSize % BLOCK_SIZE;
				if(currRest > 0) currBlocks = currBlocks + 1;
				blocks = (offset + size) / BLOCK_SIZE;
				int rest = (offset + size) % BLOCK_SIZE;
				if(rest > 0) blocks = blocks + 1;
				if(currBlocks < blocks) {
					long blockAddress = (long)getBlockAddress();
					if(blockAddress != (fileNStartBlock + currBlocks*BLOCK_SIZE)) {
						FILE *fp = fopen(".disk", "rb");
						fseek(fp, fileNStartBlock, SEEK_SET);
						unsigned char temp[fileSize];
						fread(temp, fileSize, 1, fp);
						fclose(fp);
						writeBlock(temp, 1, blockAddress);
						fileNStartBlock = blockAddress;
					}
				}
			}
			writeMultiBlock(buf, blocks*BLOCK_SIZE, fileNStartBlock+offset);
			currDir.files[i].nStartBlock = fileNStartBlock;
			if(offset+size > fileSize)
				currDir.files[i].fsize = offset+size;
			int k;
			for(k = 0 ; k < blocks ; k++) {
				updateBitmap(fileNStartBlock + k*BLOCK_SIZE);
			}
			writeBlock(&currDir, 1, dirNStartBlock);
			return size;
		}
	}
	return 0;
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
