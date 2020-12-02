#include <boost/filesystem.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <iostream>
#include "Message.h"
#include <sstream>
#include <boost/asio.hpp>



#define CHUNK_SIZE 1024

#define ROOT "../ToSincronize"

using namespace boost::filesystem;
using namespace boost::archive;
using namespace boost::asio;
using namespace boost::asio::ip;

int main()
{
    std::string root = std::string(ROOT);
    std::string indent = "    ";
    path root_dir (ROOT);

    if(exists(root_dir))    std::cout<<"La directory esiste"<<std::endl;
    else {
        create_directory(ROOT);
        std::cout<<"Directory Creata"<<std::endl;
    }

    recursive_directory_iterator dir(root_dir), end;

    try {

        while (dir != end) {

            if (is_regular_file(dir->path())) {
                std::cout <<indent<< dir->path().filename()<<" "<< file_size(dir->path())<<" B"<<std::endl;

            } else if (is_directory(dir->path())) {
                std::cout <<"La cartella "<<dir->path().filename()<<" contiene:"<<std::endl;
            }

            ++dir;

        }
    }    catch (const filesystem_error& ex)
    {
        std::cout << ex.what() << '\n';
    }

    size_t chunk_size = CHUNK_SIZE;
    std::ifstream ifs(root + "/prova.txt", std::ios::binary);
//    std::ofstream ofs(root + "/output.txt", std::ios::binary);
//
//    unsigned char hash[SHA256_DIGEST_LENGTH];
//
    std::vector<char> buffer( CHUNK_SIZE );
//    ifs.read( buffer.data(), buffer.size() );
    size_t size= ifs.gcount();
//    std::streamsize ssize=ifs.gcount();
    ifs.close();
//
//    ofs.write(buffer.data(), sizze);
//    ofs.close();



    std::ostringstream os;
    os << buffer;
    binary_oarchive oa(os);

    auto message = Message(0, buffer, size);

    std::ostringstream archive_stream;
    auto oba = binary_oarchive(archive_stream);
    oba << message;
    std::string outbound_data_ = archive_stream.str();

    io_context ioc;
    tcp::socket socket(ioc);
    socket.connect(tcp::endpoint(address::from_string("127.0.0.1"),5001));

    std::size_t sizeW;

    boost::asio::async_write(socket, boost::asio::buffer(outbound_data_),
                             [&sizeW](const boost::system::error_code& error, std::size_t bytes_transferred){
        sizeW = bytes_transferred;
        if(error != 0) throw std::runtime_error(error.message());
    });






    return 0;
}