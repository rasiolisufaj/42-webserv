#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <sys/epoll.h>
#include "../includes/config/ConfigParser.hpp"
#include "../includes/http/Request.hpp"
#include "../includes/http/RequestContext.hpp"
#include "../includes/server/Connection.hpp"
#include "../includes/server/IServerCallback.hpp"
#include "../includes/cgi/Cgi.hpp"
#include "../includes/utils/Logger.hpp"

// Mock callback for testing Connection
class MockServerCallback : public IServerCallback
{
public:
    int newConnectionCount;
    int clientINCount;
    int clientOUTCount;
    int closeConnectionCount;
    int cgiPipeINCount;
    int cgiPipeOUTCount;

    MockServerCallback() : newConnectionCount(0), clientINCount(0), clientOUTCount(0),
                           closeConnectionCount(0), cgiPipeINCount(0), cgiPipeOUTCount(0) {}

    virtual void onNewConnection(int, int) { newConnectionCount++; }
    virtual void onClientIN(Connection *) { clientINCount++; }
    virtual void onClientOUT(Connection *) { clientOUTCount++; }
    virtual void onCloseConnection(int, Connection *) { closeConnectionCount++; }
    virtual void onCgiPipeIN(Connection *) { cgiPipeINCount++; }
    virtual void onCgiPipeOUT(Connection *) { cgiPipeOUTCount++; }
};

// ============================================================================
// TEST 1: CONFIG LAYER (Leny)
// ============================================================================
void test_config_parser()
{
    std::cout << "\n=== TEST 1: Config Parser (Leny's Layer) ===\n";

    ConfigParser parser;
    int result = parser.parseConfig("config/default.conf");
    assert(result == 0 && "Config parsing should succeed");

    const std::vector<ServerConfig> &servers = parser.getServers();
    assert(!servers.empty() && "Should have at least one server");

    const ServerConfig &srv = servers[0];
    assert(srv.port > 0 && "Server port should be valid");
    assert(!srv.locations.empty() && "Server should have at least one location");

    std::cout << "✓ Config parsed successfully\n";
    std::cout << "  - Servers: " << servers.size() << "\n";
    std::cout << "  - Server 0 port: " << srv.port << "\n";
    std::cout << "  - Locations: " << srv.locations.size() << "\n";
}

// ============================================================================
// TEST 2: HTTP PARSING LAYER (Thomas)
// ============================================================================
void test_http_parsing()
{
    std::cout << "\n=== TEST 2: HTTP Parsing (Thomas's Layer) ===\n";

    // Test GET request
    std::string raw_get =
        "GET /index.html?x=1 HTTP/1.1\r\n"
        "Host: example.com:8080\r\n"
        "User-Agent: curl/8.5.0\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    Request req1;
    req1.parse(raw_get);
    assert(req1.requestLine().method() == "GET" && "Method should be GET");
    assert(req1.requestLine().target() == "/index.html?x=1" && "Target should match");

    // Test POST request
    std::string raw_post =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, World!";

    Request req2;
    req2.parse(raw_post);
    assert(req2.requestLine().method() == "POST" && "Method should be POST");
    assert(req2.headers().getContentLength() == 13 && "Content-Length should be 13");

    // Test RequestContext
    RequestContext ctx;
    ctx.build(req2, 8080);
    assert(ctx.path() == "/upload" && "Path should be /upload");
    assert(ctx.bodySize() == 13 && "Body size should be 13");

    std::cout << "✓ HTTP parsing successful\n";
    std::cout << "  - GET request parsed\n";
    std::cout << "  - POST request with body parsed\n";
    std::cout << "  - RequestContext built correctly\n";
}

// ============================================================================
// TEST 3: CONNECTION LIFECYCLE (Rasiol's Layer - Part 1)
// ============================================================================
void test_connection_lifecycle()
{
    std::cout << "\n=== TEST 3: Connection Lifecycle (Rasiol's Layer) ===\n";

    ConfigParser parser;
    parser.parseConfig("config/default.conf");
    const ServerConfig &srv = parser.getServers()[0];

    // Test 1: Create and destroy connection
    {
        Connection *conn = new Connection(5, &srv);
        assert(conn->getFd() == 5 && "FD should be 5");
        assert(conn->state == Connection::READING_HEADER && "Initial state should be READING_HEADER");
        assert(conn->in_buffer.size() == 0 && "Input buffer should be empty");
        assert(conn->out_buffer.size() == 0 && "Output buffer should be empty");
        assert(conn->upload_fd == -1 && "Upload fd should be uninitialized (-1)");
        assert(conn->expected_request_total == 0 && "Expected total should be 0");

        std::cout << "✓ Connection created successfully\n";

        delete conn;
        std::cout << "✓ Connection destroyed (resources cleaned)\n";
    }

    // Test 2: Multiple connections
    {
        Connection *conn1 = new Connection(10, &srv);
        Connection *conn2 = new Connection(11, &srv);
        Connection *conn3 = new Connection(12, &srv);

        assert(conn1->getFd() == 10 && "Conn1 fd should be 10");
        assert(conn2->getFd() == 11 && "Conn2 fd should be 11");
        assert(conn3->getFd() == 12 && "Conn3 fd should be 12");

        delete conn1;
        delete conn2;
        delete conn3;
        std::cout << "✓ Multiple connections handled correctly\n";
    }
}

