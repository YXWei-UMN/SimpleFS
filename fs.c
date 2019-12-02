#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

#define user_defined_inodeblock_ratio 10
#define user_defined_max_nblocks 1024

int freemap[user_defined_max_nblocks]={0};

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
	int bitmap[user_defined_max_nblocks];
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

int find_free_inode(union fs_block block){
    for(int i=1; i<block.super.ninodeblocks; i++){
        union fs_block inodeblock;
        disk_read(i,inodeblock.data);
        for(int j=0; j<INODES_PER_BLOCK; j++){
            if(!inodeblock.inode[j].isvalid)
                return (i-1)*INODES_PER_BLOCK+j;
        }
    }
    printf("no free inode!\n");
    return -1;
}

int find_freedatablock(union fs_block block){
    for(int i=block.super.ninodeblocks+1; i<user_defined_max_nblocks; i++){
        if(freemap[i] == 0) return i;
    }
    printf("no free data blocks\n");
    return -1;
}
int find_inodeblock(inumber){
    return inumber/128+1;
}
int find_datablock(struct fs_inode inode,int offset){
    if(offset>=inode.size) return -1;
    int order = offset/DISK_BLOCK_SIZE;
    if(order<POINTERS_PER_INODE){
        return inode.direct[order];
    } else{
        order-=POINTERS_PER_INODE;
        union fs_block indirectblock;
        disk_read(inode.indirect,indirectblock.data);
        return indirectblock.pointers[order];
    }
}
struct fs_inode inode_load( int inumber ) {
    struct fs_inode inode;
    int location = find_inodeblock(inumber);
    union fs_block inodeblock;
    disk_read(location,inodeblock.data);
    inode = inodeblock.inode[inumber%128];
    return inode;
}

void inode_save( int inumber, struct fs_inode inode ) {
    int location = find_inodeblock(inumber);
    union fs_block inodeblock;
    disk_read(location,inodeblock.data);
    inodeblock.inode[inumber%128] = inode;
    disk_write(location,inodeblock.data);
}
int fs_format()
{
    //an attempt to format an already-mounted disk should do nothing and return failure
    union fs_block old_block;
    disk_read(0,old_block.data);
    if(old_block.super.magic==FS_MAGIC) {
        //destroying any data already present clears the inode table
        printf("this disk already has a FS mounted, we will erase all previous information \n");
    }
    char data[DISK_BLOCK_SIZE]={0};
    for(int i=0; i<disk_size(); i++){
        disk_write(i,data);
    }
    //creat superblock on disk
    union fs_block block;
    block.super.magic=FS_MAGIC;
    int nblocks = disk_size();
    block.super.nblocks=nblocks;
    //Sets aside ten percent of the blocks for inodes
    int ninodeblocks = nblocks/user_defined_inodeblock_ratio;
    if(ninodeblocks<=0) ninodeblocks=1; //in case nblocks small than 10, ninodeblocks should be at least 1
    block.super.ninodeblocks=ninodeblocks;
    block.super.ninodes=0;
    block.super.bitmap[0]=1;
    disk_write(0,block.data);
    memset(freemap,0, sizeof(freemap));
    freemap[0]=1;
	return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	for(int i=1; i<= block.super.ninodeblocks; i++){
        union fs_block inodeblock;
        disk_read(i,inodeblock.data);
        for(int j=0; j< INODES_PER_BLOCK; j++){
            struct fs_inode inode = inodeblock.inode[j];
            if(inode.isvalid){
                printf("inode %d:\n",j);
                printf("     size: %d \n",inode.size);
                printf("     direct block: ");
                for(int k=0; k<POINTERS_PER_INODE; k++) {
                    if(inode.direct[k]>0) printf("%d ", inode.direct[k]);
                }
                printf("\n");
                if(inode.indirect>block.super.ninodeblocks){
                    printf("\nindirect block: %d \n", inode.indirect);
                    union fs_block indirect_block;
                    disk_read(inode.indirect,indirect_block.data);
                    printf("indirect data block: ");
                    for(int n=0; n<POINTERS_PER_BLOCK; n++){
                        if(indirect_block.pointers[n]>0)
                            printf("%d ",indirect_block.pointers[n]);
                    }

                    printf("\n");
                }

            }
        }
	}
}

