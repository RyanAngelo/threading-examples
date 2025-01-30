#include <iostream>
#include <thread>
#include <mutex>
#include <semaphore>
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <random>
#include <unordered_map>
#include <thread>

namespace threading {

namespace detail {  // Hide implementation details
    std::mutex console_mutex;
    std::mutex thread_numbers_mutex;
    std::unordered_map<std::thread::id, int> thread_numbers;
    std::condition_variable thread_started_cv;
    std::binary_semaphore signal_semaphore{0};
}

/**
 * Internal implementation details.
 * Not intended for direct use by clients.
 */
namespace detail {
    // ...
}

// Function to generate a random number
int generate_random_number() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(1, 100);
    return dis(gen);
}

// Function to register thread's random number
void register_thread_number() {
    auto thread_id = std::this_thread::get_id();
    int random_num = generate_random_number();
    {
        std::lock_guard<std::mutex> lock(detail::thread_numbers_mutex);
        detail::thread_numbers[thread_id] = random_num;
    }
    detail::thread_started_cv.notify_all();
}

/**
 * Thread-safe queue for managing tasks in the thread pool.
 * Thread safety: All public methods are thread-safe.
 */
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
        if (!condition.wait_for(lock, std::chrono::seconds(30), 
            [this] { return !tasks.empty(); })) {
            throw std::runtime_error("Timeout waiting for task");
        }
        auto task = std::move(tasks.front());
        tasks.pop();
        return task;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return tasks.empty();
    }
};

/**
 * Thread pool for executing tasks in parallel.
 * Thread safety: All public methods are thread-safe.
 * Exception safety: Strong guarantee for enqueue operations.
 * Resource management: All threads are properly joined in destructor.
 */
class ThreadPool {
private:
    static constexpr auto SHUTDOWN_TASK = nullptr;
    std::vector<std::thread> workers;
    ThreadSafeQueue task_queue;
    bool stop;
    std::atomic<size_t> active_tasks;
    std::condition_variable completion_cv;
    std::mutex completion_mutex;

public:
    ThreadPool(size_t num_threads)
        : workers()
        , task_queue()
        , stop(false)
        , active_tasks(0)
        , completion_mutex()
        , completion_cv()
    {
        for(size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                register_thread_number();
                while(true) {
                    try {
                        auto task = task_queue.pop();
                        if(stop && task == SHUTDOWN_TASK) break;
                        
                        if (task) {
                            active_tasks++;
                            try {
                                task();
                            } catch (...) {
                                // Log error but continue processing
                                std::cerr << "Task execution failed\n";
                            }
                            active_tasks--;
                            completion_cv.notify_all();
                        }
                    } catch (const std::runtime_error& e) {
                        if (!stop) std::cerr << "Queue error: " << e.what() << "\n";
                        break;
                    }
                }
            });
        }
    }

    ~ThreadPool() noexcept {
        stop = true;
        // Push shutdown tasks to unblock threads
        for(size_t i = 0; i < workers.size(); ++i) {
            task_queue.push(SHUTDOWN_TASK);
        }
        for(auto& worker : workers) {
            worker.join();
        }
    }

    void enqueue(std::function<void()> task) {
        if (!task) {
            throw std::invalid_argument("Task cannot be null");
        }
        if (stop) {
            throw std::runtime_error("ThreadPool is shutting down");
        }
        task_queue.push(std::move(task));
    }

    // Wait for all tasks to complete
    void wait_for_completion() {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_cv.wait(lock, [this] {
            return active_tasks == 0 && task_queue.empty();
        });
    }

    // Get the number of worker threads
    [[nodiscard]] size_t worker_count() const noexcept {
        return workers.size();
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return task_queue.empty();
    }
};

// Function to print a line with thread ID and its random number
void print_line(std::string_view line) {
    std::lock_guard<std::mutex> lock(detail::console_mutex);
    auto thread_id = std::this_thread::get_id();
    int random_num;
    {
        std::lock_guard<std::mutex> num_lock(detail::thread_numbers_mutex);
        random_num = detail::thread_numbers[thread_id];
    }
    std::cout << "Thread " << thread_id << " (random number: " << random_num << "): " << line << std::endl;
}

// Function to wait for thread initialization
void wait_for_thread_start(const std::thread& t) {
    std::unique_lock<std::mutex> lock(detail::thread_numbers_mutex);
    detail::thread_started_cv.wait(lock, [&t] {
        return detail::thread_numbers.find(t.get_id()) != detail::thread_numbers.end();
    });
}

class ThreadGuard {
    std::thread& t;
public:
    explicit ThreadGuard(std::thread& t_) : t(t_) {}
    ~ThreadGuard() { if (t.joinable()) t.join(); }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;
};

} // namespace threading

// Move main outside the namespace
int main() {
    // Create thread pool with 4 worker threads
    threading::ThreadPool pool(4);

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
    std::thread t1([&]{ 
        threading::register_thread_number();
        threading::print_line(lines[0]); 
    });
    threading::ThreadGuard g1(t1);  // Use RAII for thread joining
    
    std::thread t2([&]{ 
        threading::register_thread_number();
        threading::print_line(lines[1]); 
    });
    threading::ThreadGuard g2(t2);
    
    // Demonstrate semaphore usage - print next two lines
    std::thread producer([&]{
        threading::register_thread_number();
        threading::print_line(lines[2]);
        threading::detail::signal_semaphore.release(); // Signal consumer
    });

    std::thread consumer([&]{
        threading::register_thread_number();
        threading::detail::signal_semaphore.acquire(); // Wait for producer
        threading::print_line(lines[3]);
    });

    // Wait for threads to register their numbers and complete
    threading::wait_for_thread_start(producer);
    threading::wait_for_thread_start(consumer);
    producer.join();
    consumer.join();

    // Wait for all thread pool workers to initialize
    {
        std::unique_lock<std::mutex> lock(threading::detail::thread_numbers_mutex);
            threading::detail::thread_started_cv.wait(lock, [&pool] {
            return threading::detail::thread_numbers.size() >= pool.worker_count();
        });
    }

    // Print all thread pool worker numbers
    {
        std::lock_guard<std::mutex> lock(threading::detail::console_mutex);
        std::cout << "\nThread pool worker random numbers:\n";
        for (const auto& [thread_id, number] : threading::detail::thread_numbers) {
            std::cout << "Worker thread " << thread_id << ": " << number << std::endl;
        }
        std::cout << std::endl;
    }

    // Demonstrate thread pool - print remaining lines
    for(size_t i = 4; i < lines.size(); ++i) {
        pool.enqueue([line = std::string(lines[i])] {
            threading::print_line(line);
        });
    }

    // Wait for all thread pool tasks to complete
    pool.wait_for_completion();

    return 0;
} 