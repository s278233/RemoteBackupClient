//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H


//Types
//-1    =       Error_Msg
//0     =       Header
//1     =       Auth_Message
//2     =

#include <string>
#include <fstream>
#include <utility>
#include <openssl/sha.h>
#include <vector>
#include <iostream>
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>


class Message {
    int type;
    std::vector<char> data;
    size_t size;
    unsigned char hash[SHA256_DIGEST_LENGTH];

public:
    Message();
    Message(int type, std::vector<char> data, size_t size);
    bool checkHash();
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version){
        ar & type;      //Se Archive è un output archive allora & è uguale a <<
        ar & data;      //Se Archive è un input  archive allora & è uguale a >>
        ar & size;
        ar & hash;
    };     //Funzione di supporto per gli archive di boost

    friend std::ostream& operator<<(std::ostream &out, Message& m)
    {
        out << m.type << " " << m.data.data() << " " << m.size << " " << m.hash <<std::endl;
        return out;
    }

    friend class boost::serialization::access;
};



#endif //REMOTEBACKUPCLIENT_MESSAGE_H
