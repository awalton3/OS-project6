
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

/*int ceiling(double n){
	double rem = fmod(n, 1);
	if (rem == 0)
		return (int)n;
	else
		return (int)n + 1;
}*/

int fs_format()
{
	union fs_block block;
	//Check if already mounted
	disk_read(0, block.data);
	if (block.super.magic == FS_MAGIC)
		return 0;

	//Create superblock
	int ninodeblocks = ceil(.1 * (double)disk_size());
	block.super.magic = FS_MAGIC;
	printf(" Magic num right after assign is %d\n", block.super.magic);
	block.super.nblocks = disk_size();
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = INODES_PER_BLOCK * ninodeblocks;
	disk_write(0, block.data);
	printf(" Magic num right after write is %d\n", block.super.magic);
	
	//Clear the inode table
	for(int i=0; i< block.super.ninodes; i++){
		block.inode[i].isvalid = 0;
	}
	return 1;
}

void print_array(int array[], int size){
	for(int i=0; i< size;  i++){
		if(array[i] == 0){ //points to a null block
			continue;
		}
		printf("%d ",array[i]);
	}
	printf("\n");
}

void fs_debug()
{
	union fs_block block;
	union fs_block indirect_block;

	disk_read(0,block.data);
	int magic = block.super.magic;


	printf("superblock:\n");
	int validSuperblock = (magic == FS_MAGIC);
	if(validSuperblock)
		printf("    magic number is valid\n");
	else{
		printf("    magic number is not valid\n");
		return;
	}
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	//Define inodes
	for(int i=1; i<block.super.ninodeblocks; i++){
		disk_read(i, block.data);
		for(int j=0; j<INODES_PER_BLOCK; j++){
			struct fs_inode inode = block.inode[(i-1)*INODES_PER_BLOCK + j];
			if(inode.isvalid){
				printf("inode %d:\n", j);
				printf("    size: %d bytes\n", inode.size);
				int direct_size = sizeof(inode.direct)/sizeof(int); 
				if(direct_size > 0){
					printf("    direct blocks: ");
					print_array(inode.direct, direct_size);
				}
				if(inode.indirect){
					printf("    indirect block: %d\n", inode.indirect);
					printf("    indirect data blocks: ");
					disk_read(inode.indirect, indirect_block.data);
					int indirect_size = sizeof(indirect_block.pointers)/sizeof(int*);
					print_array(indirect_block.pointers, indirect_size);
				}
			}
		}
	}	
}

int fs_mount()
{
	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
