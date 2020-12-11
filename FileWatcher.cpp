//
// Created by lucio on 03/12/2020.
//
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include "FileWatcher.h"

#define CHUNK_SIZE 1024

unsigned char* fileHash(const std::string& file){
    auto* hash = new unsigned char[SHA256_DIGEST_LENGTH]();
    SHA256_CTX sha256;
    std::ifstream ifs;
    std::vector<char> buffer;

    ifs.open(file, std::ios::binary);
    while(!ifs.eof()) {
    ifs.read(buffer.data(), CHUNK_SIZE);
    size_t size= ifs.gcount();
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, buffer.data(), size);
    }
    SHA256_Final(hash, &sha256);
    ifs.close();

    return hash;
}

FileWatcher::FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay, std::atomic_bool& running_) : path_to_watch{path_to_watch}, delay{delay}, running_(running_){



    //Genero una map tra il path dei file e l'hash
    for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
        paths_[file.path().string()] = nullptr;
        if(file.is_regular_file())
            paths_[file.path().string()] = fileHash(file.path().string());
}
}

// Monitor "path_to_watch" for changes and in case of a change execute the user supplied "action" function
void FileWatcher::start(const std::function<void (std::string, FileStatus)> &action) {
    unsigned char* recomputedHash;
    while(running_.load()) {
          // Wait for "delay" milliseconds
             std::this_thread::sleep_for(delay);

                    auto it = paths_.begin();
                     while (it != paths_.end()) {
                        if (!std::filesystem::exists(it->first)) {
                                 action(it->first, FileStatus::erased);
                                 it = paths_.erase(it);
                             }  else {
                               it++;
                            }
                      }

                 // Check if a file was created or modified
                 for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
                          auto current_file_last_write_time = std::filesystem::last_write_time(file);

                           // File creation
                           if(!paths_.contains(file.path().string())) {
                                   paths_[file.path().string()] = fileHash(file.path().string());
                                   action(file.path().string(), FileStatus::created);
                           // File modification
                             } else {
                               recomputedHash = fileHash(file.path().string());
                                   if(memcmp(paths_[file.path().string()], recomputedHash, SHA256_DIGEST_LENGTH) != 0)  {
                                            paths_[file.path().string()] = recomputedHash;
                                            recomputedHash = nullptr;
                                            action(file.path().string(), FileStatus::modified);
                                        }
                                }
                         }
               }
        }


