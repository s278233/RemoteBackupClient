//
// Created by lucio on 01/12/2020.
//

#ifndef REMOTEBACKUPCLIENT_MESSAGE_H
#define REMOTEBACKUPCLIENT_MESSAGE_H

//Types
//-1    =       Error
//0     =       Auth_Message
//1     =       EndInit_Message
//2     =

#include <string>
#include <boost/archive/binary_oarchive.hpp>
#include <fstream>

using namespace boost::archive;


class Message {
    int type;
    binary_oarchive oba();

    void write(std::ofstream ofs){

    }

};


#endif //REMOTEBACKUPCLIENT_MESSAGE_H
