
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
#define DATA_BLOCK_SIZE    4096

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
				if(inode.size > 0){
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
	for(int i=1; i<block.super.ninodeblocks; i++){
		printf("first for loop in format\n");
		disk_read(i, iblock.data);
		printf("after disk_read\n");
		for(int j=0; j<INODES_PER_BLOCK; j++){
			iblock.inode[j].isvalid = 0;
		}
		disk_write(i, iblock.data);
	}

	/*for(int i=0; i<block.super.ninodeblocks; i++){
		printf("second for loop in format\n");
		disk_read(i, iblock.data);
		printf("%d : %d\n", i, iblock.inode[i].isvalid);
	}*/

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

		//Initialize to free - 1s
	for(int i=1; i<nblocks; i++){
		bitmap[i] = 1;
	}
	bitmap[0] = 0;	// Super block is never free

	// setting inode blocks to not free
	for (int j=1; j<=block.super.ninodeblocks; j++){
		bitmap[j] = 0;
	}

	printf("Initialized bitmap\n");

	for (int i = 0; i < nblocks; i++) {
		printf("%d ", bitmap[i]);
	}
	printf("\n");

	union fs_block iblock;
	union fs_block indirect_block;
	for(int i = 1; i < block.super.ninodeblocks; i++) {
		printf("first for loop\n");
		disk_read(i, iblock.data); // Ask about "zero cannot be a valid inumber"

		//printf("iblock.inode[i].isvalid: %d\n", iblock.inode[i].isvalid);
		for(int j=0; j< INODES_PER_BLOCK; j++){
			if (!iblock.inode[j].isvalid)
				continue;

			for (int k = 0; k < POINTERS_PER_INODE; k++) {
				printf("second for loop\n");
				printf("%d\n", iblock.inode[j].isvalid);
				printf("%d\n", iblock.inode[j].direct[k]);
				if (iblock.inode[j].direct[k] != 0){  // To make sure we don't set superblock to free.
					bitmap[iblock.inode[j].direct[k]] = 0; // Set block to not free in free block bitmap
				}
			}

			printf("index is %d\n", iblock.inode[j].indirect);
			if(iblock.inode[j].indirect !=0){
				bitmap[iblock.inode[j].indirect] = 0;
				disk_read(iblock.inode[j].indirect, indirect_block.data);
				for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
					printf("third for loop\n");
					printf("%d\n", indirect_block.pointers[k]);
					bitmap[indirect_block.pointers[k]] = 0;
				}
			}
		}
	}

	return 1;
}

int fs_create()
{
	union fs_block block;
	union fs_block iblock;
	// reading in superblock
	disk_read(0, block.data);

	for(int i=1; i<=block.super.ninodeblocks; i++){
		disk_read(i, iblock.data);
		for (int j=0; j<INODES_PER_BLOCK; j++){
			int inumber = ((i-1)*INODES_PER_BLOCK + j);
			if ( inumber == 0 )  //inumber
				continue;
			if(iblock.inode[j].isvalid == 0){ //not valid means its free to use
				iblock.inode[j].isvalid = 1;
				iblock.inode[j].size = 0;
				for (int k=0; k<POINTERS_PER_INODE; k++){
					iblock.inode[j].direct[k] = 0; // setting entire array to 0
				}
				iblock.inode[j].indirect = 0;
				disk_write(i, iblock.data);
				return inumber;
			}
		}
	}
	return 0;
}

