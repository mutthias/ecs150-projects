#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <deque>

#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HttpService.h"
#include "HttpUtils.h"
#include "FileService.h"
#include "MySocket.h"
#include "MyServerSocket.h"
#include "dthread.h"

using namespace std;

int PORT = 8080;
int THREAD_POOL_SIZE = 1;
int BUFFER_SIZE = 1;
string BASEDIR = "static";
string SCHEDALG = "FIFO";
string LOGFILE = "/dev/null";

// mutex as well as some conditional variables and deque for sockets
// Deque for FIFO order
pthread_mutex_t Thread_Mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t producer_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t consumer_cond = PTHREAD_COND_INITIALIZER;
std::deque<MySocket *> socket_deque;

vector<HttpService *> services;

HttpService *find_service(HTTPRequest *request) {
   // find a service that is registered for this path prefix
  for (unsigned int idx = 0; idx < services.size(); idx++) {
    if (request->getPath().find(services[idx]->pathPrefix()) == 0) {
      return services[idx];
    }
  }

  return NULL;
}

void invoke_service_method(HttpService *service, HTTPRequest *request, HTTPResponse *response) {
  stringstream payload;

  // invoke the service if we found one
  if (service == NULL) {
    // not found status
    response->setStatus(404);
  } else if (request->isHead()) {
    service->head(request, response);
  } else if (request->isGet()) {
    service->get(request, response);
  } 
  else {
    // The server doesn't know about this method
    response->setStatus(405);
  }
}

void handle_request(MySocket *client) {
  HTTPRequest *request = new HTTPRequest(client, PORT);
  HTTPResponse *response = new HTTPResponse();
  stringstream payload;
  
  // read in the request
  bool readResult = false;
  try {
    payload << "client: " << (void *) client;
    sync_print("read_request_enter", payload.str());
    readResult = request->readRequest();
    sync_print("read_request_return", payload.str());
  } catch (...) {
    // swallow it
  }    
    
  if (!readResult) {
    // there was a problem reading in the request, bail
    delete response;
    delete request;
    sync_print("read_request_error", payload.str());
    return;
  }
  
  HttpService *service = find_service(request);
  invoke_service_method(service, request, response);

  // send data back to the client and clean up
  payload.str(""); payload.clear();
  payload << " RESPONSE " << response->getStatus() << " client: " << (void *) client;
  sync_print("write_response", payload.str());
  cout << payload.str() << std::endl;
  client->write(response->response());
    
  delete response;
  delete request;

  payload.str(""); payload.clear();
  payload << " client: " << (void *) client;
  sync_print("close_connection", payload.str());
  client->close();
  delete client;
}

// Inspired by coke producer/consumer example with two conditionals (producer_consumer.cpp)
// Wrapped in `while (true)` because we have to keep taking requests
// Also a bit analogous to the threadHandler function in `pthread_cat.cpp`
void *consumer(void * arg) {
  while (true) {
    dthread_mutex_lock(&Thread_Mutex);
    while (!socket_deque.size()) {
      dthread_cond_wait(&producer_cond, &Thread_Mutex);
    }
    MySocket *req = socket_deque.front();
    socket_deque.pop_front();
    dthread_cond_signal(&consumer_cond);
    dthread_mutex_unlock(&Thread_Mutex);
    handle_request(req);
  }
}

// Inspired by coke producer/consumer example with two conditionals (producer_consumer.cpp)
// Again, wrapped in `while (true)` to continuously take requests
void producer(MyServerSocket * server) {
  while (true) {
    sync_print("waiting_to_accept", "");
    MySocket *client = server->accept();
    sync_print("client_accepted", "");
    dthread_mutex_lock(&Thread_Mutex);
    // int size = socket_deque.size();
    while (static_cast<int>(socket_deque.size()) >= BUFFER_SIZE) {
      dthread_cond_wait(&consumer_cond, &Thread_Mutex);
    }
    socket_deque.push_back(client);
    dthread_cond_signal(&producer_cond);
    dthread_mutex_unlock(&Thread_Mutex);
  }
}

int main(int argc, char *argv[]) {

  signal(SIGPIPE, SIG_IGN);
  int option;

  while ((option = getopt(argc, argv, "d:p:t:b:s:l:")) != -1) {
    switch (option) {
    case 'd':
      BASEDIR = string(optarg);
      break;
    case 'p':
      PORT = atoi(optarg);
      break;
    case 't':
      THREAD_POOL_SIZE = atoi(optarg);
      break;
    case 'b':
      BUFFER_SIZE = atoi(optarg);
      break;
    case 's':
      SCHEDALG = string(optarg);
      break;
    case 'l':
      LOGFILE = string(optarg);
      break;
    default:
      cerr<< "usage: " << argv[0] << " [-p port] [-t threads] [-b buffers]" << endl;
      exit(1);
    }
  }

  set_log_file(LOGFILE);

  sync_print("init", "");
  MyServerSocket *server = new MyServerSocket(PORT);
  pthread_t thread_list[THREAD_POOL_SIZE];
  
  // Start creating the pool
  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    dthread_create(&thread_list[i], NULL, consumer, NULL);
  }

  // The order that you push services dictates the search order
  // for path prefix matching
  services.push_back(new FileService(BASEDIR));
  producer(server);
  
}
