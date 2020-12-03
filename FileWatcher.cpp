//
// Created by lucio on 03/12/2020.
//

#include "FileWatcher.h"

// Keep a record of files from the base directory and their last modification time
FileWatcher::FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay) : path_to_watch{path_to_watch}, delay{delay} {
for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
paths_[file.path().string()] = std::filesystem::last_write_time(file);
}
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
                                   paths_[file.path().string()] = current_file_last_write_time;
                                   action(file.path().string(), FileStatus::created);
                           // File modification
                             } else {
                                   if(paths_[file.path().string()] != current_file_last_write_time) {
                                            paths_[file.path().string()] = current_file_last_write_time;
                                            action(file.path().string(), FileStatus::modified);
                                        }
                                }
                         }
               }
        }
