#ifndef HTTPSERVER_H
#define HTTPSERVER_H
#include <string>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>
#include <thread>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <sstream>
#include <memory>
#include <fstream>
#include <chrono>

#define LOG() std::cout << __FILE__ << ":" << __LINE__ << std::endl

namespace httpserver{

    class Request;
    class Response;
    class Connection;
    class HttpServer;
    enum HttpStatus{OK, NOT_FOUND};

    typedef std::unordered_map<std::string, boost::function<bool (Request&, Response&, Connection&)> > res_type;

    class HttpStatusUtil{
    public:
        static const std::string get_status_line(HttpStatus status){
            switch (status) {
            case NOT_FOUND:
                return "404 Not Found";
            default:            // default to ok
                return "200 OK";
            }
        }
    };

    class Request
    {
        friend class Connection;
    private:
        boost::asio::streambuf streambuf_;

    public:
        std::string content;
        std::unordered_map<std::string, std::string> headers;
        std::string method;
        std::string path;
        std::string http_version;
        
        void parse_headers(){
            std::string line;
            std::istream is(&streambuf_);

	    // parse status line
	    std::getline(is, line);
	    std::vector<std::string> split_container;
	    boost::split(split_container, line, boost::is_any_of(" \r\n"), boost::token_compress_on);
	    method = split_container[0];
	    path = split_container[1];
	    http_version = split_container[2];

	    while(true){
		std::getline(is, line);

		size_t index = line.find(':');
		if(index == std::string::npos)
		    break;
		std::string key = line.substr(0, index);
		std::string value = line.substr(index+1);

		boost::trim(key);
		boost::trim(value);
		
		//std::cout << key << ": " << value << std::endl;
		headers[key] = value;
	    }
        }
    };

    class Response
    {
        friend class Connection;
    private:
        boost::asio::streambuf streambuf_;
        bool headers_filled_;

        bool key_exists(std::string key){
            auto iter = headers.find(key);
            return !(iter == headers.end());           
        }
        
    public:
        std::string content;
        std::unordered_map<std::string, std::string> headers;
        HttpStatus status;

        void fill_headers_and_content(){
            std::ostream os(&streambuf_);
            if(headers_filled_ == false){
                if(!key_exists("Content-Length")){
                    try{
                        headers["Content-Length"] = boost::lexical_cast<std::string>(content.size());
                    } catch(const std::exception &e){
                        headers["Content-Length"] = "0";
                    }
                }

                if(!key_exists("Content-type")){
                    headers["Content-Type"] = "text/plain";
                }
                
                os << "HTTP/1.1 " << HttpStatusUtil::get_status_line(status) << "\r\n";
                for(auto p : headers){
                    os << p.first << ": " << p.second << "\r\n";
                }
                os << "\r\n";
                headers_filled_ = true;
            }
            os << content;
        }

        Response(): headers_filled_(false){}
    };

    class Connection : public boost::enable_shared_from_this<Connection> {
    private:
        boost::shared_ptr<boost::asio::ip::tcp::socket> socket_;
        Request request_;
        Response response_;

	void process_request(const res_type &resources){
	    auto iter = resources.find(request_.path);
	    bool need_write = true;
	    if(iter == resources.end()){
		response_.status = NOT_FOUND;
		std::cout << "404 request: " << request_.path << std::endl;
	    } else{
		need_write = iter->second(request_, response_, *this);
	    }
	    if(need_write)
		do_write();
	}

        void handler_read_request(const boost::system::error_code& e,
                                  std::size_t size,
                                  res_type &resources){
            if(e){
                std::cerr << __FUNCTION__ << e.message() << std::endl;
                return;
            }
            request_.parse_headers();

	    std::cout << "Got Request: " << request_.method << " "
		      << request_.path << " "
		      << request_.http_version << std::endl;
	    
            size_t bytes_remain = request_.streambuf_.size() - size;
            auto iter = request_.headers.find("Content-Length");
            if(iter != request_.headers.end()){ // content-length found
                unsigned long long content_length;
                try {
                    content_length = stoull(iter->second);
                }
                catch(const std::exception &e) {
                    return;
                }
                if(content_length > bytes_remain) {
                    boost::asio::async_read(*socket_, request_.streambuf_,
                                            boost::asio::transfer_exactly(content_length - bytes_remain),
                                            [this, resources]
                                            (const boost::system::error_code& ec, size_t) {
                                                if(ec)
                                                    std::cerr <<  __FUNCTION__ << ec.message() << std::endl;
                                                else{
						    process_request(resources);
                                                }
                                            });
                }

            } else{
		process_request(resources);
            }
        }

