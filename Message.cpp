//
// Created by lucio on 01/12/2020.
//

#include "Message.h"
#include <cstring>


Message::Message(int type, std::vector<char> data, size_t size) : type(type), data(std::move(data)), size(size) {

    if(!this->data.empty()) {

        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, this->data.data(), this->data.size());
        SHA256_Final(hash, &sha256);
    }

}

bool Message::checkHash() {

    if(this->data.empty()) return -1;

    unsigned char hashRecomputed[SHA256_DIGEST_LENGTH];

    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, this->data.data(), this->data.size());
    SHA256_Final(hashRecomputed, &sha256);

    bool isEqual = (memcmp(hash, hashRecomputed, SHA256_DIGEST_LENGTH) == 0);

    return isEqual;
}

Message::Message() {
    type = -100;
    size = -1;
}
