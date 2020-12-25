//
// Created by lucio on 17/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_SAFECOUT_H
#define REMOTEBACKUPCLIENT_SAFECOUT_H


#include <iostream>
#include <mutex>
#include <fstream>

constexpr bool debug = true;

static std::mutex cout_mtx;
static bool firstUse = true;
static std::ofstream ofs_sc;
static std::string log_path;

class SafeCout {

public:
    template<typename... T, typename U>
    static void safe_cout(const U &arg, const T &... args) {
        std::lock_guard<std::mutex> lg(cout_mtx);
        if(debug) {
            if (firstUse) {
                firstUse = false;
                ofs_sc.open(log_path, std::ios::binary);
            } else ofs_sc.open(log_path, std::ios::binary | std::ios_base::app);
        }

        inner_safe_cout(arg, args...);
        std::cout << std::endl;
        if(debug) {
            ofs_sc << std::endl;
            ofs_sc.close();
        }
    }

    static void set_log_path(const std::string& log_directory, const std::string& log_filename){
        if(!std::filesystem::exists(log_directory))
            std::filesystem::create_directory(log_directory);

        log_path = log_directory + log_filename;
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
        if(debug)
        ofs_sc << arg;
    }

};






#endif //REMOTEBACKUPCLIENT_SAFECOUT_H
