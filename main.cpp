#include <boost/filesystem.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <iostream>
#include "Message.h"
#include <sstream>
#include <boost/asio.hpp>
#include "FileWatcher.h"

#define HEADER_LENGTH 8

#define CHUNK_SIZE 1024

#define ROOT "../ToSincronize"

using namespace boost::filesystem;
using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;

tcp::socket* socket_ptr;

Message syncRead(io_context& ioc){
    std::vector<char> header(HEADER_LENGTH);
    size_t message_length;
    size_t sizeR;

    //Ricezione Header
    boost::asio::read((*socket_ptr), boost::asio::buffer(header));


    //Deserializzazione Header
    std::istringstream header_stream(std::string(header.begin(),header.end()));
    header_stream >> std::hex >> message_length;

    //Ricezione Messaggio
    std::vector<char> message(message_length);
    boost::asio::read((*socket_ptr), boost::asio::buffer(message));

    //Deserializzazione Messaggio
    Message m;
    std::istringstream archive_stream(std::string(message.begin(), message.end()));
    auto ita = text_iarchive(archive_stream);
    ita >> m;

    return m;
}

void syncWrite(const std::vector<char>& buffer, size_t size, int message_type){

    //Creazione messaggio
    auto message = Message(message_type,buffer, size);

    //Serializzazione messaggio
    std::ostringstream archive_stream;
    auto ota = text_oarchive(archive_stream);
    ota << message;
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
    boost::asio::write((*socket_ptr), buffers);
}

void FileWatcherThread(FileWatcher fw, void sendFile(const std::string& file)){


    // Start monitoring a folder for changes and (in case of changes)
    // run a user provided lambda function
    fw.start([&sendFile] (const std::string& path_to_watch, FileStatus status) -> void {
        // Process only regular files, all other file types are ignored
        if(!std::filesystem::is_regular_file(std::filesystem::path(path_to_watch)) && status != FileStatus::erased) {
            return;
        }

        switch(status) {
            case FileStatus::created:
                std::cout << "File created: " << path_to_watch << '\n';
                sendFile(path_to_watch);
                break;
            case FileStatus::modified:
                std::cout << "File modified: " << path_to_watch << '\n';
                sendFile(path_to_watch);
                break;
            case FileStatus::erased:
                std::cout << "File erased: " << path_to_watch << '\n';
                break;
            default:
                std::cout << "Error! Unknown file status.\n";
        }
    });
}

void sendFile(const std::string& file){
    std::ifstream ifs;
    std::vector<char> buffer( CHUNK_SIZE );
    size_t size;
    ifs.open(file, std::ios::binary);
    while(!ifs.eof()) {
        ifs.read(buffer.data(), buffer.size());
        size = ifs.gcount();
        syncWrite(buffer, size, 2);
    }
    ifs.close();
}


int main()
{
    //Connessione col server
    io_context ioc;
    tcp::socket socket_(ioc);
    socket_ptr = &socket_;
    boost::system::error_code bindError;

    socket_.open(boost::asio::ip::tcp::v4());
    tcp::endpoint localEndpoint(ip::address::from_string("192.168.100.105"), 5001);
    socket_.bind(localEndpoint, bindError);
    if(bindError)
        throw std::runtime_error("Bind Error");
    socket_.connect(tcp::endpoint(address::from_string("192.168.100.105"),5000));

    //Prova read
    std::vector<char> br;
    Message message = syncRead(ioc);

    std::cout<<"Messaggio: "<<message<<std::endl;

    //Prova write
    std::string s = "ciao";
    const std::vector<char> bw(s.begin(),s.end());
    syncWrite(bw, s.size(), 5);


//    std::string root = std::string(ROOT);
//
//    size_t chunk_size = CHUNK_SIZE;
//    std::ifstream ifs(root + "/prova.txt", std::ios::binary);
//
//    std::vector<char> buffer( CHUNK_SIZE );
//    ifs.read( buffer.data(), buffer.size() );
//    size_t size= ifs.gcount();
//    std::streamsize ssize=ifs.gcount();
//    ifs.close();
//
//
//    std::ostringstream os;
//    os << buffer;
//    text_oarchive oa(os);
//
//    auto message = Message(0, buffer, size);
//
//    std::ostringstream archive_stream;
//    auto ota = text_oarchive(archive_stream);
//    ota << message;
//    std::string outbound_data_ = archive_stream.str();
//
//
//
//    std::size_t sizeW;
//
//    boost::asio::async_write(socket, boost::asio::buffer(outbound_data_),
//                             [&sizeW](const boost::system::error_code& error, std::size_t bytes_transferred){
//        sizeW = bytes_transferred;
//        if(error != 0) throw std::runtime_error(error.message());
//    });
//
//    //Inizializzo il filewatcher (viene effettuato un primo controllo all'avvio sui file)
//    FileWatcher fw{ROOT, std::chrono::milliseconds(5000)};//5 sec of delay
//
//    std::thread fwt(FileWatcherThread, fw, sendFile);

    return 0;
}