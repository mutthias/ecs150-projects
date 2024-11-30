#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <sstream>

#include "StringUtils.h"
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


  // Use this function with std::sort for directory entries
bool compareByName(const dir_ent_t& a, const dir_ent_t& b) {
    return std::strcmp(a.name, b.name) < 0;
}


int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile directory" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img /a/b" << endl;
    return 1;
  }

  // parse command line arguments
  
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  string directory = string(argv[2]);

  int local_inum = UFS_ROOT_DIRECTORY_INODE_NUMBER;
  inode_t inode;
  std::string temp;
  std::stringstream s(directory);
  std::vector<std::string> dirs;
  std::vector<dir_ent_t> files_in_dir;
  
  if (directory == "/") {
    dirs = {};
  } else {
    while (std::getline(s, temp, '/')) {
      if (temp.size()) {
        dirs.push_back(temp);
      }
    }
  }
 
  for (size_t idx = 0; idx < dirs.size(); idx++) {
    // std::cout << "lookup " << dirs[idx] << " and " << local_inum << std::endl;
    local_inum = fileSystem->lookup(local_inum, dirs[idx]);
    if (local_inum < 0) {
      std::cerr << "Directory not found" << std::endl;
      return 1;
    }
  }

  if (fileSystem->stat(local_inum, &inode) < 0) {
    std::cerr << "Directory not found" << std::endl;
    return 1;
  }

  if (inode.type == UFS_REGULAR_FILE) {
    std::cout << local_inum << "\t" << dirs.back() << std::endl;
  } else {
    int blocks = inode.size / UFS_BLOCK_SIZE;
    if ((inode.size % UFS_BLOCK_SIZE) != 0) {
      blocks += 1;
    }
    for (int idx = 0; idx < blocks; idx++) {
      if (inode.direct[idx] != 0) {
        char local_buffer[UFS_BLOCK_SIZE]; 
        fileSystem->read(local_inum, local_buffer, inode.size);
        dir_ent_t *file_list = reinterpret_cast<dir_ent_t *>(local_buffer);
        
        for (size_t idx2 = 0; idx2 < inode.size / sizeof(dir_ent_t); idx2++) {
          files_in_dir.push_back(file_list[idx2]);
        }
      }
    }
    std::sort(files_in_dir.begin(), files_in_dir.end(), compareByName);

    for (size_t idx = 0; idx < files_in_dir.size(); idx++) {
      std::cout << files_in_dir[idx].inum << "\t" << files_in_dir[idx].name << std::endl;
    }
  }
  
  return 0;
}
