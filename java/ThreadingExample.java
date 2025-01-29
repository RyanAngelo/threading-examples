import java.util.Vector;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

public class ThreadingExample {
    // Instance fields instead of static
    private final Object consoleLock = new Object();
    private final Semaphore signalSemaphore = new Semaphore(0);
    
    // Thread-safe queue implementation using BlockingQueue
    static class ThreadSafeQueue {
        private final BlockingQueue<Runnable> tasks = new LinkedBlockingQueue<>();
        
        public void push(Runnable task) {
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
    static class ThreadPool {
        private final Vector<Thread> workers;
        private final ThreadSafeQueue taskQueue;
        private volatile boolean stop = false;
        private final AtomicInteger activeTasks = new AtomicInteger(0);
        private final Object completionLock = new Object();
        
        public ThreadPool(int numThreads) {
            this.workers = new Vector<>();
            this.taskQueue = new ThreadSafeQueue();
            
            for (int i = 0; i < numThreads; i++) {
                workers.add(new Thread(() -> {
                    while (true) {
                        try {
                            Runnable task = taskQueue.pop();
                            if (stop && task == null) break;
                            
                            if (task != null) {
                                activeTasks.incrementAndGet();
                                task.run();
                                activeTasks.decrementAndGet();
                                synchronized (completionLock) {
                                    completionLock.notifyAll();
                                }
                            }
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            break;
                        }
                    }
                }));
                workers.lastElement().start();
            }
        }
        
        public void enqueue(Runnable task) {
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
            stop = true;
            workers.forEach(worker -> taskQueue.push(null));
            workers.forEach(worker -> {
                try {
                    worker.join();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            });
        }
    }
    
    // Make this an instance method
    private void printLine(String line) {
        synchronized (consoleLock) {
            System.out.println("Thread " + Thread.currentThread().getName() + ": " + line);
        }
    }
    
    // Make this an instance method
    public void runExample() throws InterruptedException {
        // Create thread pool with 4 worker threads
        ThreadPool pool = new ThreadPool(4);
        
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
        pool.shutdown();
    }
    
    // Keep main method static, but create an instance to run the example
    public static void main(String[] args) throws InterruptedException {
        ThreadingExample example = new ThreadingExample();
        example.runExample();
    }
} 