int fs_mount()
{   //Examine the disk for a filesystem
    union fs_block block;
    disk_read(0,block.data);
    if(block.super.magic!=FS_MAGIC) {
        printf("this disk already has not been formatted by any FS yet, please first format and then mount \n");
        return 0;
    }
    //read the superblock, build a free block bitmap
    fs_debug();
    memcpy(freemap,block.super.bitmap,sizeof(int)*user_defined_max_nblocks);
    return 1;
}

int fs_create()
{
    /* inster inode into inodeblocks */
    //step 1: find inodeblock
    union fs_block block;
    disk_read(0,block.data);
    int inumber=find_free_inode(block);

    if(inumber==-1){
        return -1;
    }
    //step2: initialize the new inode
    struct fs_inode inode;
    inode.isvalid = 1;
    inode.size = 0;
    inode.indirect=-1;
    memset(inode.direct,0, sizeof(inode.direct));
    block.super.ninodes++;
    disk_write(0,block.data);

    //step3: insert
    inode_save(inumber,inode);
    return inumber;
}

int fs_delete( int inumber )
{
    if(inumber<0) {
        printf("illegal inumber!\n");
        return 0;
    }
    struct fs_inode inode = inode_load(inumber);
    if(!inode.isvalid){
        printf("inumber point to a invalid inode\n");
        return 0;
    }
    union fs_block block;
    disk_read(0,block.data);
    block.super.ninodes--;

    //free indirect block
    if(inode.indirect > block.super.ninodeblocks+1){
        union fs_block indirectblock;
        disk_read(inode.indirect,indirectblock.data);
        for(int i=0; i<POINTERS_PER_BLOCK; i++){
            int datalocation = indirectblock.pointers[i];
            if(datalocation<=block.super.ninodeblocks)continue;
            else if(datalocation>block.super.nblocks){
                printf("too large data_block number in indirect_block\n");
                return 0;
            }
            union fs_block datablock;
            disk_read(datalocation,datablock.data);
            memset(datablock.data,0, sizeof(datablock.data));
            disk_write(datalocation,datablock.data);
            freemap[datalocation]=0;
            block.super.bitmap[datalocation]=0;
        }
        memset(indirectblock.data,0, sizeof(indirectblock.data));
        disk_write(inode.indirect,indirectblock.data);
        freemap[inode.indirect]=0;
        block.super.bitmap[inode.indirect]=0;
    }
    // free data block
    for(int i=0; i<POINTERS_PER_INODE; i++){
        if(inode.direct[i]<=block.super.ninodeblocks)continue;
        else if (inode.direct[i]>block.super.nblocks){
            printf("too large data_block number in indirect_block\n");
            return 0;
        }
        union fs_block datablock;
        disk_read(inode.direct[i],datablock.data);
        memset(datablock.data,0, sizeof(datablock.data));
        disk_write(inode.direct[i],datablock.data);
        freemap[inode.direct[i]]=0;
        block.super.bitmap[inode.direct[i]]=0;
    }
    //delete inode update inodeblock
    inode.isvalid = 0;
    inode.indirect = 0;
    inode.size = 0;
    memset(inode.direct,0, sizeof(inode.direct));
    inode_save(inumber,inode);
    disk_write(0,block.data);
    return 1;
}

int fs_getsize( int inumber )
{
	struct fs_inode inode;
    inode = inode_load(inumber);
    if(inode.isvalid){
        return inode.size;
    } else {
    printf("no such inode\n");
    return -1;
    }
}

