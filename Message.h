//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H

//Tipi di messaggio
#define INVALID    -100
#define AUTH_REQ    0
#define AUTH_RES    1
#define FILE_LIST   2
#define DIR         3
#define DIR_DEL     4
#define FILE_START  5
#define FILE_DATA   6
#define FILE_END    7
#define FILE_DEL    8


//Delimitatori
#define UDEL "/:USERNAME/"
#define PDEL "/:PASSWORD/"
#define FDEL "/:FILE/"
#define HDEL "/:HASH/"

#define HEADER_LENGTH   8


#include <string>
#include <fstream>
#include <utility>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <iostream>
#include <boost/serialization/access.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/asio.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_unique.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <functional>
#include <cstring>
#include <sstream>
#include <iomanip>
#include "SafeCout.h"

using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;

class Message: public boost::enable_shared_from_this<Message>{
    int type;
    std::vector<char> data{};
    std::string hash;

    static boost::weak_ptr<ssl::stream<tcp::socket>> socket_wptr;
    static boost::weak_ptr<io_context::strand> strand_wptr;

    std::array<char, HEADER_LENGTH> inbound_header_{};
    std::vector<char> inbound_data_{};
    std::string outbound_header_{};
    std::string outbound_data_{};

    void hashData();    //Calcolo digest
    static unsigned char* HEXtoUnsignedChar(const std::string& src);    //Conversione da string ad unsigned char*
    static unsigned char *generate_salt(int salt_length);   //Produzione sale crittografico

public:

    static void setSocket(boost::weak_ptr<ssl::stream<tcp::socket>> socket_wptr_, boost::weak_ptr<io_context::strand> strand_wptr_);

    Message();  //Costruttore per messaggio vuoto

    explicit Message(int type); //Costruttore per messaggio senza dato

    Message(int type, std::vector<char> data);  //Costruttore per dato generico

    explicit Message(const std::pair<std::string, std::string>& authData);  //Costruttore per coppia<username, password>

    explicit Message(const std::map<std::string, std::string>& paths);    //Costruttore per mappa <file/directory, hash>

    friend std::ostream& operator<<(std::ostream &out, Message& m); //ToString

    [[nodiscard]] int getType() const;  //Getter per il campo Type

    [[nodiscard]] const std::vector<char> &getData() const; //Getter per il campo data

    template<class Archive> void serialize(Archive& ar, unsigned int version);  //Funzione di supporto per gli archive di boost

    std::optional<bool> checkHash();   //Verifica integrità messaggio

    std::optional<std::pair<std::string, std::string>> extractAuthData();   //Estrazione pair<username, password> dal campo data

    std::optional<std::map<std::string, std::string>> extractFileList();  //Estrazione mappa<file/directory, hash> dal campo data

    void syncRead();    //Lettura sincrona del messaggio da boost_socket

    void syncWrite();  //Scrittura sincrona del messaggio su boost_socket

    static std::string unsignedCharToHEX(unsigned char *src, size_t src_length);  //Conversione da unsigned char* a string

    static std::string compute_password(const std::string& password, const std::string& salt, int iterations, int dkey_lenght); //PBKDF2

    friend class boost::serialization::access;

    template <typename Handler>
    void asyncWrite(Handler handler);

    template <typename Handler>
    void asyncRead(Handler handler);
};

#include "Async.tpp"


#endif //REMOTEBACKUPCLIENT_MESSAGE_H
