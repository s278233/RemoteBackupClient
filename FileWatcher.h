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

// Define available file changes
enum class FileStatus {created, modified, erased};

 class FileWatcher {
     std::unordered_map<std::string, std::filesystem::file_time_type> paths_;
     bool running_ = true;
 public:
    std::string path_to_watch;
    std::chrono::duration<int, std::milli> delay;

    FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay);
    void start(const std::function<void (std::string, FileStatus)> &action);
 };



#endif //REMOTEBACKUPCLIENT_FILEWATCHER_H
