//
// Created by lucio on 03/12/2020.
//
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <iostream>
#include "FileWatcher.h"
#include "SafeCout.h"

#define CHUNK_SIZE 1024

std::mutex FileWatcher::path_mtx;
std::map<std::string, std::string> FileWatcher::paths_;


std::string fileHash(const std::string& file){
    unsigned char tmp[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    std::ifstream ifs;
    std::vector<char> buffer(CHUNK_SIZE);

    //Lettura + hash chunk file
    ifs.open(file, std::ios::binary);
    while(!ifs.eof()) {
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

        {
            std::lock_guard<std::mutex> lg(path_mtx);

            //Rileva file cancellato
            auto it = paths_.begin();
            while (it != paths_.end()) {
                if (!std::filesystem::exists(it->first)) {
                    if(it->second.empty()) {
                        action(it->first, FileStatus::erasedDir);
                        //Rimozione di tutti i path inclusi nella cartella eliminata
                        auto itInner = paths_.begin();
                        while(itInner != paths_.end()) {
                            if (itInner->first.find(it->first) != std::string::npos && itInner->first != it->first) {
                                itInner = paths_.erase(itInner);
                            } else itInner++;
                        }
                    }
                    else {
                        action(it->first, FileStatus::erasedFile);
                    }
                    it = paths_.erase(it);
                    if (!running_.load()) return;
                } else {
                    it++;
                }
            }


            // Check if a file was created or modified
            for (auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {

                // File creation
                if (!paths_.contains(file.path().string())) {
                    if(file.is_regular_file())
                        paths_[file.path().string()] = fileHash(file.path().string());
                    else paths_[file.path().string()] = "";
                    action(file.path().string(), FileStatus::created);
                    if (!running_.load()) return;
                }
                // File modification
                if (!std::filesystem::is_directory(file.path().string())) {
                    recomputedHash = fileHash(file.path().string());
                    if (paths_[file.path().string()] != recomputedHash) {
                        paths_[file.path().string()] = recomputedHash;
                        recomputedHash = "";
                        action(file.path().string(), FileStatus::modified);
                        if (!running_.load()) return;
                    }
                }
            }
        }
    }
}

const std::map<std::string, std::string> &FileWatcher::getPaths() {
    return paths_;
}

void FileWatcher::addPath(const std::string &path) {
    std::lock_guard<std::mutex> lg(path_mtx);
    if(std::filesystem::is_regular_file(path))
        paths_[path] = fileHash(path);
    else paths_[path] = "";
}


