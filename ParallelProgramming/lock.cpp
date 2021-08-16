//
// Created by artemiy on 17.04.2021.
//

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <iostream>
#include <cassert>
#include <climits>


class Timer {
public:
    Timer() {
        begin = std::chrono::steady_clock::now();
    }

    ~Timer() {
        auto end = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
        std::cout << elapsed_ms.count() << " ms" << std::endl;
    }

private:
    std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long int, std::ratio<1, 1000000000>>>
            begin;


};

struct Backoff {
public:
    inline void operator()() {
#ifdef __GNUC__
        __asm volatile ("pause");
#endif
        if (++iteration_num == limit) {
#ifdef __GNUC__
            __asm volatile ("pause");
#endif
            iteration_num = 0;
            limit = (limit >= min) ? limit * 100 / 85 : min;
            std::this_thread::yield();
        }

#ifdef __GNUC__
        __asm volatile ("pause");
#endif
    }

private:
    int min = 45000;
    int limit = 100'000;
    int iteration_num = 0;
};

class TTAS {
private:
    std::atomic<bool> locked;

public:
    TTAS() : locked(false) {}

    void lock() {

        bool flag = false;
        Backoff backoff;

        do {

//            flag = false;

            while (locked.load(std::memory_order_relaxed)) {
                backoff();
            }// wait


        } while (!locked.compare_exchange_weak(flag, true, std::memory_order_acquire, std::memory_order_relaxed));

    }

    void unlock() {
        locked.store(false, std::memory_order_release);
    }
};

class TAS {
private:
    std::atomic<bool> locked;

public:
    TAS() : locked(false) {}

    void lock() {

        bool flag = false;
        Backoff backoff;

        while (!locked.compare_exchange_weak(flag, true, std::memory_order_acquire, std::memory_order_relaxed)) {
            backoff();
        }

    }

    void unlock() {
        locked.store(false, std::memory_order_release);
    }
};


class TicketLock {
private:
    std::atomic<unsigned int> current_ticket;
    std::atomic<unsigned int> next_ticket;


public:
    void lock() {
        const unsigned my_ticket = next_ticket.fetch_add(1, std::memory_order_relaxed);
        Backoff backoff;
        while (current_ticket.load(std::memory_order_relaxed) != my_ticket) {
            backoff();
        }
        current_ticket.load(std::memory_order_acquire);
    }

    void unlock() {

        const unsigned next = current_ticket.load(std::memory_order_relaxed) + 1;
        current_ticket.store(next, std::memory_order_release);

        //current_ticket.fetch_add(1, std::memory_order_release);
    }
};


template<typename Lock=TTAS>
class Tester {
private:
    const int thread_cnt = 100;
    Lock lock;

public:


    void test1() {

        using namespace std::chrono;
        using time_point = time_point<std::chrono::steady_clock>;


        for (int thread_cnt = 1; thread_cnt < 9; ++thread_cnt) {

            Timer* t = new Timer;

            int i = 0;
            const long long limit = 1000000;
            std::vector<std::vector<std::pair<time_point, time_point>>> vec(thread_cnt);

            auto task = [&](int num) {
                steady_clock clock;
                while (true) {

                    auto knock = clock.now();
                    std::lock_guard<Lock> _lock(lock);
                    auto enter = clock.now();
                    vec[num].push_back(std::make_pair(knock, enter));
                    if (i < limit) {
                        ++i;
                    } else {
                        break;
                    }

                }
            };

            std::vector<std::thread> workers(thread_cnt);

            for (int i = 0; i < thread_cnt; ++i) {
                workers[i] = std::thread(task, i);
            }

            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            std::cout << thread_cnt << " threads\n";
            std::cout << "time elapsed" << std::endl;
            delete t;

            assert(i == limit);

            double avg;
            int cnt = 0;
            for (auto& arr: vec) {
                for (auto& item: arr) {
                    ++cnt;
                    avg += duration_cast<milliseconds>(item.second - item.first).count();
                }
            }

            std::cout << "avg = " << avg / static_cast<double>(cnt) << std::endl <<std::endl;

        }


    } // test if lock works correctly

    /*
    void test2() {
        using namespace std::chrono;

        using time_point = time_point<std::chrono::steady_clock>;

        std::chrono::steady_clock clock;

        std::vector<std::vector<std::pair<time_point, time_point>>> vec(thread_cnt);

        auto task = [&](int num) {
            for (int i = 0; i < 2; ++i) {
//                int my_moment_of_request = clock();
                auto knock = clock.now();
                std::lock_guard<TTAS> _lock(lock);
                auto enter = clock.now();
                vec[num].push_back(std::make_pair(knock, enter));
            }
        };


        std::vector<std::thread> workers(thread_cnt);


        for (int i = 0; i < thread_cnt; ++i) {
            workers[i] = std::thread(task, i);
        }

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

//        for (int i = 0; i < thread_cnt; ++i) {
//            std::cout << "thread " << i << std::endl;
//            for (auto& item: vec[i]) {
//                std::cout << "I requested lock at " << item.first.time_since_epoch() << " and got lock at "
//                          << item.second << std::endl;
//            }
//        }

    }// show fairness/unfairness
     */



    void test3() {

        using namespace std::chrono;
        using time_point = time_point<std::chrono::steady_clock>;


        for (int thread_cnt = 100; thread_cnt < 110; ++thread_cnt) {

            std::vector<std::vector<std::pair<time_point, time_point>>> vec(thread_cnt);
            Timer* t = new Timer;

            auto task = [&](int num) {
                steady_clock clock;

                for (int i = 0; i < 10; ++i) {
                    auto knock = clock.now();
                    std::lock_guard<Lock> _lock(lock);
                    auto enter = clock.now();
                    vec[num].push_back(std::make_pair(knock, enter));
                    for (int j = 0; j < 1000; ++j);
                }

            };

            std::vector<std::thread> workers(thread_cnt);

            for (int i = 0; i < thread_cnt; ++i) {
                workers[i] = std::thread(task, i);
            }

            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            std::cout << thread_cnt << " threads\n";

            std::cout << "time elapsed" << std::endl;
            delete t;

            double avg;
            int cnt = 0;
            for (auto& arr: vec) {
                for (auto& item: arr) {
                    ++cnt;
                    avg += duration_cast<milliseconds>(item.second - item.first).count();
                }
            }

//            std::cout << avg << "   " << cnt << std::endl;
            std::cout << "avg = " << avg / static_cast<double>(cnt) << std::endl << std::endl;

        }

    }

};

int main() {
    Tester<TTAS> tester;
    tester.test1();
//    std::cout << "next test\n";
//    tester.test3();

}


//1 threads
//        time elapsed
//356 ms
//        avg = 6.93381e-316
//
//2 threads
//        time elapsed
//606 ms
//        avg = 5.69999e-05
//
//3 threads
//        time elapsed
//679 ms
//        avg = 0.000222999
//
//4 threads
//        time elapsed
//652 ms
//        avg = 0.000379998
//
//5 threads
//        time elapsed
//573 ms
//        avg = 0.000608997
//
//6 threads
//        time elapsed
//363 ms
//        avg = 0.000773995
//
//7 threads
//        time elapsed
//806 ms
//        avg = 0.00344698
//
//8 threads
//        time elapsed
//10184 ms
//        avg = 0.0728824