#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <condition_variable>
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

template<typename... Args>
void print_sync(Args&&... args) {
    std::lock_guard<std::mutex> lock(printMutex);
    (std::cout << ... << std::forward<Args>(args)) << std::endl;
}

struct Task {
    int id;
    std::function<void()> func;

    explicit Task(int id = 0, std::function<void()> func = [] {})
        : id(id), func(std::move(func)) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = default;
    Task& operator=(Task&&) = default;
};

class tasksQueue {
    std::shared_mutex mutexQ;
    std::queue<Task> tasks;
public:
    tasksQueue() = default;
    ~tasksQueue() { clear(); }

    tasksQueue(const tasksQueue&) = delete;
    tasksQueue& operator=(const tasksQueue&) = delete;
    tasksQueue(tasksQueue&&) = delete;
    tasksQueue& operator=(tasksQueue&&) = delete;

    bool empty() {
        std::shared_lock lock(mutexQ);
        return tasks.empty();
    }

    std::size_t size() {
        std::shared_lock lock(mutexQ);
        return tasks.size();
    }

    void clear() {
        std::unique_lock lock(mutexQ);
        while (!tasks.empty()) {
            tasks.pop();
        }
    }

    bool pop(Task& task) {
        std::unique_lock lock(mutexQ);
        if (tasks.empty()) return false;
        task = std::move(tasks.front()); // Task& task now has struct that was required to be popped
        tasks.pop();
        return true;
    }

    bool push(int taskId, std::function<void()> func) {
        Task newTask(taskId, std::move(func));
        std::unique_lock lock(mutexQ);
        tasks.push(std::move(newTask));
        return true;
    }
};

class threadPool {
    std::vector<std::thread> workers;
    tasksQueue queue1;
    tasksQueue queue2;

    std::mutex q1_mutex;
    std::condition_variable q1_cv;
    std::mutex q2_mutex;
    std::condition_variable q2_cv;

    std::mutex addTask;
    std::mutex poolStateMutex; // for stop/terminate/pause

    std::atomic<bool> terminated{false};
    std::atomic<bool> initialized{false};
    std::atomic<bool> paused{false};

    std::atomic<int> totalTasks{0};

    bool working_unsafe() const {
        return initialized.load() && !terminated.load();
    }

public:
    bool working() {
        std::lock_guard<std::mutex> lock(poolStateMutex);
        return working_unsafe();
    }

    void initialize(int workerNumber) {
        std::lock_guard<std::mutex> lock(poolStateMutex);
        if (initialized || terminated) return;

        int workersQ1 = workerNumber / 2;

        workers.reserve(workerNumber);
        print_sync("[ThreadPool] Initializing with ", workerNumber, " workers...");

        for (int i = 0; i < workersQ1; ++i) { // reference to q2, to steal
            workers.emplace_back(&threadPool::routine, this, i, "Queue 1", std::ref(queue1), std::ref(q1_mutex), std::ref(q1_cv), std::ref(queue2));
        }
        for (int i = workersQ1; i < workerNumber; ++i) { // reference to q1, to steal
            workers.emplace_back(&threadPool::routine, this, i, "Queue 2", std::ref(queue2), std::ref(q2_mutex), std::ref(q2_cv), std::ref(queue1));
        }
        initialized = true;
        paused = false;
        terminated = false;
        print_sync("[ThreadPool] Initialized.");
    }

    void add_task(std::function<void()> func) {
        if (!initialized.load() || terminated.load()) {
            print_sync("[ThreadPool] Warning: Failed to add new task. Pool not ready or terminated.");
            return;
        }

        int taskId = totalTasks.fetch_add(1);
        tasksQueue* targetQueuePtr = nullptr;
        std::mutex* targetMutexPtr = nullptr;
        std::condition_variable* targetCVPtr = nullptr;
        std::string targetQueueName;

        {  // critical section for adding task
            std::lock_guard<std::mutex> lock(addTask);

            std::size_t firstQueueSize = queue1.size();
            std::size_t secondQueueSize = queue2.size();
            print_sync("[Debug Task ", taskId, "]: Queue sizes -> Q1 = ", firstQueueSize, "s, Q2 = ", secondQueueSize, "s");

            if (firstQueueSize <= secondQueueSize) {
                targetQueuePtr = &queue1;
                targetMutexPtr = &q1_mutex;
                targetCVPtr = &q1_cv;
                targetQueueName = "Queue 1";
            } else {
                targetQueuePtr = &queue2;
                targetMutexPtr = &q2_mutex;
                targetCVPtr = &q2_cv;
                targetQueueName = "Queue 2";
            }
            print_sync("[Debug] Task ", taskId, ": Decided on ", targetQueueName);

            targetQueuePtr->push(taskId, std::move(func));
        } // end of critical section

        {
            std::lock_guard<std::mutex> lock(*targetMutexPtr); // locking the correct's queue
            targetCVPtr->notify_one();
        }
    }

