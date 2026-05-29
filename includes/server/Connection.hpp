#pragma once
#include "IEventHandler.hpp"
#include <cstddef>
#include <ctime>
#include <vector>
#include <string>
struct ServerConfig;
class Connection;

class Connection : public IEventHandler
{
    private:
        int _fd;
        Connection(const Connection &other);
        Connection &operator=(const Connection &rhs);

    public:
        const ServerConfig   *server_cfg;
       
        // INPUT: what the client sent
        size_t   read_idx;
        std::vector<char>    in_buffer;

        // OUTPUT: what we will send back to the client
        size_t  write_idx;
        std::vector<char>    out_buffer;
        
        enum ConnectionState {
            READING_HEADER,
            READING_BODY,
            SENDING_RESPONSE,
            CGI_WRITING_BODY,
            CGI_READING_OUTPUT,
            CLOSED
        };
        ConnectionState state;
        
        // TIMEOUT MANAGEMENT
        std::time_t last_activity;
        std::time_t first_byte_time;
        bool header_complete;
        
        // KEEP ALIVE
        bool closeAfterWrite;

        // File Streaming (for large files)
        int      file_fd;
        size_t   file_bytes_sent;
        size_t   file_total_size;
        bool     is_streaming_file;
        
        size_t   header_check_len;
        size_t   chunk_raw_offset; // resumable chunked parser: offset in the raw buffer where the current chunk starts

        std::vector<char>    chunk_decoded; // body bytes decoded from chunked encoding, used for CGI input
        size_t   expected_request_total; // total expected request size (header + body) based on Content-Length or chunked encoding, used for connection timeout and to prevent DoS

        // streaming state
        int      upload_fd;
        size_t   body_bytes_expected;
        size_t   body_bytes_received;
        bool     is_streaming_upload;
    
        // METHODS

         /*
            * Constructor for creating a new connection.
            * Parameters:
            * - fd: file descriptor of the accepted client socket
            * - serverCfg: pointer to the server configuration, used for managing timeouts and other connection
        */
        Connection(int fd, const ServerConfig *serverCfg);
        ~Connection();

        /*
            * Returns the client socket fd for this connection.
        */
        int  getFd() const;

        /*
            * Updates the connection state machine (and emits a DEBUG log).
        */
        void setState(ConnectionState new_state);
        
        /*
            * IEventHandler entry point: handles events on the client socket, including reading requests, writing responses, and managing CGI processes.
        */
        virtual void handle(uint32_t event, IServerCallback &callback);
};
