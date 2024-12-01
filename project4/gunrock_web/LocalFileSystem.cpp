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
      return -EINVALIDINODE; // couldn't find anything matching the name
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

  // checking for valid inode # and size
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
  
  // memcpy (read) data into buffer
  for (int i = 0; i < blocks; i++) {
    if (inode.direct[i] != 0) {
      char block[UFS_BLOCK_SIZE];
      disk->readBlock(inode.direct[i], block);
      memcpy(data_buffer, block, size);
      break;
    }
  }
  return size;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
  char local_buffer[UFS_BLOCK_SIZE];
  inode_t inode;
  inode_t new_inode;
  new_inode.type = type;
  
  if (type == UFS_DIRECTORY) { 
    new_inode.size = 2 * sizeof(dir_ent_t);
  } else {
    new_inode.size = 0;
  }

  int check_stat = stat(parentInodeNumber, &inode);
  int new_inode_num = -1;
  super_t super;
  readSuperBlock(&super);
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);

  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }

  // Checking if parent inode noexistent, not dir, or name is too long
  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes || check_stat < 0) {
    return -EINVALIDINODE;
  } else if (inode.type != UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  } else if (name.size() > DIR_ENT_NAME_SIZE - 1) {
    return -EINVALIDNAME;
  }

  // checking if name exists and is the right type or not
  for (int i = 0; i < blocks; i++) {
    if(inode.direct[i] != 0) {
      disk->readBlock(inode.direct[i], local_buffer);
      dir_ent_t *file_in_dir = reinterpret_cast<dir_ent_t *>(local_buffer);
      
      for (size_t j = 0; j < inode.size / sizeof(dir_ent_t); j++) {
        if (file_in_dir[j].name == name) {
          if (inodeTable[file_in_dir[j].inum].type == type) {
            return file_in_dir[j].inum;
          } else {
            return -EINVALIDTYPE;
          }
        }
      }
    }
  }

  // find a free inode
  unsigned char bitmap[super.num_inodes / 8];
  readInodeBitmap(&super, bitmap);
  for (int i = 0; i < super.num_inodes; i++) {
    if ((bitmap[i / 8] & (1 << (i % 8))) == 0) {
      new_inode_num = i;
      bitmap[new_inode_num / 8] |= (1 << (new_inode_num % 8));
      writeInodeBitmap(&super, bitmap);
      break;
    }
  }
  if (new_inode_num < 0) {
    return -ENOTENOUGHSPACE;
  }

  inodeTable[new_inode_num] = new_inode;
  writeInodeRegion(&super, inodeTable);
  for (int i = 0; i < blocks; i++) {
    if (inode.direct[i] != 0) {
      disk->readBlock(inode.direct[i], local_buffer);
      dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(local_buffer);

      for (size_t j = 0; j < UFS_BLOCK_SIZE / sizeof(dir_ent_t); j++) {
        if (entries[j].inum == 0) {
          std::strcpy(entries[j].name, name.c_str());
          entries[j].inum = new_inode_num;
          disk->writeBlock(inode.direct[i], local_buffer);

          inode.size += sizeof(dir_ent_t);
          inodeTable[parentInodeNumber] = inode;
          writeInodeRegion(&super, inodeTable);

          return new_inode_num;
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
  int bytes_left = 0;

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
  //const char *block_buffer = static_cast<const char*>(buffer);
//  char local_buffer[UFS_BLOCK_SIZE];


  return bytes_left;
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  inode_t inode;
  super_t super;
  readSuperBlock(&super);

  // Check valid parent inode, valid name, and if unlink is allowed
  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes) {
    return -EINVALIDINODE;
  } else if (!name.size() || name.size() > DIR_ENT_NAME_SIZE - 1) {
    return -EINVALIDNAME;
  } else if (name == "." || name == "..") {
    return -EUNLINKNOTALLOWED;
  }

  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);
  inode = inodeTable[parentInodeNumber];
  int local_inum = lookup(parentInodeNumber, name);
  // cout << local_inum << endl;

  if (inode.type != UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  } else if (local_inum < 0) {
    return 0;
  } else if (inode.size != 2 * sizeof(dir_ent_t)) {
    return -EDIRNOTEMPTY;
  }
  
  return 0;
}

