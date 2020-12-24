//
// Created by lucio on 01/12/2020.
//

#include "Message.h"


std::mutex Message::syncR_mtx;
std::mutex Message::syncW_mtx;

//COSTRUTTORI, OVERLOADS E DISTRUTTORE

//Costruttore di copia
Message::Message(const Message &m) {
    this->type = m.type;
    this->data = m.data;
    this->hash= m.hash;
}

//Costruttore di movimento
Message::Message(Message &&src) noexcept : type(INVALID){
    this->hash.clear();
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
    return *this;
}

//Distruttore
Message::~Message() = default;

//Costruttore per messaggio vuoto
Message::Message() {
    this->type = INVALID;
}

//Costruttore per messaggio senza dato
Message::Message(int type): type(type){}

//Costruttore per dato generico
Message::Message(int type, std::vector<char> data) : type(type), data(std::move(data)) {
    hashData();
}

//Costruttore per coppia<username, password>
Message::Message(const std::pair<std::string, std::string>& authData) {
    this->type = AUTH_RES;
    std::string tmp;

    tmp += authData.first + UDEL + authData.second + PDEL;

    this->data = std::vector<char>(tmp.begin(), tmp.end());

    hashData();

}

//Costruttore per mappa <file/directory, hash>
Message::Message(const std::map<std::string, std::string>& fileList) {
    this->type = FILE_LIST;
    std::string tmp;

    for (const auto & file : fileList) {
        tmp += file.first + FDEL;
        if(!file.second.empty())  tmp+=(file.second);
        tmp+=HDEL;
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
    out << "TIPO:"<< std::quoted(std::to_string(m.type));
    if(!m.data.empty()) {
        out << " DATA:" ;
        out<<'"';
        for (auto it = m.data.begin(); it != m.data.end(); ++it)
            out << *it;
        out<<'"';
    }
    if(!m.hash.empty()) out << " HASH:" << std::quoted(m.hash);
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
}

//Verifica integrità messaggio
std::optional<bool> Message::checkHash() {

    if(this->type <-2 || this->type>7) return std::optional<bool>();

    if(this->data.empty() || this->hash.empty()) return std::optional<bool>();

    Message tmp(INVALID, this->data);

    if(this->hash == tmp.hash) return true;
    else return false;
}

//Estrazione pair<username, password> dal campo data
std::optional<std::pair<std::string, std::string>> Message::extractAuthData(){

    if(this->type != AUTH_RES) return std::optional<std::pair<std::string, std::string>>();

    std::string codedAuthData(this->data.begin(), this->data.end());

    size_t pos;
    std::string username;
    std::string password;

    //Estrazione USERNAME
    pos = codedAuthData.find(UDEL);
    username = codedAuthData.substr(0, pos);
    codedAuthData.erase(0,pos + sizeof(UDEL) - 1);

    //Estrazione PASSWORD
    pos = codedAuthData.find(PDEL);
    password = codedAuthData.substr(0,  pos);
    codedAuthData.erase(0, pos + sizeof(PDEL) - 1);

    return std::pair(username, password);
}

//Estrazione mappa<file/directory, hash> dal campo data
std::optional<std::map<std::string, std::string>> Message::extractFileList(){

    if(this->type != FILE_LIST) return std::optional<const std::map<std::string, std::string>>();

    std::map<std::string, std::string> decodedFileList;
    std::string codedFileList(this->data.begin(), this->data.end());

    size_t pos;
    std::string file;
    std::string hash_;
    while ((pos = codedFileList.find(FDEL)) != std::string::npos) {
        //Estrazione FILE
        file = codedFileList.substr(0, pos);
        codedFileList.erase(0, pos + sizeof(FDEL) - 1);

        //Estrazione HASH
        pos = codedFileList.find(HDEL);
        hash_ = codedFileList.substr(0,  pos);
        codedFileList.erase(0, pos + sizeof(HDEL) - 1);
        if(!hash_.empty()) {
            decodedFileList[file] = hash_;
        } else decodedFileList[file] = "";
    }

    return decodedFileList;
}

//Lettura sincrona del messaggio da boost_socket
void Message::syncRead(const boost::weak_ptr<ssl::stream<tcp::socket>>& socket_wptr){

    std::lock_guard<std::mutex> lg(syncR_mtx);

    boost::system::error_code ec;

    std::vector<char> header(HEADER_LENGTH);
    size_t message_length;
    size_t sizeR;

    //Ricezione Header
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(header), boost::asio::transfer_exactly(HEADER_LENGTH));

    if(header.empty() || (!header.empty() && sizeR != header.size()))
        throw std::runtime_error("Broken Header");

    //Deserializzazione Header
    std::istringstream header_stream(std::string(header.begin(),header.end()));
    header_stream >> std::hex >> message_length;

    //Ricezione Messaggio
    std::vector<char> message(message_length);
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(message), boost::asio::transfer_exactly(message_length));

    if(sizeR != message_length)
        throw std::runtime_error("Broken Message");

    //Deserializzazione Messaggio
    std::istringstream archive_stream(std::string(message.begin(), message.end()));
    auto ita = text_iarchive(archive_stream);
    ita >> *this;
}

