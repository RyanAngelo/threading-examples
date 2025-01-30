use std::sync::{Arc, Mutex, Condvar};
use std::thread;
use std::sync::mpsc;
use std::collections::VecDeque;

/// Thread-safe queue implementation using Mutex and Condvar
struct ThreadSafeQueue {
    tasks: Mutex<VecDeque<Box<dyn FnOnce() + Send + 'static>>>,
    condvar: Condvar,
}

impl ThreadSafeQueue {
    fn new() -> Self {
        Self {
            tasks: Mutex::new(VecDeque::new()),
            condvar: Condvar::new(),
        }
    }

    fn push(&self, task: Box<dyn FnOnce() + Send + 'static>) {
        let mut tasks = self.tasks.lock().unwrap();
        tasks.push_back(task);
        self.condvar.notify_one();
    }

    fn pop(&self) -> Box<dyn FnOnce() + Send + 'static> {
        let mut tasks = self.tasks.lock().unwrap();
        while tasks.is_empty() {
            tasks = self.condvar.wait(tasks).unwrap();
        }
        tasks.pop_front().unwrap()
    }

    fn is_empty(&self) -> bool {
        self.tasks.lock().unwrap().is_empty()
    }
}

/// Thread pool implementation
struct ThreadPool {
    workers: Vec<thread::JoinHandle<()>>,
    task_queue: Arc<ThreadSafeQueue>,
    active_tasks: Arc<Mutex<usize>>,
    completion: Arc<(Mutex<bool>, Condvar)>,
}

impl ThreadPool {
    fn new(num_threads: usize) -> Self {
        let task_queue = Arc::new(ThreadSafeQueue::new());
        let active_tasks = Arc::new(Mutex::new(0));
        let completion = Arc::new((Mutex::new(false), Condvar::new()));
        let mut workers = Vec::with_capacity(num_threads);

        for _ in 0..num_threads {
            let task_queue = Arc::clone(&task_queue);
            let active_tasks = Arc::clone(&active_tasks);
            let completion = Arc::clone(&completion);

            workers.push(thread::spawn(move || {
                loop {
                    let task = task_queue.pop();
                    *active_tasks.lock().unwrap() += 1;
                    task();
                    *active_tasks.lock().unwrap() -= 1;
                    
                    let (lock, cvar) = &*completion;
                    let mut completed = lock.lock().unwrap();
                    if *active_tasks.lock().unwrap() == 0 && task_queue.is_empty() {
                        *completed = true;
                        cvar.notify_all();
                    }
                }
            }));
        }

        Self {
            workers,
            task_queue,
            active_tasks,
            completion,
        }
    }

    fn enqueue<F>(&self, task: F)
    where
        F: FnOnce() + Send + 'static,
    {
        self.task_queue.push(Box::new(task));
    }

    fn wait_for_completion(&self) {
        let (lock, cvar) = &*self.completion;
        let mut completed = lock.lock().unwrap();
        while !*completed {
            completed = cvar.wait(completed).unwrap();
        }
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        for worker in self.workers.drain(..) {
            worker.join().unwrap();
        }
    }
}

fn main() {
    // Create a mutex for console output synchronization
    let console_mutex = Arc::new(Mutex::new(()));

    // Create a channel for thread signaling
    let (tx, rx) = mpsc::sync_channel(0);

    // Lines from "I'm a Little Teapot"
    let lines = vec![
        "I'm a little teapot",
        "Short and stout",
        "Here is my handle",
        "Here is my spout",
        "When I get all steamed up",
        "Hear me shout",
        "Tip me over",
        "And pour me out!",
    ];

    // Demonstrate mutex usage - print first two lines
    let console = Arc::clone(&console_mutex);
    let line1 = lines[0].to_string();
    let t1 = thread::spawn(move || {
        let _lock = console.lock().unwrap();
        println!("Thread {:?}: {}", thread::current().id(), line1);
    });

    let console = Arc::clone(&console_mutex);
    let line2 = lines[1].to_string();
    let t2 = thread::spawn(move || {
        let _lock = console.lock().unwrap();
        println!("Thread {:?}: {}", thread::current().id(), line2);
    });

    t1.join().unwrap();
    t2.join().unwrap();

    // Demonstrate channel usage - print next two lines
    let console = Arc::clone(&console_mutex);
    let line3 = lines[2].to_string();
    let tx_clone = tx.clone();
    let producer = thread::spawn(move || {
        let _lock = console.lock().unwrap();
        println!("Thread {:?}: {}", thread::current().id(), line3);
        tx_clone.send(()).unwrap();
    });

    let console = Arc::clone(&console_mutex);
    let line4 = lines[3].to_string();
    let consumer = thread::spawn(move || {
        rx.recv().unwrap();
        let _lock = console.lock().unwrap();
        println!("Thread {:?}: {}", thread::current().id(), line4);
    });

    producer.join().unwrap();
    consumer.join().unwrap();

    // Create thread pool with 4 worker threads
    let pool = ThreadPool::new(4);

    // Demonstrate thread pool - print remaining lines
    for line in lines.iter().skip(4) {
        let console = Arc::clone(&console_mutex);
        let line = line.to_string();
        pool.enqueue(move || {
            let _lock = console.lock().unwrap();
            println!("Thread {:?}: {}", thread::current().id(), line);
        });
    }

    pool.wait_for_completion();
} 