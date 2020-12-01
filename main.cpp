#include <boost/filesystem.hpp>
#include <iostream>

#define CHUNK_SIZE 1024

#define ROOT "../ToSincronize"

using namespace boost::filesystem;

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
    std::ofstream ofs(root + "/output.txt", std::ios::binary);

    while(!ifs.eof())
    {
        std::vector<char> buffer( CHUNK_SIZE );
        ifs.read( buffer.data(), buffer.size() );
        std::streamsize ssize=ifs.gcount();

        ofs.write( buffer.data(), ssize );
    }

    return 0;
}