    void routine(int worker_id, const char* queue_name, tasksQueue& assignedQueue, std::mutex& assignedMutex, std::condition_variable& assignedCV, tasksQueue& otherQueue) {
        print_sync("[Worker ", worker_id, "] started, assigned to ", queue_name);

        while (true) {
            Task task;
            bool task_popped = false;
            std::string source = queue_name;
            {
                std::unique_lock<std::mutex> lock(assignedMutex);
                assignedCV.wait_for(lock, std::chrono::milliseconds(50), [&] { // not sleeping forever if queue is empty
                    bool term = terminated.load();
                    bool pause = paused.load();
                    return term || (!pause && !assignedQueue.empty());
                });

                if (terminated.load()) {
                    print_sync("[Worker ", worker_id, "] Terminating signal detected.");
                    break;
                }

                if (paused.load()) {
                    print_sync("[Worker ", worker_id, "] Paused, continuing wait.");
                    continue; // go back to wait
                }

                if (!assignedQueue.empty()) { // firstly checking own queue
                    task_popped = assignedQueue.pop(task); // task is taken
                    source = queue_name;
                }
            } // lock released here

            if (!task_popped && !paused.load() && !terminated.load()) { // stealing
                if (otherQueue.pop(task)) {
                    task_popped = true;
                    if (std::string(queue_name) == "Queue 1") {
                        source = "Queue 2 (stolen)";
                    } else { source = "Queue 1 (stolen)"; }
                    print_sync("[Worker ", worker_id, "] has stolen the task.");
                }
            }

            if (task_popped) {
                print_sync("[Worker ", worker_id, "] Executing Task ", task.id, " source is ", source);
                task.func();
                print_sync("[Worker ", worker_id, "] Finished Task ", task.id);
            }
        } // end while loop
        print_sync("[Worker ", worker_id, "] Exiting routine.");
    }

    void pause() {
        std::lock_guard<std::mutex> lock(poolStateMutex);
        if (!initialized.load() || terminated.load()) return;
        paused = true;
        print_sync("[ThreadPool] Paused.");
    }

    void unpause() {
        bool needs_notify = false;
        {
            std::lock_guard<std::mutex> lock(poolStateMutex);
            if (!initialized.load() || terminated.load() || !paused.load()) {
                return;
            }
            paused = false;
            needs_notify = true;
            print_sync("[ThreadPool] Unpaused.");
        } // releasing poolStateMutex

        if (needs_notify) {  // notifying all workers on both queues
            { std::lock_guard lock1(q1_mutex); q1_cv.notify_all(); }
            { std::lock_guard lock2(q2_mutex); q2_cv.notify_all(); }
        }
    }

    void terminate() {
        bool needs_notify = false;
        {
            std::lock_guard<std::mutex> lock(poolStateMutex);
            if (!initialized.load() || terminated.load()) {
                return;
            }
            terminated = true;
            paused = false;  //  not stuck paused
            needs_notify = true;
            print_sync("[ThreadPool] Terminating...");
        } // release poolStateMutex

        if (needs_notify) {
            { std::lock_guard<std::mutex> lock1(q1_mutex); q1_cv.notify_all(); }
            { std::lock_guard<std::mutex> lock2(q2_mutex); q2_cv.notify_all(); }
            print_sync("[ThreadPool] Termination signals sent.");
        }

        print_sync("[ThreadPool] Joining worker threads...");
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        print_sync("[ThreadPool] Worker threads joined.");

        std::lock_guard<std::mutex> lock(poolStateMutex);
        workers.clear();
        queue1.clear();
        queue2.clear();
        initialized = false;
        terminated = false;
        paused = false;
        totalTasks = 0;
        print_sync("[ThreadPool] Terminated and cleaned.");
    }
};

#endif