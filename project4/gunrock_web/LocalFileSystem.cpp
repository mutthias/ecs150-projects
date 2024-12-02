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
  int bytes_to_read = super->num_inodes / 8;
  int blocks_to_read = (bytes_to_read + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

  for (int i = 0; i < blocks_to_read; i++) {
    char local_buffer[UFS_BLOCK_SIZE];
    disk->readBlock(super->inode_bitmap_addr + i, local_buffer);

    int copy_size = std::min(UFS_BLOCK_SIZE, bytes_to_read - (i * UFS_BLOCK_SIZE));
    memcpy(inodeBitmap + (i * UFS_BLOCK_SIZE), local_buffer, copy_size);
  }
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  int bytes_to_write = super->num_inodes / 8;
  int blocks_to_write = (bytes_to_write + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;

  for (int i = 0; i < blocks_to_write; i++) {
    char local_buffer[UFS_BLOCK_SIZE];
    int copy_size = std::min(UFS_BLOCK_SIZE, bytes_to_write - (i * UFS_BLOCK_SIZE));
    memcpy(local_buffer, inodeBitmap + (i * UFS_BLOCK_SIZE), copy_size);

    disk->writeBlock(super->inode_bitmap_addr + i, local_buffer);
  }
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
  int inodes_per_block = UFS_BLOCK_SIZE / sizeof(inode_t);

  for (int block_index = 0; block_index < super->inode_region_len; block_index++) {
    char local_buffer[UFS_BLOCK_SIZE];
    int start_inode = block_index * inodes_per_block;
    int end_inode = std::min(start_inode + inodes_per_block, super->num_inodes);

    memcpy(local_buffer, &inodes[start_inode], (end_inode - start_inode) * sizeof(inode_t));
    disk->writeBlock(super->inode_region_addr + block_index, local_buffer);
  }
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  char local_buffer[UFS_BLOCK_SIZE];
  inode_t inode;

  if (stat(parentInodeNumber, &inode) < 0) { // Check if name even exists
    return -ENOTFOUND;
  } else if (inode.type != UFS_DIRECTORY) { // Now check invalid parent inode
    return -EINVALIDINODE;
  }

  for (int i = 0; i < DIRECT_PTRS; i++) {
    if (inode.direct[i] > 0 && inode.direct[i] < MAX_FILE_SIZE) {
      disk->readBlock(inode.direct[i], local_buffer);
      dir_ent_t entry;

      for (size_t N = 0; N < UFS_BLOCK_SIZE / sizeof(dir_ent_t); N++) {
        memcpy(&entry, local_buffer + N * sizeof(dir_ent_t), sizeof(dir_ent_t));
        if (std::strcmp(entry.name, name.c_str()) == 0) {
          return entry.inum;
        }
      }
    } else {
      return -EINVALIDINODE; // couldn't find anything matching the name
    }
  }
  
  return -ENOTFOUND;
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
  int check_stat = stat(parentInodeNumber, &inode);

  super_t super;
  readSuperBlock(&super);
  
  inode_t inodeTable[super.num_inodes];
  readInodeRegion(&super, inodeTable);

  unsigned char bitmap[super.num_inodes / 8];
  readInodeBitmap(&super, bitmap);

  unsigned char data_bitmap[super.num_data / 8];
  readDataBitmap(&super, data_bitmap);

  unsigned char inode_bitmap[super.num_inodes / 8];
  readInodeBitmap(&super, inode_bitmap);

  // Checking if parent inode noexistent, not dir, or name is too long
  if (parentInodeNumber < 0 || parentInodeNumber >= super.num_inodes || check_stat < 0) {
    return -EINVALIDINODE;
  } else if (inode.type != UFS_DIRECTORY) {
    return -EINVALIDTYPE;
  } else if (name.size() > DIR_ENT_NAME_SIZE - 1) {
    return -EINVALIDNAME;
  }

  // checking if name exists and is the right type or not
  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }

  for (int i = 0; i < blocks; i++) {
    if (inode.direct[i] >= 0 && inode.direct[i] < (unsigned)disk->numberOfBlocks()) {
      disk->readBlock(inode.direct[i], local_buffer);
      dir_ent_t entry;
      
      for (size_t N = 0; N < inode.size / sizeof(dir_ent_t); N++) {
        memcpy(&entry, local_buffer + N * sizeof(dir_ent_t), sizeof(dir_ent_t));
        if (std::strcmp(entry.name, name.c_str()) == 0) {
          if (inodeTable[entry.inum].type == type) {
            return entry.inum;
          } else {
            return -EINVALIDTYPE;
          }
        }
      }
    }
  }

  // Find a free noode
  int new_inode_num = -1;
  for (int i = 0; i < super.num_inodes; i++) {
    if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
      new_inode_num = i;
      inode_bitmap[i / 8] |= (1 << (i % 8));
      break;
    }
  }
  if (new_inode_num == -1){
    return -ENOTENOUGHSPACE;
  } 

// then set up the new node
  inode_t newInode = {};
  newInode.type = type;
  newInode.size = 0;
  for (int i = 0; i < DIRECT_PTRS; i++) {
    newInode.direct[i] = 0;
  }
  inodeTable[new_inode_num] = newInode;

  // write content of new node to directory (???)
  bool is_inum_added = false;
  for (int i = 0; i < blocks; i++) {
    if (inode.direct[i] >= 0 && inode.direct[i] < (unsigned)disk->numberOfBlocks()) {
      disk->readBlock(inode.direct[i], local_buffer);
        for (size_t N = 0; N < UFS_BLOCK_SIZE / sizeof(dir_ent_t); N++) {
          dir_ent_t entry;
          memcpy(&entry, local_buffer + N * sizeof(dir_ent_t), sizeof(dir_ent_t));
          if (entry.inum == 0) {
            strncpy(entry.name, name.c_str(), DIR_ENT_NAME_SIZE - 1);
            entry.name[DIR_ENT_NAME_SIZE - 1] = '\0';
            entry.inum = new_inode_num;
            memcpy(local_buffer + N * sizeof(dir_ent_t), &entry, sizeof(dir_ent_t));
            disk->writeBlock(inode.direct[i], local_buffer);
            is_inum_added = true;
            break;
          }
        }
      }
    if (is_inum_added) {
      break;
    }
  }

  if (!is_inum_added) {
    return -ENOTENOUGHSPACE;
  }


  writeInodeBitmap(&super, inode_bitmap);
  writeInodeRegion(&super, inodeTable);

  inode.size += sizeof(dir_ent_t);
  inodeTable[parentInodeNumber] = inode;
  writeInodeRegion(&super, inodeTable);

  return new_inode_num;

  // return -1;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  super_t super;
  readSuperBlock(&super);

  unsigned char data_buffer[super.num_data / 8];
  inode_t inode;
  inode_t inodeTable[super.num_inodes];
  int bytes_left = 0;

  // int blocks = size / UFS_BLOCK_SIZE;
  // if ((size % UFS_BLOCK_SIZE) != 0) {
  //   blocks += 1;
  // }

  if (inodeNumber < 0 || inodeNumber > super.num_inodes) {
    return -EINVALIDINODE;
  } else if (size > MAX_FILE_SIZE || size <= 0) {
    return -ENOTENOUGHSPACE;
  } 

  readInodeRegion(&super, inodeTable);
  inode = inodeTable[inodeNumber];

  if (inode.type == UFS_DIRECTORY) {
    return -EWRITETODIR;
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

