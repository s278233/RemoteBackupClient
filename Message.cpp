//
// Created by lucio on 01/12/2020.
//

#include "Message.h"
#include <cstring>


Message::Message(int type, std::vector<char> data) : type(type), data(std::move(data)) {

        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, this->data.data(), this->data.size());
        SHA256_Final(hash, &sha256);
}

Message::Message(int type): type(type){}


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

const std::vector<char> &Message::getData() const {
    return data;
}

int Message::getType() const {
    return type;
}

Message::Message(const Message &m) {
    this->type = m.type;
    this->data = m.data;
    memcpy(this->hash, m.hash, SHA256_DIGEST_LENGTH);
}

Message::Message() {
    this->type = -100;
}

Message &Message::operator=(const Message &m) {
    this->type = m.type;
    this->data = m.data;
    memcpy(this->hash, m.hash, SHA256_DIGEST_LENGTH);
    return *this;
}

