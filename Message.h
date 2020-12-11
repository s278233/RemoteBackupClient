//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H

//Tipi di messaggio
#define INVALID 100
#define FILE_ERR -2
#define AUTH_ERR -1
#define AUTH_REQ 0
#define AUTH_RES 1
#define AUTH_OK 2
#define FILE_LIST 3
#define FILE_START 4
#define FILE_DATA 5
#define FILE_END 6

//Delimitatori
#define UDEL "/USERNAME/:"
#define PDEL "/PASSWORD/:"
#define FDEL "/FILE/:"
#define HDEL "/HASH/:"

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
    unsigned char* hash;

    void hashData();

public:
    //Costruttore di copia
    Message(const Message& m);
    //Costruttore di movimento
    Message(Message &&src) noexcept;
    //Overload operatore di assegnazione tramite copia
    Message& operator= (const Message &m);
    //Overload operatore di assegnazione tramite movimento
    Message& operator=(Message&& src) noexcept ;
    //Distruttore
    virtual ~Message();
    //Costruttore per messaggio vuoto
    Message();
    //Costruttore per messaggio senza dato
    explicit Message(int type);
    //Costruttore per dato generico
    Message(int type, std::vector<char> data);
    //Costruttore per coppia<username, password>
    explicit Message(const std::pair<std::string, std::string>& authData);
    //Costruttore per mappa <file/directory, hash>
    explicit Message(const std::unordered_map<std::string, unsigned char*>& paths);

    //SWAP
    friend void swap(Message& src, Message& dst);
    //ToString
    friend std::ostream& operator<<(std::ostream &out, Message& m);
    //Getter per il campo Type
    [[nodiscard]] int getType() const;
    //Getter per il campo data
    [[nodiscard]] const std::vector<char> &getData() const;
    //Funzione di supporto per gli archive di boost
    template<class Archive> void serialize(Archive& ar, unsigned int version);
    //Verifica integrità messaggio
    bool checkHash();
    //Estrazione pair<username, password> dal campo data
    std::pair<std::string, std::string> extractAuthData();
    //Estrazione mappa<file/directory, hash> dal campo data
    std::unordered_map<std::string, unsigned char *> extractFileList();
    //Lettura sincrona del messaggio da boost_socket
    void syncRead(const boost::weak_ptr<tcp::socket> &socket_wptr, void (*connectionHandler)());
    //Scrittura sincrona del messaggio su boost_socket
    void syncWrite(const boost::weak_ptr<tcp::socket> &socket_wptr, void(*connectionHandler)()) const;

    friend class boost::serialization::access;
};



#endif //REMOTEBACKUPCLIENT_MESSAGE_H
