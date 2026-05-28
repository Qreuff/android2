#include "server.h"

ZMQServer::ZMQServer() : context_(1), socket_(context_, zmq::socket_type::rep) {
    try {
        socket_.bind("tcp://*:5566");
        socket_.set(zmq::sockopt::rcvtimeo, 1000);
        std::cout << "Socket bound successfully on port 5566" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Bind Error: " << e.what() << std::endl;
        throw;
    }
}

ZMQServer::~ZMQServer() {
    stop();
}

void ZMQServer::start() {
    if (!running_) return;
    
    server_thread_ = std::thread(&ZMQServer::serverLoop, this);
    std::cout << "Server thread started" << std::endl;
}

void ZMQServer::stop() {
    running_ = false;
    if (server_thread_.joinable()) {
        server_thread_.join();
        std::cout << "Server thread stopped" << std::endl;
    }
}

bool ZMQServer::isRunning() const {
    return running_;
}

void ZMQServer::serverLoop() {
    std::cout << "Waiting for connections... (Ctrl+C to stop)" << std::endl;
    
    while (running_) {
        zmq::message_t request;
        auto res = socket_.recv(request, zmq::recv_flags::none);
        
        if (res.has_value()) {
            std::string msg_str(static_cast<char*>(request.data()), request.size());
            processMessage(msg_str);
        }
    }
}

void ZMQServer::processMessage(const std::string& msg_str) {
    try {
        auto j = json::parse(msg_str);
        
        LocationData::instance().updateFromJson(j);
        
        std::ofstream file(log_file_path_, std::ios::app);
        if (file.is_open()) {
            file << j.dump() << std::endl;
            file.close();
        }
        
        socket_.send(zmq::str_buffer("OK"), zmq::send_flags::none);

        auto& loc = LocationData::instance();
        std::cout << "Received data - Lat: " << loc.getLat() 
                  << ", Lon: " << loc.getLon() 
                  << ", RSRP: " << loc.getRSRP() 
                  << ", Network: " << loc.getNetworkType() 
                  << ", Cells: " << loc.getCurrentCells().size() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing message: " << e.what() << std::endl;
        socket_.send(zmq::str_buffer("Error"), zmq::send_flags::none);
    }
}