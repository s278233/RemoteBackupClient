#include <boost/filesystem.hpp>
#include <iostream>
#include <csignal>
#include "Message.h"
#include "FileWatcher.h"
#include "SafeCout.h"

#define RECONN_DELAY 5

#define CHUNK_SIZE 1024

using namespace boost::filesystem;
using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;

boost::weak_ptr<tcp::socket> socket_wptr;
std::mutex reconnection_mtx;
std::condition_variable reconnection_cv;

std::mutex download_mtx;
std::condition_variable download_cv;

std::mutex upload_mtx;
std::condition_variable upload_cv;

std::atomic_bool running;

std::list<Message> download_pool;

std::list<std::pair<std::string,FileStatus>> upload_pool;

std::list<std::string> path_ignore_pool;



void checkDifferences(const std::map<std::string, std::string>& src,std::map<std::string, std::string>& dst){
    for(const auto& file:src)
        if(!dst.contains(file.first) || (dst.contains(file.first) && file.second != dst[file.first])) {
            upload_pool.push_front(std::pair(file.first, FileStatus::created));
        } else if(file.second != dst[file.first]) {
            upload_pool.push_front(std::pair(file.first, FileStatus::modified));
        }
}

void FileWatcherThread(FileWatcher fw){

    while(!running.load());

    SafeCout::safe_cout("FileWatcher Thread avviato");

    fw.start([] (const std::string& path_to_watch, FileStatus status) -> void {
        if(!running.load()) return;
        //Processo solo i file che non sono in download
        if(!std::filesystem::is_regular_file(std::filesystem::path(path_to_watch)) && status != FileStatus::erased
        && (!(std::find(path_ignore_pool.begin(), path_ignore_pool.end(), path_to_watch) != path_ignore_pool.end())
        )) {
            return;
        }

        switch(status) {
            case FileStatus::created:
                SafeCout::safe_cout("File created: ", path_to_watch);
                upload_pool.push_front(std::pair(path_to_watch, FileStatus::created));
                upload_cv.notify_one();
                break;
            case FileStatus::modified:
                SafeCout::safe_cout("File modified: ", path_to_watch);
                upload_pool.push_front(std::pair(path_to_watch, FileStatus::modified));
                upload_cv.notify_one();
                break;
            case FileStatus::erased:
                SafeCout::safe_cout("File erased: ", path_to_watch);
                upload_pool.push_front(std::pair(path_to_watch, FileStatus::erased));
                upload_cv.notify_one();
                break;
            default:
                SafeCout::safe_cout("Error! Unknown file status.");
        }
    });

    SafeCout::safe_cout("FileWatcher Thread terminato");
}

void FileUploaderDispatcherThread(){
    std::unique_lock<std::mutex> lck(upload_mtx);
    std::ifstream ifs;
    std::vector<char> buffer( CHUNK_SIZE );
    size_t size;
    Message message;
    std::string file;

    while(!running.load());

    SafeCout::safe_cout("FileUploader Thread avviato ");

    try {
        while (running.load()) {
            upload_cv.wait(lck, [](){
                return (!upload_pool.empty() || !running.load());
            });
            if (!running.load()) break;
            file = upload_pool.back().first;

            SafeCout::safe_cout("uploading ", file);

            //Upload cartella
            if (std::filesystem::is_directory(file)) {
                message = Message(DIR, std::vector<char>(file.begin(), file.end()));
                message.syncWrite(socket_wptr);
            } else {
                //Upload file
                ifs.open(file, std::ios::binary);
                message = Message(FILE_START, std::vector<char>(file.begin(), file.end()));
                message.syncWrite(socket_wptr);
                if (std::filesystem::file_size(file) != 0)
                    while (!ifs.eof()) {
                        ifs.read(buffer.data(), buffer.size());
                        size = ifs.gcount();
                        if (size < CHUNK_SIZE)
                            buffer.resize(size);
                        message = Message(FILE_DATA, buffer);
                        message.syncWrite(socket_wptr);
                        buffer.clear();
                        std::vector<char>(CHUNK_SIZE).swap(buffer);
                    }
                ifs.close();
                message = Message(FILE_END);
                message.syncWrite(socket_wptr);
            }
            upload_pool.pop_back();
        }
            upload_pool.clear();
            SafeCout::safe_cout("FileUploader Thread terminato");
    }catch (boost::system::system_error const &e) {
        SafeCout::safe_cout("FileUploader exception: ", e.what());
        if ((e.code() == boost::asio::error::eof) || (e.code() == boost::asio::error::connection_reset)) {
            upload_pool.clear();
            reconnection_cv.notify_one();
        }
    } catch (const std::runtime_error &e) {
        SafeCout::safe_cout("FileUploader exception: ", e.what());
    }

}

