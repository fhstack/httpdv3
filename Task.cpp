#include "Task.h"


//template <class T>
//Task<T>::Task(void (*fn_ptr)(void*), void* arg) : m_fn_ptr(fn_ptr), m_arg(arg) {
//}

Task::~Task() {
}

/*template <class T>
void Task<T>::operator()() {
  (*m_fn_ptr)(m_arg);
  if (m_arg != NULL) {
    delete m_arg;
  }
}*/

void Task::run() {
    m_conn->process();
}
