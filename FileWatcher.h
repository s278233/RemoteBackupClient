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
#include "Message.h"


// Define available file changes
enum class FileStatus {created, modified, erasedFile, erasedDir};

 class FileWatcher {
     static std::map<std::string, std::string> paths_;
     static std::mutex path_mtx;
 public:
     static const std::map<std::string, std::string> &getPaths() ;

 private:
     std::string path_to_watch;
     std::chrono::duration<int, std::milli> delay;
     std::atomic_bool& running_;
 public:
     FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay, std::atomic_bool& running);
     void start(const std::function<void (std::string, FileStatus)> &action);
     static void addPath(const std::string& path);
 };



#endif //REMOTEBACKUPCLIENT_FILEWATCHER_H