void FileDownloaderDispatcherThread(){
    Message message;
    std::ofstream ofs;
    std::unique_lock<std::mutex> lck(download_mtx);

    while(!running.load());

    SafeCout::safe_cout("FileDownloader Thread avviato ");

    try {

        while (running.load()) {
            download_cv.wait(lck, [](){
                return (!download_pool.empty() || !running.load());
            });
            if(!running.load()) break;

            message = download_pool.back();
            download_pool.pop_back();

            std::string path(message.getData().begin(), message.getData().end());

            SafeCout::safe_cout("downloading ", path);

            if(message.getType() == DIR) std::filesystem::create_directory(path);

            else if (message.getType() != FILE_START) {
                SafeCout::safe_cout("Error File Download");

            } else {
                path_ignore_pool.push_front(path);

                ofs.open(path, std::ios::binary);

                while (true) {

                    download_cv.wait(lck, [](){
                        return !download_pool.empty();
                    });

                    message = download_pool.back();

                    download_pool.pop_back();

                    if (message.getType() == FILE_END) {
                        ofs.close();
                        FileWatcher::addPath(path);
                        path_ignore_pool.pop_back();
                        break;
                    }

                    if (message.getType() != FILE_DATA) {
                        SafeCout::safe_cout("Error File Download");
                        ofs.close();
                        std::remove(path.c_str());
                        break;
                    }

                    ofs.write(message.getData().data(), message.getData().size());

                }
            }
        }
        download_pool.clear();
        SafeCout::safe_cout("FileDownloader Thread terminato");
    } catch (boost::system::system_error const &e) {
        SafeCout::safe_cout("FileDownloader exception: ", e.what());
        if ((e.code() == boost::asio::error::eof) || (e.code() == boost::asio::error::connection_reset)) {
            download_pool.clear();
            reconnection_cv.notify_one();
        }
    }catch (const std::runtime_error &e) {
        SafeCout::safe_cout("FileDownloader exception: ", e.what());
    }


}

void ReceiverThread(){
    Message message;

    while(!running.load());

    SafeCout::safe_cout("Receiver Thread avviato");

    try {
    while(running.load()){
                //Ricezione messaggio
                message.syncRead(socket_wptr);
                //Controllo tipo
                if(message.getType()<-2 || message.getType()>7)
                    SafeCout::safe_cout("Wrong Type!, message discarded");
                //Controllo integrità
                else if((message.checkHash().has_value() && !message.checkHash().value()) ||
                !message.getData().empty() && !message.checkHash().has_value()) {
                    SafeCout::safe_cout("Wrong Hash!, message discarded");
                } else
            //Smistamento messaggio
            switch (message.getType()) {
            case DIR:
            case FILE_START:
            case FILE_DATA:
            case FILE_END:
                download_pool.push_front(message);
                download_cv.notify_one();
            break;
            default: SafeCout::safe_cout("Message type not recognized!");
            break;
        }
    }
        SafeCout::safe_cout("Receiver Thread terminato");
    } catch (boost::system::system_error const &e) {
        SafeCout::safe_cout("Receiver exception: ", e.what());
        if ((e.code() == boost::asio::error::eof) || (e.code() == boost::asio::error::connection_reset)) {
            reconnection_cv.notify_one();
        }
    }catch (const std::runtime_error &e) {
        SafeCout::safe_cout("Receiver exception: ", e.what());
    }

}

