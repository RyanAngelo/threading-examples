# Threading Examples

This project demonstrates threading concepts in C++, Java, and Rust, including:
- Mutex/Lock synchronization
- Semaphores/Channels for thread signaling
- Thread pools with task queues
- Condition variables for thread coordination

## Implementation Details

### C++ Versions
- Uses modern C++ threading facilities (`std::thread`, `std::mutex`)
- Thread-safe queue using `std::mutex` and `std::condition_variable`
- Uses `std::binary_semaphore` for thread signaling
- Thread pool with task queuing and completion tracking

### Java Versions
- Uses Java's built-in threading capabilities
- Thread-safe queue using `BlockingQueue`
- Uses `Semaphore` for thread signaling
- Custom thread pool implementation

### Rust Versions
- Uses Rust's safe concurrency model with `Arc` and `Mutex`
- Thread-safe queue using `Mutex` and `Condvar`
- Uses channels (`mpsc`) for thread signaling
- Thread pool with lifetime-safe task handling

## Examples

### Basic Threading
1. `cpp/threading.cpp` - Basic C++ threading example
2. `java/ThreadingExample.java` - Basic Java threading example
3. `rust/threading.rs` - Basic Rust threading example

### Threading with Random Numbers
1. `cpp/threading_numbers.cpp` - Extended C++ version with random numbers
2. `java/ThreadingNumbers.java` - Extended Java version with random numbers
3. `rust/threading_numbers.rs` - Extended Rust version with random numbers

Each version demonstrates:
- Thread-specific random number generation
- Thread initialization synchronization
- Parent thread notification of child thread startup
- Display of thread-specific numbers alongside output

## Requirements

### C++
- C++20 or later
- Modern C++ compiler (GCC, Clang, or MSVC)

### Java
- Java 11 or later
- Java Development Kit (JDK)

### Rust
- Rust 1.56 or later
- Cargo package manager
- Required dependencies in `Cargo.toml`:
  ```toml
  [dependencies]
  rand = "0.8"
  ```

## Building and Running

### C++ Examples
```bash
# Basic threading example
g++ -std=c++20 cpp/threading.cpp -o threading -pthread
./threading

# Threading with random numbers
g++ -std=c++20 cpp/threading_numbers.cpp -o threading_numbers -pthread
./threading_numbers
```

### Java Examples
```bash
# Basic threading example
javac java/ThreadingExample.java
java -cp java ThreadingExample

# Threading with random numbers
javac java/ThreadingNumbers.java
java -cp java ThreadingNumbers
```

### Rust Examples
```bash
# Basic threading example
rustc rust/threading.rs
./threading

# Threading with random numbers
rustc rust/threading_numbers.rs
./threading_numbers
```

## Output

All versions demonstrate threading concepts by printing the lines of "I'm a Little Teapot" using different threading mechanisms:
1. Basic thread synchronization with mutex/locks
2. Producer-consumer pattern using semaphores/channels
3. Parallel task execution using a thread pool

The random numbers versions additionally show:
- Random numbers generated for each thread
- Thread IDs with their associated random numbers
- Summary of thread pool worker random numbers

