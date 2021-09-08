#include "../include/smart_mutex.h"
#include <gtest/gtest.h>
#include <chrono>


TEST(SmokeTest, RecursiveDeadlock) {
    CSC::SmartMutex sm;

    std::unique_lock<CSC::SmartMutex> ulock(sm);
    ASSERT_DEATH(sm.lock(), "");
}


void cross_deadlock_locker(CSC::SmartMutex& my_mutex, CSC::SmartMutex& other_mutex, std::chrono::duration<int, std::milli> wait_time) {
    std::unique_lock<CSC::SmartMutex> ulock1(my_mutex);
    std::this_thread::sleep_for(wait_time);
    std::unique_lock<CSC::SmartMutex> ulock2(other_mutex);
}


TEST(SmokeTest, CrossDoubleDeadlock) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    CSC::SmartMutex sm1;
    CSC::SmartMutex sm2;

    std::jthread th(cross_deadlock_locker, std::ref(sm1), std::ref(sm2), std::chrono::duration<int, std::milli>(1000));

    sm2.lock();

    std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(2000));

    ASSERT_DEATH(sm1.lock(), "");

    sm2.unlock();
}


template <class InputIt, class OutputIt, class BinaryOp>
void adjacent_tranform(InputIt first, InputIt last, OutputIt out, BinaryOp binop) {
    for (InputIt second = first + 1; second != last; ++first, ++second) {
        *out = binop(*first, *second);
        ++out; //no-op for back_inserter
    }
}


TEST(SmokeTest, CrossOctoDeadlock) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    std::vector<CSC::SmartMutex> smart_mxs(8);

    smart_mxs.back().lock();

    std::vector<std::jthread> threads;

    adjacent_tranform(smart_mxs.begin(), smart_mxs.end(), std::back_inserter(threads), [] (CSC::SmartMutex& sm1, CSC::SmartMutex& sm2) {
            return std::jthread(cross_deadlock_locker, std::ref(sm1), std::ref(sm2), std::chrono::duration<int, std::milli>(1000));
    });

    std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(2000));

    ASSERT_DEATH(smart_mxs.front().lock(), "");

    smart_mxs.back().unlock();
}


void try_lock_locker(CSC::SmartMutex& sm) {
    for (int i = 0; i != 1000; ++i) {
        sm.try_lock();
    }
}


TEST(SmokeTest, TryLockTest) {
    CSC::SmartMutex sm;
    
    std::unique_lock<CSC::SmartMutex> ulock(sm);

    std::jthread(try_lock_locker, std::ref(sm));
}


class SillyAtomic {
public:
    SillyAtomic& operator++() {
        std::unique_lock<CSC::SmartMutex> ulock(mx);
        ++value;
        return *this;
    }

    int get() const noexcept {
        return value;
    }
private:
    CSC::SmartMutex mx;
    int value = 0;
};


void basic_properties_test_routine(SillyAtomic& atomic) {
    for (int i = 0; i != 10000; ++i) {
        ++atomic;
    }
}


TEST(SmokeTest, BasicUsageTest) {
    SillyAtomic atomic;

    std::thread th(basic_properties_test_routine, std::ref(atomic));

    for (int i = 0; i != 10000; ++i) {
        ++atomic;
    }

    th.join();

    ASSERT_EQ(atomic.get(), 20000);
}


TEST(SmokeTest, StressUsageTest) {
    SillyAtomic atomic;

    {
        std::vector<std::jthread> threads;
        for (unsigned i = 0; i != std::thread::hardware_concurrency() * 20; ++i) {
            threads.emplace_back(basic_properties_test_routine, std::ref(atomic));
        }
    }

    ASSERT_EQ(atomic.get(), std::thread::hardware_concurrency() * 20 * 10000);
}



TEST(SmokeTest, StressLockTest) {
    CSC::SmartMutex sm;

    for (int i = 0; i != 10000; ++i) {
        std::unique_lock<CSC::SmartMutex> ulock(sm);
    }
}
