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

// Mutex for synchronizing console output
std::mutex console_mutex;

// Mutex and map for thread initialization numbers
std::mutex thread_numbers_mutex;
std::unordered_map<std::thread::id, int> thread_numbers;
std::condition_variable thread_started_cv;

// Binary semaphore for signaling between threads
std::binary_semaphore signal_semaphore{0};

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
        std::lock_guard<std::mutex> lock(thread_numbers_mutex);
        thread_numbers[thread_id] = random_num;
    }
    thread_started_cv.notify_all();
}

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
                register_thread_number();  // Register random number when thread starts
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

    // Get the number of worker threads
    size_t worker_count() const {
        return workers.size();
    }
};

// Function to print a line with thread ID and its random number
void print_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(console_mutex);
    auto thread_id = std::this_thread::get_id();
    int random_num;
    {
        std::lock_guard<std::mutex> num_lock(thread_numbers_mutex);
        random_num = thread_numbers[thread_id];
    }
    std::cout << "Thread " << thread_id << " (random number: " << random_num << "): " << line << std::endl;
}

// Function to wait for thread initialization
void wait_for_thread_start(const std::thread& t) {
    std::unique_lock<std::mutex> lock(thread_numbers_mutex);
    thread_started_cv.wait(lock, [&t] {
        return thread_numbers.find(t.get_id()) != thread_numbers.end();
    });
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
    std::thread t1([&]{ 
        register_thread_number();
        print_line(lines[0]); 
    });
    std::thread t2([&]{ 
        register_thread_number();
        print_line(lines[1]); 
    });
    
    // Wait for threads to register their numbers and complete
    wait_for_thread_start(t1);
    wait_for_thread_start(t2);
    t1.join();
    t2.join();

    // Demonstrate semaphore usage - print next two lines
    std::thread producer([&]{
        register_thread_number();
        print_line(lines[2]);
        signal_semaphore.release(); // Signal consumer
    });

    std::thread consumer([&]{
        register_thread_number();
        signal_semaphore.acquire(); // Wait for producer
        print_line(lines[3]);
    });

    // Wait for threads to register their numbers and complete
    wait_for_thread_start(producer);
    wait_for_thread_start(consumer);
    producer.join();
    consumer.join();

    // Wait for all thread pool workers to initialize
    {
        std::unique_lock<std::mutex> lock(thread_numbers_mutex);
        thread_started_cv.wait(lock, [&pool] {
            return thread_numbers.size() >= pool.worker_count();
        });
    }

    // Print all thread pool worker numbers
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "\nThread pool worker random numbers:\n";
        for (const auto& [thread_id, number] : thread_numbers) {
            std::cout << "Worker thread " << thread_id << ": " << number << std::endl;
        }
        std::cout << std::endl;
    }

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