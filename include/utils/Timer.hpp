#pragma once

#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>

class Timer {
public:
    Timer();
    ~Timer();
    
    // One-shot timer
    void setTimeout(std::function<void()> callback, int milliseconds);
    
    // Repeating timer
    void setInterval(std::function<void()> callback, int milliseconds);
    
    void start();
    void stop();
    void reset();
    
    bool isRunning() const { return m_running; }

private:
    void timerLoop();
    
    std::function<void()> m_callback;
    std::chrono::milliseconds m_interval;
    std::atomic<bool> m_running;
    std::atomic<bool> m_repeat;
    std::thread m_timerThread;
};

class TimerManager {
public:
    static TimerManager& instance();
    
    int createTimer(std::function<void()> callback, int milliseconds, bool repeat = false);
    void destroyTimer(int timerId);
    void startTimer(int timerId);
    void stopTimer(int timerId);
    
    void cleanup();

private:
    TimerManager() = default;
    
    struct TimerInfo {
        std::unique_ptr<Timer> timer;
        int id;
        bool active;
    };
    
    std::vector<TimerInfo> m_timers;
    std::atomic<int> m_nextId{1};
    std::mutex m_mutex;
};
