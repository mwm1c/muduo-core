#pragma once 

/**
 * after noncopyable is inherited, the derived class object 
 * can be constructed and destructed normally, 
 * but the derived class object cannot be copy-constructed and assigned.
 **/
class noncopyable
{
public:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator=(const noncopyable &) = delete;
    // void operator=(const noncopyable &) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};