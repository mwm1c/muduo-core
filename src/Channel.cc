#include <sys/epoll.h>

#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false)
{
}

Channel::~Channel()
{
}

void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

void Channel::update()
{
    // TODO
    // loop_->updateChannel(this);
}

void Channel::remove()
{
    // TODO
    // loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
            handleEventWithGuard(receiveTime);
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents: %d\n", revents_);
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN) && closeCallback_)
    {
        closeCallback_();
    }
    if (revents_ & EPOLLERR && errorCallback_)
    {
        errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI) && readCallback_)
    {
        readCallback_(receiveTime);
    }
    if (revents_ & EPOLLOUT && writeCallback_)
    {
        writeCallback_();
    }
}