#include <boost/filesystem.hpp>
#include <iostream>
#include <csignal>
#include "Message.h"
#include "FileWatcher.h"

#define RECONN_DELAY 5

#define CHUNK_SIZE 1024

#define ROOT "../ToSincronize/"

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

std::list<Message> download_pool;

std::list<std::pair<std::string,FileStatus>> upload_pool;

std::list<std::string> path_ignore_pool;

void errorConnectionHandler(){
    connection_cv.notify_one();//prova a riconnettersi
    std::unique_lock<std::mutex> lck(connection_mtx);
    connection_cv.wait(lck);
}

void FileWatcherThread(FileWatcher fw){

    fw.start([] (const std::string& path_to_watch, FileStatus status) -> void {
        //Processo solo i file che non sono in download
        if(!std::filesystem::is_regular_file(std::filesystem::path(path_to_watch)) && status != FileStatus::erased
        && (!(std::find(path_ignore_pool.begin(), path_ignore_pool.end(), path_to_watch) != path_ignore_pool.end())
        )) {
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

    try{
    while(running){
        upload_cv.wait(lck);
        file = upload_pool.back().first;
        ifs.open(file, std::ios::binary);
        message = Message(FILE_START, std::vector<char>(file.begin(), file.end()));
        message.syncWrite(socket_wptr, errorConnectionHandler);
        while(!ifs.eof()) {
            ifs.read(buffer.data(), buffer.size());
            size = ifs.gcount();
            if(size < CHUNK_SIZE)
                buffer.resize(size);
            message = Message(FILE_DATA, buffer);
            message.syncWrite(socket_wptr, errorConnectionHandler);
            buffer.clear();
        }
        ifs.close();
        message = Message(FILE_END);
        message.syncWrite(socket_wptr, errorConnectionHandler);
    }
    } catch (const std::runtime_error& e) {
        std::cout<<e.what()<<std::endl;
    }
}

void FileDownloaderDispatcherThread(){
    Message message;
    std::ofstream ofs;
    std::unique_lock<std::mutex> lck(download_mtx);

    try {

        while (running) {
            download_cv.wait(lck);

            message = download_pool.back();
            download_pool.pop_back();

            if (message.getType() != FILE_START) {
                std::cout << "Error File Download" << std::endl;
                message = Message(FILE_ERR);
                message.syncWrite(socket_wptr, errorConnectionHandler);
            }

            std::string path(message.getData().begin(), message.getData().end());

            path_ignore_pool.push_front(path);

            ofs.open(path, std::ios::binary);

            while (true) {
                download_cv.wait(lck);

                ofs.open(path, std::ios::binary | std::ios_base::app);
                message = download_pool.back();

                download_pool.pop_back();

                if (message.getType() == FILE_END) {
                    ofs.close();
                    path_ignore_pool.pop_back();
                    break;
                }

                if (message.getType() != FILE_DATA || !message.checkHash()) {
                    std::cout << "Error File Download" << std::endl;
                    message = Message(FILE_ERR);
                    message.syncWrite(socket_wptr, errorConnectionHandler);
                    break;
                }

                ofs << message.getData();

            }
        }
    } catch (const std::runtime_error& e) {
        std::cout<<e.what()<<std::endl;
    }

}

void ReceiverThread(){
    Message message;
    Message error = Message(AUTH_RES, auth_data);
    try {
    while(running){
                message.syncRead(socket_wptr, errorConnectionHandler);
        switch (message.getType()) {
            case AUTH_REQ:  error.syncWrite(socket_wptr, errorConnectionHandler);
            break;
            case AUTH_ERR:  throw std::runtime_error("Wrong username/password!");
            break;
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
    } catch (const std::runtime_error& e) {
        std::cout<<e.what()<<std::endl;
    }
}

void signal_callback_handler(int signum) {
    std::cout << "Caught signal " << signum << std::endl;
    // Terminate program
    running.store(false);
    //exit(signum);
}

int main(int argc, char* argv[])
{
    //Signal Handler(Chiusura con Ctrl+C)
    signal(SIGINT, signal_callback_handler);

    std::cout<<"Ctrl+C to close the program..."<<std::endl;

    boost::system::error_code ec;
    Message message;

    //Dati di autenticazione
    auto username = std::string("gold");
    auto password = std::string("experience");
    auto auth_string = std::string("/USERNAME/:"+username+"/PASSWORD/:"+password);
    auth_data = std::vector<char>(auth_string.begin(), auth_string.end());

    //Dati di connessione
    auto src_ip = ip::address::from_string("127.0.0.1");
    int src_port = 6000;
    auto dst_ip = ip::address::from_string("127.0.0.1");
    int dst_port = 5000;

    //Connessione col server
    io_context ioc;
    auto socket_ = boost::make_shared<tcp::socket>(ioc);
    socket_wptr = boost::weak_ptr<tcp::socket>(socket_);

    //Creazione socket
    socket_->open(boost::asio::ip::tcp::v4(), ec);
    if(ec) throw std::runtime_error("Error opening socket!");
    tcp::endpoint localEndpoint(src_ip, src_port);
    socket_->bind(localEndpoint, ec);
    if(ec) throw std::runtime_error("Bind Error!");
    socket_->connect(tcp::endpoint(dst_ip,dst_port), ec);
    if(ec) throw std::runtime_error("Can't connect to remote server!");

    //Autenticazione(three-way)

    try {

        //REQ
        message.syncRead(socket_wptr, errorConnectionHandler);
        if (message.getType() != AUTH_REQ) throw std::runtime_error("Handshake Error!");

        //RES
        message = Message(AUTH_RES, auth_data);
        message.syncWrite(socket_wptr, errorConnectionHandler);

        //OK
        message.syncRead(socket_wptr, errorConnectionHandler);
        if (message.getType() != AUTH_OK) throw std::runtime_error("Handshake Error!");
    } catch (const std::runtime_error& e) {
        std::cout<<e.what()<<std::endl;
    }

    //Connessione stabilita
    running.store(true);

    //Inizializzo il filewatcher (viene effettuato un primo controllo all'avvio sui file)
    FileWatcher fw{ROOT, std::chrono::milliseconds(5000), running};//5 sec of delay

    //Scambio lista file
    //message = Message(5, fw.getPaths());

    //Avvio il thread che gestisce il FileWatcher
    std::thread fwt(FileWatcherThread, fw);
    fwt.detach();

    //Avvio il thread che gestisce i messaggi in entrata
    std::thread rt(ReceiverThread);
    rt.detach();

    //Avvio il thread che gestische i download dei file
    std::thread fdt(FileDownloaderDispatcherThread);
    fdt.detach();

    //Avvio il thread che gestice l'upload dei file
    std::thread fut(FileUploaderDispatcherThread);
    fut.detach();

    //Gestisco possibili connection lost
    while(running) {
        std::unique_lock<std::mutex> lck(connection_mtx);
        connection_cv.wait(lck);
        while(running) {
            //Reconnection
            socket_.reset(new boost::asio::ip::tcp::socket(ioc));
            socket_->connect(tcp::endpoint(dst_ip, dst_port), ec);
            if (!ec) {
                connection_cv.notify_one();
                break;
            }
            std::cout<<"Trying to reconnect..."<<std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RECONN_DELAY));
        }
    }
    return 0;
    //return EXIT_SUCCESS;
}