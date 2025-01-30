import java.util.Vector;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

public class ThreadingExample {
    private final Object consoleLock = new Object();
    private final Semaphore signalSemaphore = new Semaphore(0);
    
    // Thread-safe queue implementation using BlockingQueue
    class ThreadSafeQueue {
        private final BlockingQueue<Runnable> tasks;
        
        public ThreadSafeQueue() {
            this.tasks = new LinkedBlockingQueue<>();
        }
        
        public void push(Runnable task) {
            if (task == null) {
                throw new NullPointerException("Task cannot be null");
            }
            tasks.offer(task);
        }
        
        public Runnable pop() throws InterruptedException {
            return tasks.take();
        }
        
        public boolean isEmpty() {
            return tasks.isEmpty();
        }
    }
    
    // Thread pool implementation
    class ThreadPool implements AutoCloseable {
        private static final AtomicInteger threadCounter = new AtomicInteger(0);
        
        // Add a sentinel task for shutdown
        private static final Runnable SHUTDOWN_TASK = () -> {};
        
        private final Vector<Thread> workers;
        private final ThreadSafeQueue taskQueue;
        private volatile boolean stop = false;
        private final AtomicInteger activeTasks = new AtomicInteger(0);
        private final Object completionLock = new Object();
        
        public ThreadPool(ThreadingExample parent, int numThreads) {
            if (numThreads <= 0) {
                throw new IllegalArgumentException("Number of threads must be positive");
            }
            this.workers = new Vector<>();
            this.taskQueue = new ThreadSafeQueue();
            
            for (int i = 0; i < numThreads; i++) {
                Thread worker = new Thread(() -> {
                    while (true) {
                        try {
                            Runnable task = taskQueue.pop();
                            if (stop && task == SHUTDOWN_TASK) break;
                            
                            if (task != null && task != SHUTDOWN_TASK) {
                                activeTasks.incrementAndGet();
                                try {
                                    task.run();
                                } finally {
                                    activeTasks.decrementAndGet();
                                    synchronized (completionLock) {
                                        completionLock.notifyAll();
                                    }
                                }
                            }
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            break;
                        }
                    }
                }, "ThreadPool-Worker-" + threadCounter.incrementAndGet());
                workers.add(worker);
                worker.start();
            }
        }
        
        public void enqueue(Runnable task) {
            if (task == null) {
                throw new NullPointerException("Task cannot be null");
            }
            if (stop) {
                throw new IllegalStateException("ThreadPool is shutting down");
            }
            taskQueue.push(task);
        }
        
        public void waitForCompletion() throws InterruptedException {
            synchronized (completionLock) {
                while (activeTasks.get() > 0 || !taskQueue.isEmpty()) {
                    completionLock.wait();
                }
            }
        }
        
        public void shutdown() {
            // First set stop flag
            stop = true;
            
            // Then wait for active tasks to complete
            synchronized (completionLock) {
                while (activeTasks.get() > 0) {
                    try {
                        completionLock.wait();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                }
            }

            // Now push shutdown tasks to stop workers
            for (int i = 0; i < workers.size(); i++) {
                taskQueue.push(SHUTDOWN_TASK);
            }

            // Finally join all worker threads
            for (Thread worker : workers) {
                try {
                    worker.join();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
        }
        
        @Override
        public void close() {
            shutdown();
        }
    }
    
    // Simplify method
    private void printLine(String line) {
        if (line == null) {
            throw new NullPointerException("Line cannot be null");
        }
        synchronized(consoleLock) {
            System.out.println("Thread " + Thread.currentThread().getName() + ": " + line);
        }
    }
    
    // Make this an instance method
    public void runExample() throws InterruptedException {
        // Create thread pool with 4 worker threads
        try (ThreadPool pool = new ThreadPool(this, 4)) {
            // Lines from "I'm a Little Teapot"
            String[] lines = {
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
            Thread t1 = new Thread(() -> printLine(lines[0]));
            Thread t2 = new Thread(() -> printLine(lines[1]));
            
            t1.start();
            t2.start();
            t1.join();
            t2.join();
            
            // Demonstrate semaphore usage - print next two lines
            Thread producer = new Thread(() -> {
                printLine(lines[2]);
                signalSemaphore.release(); // Signal consumer
            });
            
            Thread consumer = new Thread(() -> {
                try {
                    signalSemaphore.acquire(); // Wait for producer
                    printLine(lines[3]);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            });
            
            producer.start();
            consumer.start();
            producer.join();
            consumer.join();
            
            // Demonstrate thread pool - print remaining lines
            for (int i = 4; i < lines.length; i++) {
                final String line = lines[i];
                pool.enqueue(() -> printLine(line));
            }
            // Wait for all thread pool tasks to complete
            pool.waitForCompletion();
        }
    }
    
    // Keep main method static, but create an instance to run the example
    public static void main(String[] args) throws InterruptedException {
        ThreadingExample example = new ThreadingExample();
        example.runExample();
    }
} 