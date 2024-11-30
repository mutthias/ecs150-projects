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
  // Read for each inode
  for (int i = 0; i < super->num_inodes; i++) {
    char local_buffer[UFS_BLOCK_SIZE];
    int block_offset = i / inodes_per_block;
    int inode_offset = i % inodes_per_block;
    disk->readBlock(super->inode_region_addr + block_offset, local_buffer);
    memcpy(&inodes[i], (sizeof(inode_t) * inode_offset) + local_buffer, sizeof(inode_t));
  }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  for (int i = 0; i < super->num_inodes; i++) {
    char local_buffer[UFS_BLOCK_SIZE];
    memcpy(local_buffer, &inodes[i], sizeof(inode_t));
    disk->writeBlock(super->inode_region_addr + i, local_buffer);
  }
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  char local_buffer[UFS_BLOCK_SIZE];
  dir_ent_t *direct;
  inode_t inode;

  if (stat(parentInodeNumber, &inode) < 0) { // Check if name even exists
    return -ENOTFOUND;
  } else if (inode.type != UFS_DIRECTORY) { // Now check invalid parent inode
    return -EINVALIDINODE;
  }

  for (int i = 0; i < DIRECT_PTRS; i++) {
    if (inode.direct[i] > 0 && inode.direct[i] < MAX_FILE_SIZE) {
      disk->readBlock(inode.direct[i], local_buffer);
      direct = reinterpret_cast<dir_ent_t *>(local_buffer);

      for (size_t N = 0; N < UFS_BLOCK_SIZE / sizeof(dir_ent_t); N++) {
        if (std::strcmp(direct[N].name, name.c_str()) == 0) {
          return direct[N].inum;
        }
      }
    } else {
      return -EINVALIDINODE; // couldn't find anything
    }
  }
  
  return 0;
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  super_t super;
  readSuperBlock(&super);

  // Check invalid inode #
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }

  // Inode fine, fill inode table
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);
  *inode = inodeTable[inodeNumber];

  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  char *data_buffer = static_cast<char *>(buffer);
  super_t super;
  readSuperBlock(&super);

  // Check for valid inode # and size
  if (inodeNumber < 0 || inodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  }
  
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);
  inode_t inode = inodeTable[inodeNumber];

  // check valid size
  if (size <= 0 || size > inode.size) {
    return -EINVALIDSIZE;
  }
  
  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }
  
  for (int idx = 0; idx < blocks; idx++) {
    if (inode.direct[idx] != 0) {
      char block[UFS_BLOCK_SIZE];
      disk->readBlock(inode.direct[idx], block);
      memcpy(data_buffer, block, size);
      break;
    }
  }
  return size;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  inode_t inode;
  int check_stat = stat(parentInodeNumber, &inode);
  super_t super;
  readSuperBlock(&super);
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);

  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }

  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes || check_stat < 0) {
    return -EINVALIDINODE;
  } else if (inode.type != UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  } else if (name.size() > 255) {
    return -EINVALIDNAME;
  }

  for (int idx = 0; idx < blocks; idx++) {
    if(inode.direct[idx] != 0) {
      char local_buffer[UFS_BLOCK_SIZE];
      disk->readBlock(inode.direct[idx], local_buffer);
      dir_ent_t *file_in_dir = reinterpret_cast<dir_ent_t *>(local_buffer);
      
      for (size_t idx2 = 0; idx2 < inode.size / sizeof(dir_ent_t); idx2++) {
        if (file_in_dir->name == name) {
          if (inodeTable[file_in_dir->inum].type == type) {
            return file_in_dir->inum;
          } else {
            return -EINVALIDTYPE;
          }
        }
      }
    }
  }
  
  return 0;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  super_t super;
  readSuperBlock(&super);

  unsigned char data_buffer[super.num_data / 8];
  inode_t inode;
  inode_t inodeTable[super.num_inodes];
  int read_byte = 0;
  int bytes_left = 0;
  int blocksNeeded = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
  int currentBlocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

  int blocks = size / UFS_BLOCK_SIZE;
  if ((size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }

  if (inodeNumber < 0 || inodeNumber > super.num_inodes) {
    return -EINVALIDINODE;
  } else if (size > MAX_FILE_SIZE || size <= 0) {
    return -ENOTENOUGHSPACE;
  } 

  readInodeRegion(&super, inodeTable);
  inode = inodeTable[inodeNumber];

  if (inode.type == UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  }

  readDataBitmap(&super, data_buffer);
  const char *block_buffer = static_cast<const char*>(buffer);
  char local_buffer[UFS_BLOCK_SIZE];

  for (int idx = 0; idx < blocks; idx++) {
    memset(local_buffer, 0, UFS_BLOCK_SIZE);
    read_byte = std::min(UFS_BLOCK_SIZE, size - bytes_left);
    memcpy(local_buffer, block_buffer + bytes_left, read_byte);
    disk->writeBlock(inode.direct[idx], local_buffer);
    bytes_left += read_byte;
  }

  for (int idx = currentBlocks; idx < blocksNeeded; idx++) {
    bool blockAllocated = false;
    int bytesToWrite = std::min(UFS_BLOCK_SIZE, size - bytes_left);
    for (int idx2 = 0; idx < super.num_inodes; idx2++) {
      if ((data_buffer[idx2 / 8]) & (1 << (idx2 % 8) == 0)) {
        data_buffer[idx2 / 8] |= (1 << (idx2 % 8));
        inode.direct[idx] = super.data_region_addr + idx2;
        blockAllocated = true;
        break;
      }
    }
    if (blockAllocated) {
      writeInodeRegion(&super, inodeTable);
      writeDataBitmap(&super, data_buffer);
      return bytes_left;
    }
    memset(local_buffer, 0, UFS_BLOCK_SIZE);
    memcpy(local_buffer, block_buffer + bytes_left, bytesToWrite);
    disk->writeBlock(inode.direct[idx], local_buffer);
    bytes_left += bytesToWrite;
  }


    // Free excess blocks if the file shrinks
    for (int i = blocksNeeded; i < currentBlocks; i++) {
    int blockNumber = inode.direct[i] - super.data_region_addr;
    int byteIndex = blockNumber / 8;
    int bitIndex = blockNumber % 8;
    data_buffer[byteIndex] &= ~(1 << bitIndex); // Mark block as free
    inode.direct[i] = 0; // Clear the pointer
  }

  inode.size = bytes_left;
  writeInodeRegion(&super, inodeTable);

  return bytes_left;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  super_t super;
  readSuperBlock(&super);
  return 0;
}

