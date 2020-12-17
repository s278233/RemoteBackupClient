//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H

//Tipi di messaggio
#define INVALID    -100
#define FILE_ERR   -2
#define AUTH_ERR   -1
#define AUTH_REQ    0
#define AUTH_RES    1
#define AUTH_OK     2
#define FILE_LIST   3
#define DIR         4
#define FILE_START  5
#define FILE_DATA   6
#define FILE_END    7

//Delimitatori
#define UDEL "/:USERNAME/"
#define PDEL "/:PASSWORD/"
#define FDEL "/:FILE/"
#define HDEL "/:HASH/"

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
    std::string hash;

    static std::mutex asyncR_mtx;
    static std::mutex asyncW_mtx;

    void hashData();

public:
    Message(const Message& m);  //Costruttore di copia
    Message(Message &&src) noexcept;    //Costruttore di movimento
    Message& operator= (const Message &m);  //Overload operatore di assegnazione tramite copia
    Message& operator=(Message&& src) noexcept ;    //Overload operatore di assegnazione tramite movimento
    virtual ~Message(); //Distruttore

    Message();  //Costruttore per messaggio vuoto

    explicit Message(int type); //Costruttore per messaggio senza dato

    Message(int type, std::vector<char> data);  //Costruttore per dato generico

    explicit Message(const std::pair<std::string, std::string>& authData);  //Costruttore per coppia<username, password>

    explicit Message(const std::unordered_map<std::string, std::string>& paths);    //Costruttore per mappa <file/directory, hash>


    friend void swap(Message& src, Message& dst);   //SWAP

    friend std::ostream& operator<<(std::ostream &out, Message& m); //ToString

    [[nodiscard]] int getType() const;  //Getter per il campo Type

    [[nodiscard]] const std::vector<char> &getData() const; //Getter per il campo data

    template<class Archive> void serialize(Archive& ar, unsigned int version);  //Funzione di supporto per gli archive di boost

    std::optional<bool> checkHash();   //Verifica integrità messaggio

    std::optional<std::pair<std::string, std::string>> extractAuthData();   //Estrazione pair<username, password> dal campo data

    std::optional<std::unordered_map<std::string, std::string>> extractFileList();  //Estrazione mappa<file/directory, hash> dal campo data

    void syncRead(const boost::weak_ptr<tcp::socket> &socket_wptr);    //Lettura sincrona del messaggio da boost_socket

    void syncWrite(const boost::weak_ptr<tcp::socket> &socket_wptr) const;  //Scrittura sincrona del messaggio su boost_socket

    friend class boost::serialization::access;
};



#endif //REMOTEBACKUPCLIENT_MESSAGE_H
