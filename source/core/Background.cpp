#include "core/Background.hpp"

#include <pthread.h>

#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

namespace {

const size_t STACK_SIZE = 512 * 1024;  // curl + mbedtls TLS handshakes need more than the default stack

void* runTask(void* arg) {
    auto* task = static_cast<std::function<void()>*>(arg);
    (*task)();
    delete task;
    return nullptr;
}

}  // namespace

void runInBackground(std::function<void()> task) {
    auto* heapTask = new std::function<void()>(std::move(task));
    // Only the stack size goes through attributes (best effort); detaching uses pthread_detach()
    pthread_attr_t attr;
    bool hasAttr = pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, STACK_SIZE) == 0;
    pthread_t thread;
    int rc = pthread_create(&thread, hasAttr ? &attr : nullptr, runTask, heapTask);
    pthread_attr_destroy(&attr);
    if (rc == 0) {
        pthread_detach(thread);
    } else {
        // Never lose the work: fall back to the shared brls::async queue
        brls::Logger::error("runInBackground: pthread_create failed ({}), running on brls::async", rc);
        brls::async([heapTask] {
            (*heapTask)();
            delete heapTask;
        });
    }
}
