#include <boost/filesystem.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <iostream>
#include "Message.h"
#include <sstream>
#include <boost/asio.hpp>
#include "FileWatcher.h"
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#define RECONN_DELAY 5

#define HEADER_LENGTH 8

#define CHUNK_SIZE 1024

#define ROOT "../ToSincronize"

using namespace boost::filesystem;
using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;

boost::weak_ptr<tcp::socket> socket_wptr;
std::mutex connection_mtx;
std::condition_variable connection_cv;

std::mutex download_mtx;
std::condition_variable download_cv;

std::mutex upload_mtx;
std::condition_variable upload_cv;

std::atomic_bool running;

std::vector<char> auth_data;

std::deque<Message> download_pool;

std::deque<std::pair<std::string,FileStatus>> upload_pool;

std::size_t errorConnectionHandler(const boost::system::error_code& error, std::size_t bytes_transferred){
    if(error != 0){
        if ((error == boost::asio::error::eof) || (error == boost::asio::error::connection_reset)) {
            connection_cv.notify_one();//prova a riconnettersi
            std::unique_lock<std::mutex> lck(connection_mtx);
            connection_cv.wait(lck);
        } else throw std::runtime_error(error.message());
    }
    return bytes_transferred;
}

Message syncRead(){

    std::vector<char> header(HEADER_LENGTH);
    size_t message_length;
    size_t sizeR;

    //Ricezione Header
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(header), errorConnectionHandler);

    if(sizeR != header.size()) throw std::runtime_error("Broken Header");

    //Deserializzazione Header
    std::istringstream header_stream(std::string(header.begin(),header.end()));
    header_stream >> std::hex >> message_length;

    //Ricezione Messaggio
    std::vector<char> message(message_length);
    sizeR = boost::asio::read((*socket_wptr.lock()), boost::asio::buffer(message));

    if(sizeR != message_length) throw std::runtime_error("Broken Message");

    //Deserializzazione Messaggio
    Message m;
    std::istringstream archive_stream(std::string(message.begin(), message.end()));
    auto ita = text_iarchive(archive_stream);
    ita >> m;

    return m;
}

void syncWrite(const Message& message){

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
    sizeW = boost::asio::write((*socket_wptr.lock()), buffers, errorConnectionHandler);

    if(sizeW != buffers.data()->size()) throw std::runtime_error("Write Error");
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
                upload_pool.push_front(std::pair(path_to_watch, FileStatus::created));
                upload_cv.notify_one();
                break;
            case FileStatus::modified:
                std::cout << "File modified: " << path_to_watch << '\n';
                upload_pool.push_front(std::pair(path_to_watch, FileStatus::modified));
                upload_cv.notify_one();
                break;
            case FileStatus::erased:
                std::cout << "File erased: " << path_to_watch << '\n';
                upload_pool.push_front(std::pair(path_to_watch, FileStatus::erased));
                upload_cv.notify_one();
                break;
            default:
                std::cout << "Error! Unknown file status.\n";
        }
    });
}

void FileUploaderDispatcherThread(){
    std::unique_lock<std::mutex> lck(upload_mtx);
    std::ifstream ifs;
    std::vector<char> buffer( CHUNK_SIZE );
    size_t size;
    Message message;
    std::string file;

    while(running){
        upload_cv.wait(lck);
        file = upload_pool.back().first;
        ifs.open(file, std::ios::binary);
        syncWrite(Message(FILE_START, std::vector<char>(file.begin(), file.end())));
        while(!ifs.eof()) {
            ifs.read(buffer.data(), buffer.size());
            size = ifs.gcount();
            if(size < CHUNK_SIZE)
                buffer.resize(size);
            message = Message(FILE_DATA, buffer);
            syncWrite(message);
            buffer.clear();
        }
        ifs.close();
        syncWrite(Message(FILE_END));
    }
}

