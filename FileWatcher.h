//
// Created by lucio on 03/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_FILEWATCHER_H
#define REMOTEBACKUPCLIENT_FILEWATCHER_H

#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <functional>
#include <openssl/sha.h>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <map>
#include <iostream>
#include "SafeCout.h"
#include "Message.h"

#define TMP_PLACEHOLDER "tmpFileDownload.tmp"

#define HASH_CHUNK_SIZE  1024

// Define available file changes
enum class FileStatus {created, modified, erasedFile, erasedDir};

 class FileWatcher {
     static std::map<std::string, std::string> paths_;
     static std::mutex path_mtx;
     std::string path_to_watch;
     std::chrono::duration<int, std::milli> delay;
     std::atomic_bool& running_;

     static std::string fileHash(const std::string &file);
 public:
     FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay, std::atomic_bool& running);
     void start(const std::function<void (std::string, FileStatus)> &action);
     static const std::map<std::string, std::string> &getPaths() ;
     static void addPath(const std::string &path, const std::string &tmp_path);

 };



#endif //REMOTEBACKUPCLIENT_FILEWATCHER_H
