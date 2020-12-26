//
// Created by lucio on 17/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_SAFECOUT_H
#define REMOTEBACKUPCLIENT_SAFECOUT_H


#include <iostream>
#include <mutex>
#include <fstream>
#include <filesystem>

constexpr bool debug = true;

class SafeCout {
    static std::mutex cout_mtx;
    static std::string log_path;

public:
    template<typename... T, typename U>
    static void safe_cout(const U &arg, const T &... args) {
        std::lock_guard<std::mutex> lg(cout_mtx);

        std::ofstream ofs(log_path, std::ios::binary | std::ios_base::app);

        inner_safe_cout(arg, args...);
        std::cout << std::endl;
        if(debug) {
            ofs<<std::endl;
        }
    }

    static void set_log_path(const std::string& log_path_){
        log_path = log_path_;
    }


private:
    template<typename... T, typename U>
    static void inner_safe_cout(const U &arg, const T &... args) {
        inner_safe_cout(arg);
        inner_safe_cout(args...);
    }

    template<typename T>
    static void inner_safe_cout(const T &arg) {
        std::cout << arg;
        if(debug) {
            std::ofstream ofs(log_path, std::ios::binary | std::ios_base::app);
            if(ofs.is_open())
            ofs << arg;
        }
    }

};


inline  std::string SafeCout::log_path;
inline std::mutex SafeCout::cout_mtx;

#endif //REMOTEBACKUPCLIENT_SAFECOUT_H
