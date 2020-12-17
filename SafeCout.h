//
// Created by lucio on 17/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_SAFECOUT_H
#define REMOTEBACKUPCLIENT_SAFECOUT_H


#include <iostream>
#include <mutex>


class SafeCout {
    static std::mutex cout_mtx;

public:
    template<typename... T, typename U>
    static void safe_cout(const U &arg, const T &... args) {
        std::lock_guard<std::mutex> lg(cout_mtx);
        inner_safe_cout(arg, args...);
        std::cout << std::endl;
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
    }

};






#endif //REMOTEBACKUPCLIENT_SAFECOUT_H
