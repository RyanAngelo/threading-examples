#include <iostream>
#include <thread>
#include <mutex>
#include <semaphore>
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>
#include <atomic>

// Mutex for synchronizing console output
std::mutex console_mutex;

// Binary semaphore for signaling between threads
std::binary_semaphore signal_semaphore{0};

// Thread-safe queue class for the thread pool
class ThreadSafeQueue {
private:
    std::queue<std::function<void()>> tasks;
    mutable std::mutex mutex;
    std::condition_variable condition;

public:
    void push(std::function<void()> task) {
        std::unique_lock<std::mutex> lock(mutex);
        tasks.push(task);
        condition.notify_one();
    }

    std::function<void()> pop() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return !tasks.empty(); });
        auto task = tasks.front();
        tasks.pop();
        return task;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return tasks.empty();
    }
};

// Thread pool implementation
class ThreadPool {
private:
    std::vector<std::thread> workers;
    ThreadSafeQueue task_queue;
    bool stop = false;
    std::atomic<size_t> active_tasks{0};
    std::condition_variable completion_cv;
    std::mutex completion_mutex;

public:
    ThreadPool(size_t num_threads) {
        for(size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    auto task = task_queue.pop();
                    if(stop && task == nullptr) break;
                    
                    if (task) {
                        active_tasks++;
                        task();
                        active_tasks--;
                        completion_cv.notify_all();
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        stop = true;
        // Push empty tasks to unblock threads
        for(size_t i = 0; i < workers.size(); ++i) {
            task_queue.push(nullptr);
        }
        for(auto& worker : workers) {
            worker.join();
        }
    }

    void enqueue(std::function<void()> task) {
        task_queue.push(task);
    }

    // Wait for all tasks to complete
    void wait_for_completion() {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_cv.wait(lock, [this] {
            return active_tasks == 0 && task_queue.empty();
        });
    }
};

// Function to print a line with thread ID
void print_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "Thread " << std::this_thread::get_id() << ": " << line << std::endl;
}

int main() {
    // Create thread pool with 4 worker threads
    ThreadPool pool(4);

    // Lines from "I'm a Little Teapot"
    std::vector<std::string> lines = {
        "I'm a little teapot",
        "Short and stout",
        "Here is my handle",
        "Here is my spout",
        "When I get all steamed up",
        "Hear me shout",
        "Tip me over",
        "And pour me out!"
    };

    // Demonstrate mutex usage - print first two lines
    std::thread t1([&]{ print_line(lines[0]); });
    std::thread t2([&]{ print_line(lines[1]); });
    
    t1.join();
    t2.join();

    // Demonstrate semaphore usage - print next two lines
    std::thread producer([&]{
        print_line(lines[2]);
        signal_semaphore.release(); // Signal consumer
    });

    std::thread consumer([&]{
        signal_semaphore.acquire(); // Wait for producer
        print_line(lines[3]);
    });

    producer.join();
    consumer.join();

    // Demonstrate thread pool - print remaining lines
    for(size_t i = 4; i < lines.size(); ++i) {
        pool.enqueue([line = lines[i]]{
            print_line(line);
        });
    }

    // Wait for all thread pool tasks to complete
    pool.wait_for_completion();

    return 0;
}

