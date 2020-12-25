#include <boost/filesystem.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include "Message.h"
#include "FileWatcher.h"

#define RECONN_DELAY 5

#define CHUNK_SIZE  1452

#define MAX_DOWNLOAD_POOL   100

#define ITERATIONS  10001
#define KEY_LENGTH  61

using namespace boost::filesystem;
using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std::placeholders;

std::condition_variable limit_cv;

std::mutex reconnection_mtx;
std::condition_variable reconnection_cv;

std::mutex download_mtx;
std::condition_variable download_cv;

std::mutex upload_mtx;
std::condition_variable upload_cv;

std::atomic_bool running;

std::list<Message> download_pool;

std::list<std::pair<std::string,FileStatus>> upload_pool;

void checkDifferences(const std::map<std::string, std::string>& src,std::map<std::string, std::string>& dst){
    for(const auto& file:src)
        if(!dst.contains(file.first) || (dst.contains(file.first) && file.second != dst[file.first])) {
            upload_pool.push_front(std::pair(file.first, FileStatus::created));
        } else if(file.second != dst[file.first]) {
            upload_pool.push_front(std::pair(file.first, FileStatus::modified));
        }
}

void FileWatcherThread(FileWatcher fw){

    SafeCout::safe_cout("FileWatcher Thread avviato");

    fw.start([] (const std::string& path_to_watch, FileStatus status) -> void {
        if(!running.load()) return;
        //Processo solo i file che non sono in download e i file/dir non corrotti
        if((!std::filesystem::is_regular_file(path_to_watch) && !std::filesystem::is_directory(path_to_watch))
        && status != FileStatus::erasedFile && status != FileStatus::erasedDir
        || (path_to_watch.find(TMP_PLACEHOLDER) != std::string::npos)
        ) {
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
                Message(FILE_DEL, std::vector<char>(path_to_watch.begin(), path_to_watch.end())).syncWrite();
                break;
            case FileStatus::erasedDir:
                SafeCout::safe_cout("Directory erased: ", path_to_watch);
                Message(DIR_DEL, std::vector<char>(path_to_watch.begin(), path_to_watch.end())).syncWrite();
                break;
            default:
                SafeCout::safe_cout("Error! Unknown file status.");
        }
    });

    SafeCout::safe_cout("FileWatcher Thread terminato");
}

void FileUploaderDispatcherThread(){
    std::ifstream ifs;
    std::vector<char> buffer( CHUNK_SIZE );
    size_t size;
    Message message;
    std::string path;

    SafeCout::safe_cout("FileUploader Thread avviato ");


    try {
        while (running.load()) {

            {
                std::unique_lock<std::mutex> lck(upload_mtx);

                upload_cv.wait(lck, []() {
                    return (!upload_pool.empty() || !running.load());
                });

                if (!running.load()) break;

                path = upload_pool.back().first;
                upload_pool.pop_back();
            }


            //Upload cartella
            if (std::filesystem::is_directory(path)) {
                SafeCout::safe_cout("uploading dir", path);
                message = Message(DIR, std::vector<char>(path.begin(), path.end()));
                message.syncWrite();
                SafeCout::safe_cout("uploaded dir", path);
            } else {
                //Upload file
                SafeCout::safe_cout("uploading file", path);
                ifs.open(path, std::ios::binary);
                message = Message(FILE_START, std::vector<char>(path.begin(), path.end()));
                message.syncWrite();
                if (std::filesystem::file_size(path) != 0)
                    while (!ifs.eof()) {

                        if(!std::filesystem::exists(path) || !running.load()) {
                            SafeCout::safe_cout("Error while uploading file!");
                            break;
                        }

                        ifs.read(buffer.data(), buffer.size());
                        size = ifs.gcount();

                        if (size < CHUNK_SIZE)
                            buffer.resize(size);
                        message = Message(FILE_DATA, buffer);
                        message.syncWrite();
                        buffer.clear();
                        std::vector<char>(CHUNK_SIZE).swap(buffer);
                    }
                ifs.close();
                message = Message(FILE_END);
                message.syncWrite();
                if(std::filesystem::exists(path) && running.load())
                SafeCout::safe_cout("uploaded file", path);
            }
        }
    }catch (boost::system::system_error const &e) {
        SafeCout::safe_cout("FileUploader connection exception: ", e.what());
    } catch (const std::exception &e) {
        SafeCout::safe_cout("FileUploader exception: ", e.what());
    }
    SafeCout::safe_cout("FileUploader Thread terminato");
    reconnection_cv.notify_one();
}

