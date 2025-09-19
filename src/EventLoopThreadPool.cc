#include <memory>

#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
#include "Logger.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop), name_(nameArg), started_(false), numThreads_(0), next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's a stack variable
    // refer EventLoopThread::threadFunc
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    for (int i = 0; i < numThreads_; ++i)
    {
        char buf[32 + name_.size()];
        snprintf(buf, sizeof buf, "%s_%d", name_.c_str(), i);
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop());
    }
    if (!numThreads_ && cb)
    {
        cb(baseLoop_);
    }
}

/**
 * if working in multi-threads, baseLoop_(mainLoop) will
 * distribute Channels to subLoops in a round-robin manner.
 */
EventLoop *EventLoopThreadPool::getNextLoop()
{
    EventLoop *loop = baseLoop_;

    if (!loops_.empty())
    {
        loop = loops_[next_++];
        if (next_ >= static_cast<int>(loops_.size()))
        {
            next_ = 0;
        }
    }

    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}