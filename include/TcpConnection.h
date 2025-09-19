#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

class Channel;
class EventLoop;
class Socket;

/**
 * TcpServer
 *      => Acceptor
 *          => A new user connects, obtaining connfd through accept()
 *              => TcpConnection sets callback
 *                  => Sets to Channel
 *                      => Poller
 *                          => Channel callback
 **/
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                  const std::string &nameArg,
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == kConnected; }

    void send(const std::string &buf);
    void sendFile(int fd, off_t *offset, size_t count);

    void shutdown(); // close write

    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    {
        highWaterMarkCallback_ = cb;
        highWaterMark_ = highWaterMark;
    }
    void setCloseCallback(const CloseCallback &cb) { closeCallback_ = cb; }

    void connectEstablished(); // called when TcpServer accepts a new connection
    void connectDestroyed();   // called when TcpServer has removed me from its map
private:
    enum StateE
    {
        kDisconnected,
        kConnecting,
        kConnected,
        kDisconnecting
    };
    void setState(StateE s) { state_ = s; }

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void *data, size_t len);
    void shutdownInLoop();
    void sendFileInLoop(int fd, off_t *offset, size_t count);

private:
    EventLoop *loop_;
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    /**
     * These callbacks are also available in TcpServer, user rigister them by writing to TcpServer,
     * which then passes the registered callbacks to TcpConnection. 
     * TcpConnection, in turn, registers the callbacks into Channel.
     */
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;
    size_t highWaterMark_;

    Buffer inputBuffer_;  // receive data
    Buffer outputBuffer_; // send data
};