// ============================================================================
// TEST 4: STATE TRANSITIONS (Rasiol's Layer - Part 2)
// ============================================================================
void test_connection_state_transitions()
{
    std::cout << "\n=== TEST 4: Connection State Transitions ===\n";

    ConfigParser parser;
    parser.parseConfig("config/default.conf");
    const ServerConfig &srv = parser.getServers()[0];

    Connection conn(7, &srv);
    assert(conn.state == Connection::READING_HEADER && "Initial state: READING_HEADER");

    conn.setState(Connection::READING_BODY);
    assert(conn.state == Connection::READING_BODY && "State should be READING_BODY");

    conn.setState(Connection::SENDING_RESPONSE);
    assert(conn.state == Connection::SENDING_RESPONSE && "State should be SENDING_RESPONSE");

    conn.setState(Connection::CLOSED);
    assert(conn.state == Connection::CLOSED && "State should be CLOSED");

    std::cout << "✓ State transitions work correctly\n";
}

// ============================================================================
// TEST 5: EVENT HANDLING (Rasiol's Layer - Part 3)
// ============================================================================
void test_connection_event_handling()
{
    std::cout << "\n=== TEST 5: Connection Event Handling (Mock) ===\n";

    ConfigParser parser;
    parser.parseConfig("config/default.conf");
    const ServerConfig &srv = parser.getServers()[0];

    Connection conn(8, &srv);
    MockServerCallback callback;

    // Test EPOLLIN event
    conn.handle(EPOLLIN, callback);
    assert(callback.clientINCount == 1 && "EPOLLIN should trigger onClientIN");
    assert(conn.state != Connection::CLOSED && "Connection should not be closed yet");

    // Test EPOLLOUT event
    conn.handle(EPOLLOUT, callback);
    assert(callback.clientOUTCount == 1 && "EPOLLOUT should trigger onClientOUT");

    // Test EPOLLERR event
    conn.handle(EPOLLERR, callback);
    assert(callback.closeConnectionCount == 1 && "EPOLLERR should trigger onCloseConnection");

    std::cout << "✓ Event handling dispatches correctly\n";
    std::cout << "  - EPOLLIN → onClientIN\n";
    std::cout << "  - EPOLLOUT → onClientOUT\n";
    std::cout << "  - EPOLLERR → onCloseConnection\n";
}

// ============================================================================
// TEST 6: CGI LAYER (Leny)
// ============================================================================
void test_cgi_setup()
{
    std::cout << "\n=== TEST 6: CGI Layer (Leny's Layer) ===\n";

    // Parse a request that would trigger CGI
    std::string raw_cgi_req =
        "GET /cgi-bin/test.py?param=value HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "\r\n";

    Request req;
    req.parse(raw_cgi_req);

    RequestContext ctx;
    ctx.build(req, 8080);

    assert(ctx.path() == "/cgi-bin/test.py" && "CGI path should be parsed");
    assert(ctx.query() == "param=value" && "Query string should be captured");

    std::cout << "✓ CGI request parsing works\n";
    std::cout << "  - Path: " << ctx.path() << "\n";
    std::cout << "  - Query: " << ctx.query() << "\n";
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================
int main()
{
    Logger::setLogLevel(Logger::WARNING);  // Suppress debug logs during tests

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "       WEBSERV UNIT TEST SUITE\n";
    std::cout << std::string(60, '=') << "\n";

    try
    {
        test_config_parser();
        test_http_parsing();
        test_connection_lifecycle();
        test_connection_state_transitions();
        test_connection_event_handling();
        test_cgi_setup();

        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "✓ ALL TESTS PASSED\n";
        std::cout << std::string(60, '=') << "\n\n";

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << "\n\n";
        return 1;
    }
}
