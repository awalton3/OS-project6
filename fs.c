#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>

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
int mounted = 0;

void print_valid_blocks(int array[], int size){
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

int inumberValid(int inumber, int ninodes) {
	return (inumber > 0 && inumber < ninodes);
}

int get_iblock(int inumber){
	return inumber/INODES_PER_BLOCK+1;
}

int get_inum(int iblock, int inode_index) {
	return (iblock - 1)*INODES_PER_BLOCK + inode_index;
}

int find_free_block(int nblocks) {
	for (int i = 1; i < nblocks; i++) {
		if (bitmap[i] == 1) {
			return i;
		}
	}
	return -1;
}

int get_inode_index(int inumber) {
	return inumber % INODES_PER_BLOCK;
}

void fs_debug()
{

	union fs_block block;
	union fs_block indirect_block;

	// Read in super block
	disk_read(0,block.data);

	//int magic = block.super.magic;
	int validSuperblock = check_magic(block.super.magic);

	printf("superblock:\n");
	if(validSuperblock)
		printf("    magic number is valid\n");
	else{
		printf("    magic number is not valid\n");
		return;
	}
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	// Traversing inode blocks
	for(int i=1; i<=block.super.ninodeblocks; i++){ //added equal
		// Read in inode block
		disk_read(i, block.data);

		// Traverse inodes
		for(int j = 0; j<INODES_PER_BLOCK; j++) {
			// Check if inode is valid
			if(block.inode[j].isvalid) {
				int inumber = get_inum(i, j);
				printf("inode %d:\n", inumber);
				printf("    size: %d bytes\n", block.inode[j].size);

				// Traverse direct pointers
				if(block.inode[j].size > 0){
					printf("    direct blocks: ");
					print_valid_blocks(block.inode[j].direct, POINTERS_PER_INODE);
				}

				// Traverse indirect pointers
				if(block.inode[j].indirect != 0){
					printf("    indirect block: %d\n", block.inode[j].indirect);
					printf("    indirect data blocks: ");
					disk_read(block.inode[j].indirect, indirect_block.data);
					print_valid_blocks(indirect_block.pointers, POINTERS_PER_BLOCK);
				}
			}
		}

	}
}

int fs_format() {
	//Read in super block
	union fs_block block;
	disk_read(0, block.data);

	//Check if FS already mounted
	if ( mounted ){
		printf("FS is already mounted, format failed\n");
	 	return 0;
	}

	//Create superblock, prepare for mount
	int ninodeblocks = ceil(.1 * (double)disk_size());
	block.super.magic = FS_MAGIC;
	block.super.nblocks = disk_size();
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = INODES_PER_BLOCK * ninodeblocks;

	// Write changes to disk
	disk_write(0, block.data);

	//Clear the inode table
	union fs_block iblock;
	for(int i=1; i<=block.super.ninodeblocks; i++){
		// Read in inode block
		disk_read(i, iblock.data);
		for(int j=0; j<INODES_PER_BLOCK; j++){
			iblock.inode[j].isvalid = 0;
		}
		disk_write(i, iblock.data);
	}

	return 1;
}

int fs_mount()
{
	// Read in the super block
	union fs_block block;
	disk_read(0, block.data);

	//Check if file system present
	if (!check_magic(block.super.magic)){
		printf("Error: Filesystem is not present on disk\n");
		return 0;
	}

	//Check if mounted already
	if (mounted){
		printf("Error: FS is already mounted. Mount failed\n");
		return 0;
	}

	//Build free block bitmap
	int nblocks = block.super.nblocks;
	bitmap = malloc(nblocks*sizeof(int));

	//Initialize to free - 1s
	for(int i=1; i<nblocks; i++){
		bitmap[i] = 1;
	}

	bitmap[0] = 0;	// Super block is never free
	//Setting inode blocks to not free
	for (int j=1; j<=block.super.ninodeblocks; j++){
		bitmap[j] = 0;
	}

	union fs_block iblock;
	union fs_block indirect_block;

	//Traversing inode blocks
	for(int i = 1; i <= block.super.ninodeblocks; i++) {

		//Read in inode block
		disk_read(i, iblock.data);

		//Traversing the inode block
		for(int j=0; j< INODES_PER_BLOCK; j++){

			if (!iblock.inode[j].isvalid)
				continue;

			//Traversing inode direct pointers
			for (int k = 0; k < POINTERS_PER_INODE; k++) {
				if (iblock.inode[j].direct[k] != 0){
					bitmap[iblock.inode[j].direct[k]] = 0;
				}
			}

			//Traversing inode indirect pointers
			if(iblock.inode[j].indirect !=0){
				bitmap[iblock.inode[j].indirect] = 0;
				disk_read(iblock.inode[j].indirect, indirect_block.data);
				for (int k = 0; k < POINTERS_PER_BLOCK; k++) {
					if (indirect_block.pointers[k] != 0)
						bitmap[indirect_block.pointers[k]] = 0;
				}
			}

		}
	}
	mounted = 1;
	return 1;
}

int fs_create()
{
	union fs_block block;
	union fs_block iblock;
	// reading in superblock
	disk_read(0, block.data);

	//Check if FS is mounted
	if (!mounted){
		printf("Error: FS not mounted. Create failed\n");
		return 0;
	}

	for(int i=1; i<=block.super.ninodeblocks; i++){
		disk_read(i, iblock.data);
		for (int j=0; j<INODES_PER_BLOCK; j++){
			int inumber = get_inum(i, j);
			if (inumber == 0)
				continue;
			if(iblock.inode[j].isvalid == 0){ //not valid means its free to use
				iblock.inode[j].isvalid = 1;
				iblock.inode[j].size = 0;
				for (int k=0; k<POINTERS_PER_INODE; k++){
					iblock.inode[j].direct[k] = 0; // setting entire array to 0
					disk_write(i, iblock.data);
				}
				iblock.inode[j].indirect = 0;

				disk_read(i, iblock.data);
				return inumber;
			}
		}
	}

	return 0;
}

int fs_delete(int inumber)
{
	// Make sure it has been mounted
	if (!mounted) {
	 	printf("Error: FS is not mounted. Delete failed\n");
	 	return 0;
	}

	int iblocknum = get_iblock(inumber);
	int real_inum = inumber;
	inumber = get_inode_index(inumber); //inode_index
	union fs_block block;
	union fs_block iblock;
	union fs_block indirect_block;
	disk_read(0, block.data);

	if (!inumberValid(real_inum,block.super.ninodes)) {
		return 0;
	}

	disk_read(iblocknum, iblock.data);
	int indirect_block_num = iblock.inode[inumber].indirect;
	disk_read(indirect_block_num, indirect_block.data);

	if (iblock.inode[inumber].isvalid == 0){  // meaning it's already invalid
		return 0;
	}
	iblock.inode[inumber].isvalid = 0;
	iblock.inode[inumber].size = 0;

	// Free all inode direct pointers
	for (int i = 0; i < POINTERS_PER_INODE; i++){
		if (iblock.inode[inumber].direct[i] != 0){
			bitmap[iblock.inode[inumber].direct[i]] = 1; // updating the bitmap free list
			iblock.inode[inumber].direct[i] = 0;
		}
	}
	disk_write(iblocknum, iblock.data);

	// Free all inode indirect pointers
	if (indirect_block_num != 0){
		for (int i = 0; i < POINTERS_PER_BLOCK; i++){
			bitmap[indirect_block.pointers[i]] = 1;
			indirect_block.pointers[i] = 0;
		}
		bitmap[indirect_block_num] = 1;
		iblock.inode[inumber].indirect = 0;
	}
	disk_write(indirect_block_num, indirect_block.data);
	disk_write(iblocknum, iblock.data);

	return 1;
}

int fs_getsize( int inumber )
{
	int iblocknum = get_iblock(inumber);
	int inode_index = get_inode_index(inumber);

	union fs_block block;
	disk_read(0, block.data);
	if (!inumberValid(inumber,block.super.ninodes)) {
		return -1;
	}

	union fs_block iblock;
	disk_read(iblocknum, iblock.data);

	// Fails for Invalid inodes
	if (!iblock.inode[inode_index].isvalid || iblock.inode[inode_index].size < 0)
		return -1;

	return iblock.inode[inode_index].size;
}

// Read from a certain inode
int fs_read(int inumber, char *data, int length, int offset)
{
	union fs_block block; //super
	disk_read(0, block.data);

	if (!inumberValid(inumber,block.super.ninodes)) {
		printf("inumber is invalid\n");
		return 0;
	}

	int iblocknum = get_iblock(inumber);
	inumber = get_inode_index(inumber);
	// Clear data
	strcpy(data, "");

	union fs_block iblock; //inode block
	union fs_block dblock; //data block
	union fs_block indirect_block;

	// Check if mounted
	if (!mounted){
		printf("Error: FS is not mounted. Read failed\n");
		return 0;
	}

	disk_read(iblocknum, iblock.data);

	// Make sure inumber is valid
	if (!iblock.inode[inumber].isvalid || iblock.inode[inumber].size <= offset)
		return 0; // fails
	if (iblock.inode[inumber].indirect > 0) {
		disk_read(iblock.inode[inumber].indirect, indirect_block.data);
	}

	int direct_portion = DATA_BLOCK_SIZE*POINTERS_PER_INODE;
	int current_direct_block;
	int current_direct_index = offset/DATA_BLOCK_SIZE;
	int current_indirect_block;
	int current_indirect_index = (offset-DATA_BLOCK_SIZE*POINTERS_PER_INODE)/DATA_BLOCK_SIZE;
	int bytes_read = 0;
	int bytes_read_rn = 0;
	int amount_to_read = iblock.inode[inumber].size - offset;

	while (amount_to_read > 0) {
		bytes_read_rn = 0;

		//Data container exceeds length (16384)
		if (strlen(data) >= length) {
			return bytes_read;
		}

		// direct block section
		if (offset + bytes_read < direct_portion) {
			current_direct_block = iblock.inode[inumber].direct[current_direct_index];
			printf("Current direct block is %d\n", current_direct_block);
			if (current_direct_block > 0) {
				disk_read(current_direct_block, dblock.data);

				// Smaller segments
				if (amount_to_read <= DATA_BLOCK_SIZE) {
					bytes_read += strlen(dblock.data);
					bytes_read_rn = strlen(dblock.data);
					strncat(data, dblock.data, bytes_read_rn);
				} else { //when amount to read exceeds block size
					// read what we can fit in - 4kb
					bytes_read += DATA_BLOCK_SIZE;
					bytes_read_rn = DATA_BLOCK_SIZE;
					strncat(data, dblock.data, DATA_BLOCK_SIZE);
				}
				amount_to_read = amount_to_read - bytes_read_rn;
			}
			current_direct_index++;
			current_indirect_index++;
		}

		// indirect block section
		else {
			// Reach end of inode or no indirect
			if (!iblock.inode[inumber].indirect || current_indirect_index >= POINTERS_PER_BLOCK){
				return bytes_read;
			}

			current_indirect_block = indirect_block.pointers[current_indirect_index];

			if (current_indirect_block > 0) {
				disk_read(current_indirect_block, dblock.data);
				// Smaller segments
				if (amount_to_read <= DATA_BLOCK_SIZE) {
					bytes_read += amount_to_read;
					bytes_read_rn = amount_to_read;
					strncat(data, dblock.data, bytes_read_rn);
				} else { //when amount to read exceeds block size
					// read what we can fit in - 4kb
					bytes_read += DATA_BLOCK_SIZE;
					bytes_read_rn = DATA_BLOCK_SIZE;
					strncat(data, dblock.data, DATA_BLOCK_SIZE);
				}
				amount_to_read = amount_to_read - bytes_read_rn;
			}
			current_indirect_index++;
		}
	}

	return bytes_read;
}

int fs_write(int inumber, const char *data, int length, int offset)
{
	union fs_block block; //super
	disk_read(0, block.data);

	if (!inumberValid(inumber,block.super.ninodes)) {
		printf("inumber is invalid\n");
		return 0;
	}

	int iblocknum = get_iblock(inumber);
	inumber = get_inode_index(inumber); //inode_index

	//Check if mounted
	if(!mounted){
		printf("Error: FS is not mounted. Write failed\n");
		return 0;
	}

	printf("invalid!\n");

	union fs_block iblock; //inode block
	union fs_block dblock; //data block
	union fs_block indirect_block;
	disk_read(iblocknum, iblock.data);

	if (!iblock.inode[inumber].isvalid)
		return 0; //fails

	int free_block, free_pointers_block;
	bool direct_found;
	int bytes_written = 0;
	int amount_to_write = length;

	while (amount_to_write > 0) {
		direct_found = false;

		free_block = find_free_block(block.super.nblocks);

		if (free_block == -1) {
			printf("The disk is full.\n");
			return bytes_written;
		}

		const char *temp = &data[bytes_written];

		// Important! Clear the data block before writing to it
		for (int i = 0; i < DISK_BLOCK_SIZE; i++)
			dblock.data[i] = 0;

		//check if temp is bigger than data block size, write up to data block size only
		if(amount_to_write > DATA_BLOCK_SIZE){
			strncpy(dblock.data, temp, DATA_BLOCK_SIZE);
			bytes_written += DATA_BLOCK_SIZE;
		}
		else{
			strncpy(dblock.data, temp, amount_to_write);
			bytes_written += amount_to_write;
		}

		disk_write(free_block, dblock.data);
		bitmap[free_block] = 0;
		amount_to_write = length - bytes_written;

		//Searhing for an available direct pointer for free block
		for (int i = 0; i < POINTERS_PER_INODE; i++) {
			if (iblock.inode[inumber].direct[i] == 0) {
				iblock.inode[inumber].direct[i] = free_block;
				direct_found = true;
				disk_write(iblocknum, iblock.data);
				break;
			}
		}

		// Look for free indirect field in inode
		if (!direct_found) {
			// Map to a new indirect block
			if (iblock.inode[inumber].indirect == 0) {
				free_pointers_block = find_free_block(block.super.nblocks);
				iblock.inode[inumber].indirect = free_pointers_block;
				disk_read(free_pointers_block, indirect_block.data);
				indirect_block.pointers[0] = free_block;
				// Initialize pointers block
				for (int i = 1; i < POINTERS_PER_BLOCK; i++) {
					indirect_block.pointers[i] = 0;
				}
				disk_write(free_pointers_block, indirect_block.data);
				bitmap[free_pointers_block] = 0;
			}
			// Look for free pointers in existing indirect pointers block
			else {
				disk_read(iblock.inode[inumber].indirect, indirect_block.data);
				for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
					if (indirect_block.pointers[i] == 0) {
						indirect_block.pointers[i] = free_block;
						break;
					}
				}
				disk_write(iblock.inode[inumber].indirect, indirect_block.data);
			}
		}

		iblock.inode[inumber].size += strlen(dblock.data);
		disk_write(iblocknum, iblock.data);
	}

	return bytes_written;
}