//Scrittura sincrona del messaggio su boost_socket
void Message::syncWrite(const boost::weak_ptr<ssl::stream<tcp::socket>>& socket_wptr) const{

    std::lock_guard<std::mutex> lg(syncW_mtx);

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
    sizeW = boost::asio::write((*socket_wptr.lock()), buffers, boost::asio::transfer_exactly(buffers[0].size() + buffers[1].size()));

    if(sizeW != (buffers[0].size() + buffers[1].size())) throw std::runtime_error("Write Error");
}


//FUNZIONI DI SUPPORTO PRIVATE

//Riempimento del campo hash
void Message::hashData(){
    int success;

    if(this->data.empty()) return;

    unsigned char hash_[SHA256_DIGEST_LENGTH];

    //Calcolo hash
    SHA256_CTX sha256;
    success = SHA256_Init(&sha256);
    if(!success) {
        this->hash = "";
        return;
    }
    success = SHA256_Update(&sha256, this->data.data(), this->data.size());
    if(!success){
        this->hash = "";
        return;
    }
    success = SHA256_Final(hash_, &sha256);
    if(!success){
        this->hash = "";
        return;
    }


    this->hash = unsignedCharToHEX(hash_, SHA256_DIGEST_LENGTH);
}

std::string Message::unsignedCharToHEX(unsigned char* src, size_t src_length){
    int error;
    char tmp[2*src_length+1];
    tmp[2*src_length] = 0;
    for (int i = 0; i < src_length; i++) {
        error = sprintf(tmp + i * 2, "%02x", src[i]);
        if(error < 0) break;
    }

    if(error < 0) return "";

    return std::string(tmp);
}

unsigned char* Message::HEXtoUnsignedChar(const std::string& src){
    int error;
    auto tmp = new unsigned char[src.length()/2]{0};
    unsigned int number = 0;

    for(int i=0, j = 0;i<src.length();i+=2, j++) {
        error = sscanf(&src.c_str()[i], "%02x", &number);
        if(error == EOF) break;
        tmp[j] = (unsigned char) number;
    }

    if(error == EOF) return nullptr;

    return tmp;
}

std::string Message::compute_password(const std::string& password, const std::string& salt, int iterations, int dkey_lenght){
    auto salt_ = HEXtoUnsignedChar(salt);

    if(!salt_) return "";

    auto key = new unsigned char[dkey_lenght];
    int success = PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                      salt_, dkey_lenght,
                      iterations, EVP_sha3_512(),
                      dkey_lenght, key);

    if(!success) return "";

    auto key_HEX = unsignedCharToHEX(key, dkey_lenght);

    delete[] salt_;
    delete[] key;

    return key_HEX;

}

unsigned char* Message::generate_salt(int salt_length){
    int success;
    auto salt = new unsigned char[salt_length];
    success = RAND_bytes(salt, salt_length);

    if(!success)
        return nullptr;
    else
        return salt;
}
