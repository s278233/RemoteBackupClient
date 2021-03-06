//
// Created by lucio on 03/12/2020.
//
#include "FileWatcher.h"

std::mutex FileWatcher::path_mtx;
std::map<std::string, std::string> FileWatcher::paths_;

std::string FileWatcher::fileHash(const std::string& file){
    int success;
    unsigned char tmp[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    std::ifstream ifs;
    std::vector<char> buffer(HASH_CHUNK_SIZE);

    success = SHA256_Init(&sha256);

    if(!success) return "";

    //Lettura + hash chunk file
    ifs.open(file, std::ios::binary);
    while(!ifs.eof()) {
        if(!std::filesystem::exists(file)) {
            success = 0;
            break;
        }
        ifs.read(buffer.data(), HASH_CHUNK_SIZE);
        size_t size= ifs.gcount();
        success = SHA256_Update(&sha256, buffer.data(), size);
        if(!success) break;
    }

    if(!success) {
        return "";
    }

    ifs.close();

    success = SHA256_Final(tmp, &sha256);

    if(!success) {
        return "";
    }

    return Message::unsignedCharToHEX(tmp, SHA256_DIGEST_LENGTH);
}

FileWatcher::FileWatcher(const std::string& path_to_watch, std::chrono::duration<int, std::milli> delay, std::atomic_bool& running_) : path_to_watch{path_to_watch}, delay{delay}, running_(running_){

    //Creo la cartella se non esiste
    if(!std::filesystem::exists(path_to_watch))
        std::filesystem::create_directory(path_to_watch);

    //Genero una map tra il path dei file e l'hash
    for(auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
        if (file.path().string().find(TMP_PLACEHOLDER) != std::string::npos)
            std::remove(file.path().string().c_str());
        else if(file.is_regular_file())
            paths_[file.path().string()] = fileHash(file.path().string());
        else paths_[file.path().string()] = "";
}
}

// Monitoro Creazione/Modifica/Cancellazione File/Directory
void FileWatcher::start(const std::function<void (std::string, FileStatus)> &action) {
    std::string recomputedHash;

    while(true) {
        //Dorme per un tempo pari a delay
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
                } else {
                    it++;
                }
            }

            if (!running_.load()) return;


            // Rilevo creazione e modifica file/directory
            for (auto &file : std::filesystem::recursive_directory_iterator(path_to_watch)) {
                if (!paths_.contains(file.path().string())) {
                    // Creazione file
                    if(file.is_regular_file()) {
                        paths_[file.path().string()] = fileHash(file.path().string());
                        action(file.path().string(), FileStatus::createdFile);
                    }
                    // Creazione directory
                    else {
                        paths_[file.path().string()] = "";
                        action(file.path().string(), FileStatus::createdDir);
                    }
                }
                // Modifica file
                if (!std::filesystem::is_directory(file.path().string())) {
                    recomputedHash = fileHash(file.path().string());
                    if (paths_[file.path().string()] != recomputedHash) {
                        paths_[file.path().string()] = recomputedHash;
                        recomputedHash = "";
                        action(file.path().string(), FileStatus::modifiedFile);
                    }
                }
            }
        }
    }
}

const std::map<std::string, std::string> &FileWatcher::getPaths() {
    return paths_;
}

void FileWatcher::addPath(const std::string &path, const std::string &tmp_path) {
    std::lock_guard<std::mutex> lg(path_mtx);

    if(std::filesystem::is_regular_file(tmp_path)) {
        std::rename(tmp_path.c_str(), path.c_str());
        paths_[path] = fileHash(path);
    }
    else paths_[path] = "";
}


