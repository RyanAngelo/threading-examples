import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.locks.Condition;
import java.util.Random;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.List;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Queue;
import java.util.LinkedList;

public class ThreadingNumbers {
    // Map to store thread random numbers
    private final ConcurrentHashMap<Long, Integer> threadNumbers = new ConcurrentHashMap<>();
    
    // Semaphore for signaling between threads (binary)
    private final Semaphore signal = new Semaphore(0);
    
    // Lock for console output
    private final ReentrantLock consoleLock = new ReentrantLock();
    
    // Condition variable for thread initialization
    private final ReentrantLock initLock = new ReentrantLock();
    private final Condition threadStarted = initLock.newCondition();

    // Thread-safe queue for the thread pool
    class ThreadSafeQueue {
        private final Queue<Runnable> tasks = new LinkedList<>();
        private final ReentrantLock lock = new ReentrantLock();
        private final Condition notEmpty = lock.newCondition();

        public void push(Runnable task) {
            lock.lock();
            try {
                tasks.offer(task);
                notEmpty.signal();
            } finally {
                lock.unlock();
            }
        }

        public Runnable pop() throws InterruptedException {
            lock.lock();
            try {
                while (tasks.isEmpty()) {
                    notEmpty.await();
                }
                return tasks.poll();
            } finally {
                lock.unlock();
            }
        }

        public boolean isEmpty() {
            lock.lock();
            try {
                return tasks.isEmpty();
            } finally {
                lock.unlock();
            }
        }
    }

    // Thread pool implementation
    class ThreadPool implements AutoCloseable {
        private final List<Thread> workers;
        private final ThreadSafeQueue taskQueue;
        private volatile boolean stop = false;
        private final AtomicInteger activeTasks = new AtomicInteger(0);
        private final ReentrantLock completionLock = new ReentrantLock();
        private final Condition completionCondition = completionLock.newCondition();

        public ThreadPool(ThreadingNumbers parent, int numThreads) {
            this.taskQueue = parent.new ThreadSafeQueue();
            this.workers = new ArrayList<>();

            for (int i = 0; i < numThreads; i++) {
                Thread worker = new Thread(() -> {
                    parent.registerThreadNumber();
                    while (true) {
                        try {
                            Runnable task = taskQueue.pop();
                            if (stop && task == null) break;
                            
                            if (task != null) {
                                activeTasks.incrementAndGet();
                                task.run();
                                activeTasks.decrementAndGet();
                                signalCompletion();
                            }
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            break;
                        }
                    }
                });
                workers.add(worker);
                worker.start();
            }
        }

        private void signalCompletion() {
            completionLock.lock();
            try {
                completionCondition.signalAll();
            } finally {
                completionLock.unlock();
            }
        }

        public void enqueue(Runnable task) {
            taskQueue.push(task);
        }

        public void waitForCompletion() throws InterruptedException {
            completionLock.lock();
            try {
                while (activeTasks.get() > 0 || !taskQueue.isEmpty()) {
                    completionCondition.await();
                }
            } finally {
                completionLock.unlock();
            }
        }

        public int getWorkerCount() {
            return workers.size();
        }

        @Override
        public void close() {
            stop = true;
            for (int i = 0; i < workers.size(); i++) {
                taskQueue.push(null);
            }
            for (Thread worker : workers) {
                try {
                    worker.join();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
        }
    }

    // Generate and register a random number for the current thread
    private void registerThreadNumber() {
        int randomNum = new Random().nextInt(100) + 1;
        threadNumbers.put(Thread.currentThread().getId(), randomNum);
        initLock.lock();
        try {
            threadStarted.signalAll();
        } finally {
            initLock.unlock();
        }
    }

    // Wait for a thread to initialize and register its number
    private void waitForThreadStart(Thread t) throws InterruptedException {
        initLock.lock();
        try {
            while (!threadNumbers.containsKey(t.getId())) {
                threadStarted.await();
            }
        } finally {
            initLock.unlock();
        }
    }

    // Print a line with thread ID and its random number
    private void printLine(String line) {
        consoleLock.lock();
        try {
            long threadId = Thread.currentThread().getId();
            Integer randomNum = threadNumbers.get(threadId);
            if (randomNum == null) {
                // Register number if thread hasn't done so yet
                registerThreadNumber();
                randomNum = threadNumbers.get(threadId);
            }
            System.out.printf("Thread %d (random number: %d): %s%n", 
                            threadId, randomNum, line);
        } finally {
            consoleLock.unlock();
        }
    }


    private void runExample() throws InterruptedException {
        // Create thread pool with 4 worker threads
        try (ThreadPool pool = new ThreadPool(this, 4)) {
            // Lines from "I'm a Little Teapot"
            List<String> lines = Arrays.asList(
                "I'm a little teapot",
                "Short and stout",
                "Here is my handle",
                "Here is my spout",
                "When I get all steamed up",
                "Hear me shout",
                "Tip me over",
                "And pour me out!"
            );

            // Demonstrate mutex usage - print first two lines
            Thread t1 = new Thread(() -> {
                registerThreadNumber();
                printLine(lines.get(0));
            });
            Thread t2 = new Thread(() -> {
                registerThreadNumber();
                printLine(lines.get(1));
            });

            t1.start();
            t2.start();
            waitForThreadStart(t1);
            waitForThreadStart(t2);
            t1.join();
            t2.join();

            // Demonstrate semaphore usage - print next two lines
            Thread producer = new Thread(() -> {
                registerThreadNumber();
                printLine(lines.get(2));
                signal.release();
            });

            Thread consumer = new Thread(() -> {
                registerThreadNumber();
                try {
                    signal.acquire();
                    printLine(lines.get(3));
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            });

            producer.start();
            consumer.start();
            waitForThreadStart(producer);
            waitForThreadStart(consumer);
            producer.join();
            consumer.join();

            // Wait for all thread pool workers to initialize
            initLock.lock();
            try {
                while (threadNumbers.size() < pool.getWorkerCount()) {
                    threadStarted.await();
                }
            } finally {
                initLock.unlock();
            }

            // Print all thread pool worker numbers
            consoleLock.lock();
            try {
                System.out.println("\nThread pool worker random numbers:");
                for (Map.Entry<Long, Integer> entry : threadNumbers.entrySet()) {
                    System.out.printf("Worker thread %d: %d%n", 
                                    entry.getKey(), entry.getValue());
                }
                System.out.println();
            } finally {
                consoleLock.unlock();
            }

            // Demonstrate thread pool - print remaining lines
            for (int i = 4; i < lines.size(); i++) {
                final String line = lines.get(i);
                pool.enqueue(() -> printLine(line));
            }

            // Wait for all thread pool tasks to complete
            pool.waitForCompletion();
        }
    }

    public static void main(String[] args) throws InterruptedException {
        ThreadingNumbers example = new ThreadingNumbers();
        example.runExample();  // Move main logic to this method
    }
}