        void handler_write_response(const boost::system::error_code &e, std::size_t size){
            if(e){
                std::cerr <<  __FUNCTION__ << e.message() << std::endl;
                return;
            } else{
                //std::cout << "write finished with " << size << "bytes" << std::endl;
            }
        }

    public:
        explicit Connection(boost::shared_ptr<boost::asio::ip::tcp::socket> socket):socket_(socket){}
        // read from socket, fill into @request_
        void do_read(res_type &resources){
            boost::asio::async_read_until(*socket_,
                                          request_.streambuf_,
                                          "\r\n\r\n",
                                          std::bind(&Connection::handler_read_request,
                                                    shared_from_this(),
                                                    std::placeholders::_1,
                                                    std::placeholders::_2,
                                                    resources));
        }

        // write headers and content of response to socket in HTTP format
        void do_write(){
            response_.fill_headers_and_content();
            boost::asio::async_write(*socket_,
                                     response_.streambuf_,
                                     std::bind(&Connection::handler_write_response,
					       shared_from_this(),
                                               std::placeholders::_1,
                                               std::placeholders::_2));
        }

	void write_staticfile(std::string file_path, std::string filename){
	    std::ifstream ifs(file_path, std::ifstream::binary);
	    std::stringstream os;
	    if(!ifs.good()){
		boost::asio::write(*socket_, boost::asio::buffer("HTTP/1.1 200 OK\r\nfile not found"));
		return;
	    }
	    
	    // get length of file:
	    ifs.seekg(0, ifs.end);
	    size_t length = ifs.tellg();
	    ifs.seekg(0, ifs.beg);
	    
	    os << "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n";
	    os << "Content-Length: " << length << "\r\n";
	    os << "Content-Disposition: attachment; filename=" << filename << "\r\n\r\n";
	    boost::asio::write(*socket_, boost::asio::buffer(os.str()));
	    
	    while(!ifs.eof()){
		std::string str;
		size_t transfer_length = length>4096?4096:length;
		str.resize(transfer_length, ' '); // reserve space
		char* begin = &*str.begin();
		ifs.read(begin, transfer_length);		    
		length -= transfer_length;
		boost::asio::write(*socket_, boost::asio::buffer(str));
		if(length == 0)
		    break;
	    }
	    ifs.close();
	}

        ~Connection(){
	    
	}
    };

    class HttpServer
    {
    private:
        int port_;
        size_t thread_num_;
        boost::asio::io_service io_service_;
        boost::asio::ip::tcp::acceptor acceptor_;
        std::vector<std::thread> threads_;
    
	res_type resources;

        void handle_accept(boost::shared_ptr<boost::asio::ip::tcp::socket> socket, const boost::system::error_code &err){
            accept();

            if(!err){
                boost::asio::ip::tcp::endpoint remote_endpoint = socket->remote_endpoint();
                //std::cout << "new connection from: " << remote_endpoint.address() << ":" << remote_endpoint.port() << std::endl;
                boost::shared_ptr<Connection> connection(new Connection(socket));
                connection->do_read(resources);
            } else{
                std::cerr << err.message() << std::endl;
            }
        }

        void accept(){
            boost::shared_ptr<boost::asio::ip::tcp::socket> socket(new boost::asio::ip::tcp::socket(io_service_));
            acceptor_.async_accept(*socket, std::bind(&HttpServer::handle_accept, this, socket, std::placeholders::_1));
        }

    
    public:
        HttpServer(int port, size_t thread_num) : port_(port),
                                                  thread_num_(thread_num),
                                                  acceptor_(io_service_){
        }
        virtual ~HttpServer(){}

	// callback function return false to ask httpserver not send response
        void add_resource(std::string url, boost::function<bool (Request&, Response&, Connection&)> fun){
            resources[url] = fun;
        }

        void start(){

            if(io_service_.stopped())
                io_service_.reset();

            boost::asio::ip::tcp::endpoint endpoint=boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port_);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
     
            accept();
            
            threads_.clear();
            for(size_t i=1; i<thread_num_; ++i){
                threads_.push_back(std::thread([this](){
                            io_service_.run();
                        }));
                    }
            io_service_.run();
        }

    };
}
#endif
