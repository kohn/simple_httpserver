#include "httpserver.hpp"

using namespace httpserver;

int main(int argc, char *argv[])
{
    HttpServer server(9000, 4);

    // normal use
    server.add_resource("/index", [](Request &request, Response &response, Connection &connection){
	    // write html code to response
            response.content = "this is a test page";
	    return true;
        });

    // download large file
    server.add_resource("/largefile", [](Request &request, Response &response, Connection &connection){
	    // call write_staticfile("path_to_file", "filename";
	    connection.write_staticfile("main.cpp", "main.cpp");
            return false;
        });
    
    server.start();
    return 0;
}
