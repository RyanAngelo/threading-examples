use std::sync::{Arc, Mutex, Condvar};
use std::thread::{self, ThreadId};
use std::sync::mpsc;
use std::collections::{VecDeque, HashMap};
use rand::Rng;

/// Thread-local state including random numbers
struct ThreadState {
    numbers: Arc<Mutex<HashMap<ThreadId, i32>>>,
    started: Arc<(Mutex<()>, Condvar)>,
}

impl ThreadState {
    fn new() -> Self {
        Self {
            numbers: Arc::new(Mutex::new(HashMap::new())),
            started: Arc::new((Mutex::new(()), Condvar::new())),
        }
    }

    fn register_thread_number(&self) {
        let thread_id = thread::current().id();
        let random_num = rand::thread_rng().gen_range(1..=100);
        
        let mut numbers = self.numbers.lock().unwrap();
        numbers.insert(thread_id, random_num);
        
        let (lock, cvar) = &*self.started;
        let _guard = lock.lock().unwrap();
        cvar.notify_all();
    }

    fn wait_for_thread(&self, handle: &thread::JoinHandle<()>) {
        let (lock, cvar) = &*self.started;
        let mut guard = lock.lock().unwrap();
        while !self.numbers.lock().unwrap().contains_key(&handle.thread().id()) {
            guard = cvar.wait(guard).unwrap();
        }
    }

    fn get_thread_number(&self, thread_id: ThreadId) -> i32 {
        self.numbers.lock().unwrap().get(&thread_id).copied().unwrap_or(0)
    }
}

/// Thread-safe queue implementation
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

/// Thread pool implementation with thread number tracking
struct ThreadPool {
    workers: Vec<thread::JoinHandle<()>>,
    task_queue: Arc<ThreadSafeQueue>,
    active_tasks: Arc<Mutex<usize>>,
    completion: Arc<(Mutex<bool>, Condvar)>,
    thread_state: Arc<ThreadState>,
}

impl ThreadPool {
    fn new(num_threads: usize, thread_state: Arc<ThreadState>) -> Self {
        let task_queue = Arc::new(ThreadSafeQueue::new());
        let active_tasks = Arc::new(Mutex::new(0));
        let completion = Arc::new((Mutex::new(false), Condvar::new()));
        let mut workers = Vec::with_capacity(num_threads);

        for _ in 0..num_threads {
            let task_queue = Arc::clone(&task_queue);
            let active_tasks = Arc::clone(&active_tasks);
            let completion = Arc::clone(&completion);
            let thread_state = Arc::clone(&thread_state);

            workers.push(thread::spawn(move || {
                thread_state.register_thread_number();
                
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
            thread_state,
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

    fn worker_count(&self) -> usize {
        self.workers.len()
    }
}

impl Drop for ThreadPool {
    fn drop(&mut self) {
        for worker in self.workers.drain(..) {
            worker.join().unwrap();
        }
    }
}

fn print_line(console: &Mutex<()>, thread_state: &ThreadState, line: &str) {
    let _lock = console.lock().unwrap();
    let thread_id = thread::current().id();
    let random_num = thread_state.get_thread_number(thread_id);
    println!("Thread {:?} (random number: {}): {}", thread_id, random_num, line);
}

fn main() {
    let console_mutex = Arc::new(Mutex::new(()));
    let thread_state = Arc::new(ThreadState::new());
    let (tx, rx) = mpsc::sync_channel(0);

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
    let thread_state = Arc::clone(&thread_state);
    let line1 = lines[0].to_string();
    let t1 = thread::spawn(move || {
        thread_state.register_thread_number();
        print_line(&console, &thread_state, &line1);
    });

    let console = Arc::clone(&console_mutex);
    let thread_state = Arc::clone(&thread_state);
    let line2 = lines[1].to_string();
    let t2 = thread::spawn(move || {
        thread_state.register_thread_number();
        print_line(&console, &thread_state, &line2);
    });

    thread_state.wait_for_thread(&t1);
    thread_state.wait_for_thread(&t2);
    t1.join().unwrap();
    t2.join().unwrap();

    // Demonstrate channel usage - print next two lines
    let console = Arc::clone(&console_mutex);
    let thread_state = Arc::clone(&thread_state);
    let line3 = lines[2].to_string();
    let tx_clone = tx.clone();
    let producer = thread::spawn(move || {
        thread_state.register_thread_number();
        print_line(&console, &thread_state, &line3);
        tx_clone.send(()).unwrap();
    });

    let console = Arc::clone(&console_mutex);
    let thread_state = Arc::clone(&thread_state);
    let line4 = lines[3].to_string();
    let consumer = thread::spawn(move || {
        thread_state.register_thread_number();
        rx.recv().unwrap();
        print_line(&console, &thread_state, &line4);
    });

    thread_state.wait_for_thread(&producer);
    thread_state.wait_for_thread(&consumer);
    producer.join().unwrap();
    consumer.join().unwrap();

    // Create thread pool with 4 worker threads
    let pool = ThreadPool::new(4, Arc::clone(&thread_state));

    // Wait for all thread pool workers to initialize
    {
        let (lock, cvar) = &*thread_state.started;
        let mut guard = lock.lock().unwrap();
        while thread_state.numbers.lock().unwrap().len() < pool.worker_count() {
            guard = cvar.wait(guard).unwrap();
        }
    }

    // Print all thread numbers
    {
        let _lock = console_mutex.lock().unwrap();
        println!("\nThread pool worker random numbers:");
        let numbers = thread_state.numbers.lock().unwrap();
        for (&thread_id, &number) in numbers.iter() {
            println!("Worker thread {:?}: {}", thread_id, number);
        }
        println!();
    }

    // Demonstrate thread pool - print remaining lines
    for line in lines.iter().skip(4) {
        let console = Arc::clone(&console_mutex);
        let thread_state = Arc::clone(&thread_state);
        let line = line.to_string();
        pool.enqueue(move || {
            print_line(&console, &thread_state, &line);
        });
    }

    pool.wait_for_completion();
} 