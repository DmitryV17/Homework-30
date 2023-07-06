// В этом коде есть ошибки, поскольку я так и не смог понять как именно 
// переводить в пул потоков не всю функцию а её часть, и вызывать  многопоточную 
// функцию для работы в main

#include <future>
#include <chrono>
#include <iostream>
#include <random>
#include <queue>
#include <condition_variable>
#include <vector>

using namespace std;

bool make_thread = true;

mutex coutLocker;
// удобное определение для сокращения кода
typedef function<void()> task_type;
// тип указатель на функцию, которая является эталоном для функций задач
typedef void (*FuncType) (int*, long, long);

// пул потоков
class ThreadPool {
public:
    // запуск
    void start();
    // остановка
    void stop();
    // проброс задач
    void push_task(FuncType f, int* array, long left, long right_bound);
    // функция входа для потока
    void threadFunc(int qindex);
private:
    // количество потоков
    int m_thread_count;
    // потоки
    vector<thread> m_threads;
    // очереди задач для потоков
    vector<BlockedQueue<task_type>> m_thread_queues;
    // для равномерного распределения задач
    int m_index;
};

ThreadPool::ThreadPool() :
    m_thread_count(thread::hardware_concurrency() != 0 ? thread::hardware_concurrency() : 4),
    m_thread_queues(m_thread_count) {

}

void ThreadPool::start() {
    for (int i = 0; i < m_thread_count; i++) {
        m_threads.emplace_back(&ThreadPool::threadFunc, this, i);
    }
}

void ThreadPool::stop() {
    for (int i = 0; i < m_thread_count; i++) {
        // кладем задачу-пустышку в каждую очередь
        // для завершения потока
        task_type empty_task;
        m_thread_queues[i].push(empty_task);
    }
    for (auto& t : m_threads) {
        t.join();
    }
}


    void ThreadPool::push_task(FuncType f, int* array, long left, long right_bound) {
        // вычисляем индекс очереди, куда положим задачу
        int queue_to_push = m_index++ % m_thread_count;
        // формируем функтор
        task_type task = [=] {f(array, left, right_bound); };
        // кладем в очередь
        m_thread_queues[queue_to_push].push(task);
    }

void ThreadPool::threadFunc(int qindex) {
    while (true) {
        // обработка очередной задачи
        task_type task_to_do;
        bool res;
        int i = 0;
        for (; i < m_thread_count; i++) {
            // попытка быстро забрать задачу из любой очереди, начиная со своей
            if (res = m_thread_queues[(qindex + i) % m_thread_count].fast_pop(task_to_do))
                break;
        }

        if (!res) {
            // вызываем блокирующее получение очереди
            m_thread_queues[qindex].pop(task_to_do);
        }
        else if (!task_to_do) {
            // чтобы не допустить зависания потока
            // кладем обратно задачу-пустышку
            m_thread_queues[(qindex + i) % m_thread_count].push(task_to_do);
        }
        if (!task_to_do) {
            return;
        }
        // выполняем задачу
        task_to_do();
    }
}

class RequestHandler {
public:
    RequestHandler();
    ~RequestHandler();
    // отправка запроса на выполнение
    void pushRequest(FuncType f, int* array, long left, long right_bound);
private:
    // пул потоков
    ThreadPool m_tpool;
};

RequestHandler::RequestHandler() {
    m_tpool.start();
}
RequestHandler::~RequestHandler() {
    m_tpool.stop();
}
void RequestHandler::pushRequest(FuncType f, int* array, long left, long right_bound) {
    m_tpool.push_task(f, array, left, right_bound);
}

template<class T>
class BlockedQueue {
public:
    void push(T& item) {
        lock_guard<mutex> l(m_locker);
        // обычный потокобезопасный push
        m_task_queue.push(item);
        // делаем оповещение, чтобы поток, вызвавший
        // pop проснулся и забрал элемент из очереди
        m_notifier.notify_one();
    }
    // блокирующий метод получения элемента из очереди
    void pop(T& item) {
        unique_lock<mutex> l(m_locker);
        if (m_task_queue.empty())
            // ждем, пока вызовут push
            m_notifier.wait(l, [this] {return !m_task_queue.empty(); });
        item = m_task_queue.front();
        m_task_queue.pop();
    }
    // неблокирующий метод получения элемента из очереди
    // возвращает false, если очередь пуста
    bool fast_pop(T& item) {
        lock_guard<mutex> l(m_locker);
        if (m_task_queue.empty())
            // просто выходим
            return false;
        // забираем элемент
        item = m_task_queue.front();
        m_task_queue.pop();
        return true;
    }
private:
    mutex m_locker;
    // очередь задач
    queue<T> m_task_queue;
    // уведомитель
    condition_variable m_notifier;
};


void quicksort(int* array, long left, long right) {
    if (left >= right) return;
    long left_bound = left;
    long right_bound = right;

    long middle = array[(left_bound + right_bound) / 2];

    do {
        while (array[left_bound] < middle) {
            left_bound++;
        }
        while (array[right_bound] > middle) {
            right_bound--;
        }

        //Меняем элементы местами
        if (left_bound <= right_bound) {
            std::swap(array[left_bound], array[right_bound]);
            left_bound++;
            right_bound--;
        }
    } while (left_bound <= right_bound);

    if (make_thread && (right_bound - left > 10000))
    {
      // Собственно тут и возникает проблема -m_tpool просто подчеркивается красным
      // не давая выполнить push_task
       
        auto f = m_tpool.push_task(quicksort, array, left, right_bound);
        f.pushRequest;//?
        quicksort(array, left_bound, right);
        f.wait(); // нельзя так написать
    }
    else {
        // запускаем обе части синхронно
        quicksort(array, left, right_bound);
        quicksort(array, left_bound, right);
    }
  
}



int main() {
    srand(0);
    long arr_size = 100000000;
    int* array = new int[arr_size];
    for (long i = 0; i < arr_size; i++) {
        array[i] = rand() % 500000;
    }

   // Так же непонятно как и здесь вызывать часть функции, обрабатываюмую в пуле потоков. 
   // Еще после добавления в пул потоков стали подчеркиваться  time(&start) и time(&end)
   // не знаю  с чем это связано. 

    // многопоточный запуск
    cout << "multi-threaded launch:" << endl;
    time(&start);
    quicksort(array, 0, arr_size);
    time(&end);

    double seconds = difftime(end, start);
    printf("The time: %f seconds\n", seconds);

    for (long i = 0; i < arr_size - 1; i++) {
        if (array[i] > array[i + 1]) {
            cout << "Unsorted" << endl;
            break;
        }
    }

    for (long i = 0; i < arr_size; i++) {
        array[i] = rand() % 500000;
    }
    // однопоточный запуск
    cout << "one-threaded launch:" << endl;
    make_thread = false;
    time(&start);
    quicksort(array, 0, arr_size);
    time(&end);
    seconds = difftime(end, start);
    printf("The time: %f seconds\n", seconds);
    delete[] array;

	return 0;
}