void signal_callback_handler(int signum) {
    SafeCout::safe_cout("Caught signal ", signum);
    // Terminate program
    running.store(false);
    //exit(signum);
}

int main()
{
    //Signal Handler(Chiusura con Ctrl+C)
//    signal(SIGINT, signal_callback_handler);

    SafeCout::safe_cout("Ctrl+C to close the program..."); //Funzionalità non ancora implementata

    boost::system::error_code ec;
    Message message;

    //Dati di autenticazione
    auto username = std::string("gold");
    auto password = std::string("experience");
    auto auth_data = std::pair<std::string, std::string>(username, password);


    //Dati di connessione
    auto src_ip = ip::address::from_string("127.0.0.1");
    int src_port = 6009;
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


    SafeCout::safe_cout("FileWatcher inizializzazione...");

        //Inizializzo il filewatcher (viene effettuato un primo controllo all'avvio sui file)
        FileWatcher fw{"../" + username, std::chrono::milliseconds(5000), running};//5 sec of delay

    SafeCout::safe_cout("FileWatcher inizializzato");

        //Inizializzo fileList da inviare
        auto fileListW = FileWatcher::getPaths();
        auto fileListMessage = Message(fileListW);
        std::optional<std::map<std::string, std::string>> fileListR;

        while(true) {
        //Autenticazione(two-way)

            SafeCout::safe_cout("Autenticazione...");

        try {

            //REQ
            message.syncRead(socket_wptr);
            if (message.getType() != AUTH_REQ) throw std::runtime_error("Handshake Error!");

            //RES
            auto authMessage = Message(auth_data);
            authMessage.syncWrite(socket_wptr);

            //Scambio lista file
            fileListMessage.syncWrite(socket_wptr);
            message.syncRead(socket_wptr);
            fileListR = message.extractFileList();

        } catch (boost::system::system_error const &e) {
            SafeCout::safe_cout(e.what());
            throw std::runtime_error("Handshake Error!");

        } catch (const std::runtime_error &e) {
            SafeCout::safe_cout(e.what());
            throw std::runtime_error("Handshake Error!");
        }


            SafeCout::safe_cout("Autenticazione riuscita");

        //Inizializzo il thread che gestisce il FileWatcher
        std::thread fwt(FileWatcherThread, fw);

        //Inizializzo il thread che gestisce i messaggi in entrata
        std::thread rt(ReceiverThread);

        //Inizializzo il thread che gestische i download dei file
        std::thread fdt(FileDownloaderDispatcherThread);

        //Inizializzo il thread che gestice l'upload dei file
        std::thread fut(FileUploaderDispatcherThread);

        SafeCout::safe_cout("Processando le differenze...");

        //Processo differenze tra le fileList
        checkDifferences(fileListW, fileListR.value());

        SafeCout::safe_cout("Differenze aggiornate");

        //Avvio tutti i thread
        running.store(true);

        //Gestisco possibili connection lost
        while (running.load()) {
            std::unique_lock<std::mutex> lck(reconnection_mtx);
            reconnection_cv.wait(lck);
            SafeCout::safe_cout("Connection lost, trying to reconnect...");

            //Comunico a tutti i thread di terminare
            running.store(false);
            download_cv.notify_all();
            upload_cv.notify_all();

            while (!running.load()) {
                //Reconnection
                socket_.reset(new boost::asio::ip::tcp::socket(ioc));
                socket_->connect(tcp::endpoint(dst_ip, dst_port), ec);
                if (!ec) {
                    SafeCout::safe_cout("Riconnessione riuscita!");
                    socket_wptr = boost::weak_ptr<tcp::socket>(socket_);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(RECONN_DELAY));
            }
            if(rt.joinable())   rt.join();
            if(fwt.joinable())  fwt.join();
            if(fdt.joinable())  fdt.join();
            if(fut.joinable())  fut.join();
            break;
        }
    }
    return 0;
}