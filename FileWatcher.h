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


// Define available file changes
enum class FileStatus {created, modified, erased};

 class FileWatcher {
 private:
     std::unordered_map<std::string, unsigned char*> paths_;
     std::string path_to_watch;
     std::chrono::duration<int, std::milli> delay;
     std::atomic_bool& running_;
 public:
    FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay, std::atomic_bool& running);
    const std::unordered_map<std::string, unsigned char[32]> & getPaths() const;
    void start(const std::function<void (std::string, FileStatus)> &action);
 };



#endif //REMOTEBACKUPCLIENT_FILEWATCHER_H
