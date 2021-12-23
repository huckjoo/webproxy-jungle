#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 " "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *user_agent_key = "User-Agent";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);
// thread를 사용해서 동시성 proxy 서버를 사용할 것이다.
void *thread(void *vargsp);


int main(int argc, char **argv){
  int listenfd, *connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2){
    fprintf(stderr, "usage :%s <port> \n", argv[0]);
    exit(1);
  }
  //프록시의 서버 역할 - argv[1]포트를 가지는 듣기 식별자를 open해주고 listenfd 생성
  listenfd = Open_listenfd(argv[1]);
  
  while (1){
    clientlen = sizeof(clientaddr);
    // proxy는 accept함수를 호출해서 클라이언트로부터의 연결 요청 기다림
    // 연결되면 connfd식별자를 생성
    connfd = Malloc(sizeof(int)); // 말록을 해야 멀티쓰레딩에서 경쟁(race)를 피할 수 있다.
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s %s).\n", hostname, port);
    // 여기서 doit과 close 안함!
    // doit(connfd);
    // Close(connfd);

    // 첫 번째 인자 *thread: 쓰레드 식별자
    // 두 번째: 쓰레드 특성 지정 (기본: NULL)
    // 세 번째: 쓰레드 함수
    // 네 번째: 쓰레드 함수의 매개변수
    Pthread_create(&tid, NULL, thread, connfd);
  }
  return 0;
}

void *thread(void *vargs) {
  int connfd = *((int*)vargs); 
  Pthread_detach(pthread_self()); //쓰레드를 process와 분리
  Free(vargs);  // Malloc한거 free 해줘야 함
  doit(connfd); // 각 쓰레드마다 connfd와 연결해주기 위해(client여러개 목적 달성!)
  Close(connfd); // connfd를 닫아줌
}

// transaction의 과정
// 1. 클라이언트 -> 서버에 request
// 2. 서버가 request 처리
// 3. 서버가 -> 클라이언트 response
// 4. 클라이언트가 response 처리
void doit(int connfd){
  // 나중에 인자들이 들어갈 빈 공간들을 생성한다.
  int end_serverfd; 
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  rio_t rio, server_rio; 
  //rio와 connfd 연결
  Rio_readinitb(&rio, connfd);
  // rio에서 읽고 buf로 복사한다.
  // 뭘 복사해? request line을 (GET / HTTP/1.0)
  Rio_readlineb(&rio, buf, MAXLINE);
  //buf에서 꺼내서 method(GET), uri(/), version(HTTP/1.0)에 채워넣는다.
  sscanf(buf, "%s %s %s", method, uri, version); 

  if (strcasecmp(method, "GET")){
    printf("Proxy does not implement the method");
    return;
  }
  
  parse_uri(uri, hostname, path, &port);// 값을 채워서 돌아왔다.
  // endserver_http_header는 빈 그릇이고 나머지는 진짜 값
  // endserver_http_header를 나머지 것을 사용해서 만든다.
  build_http_header(endserver_http_header, hostname, path, port, &rio);
  //proxy 입장에서 clientfd처럼 end_serverfd를 만든다.
  end_serverfd = connect_endServer(hostname, port, endserver_http_header);
  if (end_serverfd < 0){
    printf("connection failed\n");
    return;
  }
  // server_rio와 end_serverfd를 연결해준다.
  Rio_readinitb(&server_rio, end_serverfd);
  //end_serverfd에다가 endserver_http_header를 쓴다.
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));
  // 서버에서 온 내용을 porxy가 받아서 client에게 보내주는 과정(짧음주의)
  size_t n;
  // 1. 서버에서 response를 받아서
  // server_rio에서 읽은 값을 buf에 복사한다.
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0){
    printf("proxy received %d bytes,then send\n", (int)n);
    // 2. client에 보내준다.
    // buf의 값을 connfd 파일로 복사한다.(즉 작성한다.)
    Rio_writen(connfd, buf, n);
  }
  Close(end_serverfd); // end_serverfd를 닫는다.
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio){
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
  // "GET %s HTTP/1.0\r\n"포맷을 채울건데 %s에 path(/home.html)를 넣어주고 난 뒤, request_hdr를 채운다.
  sprintf(request_hdr, requestlint_hdr_format, path);
  // 우리는 build_http_header에 client_rio에 rio를 넣었다.
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0){
    if (strcmp(buf, endof_hdr) == 0) /*EOF*/
      break;

    if (!strncasecmp(buf, host_key, strlen(host_key))){
      strcpy(host_hdr, buf);
      continue;
    }

    if (!strncasecmp(buf, connection_key, strlen(connection_key)) || !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) || !strncasecmp(buf, user_agent_key, strlen(user_agent_key))){
      strcat(other_hdr, buf);
    }
  }
  if (strlen(host_hdr) == 0){
    sprintf(host_hdr, host_hdr_format, hostname);
  }
  // http header를 다음 것들로 다 합쳐서 만든다.
  sprintf(http_header, "%s%s%s%s%s%s%s",
          request_hdr,
          host_hdr,
          conn_hdr,
          prox_hdr,
          user_agent_hdr,
          other_hdr,
          endof_hdr);
  return;
}

inline int connect_endServer(char *hostname, int port, char *http_header){
  char portStr[100];
  sprintf(portStr, "%d", port);
  return Open_clientfd(hostname, portStr);
}
// 처음에는 빈 그릇들만 들어옴
void parse_uri(char *uri, char *hostname, char *path, int *port){
  *port = 80; // 기본 port는 80으로 설정해줌
  char *pos = strstr(uri, "//");// pos는 uri에서 "//"을 찾아서 가리킨다.

  pos = pos != NULL ? pos + 2 : uri; // NULL이 아니면 pos = pos+2
  // http://localhost:8000/home.html
  char *pos2 = strstr(pos, ":");// pos2는 pos문자열에서 :를 찾아서 가리킨다.
  if (pos2 != NULL){//pos2가 있다면
    *pos2 = '\0'; // pos2를 \0으로 변경
    sscanf(pos, "%s", hostname); //pos가 가리키는 것을 hostname에 넣는다.(localhost)
    sscanf(pos2 + 1, "%d%s", port, path); //(8000,/home.html)
  }
  else{//pos2가 없으면
    pos2 = strstr(pos, "/");
    if (pos2 != NULL){
      *pos2 = '\0';
      sscanf(pos, "%s", hostname);
      *pos2 = '/';
      sscanf(pos2, "%s", path);
    }
    else{
      sscanf(pos, "%s", hostname);
    }
  }
  return;
}