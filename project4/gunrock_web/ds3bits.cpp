#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 2) {
    cerr << argv[0] << ": diskImageFile" << endl;
    return 1;
  }

  // Parse command line arguments
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  
  super_t super;
  fileSystem->readSuperBlock(&super);

  int inode_bytes = super.num_inodes / 8;
  int data_bytes = super.num_data / 8;
  unsigned char inode_buffer[inode_bytes];
  unsigned char data_buffer[data_bytes];
  
  // Super's stats
  std::cout << "Super" << std::endl;
  std::cout << "inode_region_addr " << super.inode_region_addr << std::endl;
  std::cout << "inode_region_len " << super.inode_region_len << std::endl;
  std::cout << "num_inodes " << super.num_inodes << std::endl;
  std::cout << "data_region_addr " << super.data_region_addr << std::endl;
  std::cout << "data_region_len " << super.data_region_len << std::endl;
  std::cout << "num_data " << super.num_data << std::endl;
  std::cout << std::endl;

  // Inode's stats
  fileSystem->readInodeBitmap(&super, inode_buffer);
  std::cout << "Inode bitmap" << std::endl;
  for (int i = 0; i < inode_bytes; i++) {
    std::cout << (unsigned int) inode_buffer[i] << " ";
  }
  std::cout << std::endl;
  std::cout << std::endl;

  // Data's stats
  fileSystem->readDataBitmap(&super, data_buffer);
  std::cout << "Data bitmap" << std::endl;
  for (int i = 0; i < data_bytes; i++) {
    std::cout << (unsigned int) data_buffer[i] << " ";
  }
  std::cout << std::endl;
  
  return 0;
}
