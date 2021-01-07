
//Scrittura asincrona del messaggio su boost_socket
template <typename Handler>
void Message::asyncWrite(Handler handler) {
//    SafeCout::safe_cout("write");

    //Serializzazione messaggio
    std::ostringstream archive_stream;
    auto ota = text_oarchive(archive_stream);
    ota << *this;
    this->outbound_data_ = archive_stream.str();

    //Serializzazione header
    std::ostringstream header_stream;
    header_stream << std::setw(HEADER_LENGTH)<< std::hex << outbound_data_.size();
    this->outbound_header_ = header_stream.str();

    strand_wptr.lock()->post([handler, self = shared_from_this()]{

        //Invio header
        socket_wptr.lock()->async_write_some(boost::asio::buffer(self->outbound_header_), [handler, self](boost::system::error_code ec, std::size_t bytes_transferred){
            if(self->outbound_header_.size() != bytes_transferred || ec) {
                SafeCout::safe_cout("Write Error!");
                handler(self, 1);
            }

            //Invio messaggio
            socket_wptr.lock()->async_write_some(boost::asio::buffer(self->outbound_data_), [handler, self](boost::system::error_code ec, std::size_t bytes_transferred){
                if(self->outbound_data_.size() != bytes_transferred || ec){
                    SafeCout::safe_cout("Write Error!");
                    handler(self, 1);
                    return;
                }
                handler(self, 0);
            });
        });
    });
}

//Lettura asincrona del messaggio da boost_socket
template<typename Handler>
void Message::asyncRead(Handler handler){
//    SafeCout::safe_cout("read");

    strand_wptr.lock()->post([handler, self = shared_from_this()]{

        //Ricezione Header
        socket_wptr.lock()->async_read_some(boost::asio::buffer(self->inbound_header_), [handler, self](boost::system::error_code ec, std::size_t bytes_transferred){
            if(bytes_transferred != self->inbound_header_.size()) {
                SafeCout::safe_cout("Read Error/Broken header!");
                handler(self, 1);
                return;
            }

            //Deserializzazione Header
            std::istringstream header_stream(std::string(self->inbound_header_.begin(),self->inbound_header_.end()));
            size_t message_length;
            header_stream >> std::hex >> message_length;

            //Ricezione Messaggio
            self->inbound_data_ = std::vector<char>(message_length);
            socket_wptr.lock()->async_read_some(boost::asio::buffer(self->inbound_data_), [handler, self](boost::system::error_code ec, std::size_t bytes_transferred){
                if(bytes_transferred != self->inbound_data_.size()){
                    SafeCout::safe_cout("Read Error/Broken message!");
                    handler(self, 1);
                    return;
                }

                //Deserializzazione Messaggio
                std::istringstream archive_stream(std::string(self->inbound_data_.begin(), self->inbound_data_.end()));
                auto ita = text_iarchive(archive_stream);
                ita >> *self;

                handler(self, 0);
            });
        });
    });
}