int fs_read( int inumber, char *data, int length, int offset )
{   // read do not need to modify any information inside FS
    // use inumber find inode
    struct fs_inode inode;
    inode = inode_load(inumber);
    int size = 0;
    // use offset find datablock
    if(inode.isvalid){
        int datablock = find_datablock(inode, offset);
        // read length bytes data or just read the rest of data if rest data is less then length
        if(datablock>0){
            while(length>0 && datablock >0){
                disk_read(datablock,data);
                size++;
                length-= DISK_BLOCK_SIZE;
                data+= DISK_BLOCK_SIZE;
                offset+= DISK_BLOCK_SIZE;
                datablock = find_datablock(inode, offset);
            }
            return inode.size;
        }else return datablock;

    } else {
        printf("no such inode\n");
        return 0;
    }
}
int inode_update(int datablock, int inumber, int length){
    union fs_block inode_block;
    disk_read(find_inodeblock(inumber),inode_block.data);
    struct fs_inode inode = inode_block.inode[inumber%128];
    if(length<DISK_BLOCK_SIZE){
        inode.size+=length;
    } else {
        inode.size+=DISK_BLOCK_SIZE;
    }
    for(int i=0; i<5; i++){
        if(inode.direct[i] == 0){
            inode.direct[i]=datablock;
            inode_block.inode[inumber%128]=inode;
            disk_write(find_inodeblock(inumber),inode_block.data);
            return 1;
        }
    }
    if(inode.indirect<=0){
        union fs_block block;
        disk_read(0,block.data);
        int location_indirectblock=find_freedatablock(block);
        if(location_indirectblock==-1){
            return -1;
        }
        if(location_indirectblock == datablock){
            freemap[datablock]=1;
            location_indirectblock=find_freedatablock(block);
            freemap[datablock]=0;
            if(location_indirectblock==-1) {
                printf("need two datablock, one for indirect block one for data\n");
                return -1;
            }
        }
        freemap[location_indirectblock]=1;
        block.super.bitmap[location_indirectblock]=1;
        inode.indirect=location_indirectblock;
        disk_write(0,block.data);
    }
    union fs_block indirectblock;
    disk_read(inode.indirect,indirectblock.data);
    for(int i=0; i<POINTERS_PER_BLOCK; i++){
        if (indirectblock.pointers[i]==0){
            indirectblock.pointers[i]=datablock;
            disk_write(inode.indirect,indirectblock.data);
            inode_block.inode[inumber%128]=inode;
            disk_write(find_inodeblock(inumber),inode_block.data);
            return 1;
        }
    }
    printf("no free pointers (direct or indirect) for inode\n");
    return -1;
}
int fs_write( int inumber, const char *data, int length, int offset )
{
    // write has to modify some information of FS
    // according to inumber find the proper inode, if inode not exist than create it
    int inodelocation = find_inodeblock(inumber);
    union fs_block inodeblock;
    disk_read(inodelocation,inodeblock.data);
    if(!inodeblock.inode[inumber%128].isvalid){
        printf("inode %d not exists, please first create\n", inumber);
        return -1;
    }
    union fs_block block;
    disk_read(0,block.data);
    int datablock = find_freedatablock(block);
    // according to offset find a free data block to write
    int write_bytes=length;
    while (length>0 && datablock>=block.super.ninodeblocks+1){
        if(inode_update(datablock,inumber,length)==-1){
            //if inode is full
            printf("%d ",datablock);
            write_bytes-=length;
            break;
        }
        disk_write(datablock,data);
        freemap[datablock]=1;
        block.super.bitmap[datablock]=1;
        if(length<DISK_BLOCK_SIZE){
            break;
        }
        data+=DISK_BLOCK_SIZE;
        length-=DISK_BLOCK_SIZE;
        datablock = find_freedatablock(block);
    }
    disk_write(0,block.data);
    // consider some boundaries like the length of data is larger than a normal data block
    // after finish writing, update those information
    disk_read(inodelocation,inodeblock.data);
    return write_bytes;
}