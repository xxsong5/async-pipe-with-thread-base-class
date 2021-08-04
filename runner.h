#pragma once
#include <xconfig.h>
#include <stdio.h>
#include <utility>
#include <mutex>
#include <thread>
#include <memory>

class ThreadRunner 
{
public:
    inline void Run(int id = 0);
    inline void Wait();
    inline void Stop();
    inline bool isRunning();

protected:
    virtual void RunInThread(int id) {
        printf("demo thread[%d]: was called\n", id);
    }

public:
    ThreadRunner(std::string const& identifier):m_id(0), m_ID(identifier), isrunning(false){}
    ThreadRunner(ThreadRunner const&) = delete;
    ThreadRunner const& operator = (ThreadRunner const&) = delete;
    ThreadRunner(ThreadRunner && other) {
        *this = std::move(other);
    }
    ThreadRunner const& operator = (ThreadRunner &&other)
    {
        if (this != &other) {
            this->m = std::move(other.m);
            this->isrunning = other.isrunning;
            this->m_id = other.m_id;
            this->m_ID = std::move(other.m_ID);
            other.isrunning = false;
            other.m_id = -1;
        }
        return *this;
    }

    std::string GetThreadIdentifier(){return this->m_ID;}

    virtual ~ThreadRunner()
    {
        if (isrunning) {
            this->Stop();
            this->Wait();
        }
    }

private:
    void RunImpl(int id) 
    {
        this->RunInThread(id);
        isrunning = false;
    }

private:
    int         m_id;
    std::string m_ID;
    std::thread m;
    volatile bool isrunning;
};

inline void ThreadRunner::Run(int id)
{
    if (!isrunning) {
        m_id = id;
        isrunning = true;
        m = std::thread(&ThreadRunner::RunImpl, this, id);
    }
}

inline void ThreadRunner::Wait()
{
    if (m.joinable()) {
        m.join();
    }
}

inline bool ThreadRunner::isRunning()
{
    return isrunning;
}

inline void ThreadRunner::Stop()
{
    isrunning = false;
}