//
// Created by lucio on 01/12/2020.
//

#include "Message.h"
#include <cstring>

Message::Message(int type, std::vector<char> data, size_t size) : type(type), data(std::move(data)), size(size) {

    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.data(), data.size());
    SHA256_Final(hash, &sha256);

}

bool Message::checkHash() {

    unsigned char hashRecomputed[SHA256_DIGEST_LENGTH];

    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.data(), data.size());
    SHA256_Final(hashRecomputed, &sha256);

    bool isEqual = (memcmp(hash, hashRecomputed, SHA256_DIGEST_LENGTH) == 0);

    return isEqual;
}
