//
// Created by lucio on 03/12/2020.
//
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <iostream>
#include "FileWatcher.h"

#define CHUNK_SIZE 1024

std::string fileHash(const std::string& file){
    unsigned char tmp[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    std::ifstream ifs;
    std::vector<char> buffer(CHUNK_SIZE);


    ifs.open(file, std::ios::binary);
    while(!ifs.eof()) {    std::cout<<"here2"<<std::endl;

        ifs.read(buffer.data(), CHUNK_SIZE);
    size_t size= ifs.gcount();
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, buffer.data(), size);
    }
    SHA256_Final(tmp, &sha256);
    ifs.close();



    //Conversione da unisgned char a string
    char hash_[2*SHA256_DIGEST_LENGTH+1];
    hash_[2*SHA256_DIGEST_LENGTH] = 0;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(hash_+i*2, "%02x", tmp[i]);
    return std::string(hash_);
}

FileWatcher::FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay, std::atomic_bool& running_) : path_to_watch{path_to_watch}, delay{delay}, running_(running_){

    //Creo la cartella se non esiste
    if(!std::filesystem::exists(path_to_watch))
        std::filesystem::create_directory(path_to_watch);

    //Genero una map tra il path dei file e l'hash
    for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
        std::cout<<"here"<<std::endl;
        if(file.is_regular_file())
            paths_[file.path().string()] = fileHash(file.path().string());
        else paths_[file.path().string()] = "";
}
}

// Monitor "path_to_watch" for changes and in case of a change execute the user supplied "action" function
void FileWatcher::start(const std::function<void (std::string, FileStatus)> &action) {
    std::string recomputedHash;
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

                           // File creation
                           if(!paths_.contains(file.path().string())) {
                               paths_[file.path().string()] = fileHash(file.path().string());
                               action(file.path().string(), FileStatus::created);
                           }
                           // File modification

                                   recomputedHash = fileHash(file.path().string());
                                   if (paths_[file.path().string()] != recomputedHash) {
                                       paths_[file.path().string()] = recomputedHash;
                                       recomputedHash = "";
                                       action(file.path().string(), FileStatus::modified);
                                   }

                                }
                         }
               }

const std::unordered_map<std::string, std::string> &FileWatcher::getPaths() const {
    return paths_;
}


