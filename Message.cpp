//
// Created by lucio on 01/12/2020.
//

#include "Message.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <boost/asio/buffer.hpp>


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

void Message::syncWrite(const boost::weak_ptr<tcp::socket>& socket_wptr, std::size_t connectionHandler(const boost::system::error_code& error, std::size_t bytes_transferred)){

    //Serializzazione messaggio
    std::ostringstream archive_stream;
    auto ota = text_oarchive(archive_stream);
    ota << *this;
    std::string outbound_data_ = archive_stream.str();

    //Serializzazione header
    std::ostringstream header_stream;
    header_stream << std::setw(HEADER_LENGTH)<< std::hex << outbound_data_.size();
    std::string outbound_header_ = header_stream.str();

    size_t sizeW;

    //Invio
    std::vector<boost::asio::const_buffer> buffers;
    buffers.emplace_back(boost::asio::buffer(outbound_header_));
    buffers.emplace_back(boost::asio::buffer(outbound_data_));
    sizeW = boost::asio::write((*socket_wptr.lock()), buffers);

    if(sizeW != (buffers[0].size() + buffers[1].size())) throw std::runtime_error("Write Error");
}

void Message::syncRead(const boost::weak_ptr<tcp::socket>& socket_wptr, std::size_t connectionHandler(const boost::system::error_code& error, std::size_t bytes_transferred)){

    std::vector<char> header(HEADER_LENGTH);
    size_t message_length;
    size_t sizeR;

    //Ricezione Header
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(header));

    if(sizeR != header.size()) throw std::runtime_error("Broken Header");

    //Deserializzazione Header
    std::istringstream header_stream(std::string(header.begin(),header.end()));
    header_stream >> std::hex >> message_length;

    //Ricezione Messaggio
    std::vector<char> message(message_length);
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(message));

    if(sizeR != message_length) throw std::runtime_error("Broken Message");

    //Deserializzazione Messaggio
    std::istringstream archive_stream(std::string(message.begin(), message.end()));
    auto ita = text_iarchive(archive_stream);
    ita >> *this;
}

