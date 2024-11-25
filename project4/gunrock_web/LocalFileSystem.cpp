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
  int inodes_per_block = UFS_BLOCK_SIZE / sizeof(inode_t);
  for (int n = 0; n < super->num_inodes; n++) {
    int block_offset = n / inodes_per_block;
    int inode_offset = n % inodes_per_block;

    char local_buffer[UFS_BLOCK_SIZE];
    disk->readBlock(super->inode_region_addr + block_offset, local_buffer);
    memcpy(&inodes[n], (sizeof(inode_t) * inode_offset) + local_buffer, sizeof(inode_t));
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  for (int n = 0; n < super->num_inodes; n++) {
    char local_buffer[UFS_BLOCK_SIZE];
    memcpy(local_buffer, &inodes[n], sizeof(inode_t));
    disk->writeBlock(super->inode_region_addr + n, local_buffer);
  }
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  char local_buffer[UFS_BLOCK_SIZE];
  dir_ent_t *direct;
  inode_t inode;
  if (stat(parentInodeNumber, &inode) < 0) {
    return -ENOTFOUND;
  } 
  if (inode.type != UFS_DIRECTORY) {
    return -EINVALIDINODE;
  }

  for (int idx = 0; idx < DIRECT_PTRS; idx++) {
    if (inode.direct[idx] > 0 && inode.direct[idx] < MAX_FILE_SIZE) {
      disk->readBlock(inode.direct[idx], local_buffer);
      direct = reinterpret_cast<dir_ent_t *>(local_buffer);
      for (size_t idx2 = 0; idx2 < UFS_BLOCK_SIZE / sizeof(dir_ent_t); idx2++) {
        if (std::strcmp(direct[idx2].name, name.c_str()) == 0) {
          return direct[idx2].inum;
        }
      }
    } else {
      return -EINVALIDINODE;
    }
  }
  
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
  if (inodeNumber < 0) {
    return -EINVALIDINODE;
  } else if (size <= 0) {
    return -EINVALIDSIZE;
  }
  
  super_t super;
  readSuperBlock(&super);
  if (inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }
  
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);
  inode_t inode = inodeTable[inodeNumber];

  if (size > inode.size) {
    return -EINVALIDSIZE;
  }
  
  char *data_buffer = static_cast<char *>(buffer);
  int read_byte = 0;
  int bytes_left = 0;

  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }
  for (int idx = 0; idx < blocks; idx++) {
    if (inode.direct[idx] != 0) {
      char block[UFS_BLOCK_SIZE];
      bytes_left = std::min(size, UFS_BLOCK_SIZE);
      
      disk->readBlock(inode.direct[idx], block);
      memcpy(data_buffer + read_byte, block, bytes_left);
      read_byte += bytes_left;
      size -= bytes_left;
    }
  }
  return read_byte;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  return 0;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  inode_t inode;
  super_t super;
  readSuperBlock(&super);

  if (inodeNumber < 0 || inodeNumber > super.num_inodes) {
    return -EINVALIDINODE;
  } else if (size > MAX_FILE_SIZE || size <= 0) {
    return -ENOTENOUGHSPACE;
  } 
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);
  inode = inodeTable[inodeNumber];

  if (inode.type == UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  }
  

  return 0;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  return 0;
}

