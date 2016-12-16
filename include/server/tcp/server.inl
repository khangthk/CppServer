/*!
    \file server.inl
    \brief TCP server inline implementation
    \author Ivan Shynkarenka
    \date 14.12.2016
    \copyright MIT License
*/

namespace CppServer {

template <class TServer, class TSession>
inline TCPServer<TServer, TSession>::TCPServer(InternetProtocol protocol, uint16_t port)
    : _service(),
      _acceptor(_service),
      _socket(_service),
      _started(false)
{
    // Create TCP endpoint
    asio::ip::tcp::endpoint endpoint;
    switch (protocol)
    {
        case InternetProtocol::IPv4:
            endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
            break;
        case InternetProtocol::IPv6:
            endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v6(), port);
            break;
    }

    // Create TCP acceptor
    _acceptor = asio::ip::tcp::acceptor(_service, endpoint);
}

template <class TServer, class TSession>
inline TCPServer<TServer, TSession>::TCPServer(const std::string& address, uint16_t port)
    : _service(),
      _acceptor(_service),
      _socket(_service),
      _started(false)
{
    // Create TCP endpoint
    asio::ip::tcp::endpoint endpoint = asio::ip::tcp::endpoint(asio::ip::address::from_string(address), port);

    // Create TCP acceptor
    _acceptor = asio::ip::tcp::acceptor(_service, endpoint);
}

template <class TServer, class TSession>
inline void TCPServer<TServer, TSession>::Start()
{
    if (IsStarted())
        return;

    // Call server starting handler
    onStarting();

    // Start server thread
    _thread = std::thread([this]() { ServerLoop(); });

    // Update started flag
    _started = true;
}

template <class TServer, class TSession>
inline void TCPServer<TServer, TSession>::Stop()
{
    if (!IsStarted())
        return;

    // Call server stopping handler
    onStopping();

    // Update started flag
    _started = false;

    // Disconnect all sessions
    DisconnectAll();

    // Stop Asio service
    _service.stop();

    // Wait for server thread
    _thread.join();
}

template <class TServer, class TSession>
inline void TCPServer<TServer, TSession>::ServerAccept()
{
    if (!IsStarted())
        return;

    _acceptor.async_accept(_socket, [this](std::error_code ec)
    {
        if (!ec)
            RegisterSession();
        else
            onError(ec.value(), ec.category().name(), ec.message());

        // Perform the next server accept
        ServerAccept();
    });
}

template <class TServer, class TSession>
inline void TCPServer<TServer, TSession>::ServerLoop()
{
    // Call initialize thread handler
    onThreadInitialize();

    try
    {
        // Call server started handler
        onStarted();

        // Perform the first server accept
        ServerAccept();

        // Run Asio service in a loop with skipping errors
        while (_started)
        {
            asio::error_code ec;
            _service.run(ec);
            if (ec)
                onError(ec.value(), ec.category().name(), ec.message());
        }

        // Call server stopped handler
        onStopped();
    }
    catch (...)
    {
        fatality("TCP server thread terminated!");
    }

    // Call cleanup thread handler
    onThreadCleanup();
}

template <class TServer, class TSession>
inline std::shared_ptr<TSession> TCPServer<TServer, TSession>::RegisterSession()
{
    auto session = std::make_shared<TSession>(*dynamic_cast<TServer*>(this), CppCommon::UUID::Generate(), std::move(_socket));
    {
        std::lock_guard<std::mutex> locker(_sessions_lock);
        _sessions.emplace(session->id(), session);
    }

    // Call a new session connected handler
    onConnected(session);

    return session;
}

template <class TServer, class TSession>
inline void TCPServer<TServer, TSession>::UnregisterSession(const CppCommon::UUID& id)
{
    std::shared_ptr<TSession> session;
    {
        std::lock_guard<std::mutex> locker(_sessions_lock);

        // Try to find session to unregister
        auto it = _sessions.find(id);
        if (it != _sessions.end())
        {
            // Cache session
            session = it->second;

            // Erase the session
            _sessions.erase(it);
        }
    }

    // Call the session disconnected handler
    if (session)
        onDisconnected(session);
}

template <class TServer, class TSession>
inline void TCPServer<TServer, TSession>::DisconnectAll()
{
    // Cache all sessions to disconnect
    std::map<CppCommon::UUID, std::shared_ptr<TSession>> sessions;
    {
        std::lock_guard<std::mutex> locker(_sessions_lock);
        sessions = _sessions;
    }

    // Disconnect all sessions from the cache
    for (auto& session : sessions)
        session.second->Disconnect();
}

} // namespace CppServer
