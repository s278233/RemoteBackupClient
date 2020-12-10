//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H

//Error types
#define FILE_ERR -2
#define AUTH_ERR -1
#define AUTH_REQ 0
#define AUTH_RES 1
#define AUTH_OK 2
#define FILE_START 2
#define FILE_DATA 3
#define FILE_END 4

#define HEADER_LENGTH 8

#include <string>
#include <fstream>
#include <utility>
#include <openssl/sha.h>
#include <vector>
#include <iostream>
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/asio.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/weak_ptr.hpp>


using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;


class Message {
    int type;
    std::vector<char> data{};
    unsigned char hash[SHA256_DIGEST_LENGTH]{};

public:
    Message();
    Message(int type);
    Message(int type, std::vector<char> data);
    Message(const Message& m);
    Message& operator= (const Message &m);
    int getType() const;
    const std::vector<char> &getData() const;
    bool checkHash();
    template<class Archive> void serialize(Archive& ar, const unsigned int version){
        ar & type;      //Se Archive è un output archive allora & è uguale a <<
        ar & data;      //Se Archive è un input  archive allora & è uguale a >>
        ar & hash;
    };     //Funzione di supporto per gli archive di boost

    friend std::ostream& operator<<(std::ostream &out, Message& m)
    {
        out << m.type << " " << m.data.data() << " " << m.hash <<std::endl;
        return out;
    }

    friend class boost::serialization::access;

    void syncWrite(const boost::weak_ptr<tcp::socket> &socket_wptr,
                   size_t (*connectionHandler)(const boost::system::error_code &, size_t));

    void syncRead(const boost::weak_ptr<tcp::socket> &socket_wptr,
                     size_t (*connectionHandler)(const boost::system::error_code &, size_t));
};



#endif //REMOTEBACKUPCLIENT_MESSAGE_H
