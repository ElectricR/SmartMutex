#pragma once
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>

namespace CSC {


namespace detail {

//
// Class responsible for managing current resourse dependencies
// and finding cyclic dependencies.
//
class DependencyManager {
public:

    //
    // Method to declare that current thread depends on another thread.
    // Returns true if no issues found or false if cyclic dependency detected.
    // 
    bool set_thread_dependency(std::thread::id target_thread_id) noexcept {
        if (std::this_thread::get_id() == target_thread_id) {
            return false;
        }

        for (std::thread::id thread_id = target_thread_id; 
                dependencies.contains(thread_id);
                thread_id = dependencies[thread_id]) 
        {
            if (dependencies[thread_id] == std::this_thread::get_id()) {
                return false;
            }
        }

        dependencies[std::this_thread::get_id()] = target_thread_id;
        return true;
    }

    //
    // Method to declare that current thread does not depend on any other thread anymore.
    //
    void clear_thread_dependency() noexcept {
        dependencies.erase(std::this_thread::get_id());
    }

private:
    std::unordered_map<std::thread::id, std::thread::id> dependencies;
};


//
// Class-abstraction to provide SmartMutex with distinctive trait.
// Ensures that there will be no two SmartMutex with the same id program-wide.
//
class SmartMutexID {
public:
    SmartMutexID():
        id(reinterpret_cast<std::size_t>(this))
    {}

    struct hash {
        std::size_t operator()(const SmartMutexID& sm_id) const noexcept {
            return std::hash<std::size_t>()(sm_id.id);
        }
    };

    bool operator==(const SmartMutexID& other) const noexcept {
        return id == other.id;
    }

    SmartMutexID(const SmartMutexID&) = default;
    SmartMutexID(SmartMutexID&&) = default;

    SmartMutexID& operator=(const SmartMutexID&) = default;
    SmartMutexID& operator=(SmartMutexID&&) = default;

private:
    std::size_t id = 0;
};


//
// Singleton responsible in processing all SmartMutex's lock-unlock 
// calls in linear order.
//
// Incapsulates main logic of deadlock detection.
//
class LockManager {
public:

    //
    // Method for locking SmartMutex's underlying mutex.
    //
    // Takes SmartMutexID to distinct different SmartMutexes and
    // std::mutex in an unlocked state.
    // 
    // Returns true and locks underlying mutex on success or
    // returns false if potential deadlock was prevented.
    //
    bool safe_lock(const SmartMutexID& smart_mutex_id, std::mutex& smart_mutex_underlying_mx) {
        std::unique_lock ulock(lock_manager_mx);

        while (current_mutex_owner.contains(smart_mutex_id)) {
            if (!dependency_manager.set_thread_dependency(current_mutex_owner[smart_mutex_id])) {
                return false;
            }
            lock_manager_CVs[smart_mutex_id].wait(ulock);
        }

        smart_mutex_underlying_mx.lock();
        current_mutex_owner[smart_mutex_id] = std::this_thread::get_id();
        dependency_manager.clear_thread_dependency();
        return true;
    }

    bool try_lock(const SmartMutexID& smart_mutex_id, std::mutex& smart_mutex_underlying_mx) {
        std::unique_lock ulock(lock_manager_mx);

        if (smart_mutex_underlying_mx.try_lock()) {
            current_mutex_owner[smart_mutex_id] = std::this_thread::get_id();
            return true;
        }
        
        return false;
    }

    void unlock(const SmartMutexID& smart_mutex_id, std::mutex& smart_mutex_underlying_mx) {
        std::unique_lock ulock(lock_manager_mx);
        smart_mutex_underlying_mx.unlock(); 
        current_mutex_owner.erase(smart_mutex_id);
        if (lock_manager_CVs.contains(smart_mutex_id)) {
            lock_manager_CVs[smart_mutex_id].notify_one();
        }
    }

    void at_smart_mutex_destruction(const SmartMutexID& smart_mutex_id) {
        lock_manager_CVs.erase(smart_mutex_id);
    }

private:
    DependencyManager dependency_manager{};
    std::mutex lock_manager_mx{};
    std::unordered_map<SmartMutexID, std::condition_variable, SmartMutexID::hash> lock_manager_CVs{};
    std::unordered_map<SmartMutexID, std::thread::id, SmartMutexID::hash> current_mutex_owner{};
};


} // namespace detail


//
// Main public class to use.
//
// Proxies all its calls to LockManager singleton.
//
class SmartMutex {
public:
    SmartMutex() = default;

    SmartMutex(const SmartMutex&) = delete;
    SmartMutex(SmartMutex&&) = delete;

    SmartMutex& operator=(const SmartMutex) = delete;
    SmartMutex& operator=(SmartMutex&&) = delete;

    ~SmartMutex() {
        lock_manager.at_smart_mutex_destruction(id);
    }

    //
    // Terminates program if potential deadlock was
    // prevented during mutex locking.
    //
    void lock() {
        if (!lock_manager.safe_lock(id, underlying_mx)) {
            std::terminate();
        }
    }

    void try_lock() {
        lock_manager.try_lock(id, underlying_mx);
    }

    void unlock() {
        lock_manager.unlock(id, underlying_mx);
    }

private:
    inline static detail::LockManager lock_manager{};

private:
    std::mutex underlying_mx{};
    detail::SmartMutexID id{};
};


} // namespace CSC
