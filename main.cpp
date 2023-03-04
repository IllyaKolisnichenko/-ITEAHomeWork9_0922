#include <ostream>
#include <ctime>
#include <string>
#include <cmath>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <thread>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

/*
 * tested using "curl" command.
 * Examples:
 * curl -X GET -d "factorial 5" 127.0.0.1:8080
 * curl -X POST -d "abs 5,87,2,5,1,4,67,6" 127.0.0.1:8080
 */

using boost::asio::ip::tcp;

class HttpServer;

class Request : public boost::enable_shared_from_this<Request>
{
    // member variables
    [[maybe_unused]] HttpServer& server;
    boost::asio::streambuf request;
    boost::asio::streambuf response;
    double answer_;

    void afterRead(const boost::system::error_code& ec, std::size_t bytes_transferred)
    {
        std::ostream res_stream(&response);

        std::istream is(&request);
        std::string buffer;
        std::getline(is, buffer);
        std::vector<std::string> requestMethod;
        boost::split(requestMethod, buffer, boost::is_any_of(" "));

        if (requestMethod[0] != "GET" && requestMethod[0] != "POST") {
            writeError(res_stream, "Wrong request type!\r\nSupported requests: POST, GET");
            return;
        }

        while (std::getline(is, buffer, '\n'))
        {
            if (buffer.empty() || buffer == "\r")
                break;

            if (buffer.back() == '\r')
                buffer.resize(buffer.size() - 1);
        }

        std::string const arguments(std::istreambuf_iterator<char>{is}, {});

        std::string operation;
        std::vector<std::string> sParameters;
        std::vector<double> dParameters;

        const auto equals_idx = arguments.find_first_of(' ');
        operation = arguments.substr(0, equals_idx);
        boost::split(sParameters, arguments.substr(equals_idx + 1), boost::is_any_of(","));

        if (sParameters.empty() || operation.empty()) {
            writeError(res_stream, "Not enough arguments.\r\nExample:\r\nfibonacci 10");
            return;
        }

        for (const auto& sParam : sParameters)
            dParameters.push_back(std::atof(sParam.c_str()));


        if(!calculateAnswer(operation, dParameters))
        {
            writeError(res_stream, "Wrong parameters");
            return;
        }


        writeAnswer(res_stream, boost::lexical_cast<std::string>(answer_));
    }

    bool calculateAnswer(const std::string& operation, const std::vector<double>& parameters)
    {
        answer_ = 0;
        if (operation == "factorial" && parameters.size() == 1)
        {
            if (parameters[0] < 0)
                return false;

            answer_ = 1;
            for (int i = 1; i <= parameters[0]; i++)
                answer_ *= i;

            return true;
        }
        else if (operation == "fibonacci" && parameters.size() == 1)
        {
            if (parameters[0] < 3)
            {
                answer_ = 1;
                return true;
            }

            int n1 = 0;
            int n2 = 1;
            int n3;

            for (int i = 2; i < parameters[0]; i++)
            {
                n3 = n1 + n2;
                n1 = n2;
                n2 = n3;
            }

            answer_ = n3;
            return true;
        }
        else if (operation == "cos" && parameters.size() == 1)
        {
            answer_ = cos(parameters[0]);
            return true;
        }
        else if (operation == "sin" && parameters.size() == 1)
        {
            answer_ = sin(parameters[0]);
            return true;
        }
        else if (operation == "tan" && parameters.size() == 1)
        {
            answer_ = tan(parameters[0]);
            return true;
        }
        else if (operation == "sqrt" && parameters.size() == 1)
        {
            answer_ = sqrt(parameters[0]);
            return true;
        }
        else if (operation == "pow" && parameters.size() == 2)
        {
            answer_ = pow(parameters[0], parameters[1]);
            return true;
        }
        else if (operation == "abs")
        {
            for (auto i : parameters)
                answer_ += i;

            answer_ = answer_ / parameters.size();
            return true;
        }

        return false;
    }

    void afterWrite(const boost::system::error_code& ec, std::size_t bytes_transferred)
    {
        // done writing, closing connection
        socket->close();
    }

    void writeError(std::ostream& res_stream, const std::string& error)
    {
        res_stream << "HTTP/1.0 400 Bad Request\r\n"
                   << "Content-Type: text/html; charset=UTF-8\r\n"
                   << "Content-Length: " << error.size() + 2 << "\r\n\r\n"
                   << error << "\r\n";

        boost::asio::async_write(*socket, response, boost::bind(&Request::afterWrite, shared_from_this(), _1, _2));
    }

    void writeAnswer(std::ostream& res_stream, const std::string& answer)
    {
        res_stream << "HTTP/1.0 200 OK\r\n"
                   << "Content-Type: text/html; charset=UTF-8\r\n"
                   << "Content-Length: " << answer.size() + 2 << "\r\n\r\n"
                   << answer << "\r\n";


        boost::asio::async_write(*socket, response, boost::bind(&Request::afterWrite, shared_from_this(), _1, _2));
    }

public:

    boost::shared_ptr<tcp::socket> socket;
    Request(HttpServer& server);
    void answer()
    {
        if (!socket) return;

        // reads request till the end
        boost::asio::async_read_until(*socket, request, "\r\n\r\n",
                                      boost::bind(&Request::afterRead, shared_from_this(), _1, _2));
    }

};


class HttpServer
{
public:

    HttpServer(unsigned int port) : acceptor(io_service, tcp::endpoint(tcp::v4(), port)) {}
    ~HttpServer() { if (sThread) sThread->join(); }

    void Run()
    {
        sThread.reset(new std::thread(boost::bind(&HttpServer::thread_main, this)));
    }

    boost::asio::io_service io_service;

private:
    tcp::acceptor acceptor;
    boost::shared_ptr<std::thread> sThread;

    void thread_main()
    {
        // adds some work to the io_service
        start_accept();
        io_service.run();
    }

    void start_accept()
    {
        boost::shared_ptr<Request> req(new Request(*this));
        acceptor.async_accept(*req->socket,
                              boost::bind(&HttpServer::handle_accept, this, req, _1));
    }

    void handle_accept(const boost::shared_ptr<Request>& req, const boost::system::error_code& error)
    {
        if (!error) { req->answer(); }
        start_accept();
    }
};

Request::Request(HttpServer& server) : server(server)
{
    socket.reset(new tcp::socket(server.io_service));
}

int main(int argc, char* argv[])
{
    HttpServer server(8080);
    server.Run();
}
