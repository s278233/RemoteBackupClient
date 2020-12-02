//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H


//Types
//-1    =       Error
//0     =       Auth_Message
//1     =       EndInit_Message
//2     =

#include <string>
#include <fstream>
#include <utility>
#include <openssl/sha.h>
#include <vector>
#include <iostream>

class Message {
    int type;
    std::vector<char> data;
    size_t size;
    unsigned char hash[SHA256_DIGEST_LENGTH]{};

public:
    Message(int type, std::vector<char> data, size_t size);
    bool checkHash();
};


#endif //REMOTEBACKUPCLIENT_MESSAGE_H