void FileDownloaderDispatcherThread(){
    Message message;
    std::ofstream ofs;
    std::string path;
    std::string tmp_path;

    SafeCout::safe_cout("FileDownloader Thread avviato ");

    try {

        while (running.load()) {


            {
                std::unique_lock<std::mutex> lck(download_mtx);

                download_cv.wait(lck, []() {
                    return (!download_pool.empty() || !running.load());
                });

                if (!running.load()) break;

                message = download_pool.back();
                download_pool.pop_back();

            }

            limit_cv.notify_one();

            path = std::string(message.getData().begin(), message.getData().end());

            if(message.getType() == DIR) {
                SafeCout::safe_cout("downloading dir", path);
                FileWatcher::addPath(path, "");
                std::filesystem::create_directory(path);
                SafeCout::safe_cout("downloaded dir ", path);
            }

            else if (message.getType() != FILE_START) {
                SafeCout::safe_cout("Error File Download");

            } else {

                SafeCout::safe_cout("downloading file", path);

                tmp_path = path.substr(0, path.find_last_of('/') +1 );
                tmp_path+=std::string(TMP_PLACEHOLDER);

                ofs.open(tmp_path, std::ios::binary);

                while (true) {

                    {
                        std::unique_lock<std::mutex> lck(download_mtx);

                        download_cv.wait(lck, []() {
                            return (!download_pool.empty() || !running.load());
                        });

                        if(!running.load()){

                            ofs.close();

                            std::remove(tmp_path.c_str());

                            break;
                        }

                        message = download_pool.back();

                        download_pool.pop_back();


                    }

                    limit_cv.notify_one();

                    if (message.getType() == FILE_END) {
                        ofs.close();
                        FileWatcher::addPath(path, tmp_path);
                        SafeCout::safe_cout("downloaded file", path);
                        break;
                    }


                    if (message.getType() != FILE_DATA) {
                        SafeCout::safe_cout("Error File Download");
                        ofs.close();
                        std::remove(tmp_path.c_str());
                        break;
                    }

                    ofs.write(message.getData().data(), message.getData().size());

                }
            }
        }
    } catch (boost::system::system_error const &e) {
        SafeCout::safe_cout("FileDownloader connection exception: ", e.what());
        ofs.close();
        std::remove(tmp_path.c_str());
    }catch (const std::exception &e) {
        ofs.close();
        std::remove(tmp_path.c_str());
        SafeCout::safe_cout("FileDownloader exception: ", e.what());
    }
    SafeCout::safe_cout("FileDownloader Thread terminato");
    reconnection_cv.notify_one();
}

void ReceiverThread(){
    Message message;

    SafeCout::safe_cout("Receiver Thread avviato");

    try {
    while(running.load()) {

        {
            std::unique_lock<std::mutex> lck(download_mtx);

        limit_cv.wait(lck, []() {
            return (download_pool.size() < MAX_DOWNLOAD_POOL || !running.load());
        });

        if (!running.load()) break;

        }

                //Ricezione messaggio
                message.syncRead();

        if (!running.load()) break;

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
            case FILE_END: {
                std::lock_guard<std::mutex> lg(download_mtx);
                download_pool.push_front(message);
            }
                download_cv.notify_one();
            break;
            default: SafeCout::safe_cout("Message type not recognized!");
            break;
        }
    }
    } catch (boost::system::system_error const &e) {
        SafeCout::safe_cout("Receiver connection exception: ", e.what());
    }catch (const std::exception &e) {
        SafeCout::safe_cout("Receiver exception: ", e.what());
    }
    SafeCout::safe_cout("Receiver Thread terminato");
    reconnection_cv.notify_one();
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
    if(username == "logs") throw std::runtime_error("Wrong Username/Password!");
    SafeCout::set_log_path("../logs/", username + "_log.txt");
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

    } catch (const std::exception &e) {
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
                    Message::setSocket(boost::weak_ptr<ssl::stream<tcp::socket>>(socket_));
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
            message.syncRead();
            if (message.getType() != AUTH_REQ){
                std::cerr<<"Auth Error!"<<std::endl;
                return 1;
            }

            //RES
            auto authMessage = Message(auth_data);
            authMessage.syncWrite();

            //Scambio lista file
            Message(fileListW).syncWrite();
            message.syncRead();
            fileListR = message.extractFileList();

            if(!fileListR.has_value()){
                std::cerr<<"Error FileList synchronization!"<<std::endl;
                return 1;
            }

        } catch (boost::system::system_error const &e) {
            std::cerr<<"Wrong Username/Password!"<<std::endl;
            return 1;
        } catch (const std::exception &e) {
            std::cerr<<"Handshake error!"<<std::endl;
            return 1;
        }

        SafeCout::safe_cout("Autenticazione riuscita");

        SafeCout::safe_cout("Processando le differenze...");

        //Processo differenze tra le fileList
        checkDifferences(fileListW, fileListR.value());

        SafeCout::safe_cout("Differenze aggiornate");

        //Avvio tutti i thread
        running.store(true);

        //Avvio il thread che gestisce i messaggi in entrata
        std::thread rt(ReceiverThread);

        //Avvio il thread che gestische i download dei file
        std::thread fdt(FileDownloaderDispatcherThread);

        //Avvio il thread che gestice l'upload dei file
        std::thread fut(FileUploaderDispatcherThread);

        //Avvio il thread che gestisce il FileWatcher
        std::thread fwt(FileWatcherThread, fw);

        //Gestisco possibili connection lost
        std::unique_lock<std::mutex> lck(reconnection_mtx);
        reconnection_cv.wait(lck);
        SafeCout::safe_cout("Connection lost, trying to reconnect...");

        //Comunico a tutti i thread di terminare
        running.store(false);
        download_cv.notify_all();
        upload_cv.notify_all();
        limit_cv.notify_all();

        socket_->lowest_layer().cancel();

        if(rt.joinable())   rt.join();
        if(fwt.joinable())  fwt.join();
        if(fdt.joinable())  fdt.join();
        if(fut.joinable())  fut.join();

            socket_->lowest_layer().close();
            socket_.reset(new ssl::stream<tcp::socket>(ioc, ctx));
            socket_->set_verify_mode(boost::asio::ssl::verify_peer);
            socket_->set_verify_callback(verify_certificate);

        download_pool.clear();
        upload_pool.clear();

    }
}