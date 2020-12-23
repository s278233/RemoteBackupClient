#include <boost/filesystem.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <csignal>
#include "Message.h"
#include "FileWatcher.h"
#include "SafeCout.h"

#define RECONN_DELAY 5

#define CHUNK_SIZE  1024

#define ITERATIONS  10001
#define KEY_LENGTH  61

using namespace boost::filesystem;
using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std::placeholders;

boost::weak_ptr<ssl::stream<tcp::socket>> socket_wptr;
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
        //Processo solo i file che non sono in download e i file/dir non corrotte
        if((!std::filesystem::is_regular_file(path_to_watch) && !std::filesystem::is_directory(path_to_watch))
        && status != FileStatus::erasedFile && status != FileStatus::erasedDir
        && (!(std::find(path_ignore_pool.begin(), path_ignore_pool.end(), path_to_watch) != path_ignore_pool.end())
        )) {
            return;
        }

        std::lock_guard<std::mutex> lg(upload_mtx);

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
            case FileStatus::erasedFile:
                SafeCout::safe_cout("File erased: ", path_to_watch);
                Message(FILE_DEL, std::vector<char>(path_to_watch.begin(), path_to_watch.end())).syncWrite(socket_wptr);
                break;
            case FileStatus::erasedDir:
                SafeCout::safe_cout("Directory erased: ", path_to_watch);
                Message(DIR_DEL, std::vector<char>(path_to_watch.begin(), path_to_watch.end())).syncWrite(socket_wptr);
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
        SafeCout::safe_cout("FileUploader connection exception: ", e.what());
            upload_pool.clear();
            reconnection_cv.notify_one();
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

            if(message.getType() == DIR) {
                FileWatcher::addPath(path);
                std::filesystem::create_directory(path);
            }

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
        SafeCout::safe_cout("FileDownloader connection exception: ", e.what());
            download_pool.clear();
            reconnection_cv.notify_one();
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
        SafeCout::safe_cout("Receiver connection exception: ", e.what());
            reconnection_cv.notify_one();
    }catch (const std::runtime_error &e) {
        SafeCout::safe_cout("Receiver exception: ", e.what());
    }

}

bool verify_certificate(bool preverified,
                        boost::asio::ssl::verify_context& ctx)
{
// The verify callback can be used to check whether the certificate that is
// being presented is valid for the peer. For example, RFC 2818 describes
// the steps involved in doing this for HTTPS. Consult the OpenSSL
// documentation for more details. Note that the callback is called once
// for each certificate in the certificate chain, starting from the root
// certificate authority.

// In this example we will simply print the certificate's subject name.
    char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    std::cout << "Verifying " << subject_name << "\n";

    return preverified;
}

int main(int argc, char* argv[])
{
    if(argc!=5) throw std::runtime_error("Wrong number of arguments! (server_ip server_port username password");

    boost::system::error_code ec;
    Message message;

    //Dati di autenticazione (WARNING!: per motivi di debug il sale è identico per tutti i client)
    auto username = std::string(argv[3]);
    auto password = std::string(argv[4]);
    std::string salt = "1238e37cc78ea0ad4a2d44ecf4b5f89919a72f76f1d097ca860689c96ea1347f210afca88c437344fc69ffd90936c979b822af9b0ee284855aa80ddda3";
    auto auth_data = std::pair<std::string, std::string>(username, Message::compute_password(password, salt, ITERATIONS, KEY_LENGTH));

    //Dati di connessione
    address dst_ip;
    int dst_port;

    //Controllo parametri programma
    try {
        auto ip = std::string(argv[1]);
        dst_ip = ip::make_address(ip);
    } catch (boost::system::system_error const &e) {
        throw std::runtime_error("Invalid IP");
    }
    try {
        auto port = std::string(argv[2]);
        dst_port = std::atoi(port.c_str());
        if(dst_port < 0 || dst_port > 655535) {
            std::cerr<<"Invalid Port"<<std::endl;
            return 1;
        }

    } catch (const std::runtime_error &e) {
        throw std::runtime_error("Invalid Port");
    }

    //Setup iniziale SSL
    boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
    ctx.load_verify_file("../sec-files/rootca.crt");

    //Creazione socket
    io_context ioc;
    tcp::resolver resolver(ioc);
    auto endpoints = resolver.resolve(tcp::endpoint(dst_ip, dst_port));
    auto socket_ = boost::make_shared<ssl::stream<tcp::socket>>(ioc, ctx);
    socket_->set_verify_mode(boost::asio::ssl::verify_peer);
    socket_->set_verify_callback(verify_certificate);

    SafeCout::safe_cout("FileWatcher inizializzazione...");

    //Inizializzo il filewatcher (viene effettuato un primo controllo all'avvio sui file)
    FileWatcher fw{"../" + username, std::chrono::milliseconds(5000), running};//5 sec di delay

    SafeCout::safe_cout("FileWatcher inizializzato");

        while(true) {

            while (!running.load()) {

                try {

                    //Connessione col server
                    boost::asio::connect(socket_->lowest_layer(), endpoints, ec);
                    socket_->handshake(boost::asio::ssl::stream_base::client);
                    SafeCout::safe_cout("Connessione Riuscita!");
                    socket_wptr = boost::weak_ptr<ssl::stream<tcp::socket>>(socket_);
                    break;

                } catch (boost::system::system_error const &e) {
                    SafeCout::safe_cout("Can't connect to remote server!", "\n", "Trying to reconnect...");
                }
                //Riconnessione
                std::this_thread::sleep_for(std::chrono::seconds(RECONN_DELAY));
                socket_->lowest_layer().close();
                socket_.reset(new ssl::stream<tcp::socket>(ioc, ctx));
                socket_->set_verify_mode(boost::asio::ssl::verify_peer);
                socket_->set_verify_callback(verify_certificate);
            }


        //Autenticazione(two-way)
        SafeCout::safe_cout("Autenticazione...");

        //Inizializzo fileList da inviare
        auto fileListW = FileWatcher::getPaths();
        std::optional<std::map<std::string, std::string>> fileListR;

        try {

            //REQ
            message.syncRead(socket_wptr);
            if (message.getType() != AUTH_REQ) throw std::runtime_error("Auth Error!");

            //RES
            auto authMessage = Message(auth_data);
            authMessage.syncWrite(socket_wptr);

            //Scambio lista file
            Message(fileListW).syncWrite(socket_wptr);
            message.syncRead(socket_wptr);
            fileListR = message.extractFileList();

            if(!fileListR.has_value()){
                std::cerr<<"Error FileList synchronization!"<<std::endl;
                return 1;
            }

        } catch (boost::system::system_error const &e) {
            throw std::runtime_error("Wrong Username/Password");

        } catch (const std::runtime_error &e) {
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


            if(rt.joinable())   rt.join();
            if(fwt.joinable())  fwt.join();
            if(fdt.joinable())  fdt.join();
            if(fut.joinable())  fut.join();

            //socket_->shutdown();
            socket_->lowest_layer().close();
            socket_.reset(new ssl::stream<tcp::socket>(ioc, ctx));
            socket_->set_verify_mode(boost::asio::ssl::verify_peer);
            socket_->set_verify_callback(verify_certificate);
            break;
        };
    }
    return 0;
}