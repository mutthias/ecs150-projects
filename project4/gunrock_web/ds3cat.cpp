#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

#include <unistd.h>

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile inodeNumber" << endl;
    return 1;
  }

  // Parse command line arguments
  
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  int inodeNumber = stoi(argv[2]);
  
  inode_t inode;
  if (fileSystem->stat(inodeNumber, &inode) < 0) {
    std::cerr << "Error reading file" << std::endl;
    return 1;
  }

  if (inode.type == UFS_DIRECTORY) {
    std::cerr << "Error reading file" << std::endl;
    return 1;
  }

  char buffer[UFS_BLOCK_SIZE];
  int bytes_left = 0;
  int blocks = inode.size / UFS_BLOCK_SIZE;
  if ((inode.size % UFS_BLOCK_SIZE) != 0) {
    blocks += 1;
  }

  std::cout << "File blocks" << std::endl;
  for (int idx = 0; idx < blocks; idx++) {
    if (inode.direct[idx] != 0) {
      std::cout << inode.direct[idx] << std::endl;
    }
  }
  std::cout << std::endl;

  std::cout << "File data" << std::endl;
  for (int idx = 0; idx < blocks; idx++) {
    if (inode.direct[idx] != 0) {
      int data = std::min(inode.size - bytes_left, UFS_BLOCK_SIZE);
      disk->readBlock(inode.direct[idx], buffer);
      write(STDOUT_FILENO, buffer, data);
      bytes_left += data;
    }
  }

  return 0;
}
