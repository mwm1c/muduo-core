#include <functional>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h>  // for open
#include <unistd.h> // for close

#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const std::string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop)),
      name_(nameArg),
      state_(kConnecting),
      reading_(true),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64 * 1024 * 1024)
{
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

/**
 * Sending data to the peer, is the main interface for the application
 * layer to call for sending TCP data.
 */
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        /**
         * check if the currrent thread is the EventLoop thread
         * to which this connection belongs
         */
        if (loop_->isInLoopThread())
        {
            // if true, directly call sendInLoop to send data syncronously
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            /**
             * else, it indicates a "cross-thread" call, and dispatch the sending task
             * to the EventLoop thread by runInLoop, ensuring thread safely.
             */
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
        }
    }
}

void TcpConnection::sendFile(int fd, off_t *offset, size_t count)
{
}

// shutdown the write side of the connection
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();
    // new connection has established, call connection callback
    connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        // remove all event interests for the channel from Poller
        channel_->disableAll();
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); // remove channel from Poller
}

/**
 * When data arrives, read the data from socket fd into input buffer,
 * and trigger a message callback or close the connection based on the read result.
 */
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

/**
 * When the socket is writable, write the data that to be sent from outputBuffer_
 * to the kernel socket buffer, and perform subsequent processing based on the write result.
 */
void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing", channel_->fd());
    }
}

/**
 * channel_->handleEvent (events & EPOLLHUP)
 *  => TcpConnection::handleClose
 */
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);
    closeCallback_(connPtr);
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d\n", name_.c_str(), err);
}

void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing");
    }

    /**
     * if channel_ isn't writing data and the output buffer has no data to send,
     * try calling write() to write directly to socket_
     */
    if (!(channel_->isWriting() || outputBuffer_.readableBytes()))
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (!remaining && writeCompleteCallback_)
            {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
            /**
             * EWOULDBLOCK:
             * A normal phenomenon, returned when a non-blocking
             *      socket has no data to write; retry later.
             * EPIPE:
             * ECONNRESET:
             * Indicate a connection issue, requiring special handling
             *      such as closing the connection or stopping writes.
             */
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }
    /**
     * This indicates that the current write did not send all the data. The remaining data needs to be saved in the buffer.
     * Then register the EPOLLOUT event for the channel. When the Poller finds that there is space in the TCP send buffer,
     * it will notify the corresponding sock->channel and call the writeCallback_ callback method registered by the channel.
     * The channel's writeCallback_ is actually the handleWrite callback set by TcpConnection,
     * which sends all the contents of the send buffer outputBuffer_.
     */
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_ &&
            oldLen < highWaterMark_ &&
            highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        outputBuffer_.append((char *)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting();
        }
    }
}

void TcpConnection::shutdownInLoop()
{
    if (! channel_->isWriting())
    {
        // the data of outputBuffer_ has been sent, shutdown the write side of the connection
        socket_->shutdownWrite();
    }
}

void TcpConnection::sendFileInLoop(int fd, off_t *offset, size_t count)
{
}