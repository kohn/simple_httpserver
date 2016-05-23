#include "httpserver.hpp"

using namespace httpserver;

int main(int argc, char *argv[])
{
    HttpServer server(9000, 4);

    server.add_resource("/index", [](Request &request, Response &response, Connection &connection){
            response.content = "this is a test page";
	    return true;
        });

    server.add_resource("/largefile", [](Request &request, Response &response, Connection &connection){
	    connection.write_staticfile("/Users/kohn/am_data/db.sqlite3", "db.sqlite3");
            return false;
        });
    
    server.start();
    return 0;
}
