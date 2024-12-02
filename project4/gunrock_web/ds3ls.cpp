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

  int local_inum = UFS_ROOT_DIRECTORY_INODE_NUMBER; // Default to root for "/" case
  inode_t inode;
  std::string temp;
  std::stringstream s(directory);
  std::vector<std::string> dirs;
  std::vector<dir_ent_t> files_in_dir;
  
  if (directory != "/") { 
    while (std::getline(s, temp, '/')) {
      if (temp.size()) {
        dirs.push_back(temp);
      }
    }
  }
 
  for (size_t N = 0; N < dirs.size(); N++) {
    // std::cout << "lookup " << dirs[N] << " and " << local_inum << std::endl;
    local_inum = fileSystem->lookup(local_inum, dirs[N]);
    
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

    for (int i = 0; i < blocks; i++) {
      if (inode.direct[i] != 0) {
        char local_buffer[UFS_BLOCK_SIZE]; 
        fileSystem->read(local_inum, local_buffer, inode.size);
        
        for (size_t N = 0; N < inode.size / sizeof(dir_ent_t); N++) {
          dir_ent_t entry;
          std::memcpy(&entry, &local_buffer[N * sizeof(dir_ent_t)], sizeof(dir_ent_t));
          files_in_dir.push_back(entry);
  
        }
      }
    }
    std::sort(files_in_dir.begin(), files_in_dir.end(), compareByName);
    // std::cout << files_in_dir.size() << endl;
    for (size_t N = 0; N < files_in_dir.size(); N++) {
      std::cout << files_in_dir[N].inum << "\t" << files_in_dir[N].name << std::endl;
    }
  }
  
  return 0;
}