int fs_delete( int inumber )
{
	union fs_block block;
	union fs_block iblock;
	disk_read(0, block.data);

	disk_read(inumber/INODES_PER_BLOCK+1, iblock.data);
	if (iblock.inode[inumber].isvalid == 0){  // meaning it's already invalid
		return 0;
	}
	iblock.inode[inumber].isvalid = 0;
	iblock.inode[inumber].size = 0;

	for (int i = 0; i < POINTERS_PER_INODE; i++){
		if (iblock.inode[inumber].direct[i] != 0){  // To make sure we don't set superblock to free.
			bitmap[iblock.inode[inumber].direct[i]] = 1; // updating the bitmap free list
			iblock.inode[inumber].direct[i] = 0;
		}
	}
	disk_write(inumber/INODES_PER_BLOCK+1, iblock.data);
	union fs_block indirect_block;
	int indirect_block_num = iblock.inode[inumber].indirect;
	disk_read(indirect_block_num, indirect_block.data);

	if (indirect_block_num != 0){
		for (int i = 0; i < POINTERS_PER_BLOCK; i++){
			bitmap[indirect_block.pointers[i]] = 1;
			indirect_block.pointers[i] = 0;
		}

		bitmap[indirect_block_num] = 1;
		iblock.inode[inumber].indirect = 0;
	}
	disk_write(indirect_block_num, indirect_block.data);

	for (int i = 0; i < block.super.nblocks; i++) {
		printf("%d ", bitmap[i]);
	}
	printf("\n");

	return 1;
}

int get_iblock(int inumber){
	return inumber/INODES_PER_BLOCK+1;
}

int fs_getsize( int inumber )
{
	union fs_block iblock;
	disk_read(get_iblock(inumber), iblock.data);
	if (!iblock.inode[inumber].isvalid || iblock.inode[inumber].size < 0)
		return -1;

	return iblock.inode[inumber].size;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	union fs_block block; //super
	union fs_block iblock; //inode block
	union fs_block dblock; //data block
	union fs_block indirect_block;

	disk_read(0, block.data);
	disk_read(get_iblock(inumber), iblock.data);
	disk_read(iblock.inode[inumber].indirect, indirect_block.data);

	printf("after disk reads\n");

	if (!iblock.inode[inumber].isvalid)
		return 0; // fails

	int bytes_read = 0;

	// TODO: Check if there are direct blocks
	if (!iblock.inode[inumber].direct[0])
		return 0;

	//int current_direct = (offset/DATA_BLOCK_SIZE) + iblock.inode[inumber].direct[0];

	//Check that offset is within range of direct
	int direct_portion = DATA_BLOCK_SIZE*POINTERS_PER_INODE;
	int current_direct, current_indirect, ending_direct;

	if (offset < direct_portion) {
		current_direct =  iblock.inode[inumber].direct[offset/DATA_BLOCK_SIZE];
	} else {
		//Check if indirect block is valid
		if (iblock.inode[inumber].indirect) {
			current_indirect = indirect_block.pointers[(offset-DATA_BLOCK_SIZE*POINTERS_PER_INODE)/DATA_BLOCK_SIZE];
		}
	}

/*	printf("offset: %d\n", offset);
	printf("length: %d\n", length);
	printf("current_direct: %d\n", current_direct);
	printf("nblocks: %d\n", block.super.nblocks); */

	printf("offset: 20,490 bytes\n");
	printf("pointer index using dbSize : %d\n", (20490-DATA_BLOCK_SIZE*POINTERS_PER_INODE)/DATA_BLOCK_SIZE);
	printf("pointer index using sizePointerEntry : %d\n", (20490-DATA_BLOCK_SIZE*POINTERS_PER_INODE)/4);

	int i = current_direct;
	int j = current_indirect;
	while (bytes_read < length) {
		// direct block section
		if (i <= ending_direct) {
			disk_read(i, dblock.data);
			strncat(data, dblock.data, DATA_BLOCK_SIZE);
			bytes_read += DATA_BLOCK_SIZE;
			i++;
		}
		// indirect block section
		else {
			//do we need this ??
			if (!iblock.inode[inumber].indirect)
				return 0;
			disk_read(j, dblock.data);
			strncat(data, dblock.data, DATA_BLOCK_SIZE);
			bytes_read += DATA_BLOCK_SIZE;
			j++;
		}
	}

	printf("bytes_read: %d\n", bytes_read);
	return bytes_read;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