void FileDownloaderDispatcherThread(){
    Message messageFile;
    std::ofstream ofs;
    std::unique_lock<std::mutex> lck(download_mtx);
    while(running){
        download_cv.wait(lck);

        messageFile = download_pool.back();
        download_pool.pop_back();

        if(messageFile.getType() != FILE_START){
            std::cout<<"Error File Download"<<std::endl;
            syncWrite(Message(FILE_ERR));
        }

        std::string path(messageFile.getData().begin(), messageFile.getData().end());

        ofs.open(path, std::ios::binary);

        while(true){
            download_cv.wait(lck);

            ofs.open(path, std::ios::binary | std::ios_base::app);
            messageFile = download_pool.back();

            download_pool.pop_back();

            if(messageFile.getType() == FILE_END){
                ofs.close();
                break;
            }

            if(messageFile.getType() != FILE_DATA || !messageFile.checkHash()){
                std::cout<<"Error File Download"<<std::endl;
                syncWrite(Message(FILE_ERR));
                break;
            }

            ofs << messageFile.getData();

        }

    }

}

void ReceiverThread(){
    while(running){
        auto message = syncRead();
        switch (message.getType()) {
            case AUTH_REQ: syncWrite(Message(AUTH_RES, auth_data));
            break;
            case AUTH_ERR: throw std::runtime_error("Wrong username/password!");
            case FILE_START:
            case FILE_DATA:
            case FILE_END:
            case FILE_ERR:
                download_pool.push_front(message);
                download_cv.notify_one();
            break;
            default: std::cout<<"Message type not recognized!"<<std::endl;
            break;
        }
    }
}

void connectionHandlerThread(const std::string& src_ip, int src_port, const std::string& dst_ip, int dst_port){
    boost::system::error_code ec;

    //Connessione col server
    io_context ioc;
    auto socket_ = boost::make_shared<tcp::socket>(ioc);
    socket_wptr = boost::weak_ptr<tcp::socket>(socket_);

    socket_->open(boost::asio::ip::tcp::v4());
    tcp::endpoint localEndpoint(ip::address::from_string(src_ip), src_port);
    socket_->bind(localEndpoint, ec);
    if(ec)
        throw std::runtime_error("Bind Error");
    socket_->connect(tcp::endpoint(address::from_string(dst_ip),dst_port));

    //Autenticazione(three-way)

    //REQ
    auto message_auth_req = syncRead();
    if(message_auth_req.getType() != AUTH_REQ) throw std::runtime_error("Handshake Error!");

    //RES
    syncWrite(Message(AUTH_RES, auth_data));

    //OK
    auto message_auth_ok = syncRead();
    if(message_auth_ok.getType() != AUTH_OK) throw std::runtime_error("Handshake Error!");

    //Connessione stabilita

    //Gestisco possibili connection lost
    while(running) {
        std::unique_lock<std::mutex> lck(connection_mtx);
        connection_cv.wait(lck);
        while(true) {
            //Reconnection
            socket_.reset(new boost::asio::ip::tcp::socket(ioc));
            socket_->connect(tcp::endpoint(address::from_string(dst_ip), dst_port), ec);
            if (!ec) {
                connection_cv.notify_one();
                break;
            }
            std::cout<<"Trying to reconnect..."<<std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RECONN_DELAY));
        }
    }
}


int main(int argc, char* argv[])
{
    auto username = std::string(argv[1]);
    auto password = std::string(argv[2]);
    auto auth_string = std::string("/USERNAME/:"+username+"/PASSWORD/:"+password);
    auth_data = std::vector<char>(auth_string.begin(), auth_string.end());

    //Running
    running.store(true);

    //Connessione col server
    std::thread cht(connectionHandlerThread, "192.168.100.105", 5000, "192.168.100.105", 5001);

//    //Prova read
//    std::vector<char> br;
//    Message message = syncRead();
//
//    std::cout<<"Messaggio: "<<message<<std::endl;
//
//    //Prova write
//    std::string s = "ciao";
//    const std::vector<char> bw(s.begin(),s.end());
//    syncWrite(bw, s.size(), 5);


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