#include <atomic>
#include <iostream>
#include <vector>
#include <thread>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <random>


std::atomic<int> deleted(0);

constexpr int THREAD_CNT = 8;

template<typename T, int thread_cnt>
struct MemoryManager {
private:
    static std::mutex mut_;
    static std::atomic<int> index_;
    using Ptr = T*;

    static const int cycle_limit = thread_cnt + 1;

    static thread_local struct ThreadLocalManagement {

        std::atomic<T*> protected_;
        int cnt;
        std::atomic<T*> expired_[thread_cnt + 1];
        int idx;

        ThreadLocalManagement() : protected_(nullptr), cnt(0) {
            std::lock_guard<std::mutex> lock(MemoryManager::mut_);
            idx = MemoryManager::index_.load();
            MemoryManager::global_manager_[idx] = &protected_;
            for (int i = 0; i < MemoryManager::cycle_limit; ++i) {
                expired_[i].store(nullptr);
            }
            index_.fetch_add(1);
        }

        ~ThreadLocalManagement() {
            global_manager_[idx] = nullptr;
        }

    } local_management;


    static std::atomic<T*>* global_manager_[];

public:
    static inline void retire(T* ptr) {

        for (int i = 0; i < cycle_limit; ++i) {
            if (local_management.expired_[i].load() == nullptr) {
                local_management.expired_[i].store(ptr);
                break;
            }
        }

        ++local_management.cnt;

        if (local_management.cnt > thread_cnt) {

            std::vector<T*> set_of_expired;
            std::unordered_set<T*> set_of_protected;
            std::unordered_set<T*> pointers_to_delete;


            for (int i = 0; i < cycle_limit; ++i) {
                set_of_expired.push_back(local_management.expired_[i].load());
            }
            auto& index_copy = index_;

            for (int i = 0; i < index_.load(); ++i) {
                if (global_manager_[i]) {
                    set_of_protected.insert(global_manager_[i]->load());
                }

            }

            for (auto& pointer: set_of_expired) {
                if (set_of_protected.find(pointer) == set_of_protected.end()) {
                    pointers_to_delete.insert(pointer);
                }
            }

            for (auto& pointer: pointers_to_delete) {
//                deleted.fetch_add(1);
                delete pointer;
            }

            for (int i = 0; i < cycle_limit; ++i) {
                if (pointers_to_delete.find(local_management.expired_[i].load()) != pointers_to_delete.end()) {
                    local_management.expired_[i].store(nullptr);
                    --local_management.cnt;
                }
            }

        }

    }

    static inline Ptr protect(Ptr ptr) {

        local_management.protected_.store(ptr);
        return local_management.protected_.load();
    }

    static inline void release() {
        local_management.protected_.store(nullptr);
    }
};

template<typename T, int thread_cnt>
thread_local typename
MemoryManager<T, thread_cnt>::ThreadLocalManagement MemoryManager<T, thread_cnt>::local_management;

template<typename T, int thread_cnt> std::atomic<int> MemoryManager<T, thread_cnt>::index_(0);

template<typename T, int thread_cnt>
std::atomic<T*>* MemoryManager<T, thread_cnt>::global_manager_[thread_cnt];

template<typename T, int thread_cnt>
std::mutex MemoryManager<T, thread_cnt>::mut_;

template<typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        Node* prev = nullptr;

        Node(const T& item) : data(item) {}
    };

    MemoryManager<Node, THREAD_CNT> manager;
    std::atomic<Node*> top_;


public:
    void push(const T& item) {


        Node* pv = new Node(item);
        pv->prev = top_.load(std::memory_order_relaxed);
        while (!top_.compare_exchange_weak(pv->prev, pv, std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    std::optional<T> pop() {
        while (true) {
            Node* current_top = manager.protect(top_.load(std::memory_order_relaxed));


            if (current_top == nullptr) {
                return std::nullopt;
            } else if (top_.compare_exchange_weak(current_top, current_top->prev, std::memory_order_acquire,
                                                  std::memory_order_relaxed)) {

                T copy = current_top->data;
                manager.release();

                manager.retire(current_top);
                return std::optional<T>(copy);
            }
        }
    }

};

int main() {


    constexpr int iterations_per_thread = 100;

    LockFreeStack<int> stack;
    std::vector<std::vector<std::string>> work_res(THREAD_CNT);

    auto task = [&](const int num) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::bernoulli_distribution rv(0.5);

        std::vector<std::string> output;

        for (int i = 0, cnt = 0; i < iterations_per_thread; ++i) {
            if (rv(gen)) {
                stack.push(++cnt);
            } else {
                auto val = stack.pop();
                output.push_back(val ? std::to_string(*val) : "nullopt");
            }
        }

        work_res[num] = std::move(output);
    };


    std::vector<std::thread> workers(THREAD_CNT);
    int cnt = -1;
    for (auto& worker : workers) {
        worker = std::thread(task, ++cnt);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    for (int i = 0; i < THREAD_CNT; ++i) {
        std::cout << i << " thread:\n";
        for (auto& str: work_res[i]) {
            std::cout << str << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "deleted " << deleted;


    return 0;
}