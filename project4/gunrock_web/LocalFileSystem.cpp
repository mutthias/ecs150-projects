#include <iostream>
#include <string>
#include <vector>
#include <assert.h>
#include <cstring>

#include "LocalFileSystem.h"
#include "ufs.h"

using namespace std;


LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

void LocalFileSystem::readSuperBlock(super_t *super) {
  char local_buffer[UFS_BLOCK_SIZE];
  disk->readBlock(0, local_buffer);
  memcpy(super, local_buffer, sizeof(super_t));
}

void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  char local_buffer[UFS_BLOCK_SIZE];
  disk->readBlock(super->inode_bitmap_addr, local_buffer);
  memcpy(inodeBitmap, local_buffer, super->num_inodes / 8);
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  char local_buffer[UFS_BLOCK_SIZE];
  memcpy(local_buffer, inodeBitmap, super->num_data / 8);
  disk->writeBlock(super->inode_bitmap_addr, local_buffer);
}

void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap) {
  char local_buffer[UFS_BLOCK_SIZE];
  disk->readBlock(super->data_bitmap_addr, local_buffer);
  memcpy(dataBitmap, local_buffer, super->num_data / 8);

}

void LocalFileSystem::writeDataBitmap(super_t *super, unsigned char *dataBitmap) {
  char local_buffer[UFS_BLOCK_SIZE];
  memcpy(local_buffer, dataBitmap, super->num_data / 8);
  disk->writeBlock(super->data_bitmap_addr, local_buffer);
}

void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) {
  for (size_t n = 0; n < super->num_inodes; n++) {
    char local_buffer[UFS_BLOCK_SIZE];
    disk->readBlock(super->inode_region_addr, local_buffer);
    memcpy(&inodes[n], local_buffer, sizeof(inode_t));
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  for (size_t n = 0; n < super->num_inodes; n++) {
    char local_buffer[UFS_BLOCK_SIZE];
    memcpy(local_buffer, &inodes[n], sizeof(inode_t));
    disk->writeBlock(super->inode_region_addr + n, local_buffer);
  }
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  return 0;
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  super_t super;
  readSuperBlock(&super);

  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);
  *inode = inodeTable[inodeNumber];
  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  super_t super;
  readSuperBlock(&super);

  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }
  
  inode_t inodeTable[super.num_inodes];
  inode_t inode = inodeTable[inodeNumber];
  int blockNum = inode.direct[0];

  if (size <= 0 || size > inode.size) {
    return -EINVALIDSIZE;
  }

  char block[UFS_BLOCK_SIZE];
  disk->readBlock(blockNum, block);
  memcpy(buffer, block, size);

  return size;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  return 0;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  return 0;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  return 0;
}

