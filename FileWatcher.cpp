//
// Created by lucio on 03/12/2020.
//
#include <cstdio>
#include <cstring>
#include <fstream>
#include "FileWatcher.h"

#define CHUNK_SIZE 1024

// Keep a record of files from the base directory and their last modification time
FileWatcher::FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay) : path_to_watch{path_to_watch}, delay{delay} {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    std::ifstream ifs;
    std::vector<char> buffer;


    //Genero una map tra il path dei file e data-modifica + hash
    for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
        paths_[file.path().string()].first = std::filesystem::last_write_time(file);
    if(file.is_regular_file()) {
        ifs.open(file.path().string(), std::ios::binary);
        while(!ifs.eof()) {
            ifs.read(buffer.data(), CHUNK_SIZE);
            size_t size= ifs.gcount();
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, buffer.data(), size);
        }
        SHA256_Final(hash, &sha256);
        memcpy(paths_[file.path().string()].second, hash, SHA256_DIGEST_LENGTH);
        ifs.close();
    }
}
}

const std::unordered_map<std::string, std::pair<std::filesystem::file_time_type, unsigned char[32]>> &
FileWatcher::getPaths() const {
    return paths_;
}

// Monitor "path_to_watch" for changes and in case of a change execute the user supplied "action" function
void FileWatcher::start(const std::function<void (std::string, FileStatus)> &action) {
  while(running_) {
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
                                   paths_[file.path().string()].first = current_file_last_write_time;
                                   action(file.path().string(), FileStatus::created);
                           // File modification
                             } else {
                                   if(paths_[file.path().string()].first != current_file_last_write_time) {
                                            paths_[file.path().string()].first = current_file_last_write_time;
                                            action(file.path().string(), FileStatus::modified);
                                        }
                                }
                         }
               }
        }

void FileWatcher::stopRunning(bool running) {
    running_ = false;
}
