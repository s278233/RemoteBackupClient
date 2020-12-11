//
// Created by lucio on 01/12/2020.
//

#include "Message.h"
#include <cstring>
#include <sstream>
#include <iomanip>

//COSTRUTTORI, OVERLOADS E DISTRUTTORE

//Costruttore di copia
Message::Message(const Message &m) {
    this->type = m.type;
    this->data = m.data;
    memcpy(this->hash, m.hash, SHA256_DIGEST_LENGTH);
}

//Costruttore di movimento
Message::Message(Message &&src) noexcept : type(INVALID), hash(nullptr) {
    swap(*this, src);
}

//Overload operatore di assegnazione tramite copia
Message &Message::operator=(const Message &src) {
    Message copy(src);
    swap(*this, copy);
    return *this;
}

//Overload operatore di assegnazione tramite movimento
Message &Message::operator=(Message&& src) noexcept{
    swap(*this, src);
}

//Distruttore
Message::~Message() { delete [] hash; }

//Costruttore per messaggio vuoto
Message::Message() {
    this->type = INVALID;
    this->hash = nullptr;
}

//Costruttore per messaggio senza dato
Message::Message(int type): type(type), hash(nullptr){}

//Costruttore per dato generico
Message::Message(int type, std::vector<char> data) : type(type), data(std::move(data)) {
    hashData();
}

//Costruttore per coppia<username, password>
Message::Message(const std::pair<std::string, std::string>& authData) {
    this->type = AUTH_RES;
    std::string tmp;

    tmp += UDEL + authData.first + PDEL + authData.second;

    this->data = std::vector<char>(tmp.begin(), tmp.end());

    hashData();

}

//Costruttore per mappa <file/directory, hash>
Message::Message(const std::unordered_map<std::string, unsigned char *>& fileList) {
    this->type = FILE_LIST;
    std::string tmp;

    for (const auto & file : fileList) {
        tmp += FDEL + file.first + HDEL;
        if(file.second)  tmp+=(*file.second);
    }

    this->data = std::vector<char>(tmp.begin(), tmp.end());

    hashData();
}


//FUNZIONI DI SUPPORTO PUBBLICHE

//SWAP
void swap(Message &src, Message &dst) {
    std::swap(src.type, dst.type);
    std::swap(src.data, dst.data);
    std::swap(src.hash, dst.hash);
}

//ToString
std::ostream& operator<<(std::ostream &out, Message& m)
{
    out << m.type << " " << m.data.data() << " " << m.hash <<std::endl;
    return out;
}

//Getter per il campo Type
int Message::getType() const {
    return type;
}

//Getter per il campo data
const std::vector<char> &Message::getData() const {
    return data;
}
//Funzione di supporto per gli archive di boost
template<class Archive> void Message::serialize(Archive& ar, const unsigned int version){
    ar & type;      //Se Archive è un output archive allora & è uguale a <<
    ar & data;      //Se Archive è un input  archive allora & è uguale a >>
    ar & hash;
};

//Verifica integrità messaggio
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

//Estrazione pair<username, password> dal campo data
std::pair<std::string, std::string> Message::extractAuthData(){

    if(this->type != AUTH_RES) throw std::runtime_error("Extraction failed: type is wrong!");

    std::string codedAuthData(this->data.begin(), this->data.end());

    size_t pos = 0;
    std::string username;
    std::string password;

    //Estrazione USERNAME
    pos = codedAuthData.find(UDEL);
    username = codedAuthData.substr(0, pos);
    codedAuthData.erase(0, pos + sizeof(UDEL) + username.length());

    //Estrazione PASSWORD
    pos = codedAuthData.find(PDEL);
    password = codedAuthData.substr(0,  pos);
    codedAuthData.erase(0, pos + sizeof(PDEL) + password.length());

    return std::pair(username, password);
}

//Estrazione mappa<file/directory, hash> dal campo data
std::unordered_map<std::string, unsigned char *> Message::extractFileList(){

    if(this->type != FILE_LIST) throw std::runtime_error("Extraction failed: type is wrong!");

    std::unordered_map<std::string, unsigned char *> decodedFileList;
    std::string codedFileList(this->data.begin(), this->data.end());

    size_t pos = 0;
    std::string file;
    std::string hash_;
    unsigned char* hash_ptr;
    while ((pos = codedFileList.find(FDEL)) != std::string::npos) {
        //Estrazione FILE
        file = codedFileList.substr(0, pos);
        codedFileList.erase(0, pos + sizeof(FDEL) + file.length());

        //Estrazione HASH
        pos = codedFileList.find(HDEL);
        hash_ = codedFileList.substr(0,  pos);
        codedFileList.erase(0, pos + sizeof(HDEL) + hash_.length());
        if(!hash_.empty()) {
            hash_ptr = new unsigned char[SHA256_DIGEST_LENGTH]();
            memcpy(hash_ptr, hash_.c_str(), SHA256_DIGEST_LENGTH);
            decodedFileList[file] = hash_ptr;
            hash_ptr = nullptr;
        } else decodedFileList[file] = nullptr;
    }

    return decodedFileList;
}

//Lettura sincrona del messaggio da boost_socket
void Message::syncRead(const boost::weak_ptr<tcp::socket>& socket_wptr, void connectionHandler()){

    std::vector<char> header(HEADER_LENGTH);
    size_t message_length;
    size_t sizeR;
    boost::system::error_code ec;

    //Ricezione Header
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(header), boost::asio::transfer_exactly(HEADER_LENGTH), ec);
    if(ec != 0) {
        if ((ec == boost::asio::error::eof) || (ec == boost::asio::error::connection_reset)) connectionHandler();
        else throw std::runtime_error(ec.message());
    }

    if(sizeR != header.size()) throw std::runtime_error("Broken Header");

    //Deserializzazione Header
    std::istringstream header_stream(std::string(header.begin(),header.end()));
    header_stream >> std::hex >> message_length;

    //Ricezione Messaggio
    std::vector<char> message(message_length);
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(message), boost::asio::transfer_exactly(message_length), ec);
    if(ec != 0) {
        if ((ec == boost::asio::error::eof) || (ec == boost::asio::error::connection_reset)) connectionHandler();
        else throw std::runtime_error(ec.message());
    }

    if(sizeR != message_length) throw std::runtime_error("Broken Message");

    //Deserializzazione Messaggio
    std::istringstream archive_stream(std::string(message.begin(), message.end()));
    auto ita = text_iarchive(archive_stream);
    ita >> *this;
}

//Scrittura sincrona del messaggio su boost_socket
void Message::syncWrite(const boost::weak_ptr<tcp::socket>& socket_wptr, void connectionHandler()) const{

    boost::system::error_code ec;

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
    sizeW = boost::asio::write((*socket_wptr.lock()), buffers, boost::asio::transfer_exactly(buffers[0].size() + buffers[1].size()), ec);

    if(ec != 0) {
        if ((ec == boost::asio::error::eof) || (ec == boost::asio::error::connection_reset)) connectionHandler();
        else throw std::runtime_error(ec.message());
    }

    if(sizeW != (buffers[0].size() + buffers[1].size())) throw std::runtime_error("Write Error");
}


//FUNZIONI DI SUPPORTO PRIVATE

//Riempimento del campo hash
void Message::hashData(){
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, this->data.data(), this->data.size());
    SHA256_Final(hash, &sha256);
}
