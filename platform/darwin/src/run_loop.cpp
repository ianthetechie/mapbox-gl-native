#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/async_task.hpp>
#include <mbgl/util/timer.hpp>
#include <mbgl/util/platform.hpp>
#include <mbgl/actor/mailbox.hpp>
#include <mbgl/actor/message.hpp>
#include <mbgl/actor/scheduler.hpp>

#include <CoreFoundation/CoreFoundation.h>

namespace  {

using namespace mbgl;
    
class ThreadBasedTimer : public Scheduler::Scheduled {
public:
    ThreadBasedTimer(mbgl::Duration timeout, std::weak_ptr<Mailbox> weakMailbox, std::unique_ptr<Message> message)
    : thread ([&, timeout{std::move(timeout)}, weakMailbox{std::move(weakMailbox)}, message{std::move(message)}]() mutable {
        platform::setCurrentThreadName("ThreadBasedTimer");
        std::this_thread::sleep_for(timeout);
        
        if (auto mailbox = weakMailbox.lock()) {
            mailbox->push(std::move(message));
        }
        
        finished.store(true);
    }){
        
    }
    
    ~ThreadBasedTimer() override {
        thread.join();
    };
    
    void cancel() override {
        //TODO thread.noti
    }
    
    bool isFinished() override {
        return finished;
    }
    
private:
    std::thread thread;
    std::atomic<bool> finished { false };
};
    
//class TimerBasedScheduledTask: public Scheduler::Scheduled {
//public:
//    TimerBasedScheduledTask(mbgl::Duration timeout, std::weak_ptr<Mailbox> weakMailbox, std::unique_ptr<Message> message) {
//        timer.start(timeout, mbgl::Duration::zero(), [&, weakMailbox{std::move(weakMailbox)}, message{std::move(message)}]() mutable {
//            if (auto mailbox = weakMailbox.lock()) {
//                mailbox->push(std::forward<std::unique_ptr<Message>>(message));
//            }
//            finished = true;
//        });
//    }
//  
//    ~TimerBasedScheduledTask() = default;
//    
//    void cancel() override {
//        timer.stop();
//    }
//    
//    bool isFinished() override {
//        return finished;
//    }
//    
//private:
//    util::Timer timer;
//    bool finished = false;
//};

} // namespace

namespace mbgl {
namespace util {

class RunLoop::Impl {
public:
    std::unique_ptr<AsyncTask> async;
};

RunLoop* RunLoop::Get() {
    assert(static_cast<RunLoop*>(Scheduler::GetCurrent()));
    return static_cast<RunLoop*>(Scheduler::GetCurrent());
}

RunLoop::RunLoop(Type)
  : impl(std::make_unique<Impl>()) {
    assert(!Scheduler::GetCurrent());
    Scheduler::SetCurrent(this);
    impl->async = std::make_unique<AsyncTask>(std::bind(&RunLoop::process, this));
}

RunLoop::~RunLoop() {
    assert(Scheduler::GetCurrent());
    Scheduler::SetCurrent(nullptr);
}

void RunLoop::push(std::shared_ptr<WorkTask> task) {
    withMutex([&] { queue.push(std::move(task)); });
    impl->async->send();
}

void RunLoop::run() {
    CFRunLoopRun();
}

void RunLoop::runOnce() {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
}

void RunLoop::stop() {
    invoke([&] { CFRunLoopStop(CFRunLoopGetCurrent()); });
}
    
std::unique_ptr<Scheduler::Scheduled> RunLoop::schedule(Duration timeout, std::weak_ptr<Mailbox> mailbox, std::unique_ptr<Message> message) {
//    return std::make_unique<TimerBasedScheduledTask>(timeout, std::move(mailbox), std::move(message));
    return std::make_unique<ThreadBasedTimer>(timeout, std::move(mailbox), std::move(message));
}

} // namespace util
} // namespace mbgl
