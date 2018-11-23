#pragma once

#include <pthread.h>
#include <unistd.h>
#include <deque>
#include <iostream>
#include <vector>
#include <errno.h>
#include <string.h>
#include "http_conn.h"
#include "Global.h"

using namespace std;

class Task
{
public:
    Task(http_conn* conn)
        :m_conn(conn)
    {}              // pass an object method pointer
    ~Task();
    void run();
private:
    http_conn* m_conn;
};
