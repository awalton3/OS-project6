
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

int *bitmap;

void print_array(int array[], int size){
	for(int i=0; i< size; i++){
		if(array[i] == 0){ //points to a null block
			continue;
		}
		printf("%d ",array[i]);
	}
	printf("\n");
}



int check_magic(int magic){
	return (magic == FS_MAGIC);
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
		for(int j=1; j<=INODES_PER_BLOCK; j++){
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
	block.super.nblocks = disk_size();
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = INODES_PER_BLOCK * ninodeblocks;
	printf("Before the write in format\n");
	disk_write(0, block.data);
	printf("After the write in format\n");

	//Clear the inode table
	union fs_block iblock;
	for(int i=0; i<block.super.ninodes; i++){
		printf("first for loop in format\n");
		disk_read(i/INODES_PER_BLOCK+1, iblock.data);
		printf("after disk_read\n");
		iblock.inode[i].isvalid = 0;
		disk_write(i/INODES_PER_BLOCK +1, iblock.data);
		printf("%d: %d\n", i, i/INODES_PER_BLOCK +1);
	}
	
	for(int i=0; i<block.super.ninodes; i++){
		printf("second for loop in format\n");
		disk_read(i/INODES_PER_BLOCK+1, iblock.data);
		printf("%d : %d\n", i/INODES_PER_BLOCK+1, iblock.inode[i].isvalid);
	}

	printf("Cleared inode table\n");
	return 1;
}

int fs_mount()
{
	//Check if file system present
	union fs_block block;
	disk_read(0, block.data);
	if (!check_magic(block.super.magic)){
		return 0;
	}

	//Build free block bitmap
	int nblocks = block.super.nblocks;
	bitmap = malloc(nblocks*sizeof(int)); //begin with all free (1)

	bitmap[0] = 0;	// Super block is never free
	//Initialize to free - 1s
	for(int i=1; i<nblocks; i++){
		bitmap[i] = 1;
	}
	printf("Initialized bitmap\n");

	for (int i = 0; i < nblocks; i++) {
		printf("%d ", bitmap[i]);
	}
	printf("\n");

	union fs_block iblock;
	union fs_block indirect_block;
	for(int i = 1; i < block.super.ninodes; i++) {
		printf("first for loop\n");
		disk_read(i/INODES_PER_BLOCK +1, iblock.data); // Ask about "zero cannot be a valid inumber"

		printf("iblock.inode[i].isvalid: %d\n", iblock.inode[i].isvalid);
		if (!iblock.inode[i].isvalid)
			continue;

		/*for (int j = 0; j < POINTERS_PER_INODE; j++) {
			printf("second for loop\n");
			printf("%d\n", iblock.inode[i].isvalid);
			printf("%d\n", iblock.inode[i].direct[j]);
			bitmap[iblock.inode[i].direct[j]] = 0; // Set block to not free in free block bitmap
		}

		bitmap[iblock.inode[i].indirect] = 0;
		disk_read(block.inode[i].indirect, indirect_block.data);
		for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
			printf("third for loop\n");
			bitmap[indirect_block.pointers[k]] = 0;
		}*/
	}

	//Prepare the File System
	
	//Clear Inodes
	// for(int i=1; i<=block.super.ninodes; i++){
	// 	for(int j=0; j<POINTERS_PER_INODE; j++){
	// 		block.inode->direct[j] = 0;
	// 	}
	// 	block.inode->indirect = 0;
	// 	disk_write(i/INODES_PER_BLOCK + 1, block.data);
	// }

	return 1;
}

int fs_create()
{
	union fs_block block;
	disk_read(0, block.data);
	int ninodes = block.super.ninodes;
	
	for(int i=1; i<=ninodes; i++){
		disk_read(i/INODES_PER_BLOCK +1, block.data);
		if(!block.inode[i].isvalid){ //not valid means its free to use
			block.inode[i].isvalid = 1;
			block.inode[i].size = 0;
			disk_write(i/INODES_PER_BLOCK + 1, block.data);
			return i;
		}
	}
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
