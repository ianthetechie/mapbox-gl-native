#include <mbgl/util/default_thread_pool.hpp>
#include <mbgl/actor/mailbox.hpp>
#include <mbgl/actor/message.hpp>
#include <mbgl/util/platform.hpp>
#include <mbgl/util/string.hpp>

namespace {

using namespace mbgl;

class ThreadBasedTimer : public Scheduler::Scheduled {
public:
    ThreadBasedTimer(Duration timeout, std::weak_ptr<Mailbox> weakMailbox, std::unique_ptr<Message> message)
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
    }
    
    void cancel() override {
         //TODO
    }
    
    bool isFinished() override {
        return finished;
    }
    
private:
    std::thread thread;
    std::atomic<bool> finished { false };
};

} // namespace

namespace mbgl {

ThreadPool::ThreadPool(std::size_t count) {
    threads.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        threads.emplace_back([this, i]() {
            platform::setCurrentThreadName(std::string{ "Worker " } + util::toString(i + 1));

            while (true) {
                std::unique_lock<std::mutex> lock(mutex);

                cv.wait(lock, [this] {
                    return !queue.empty() || terminate;
                });

                if (terminate) {
                    return;
                }

                auto mailbox = queue.front();
                queue.pop();
                lock.unlock();

                Mailbox::maybeReceive(mailbox);
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        terminate = true;
    }

    cv.notify_all();

    for (auto& thread : threads) {
        thread.join();
    }
}

void ThreadPool::schedule(std::weak_ptr<Mailbox> mailbox) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(mailbox);
    }

    cv.notify_one();
}
    
    
std::unique_ptr<Scheduler::Scheduled> ThreadPool::schedule(Duration timeout, std::weak_ptr<Mailbox> mailbox, std::unique_ptr<Message> message) {
    return std::make_unique<ThreadBasedTimer>(timeout, mailbox, std::move(message));
}

} // namespace mbgl
