/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method); // @과제11번 수정
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method); // @과제11번 수정
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 듣기 소켓 오픈
  // argv는 main 함수가 받은 각각의 인자들
  // argv[1]은 ./tiny 8000 할때 8000 (우리가 부여하는 port번호), 듣기 식별자 return
  printf("현재 port(argv[1]):%s\n",argv[1]);
  listenfd = Open_listenfd(argv[1]);// 8000port에 연결 요청을 받을 준비가 된 듣기 식별자 return
  while (1) { //무한루프
    clientlen = sizeof(clientaddr);
    // 서버는 accept함수를 호출해서 클라이언트로부터의 연결 요청을 기다린다.
    // client 소켓은 server 소켓의 주소를 알고 있으니까 
    // client에서 server로 넘어올 때 add정보를 가지고 올 것이라고 가정
    // accept 함수는 연결되면 식별자 connfd를 return한다.
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    printf("connfd:%d\n",connfd); //connfd 확인용
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port); //어떤 client가 들어왔는지 알려줌
    doit(connfd);   // line:netp:tiny:doit 트랜잭션 수행
    Close(connfd);  // line:netp:tiny:close 자신쪽의 연결 끝 닫기
  }
}

void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    // Read request line and headers
    // 요청 라인을 읽고 분석한다
    // 식별자 fd를 rio_t 타입의 읽기 버퍼(rio)와 연결
    // 한 개의 빈 버퍼를 설정하고, 이 버퍼와 한 개의 오픈한 파일 식별자를 연결
    Rio_readinitb(&rio, fd); 
    // 다음 텍스트 줄을 파일 rio에서 읽고, 이를 메모리 위치 buf로 복사하고, 텍스트라인을 NULL로 종료시킴
    Rio_readlineb(&rio, buf, MAXLINE);// 
    printf("Request headers:\n");
    printf("%s",buf); // 요청된 라인을 printf로 보여줌
    sscanf(buf, "%s %s %s", method, uri, version);
    if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method,"HEAD") == 0)){ // GET,HEAD 메소드만 지원함
        // 다른 메소드가 들어올 경우, 에러를 출력하고
        clienterror(fd, method, "501", "Not implemented",
                    "Tiny does not implement this method");
        return; // main으로 return시킴
    }
    read_requesthdrs(&rio); // 읽어들이는데 아무것도 하지 않음

    // Parse URI from GET request
    // URI를 filename과 CGI argument string으로 parse하고 
    // request가 static인지 dynamic인지 확인하는 flag를 return한다.(1이면 static)
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0){ // disk에 파일이 없다면
        clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn't find this file");
        return;
    }

    if (is_static){ // Serve static content
        // S_ISREG -> 파일 종류 확인: 일반(regular) 파일인지 판별
        // 읽기 권한을 가지고 있는지 판별
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
            clienterror(fd, filename, "403", "Forbidden", // 읽기 권한이 없거나 정규파일 아니면
                        "Tiny couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size, method); // 정적(static) 컨텐츠를 클라이언트에게 제공
    }
    else{ // Serve dynamic content
        // 파일이 실행 가능한지 검증
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs, method); // 실행가능하다면 동적(dynamic) 컨텐츠를 클라이언트에게 제공
    }
}

void clienterror(int fd, char *cause, char *errnum,
                char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body
    // sprintf는 출력하는 결과 값을 변수에 저장하게 해주는 기능있음
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n",body);

    // Print the HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
// Tiny는 request header의 정보를 하나도 사용하지 않는다.
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE); //한 줄씩 읽어들인다('\n'을 만나면 break되는 식으로)
    //strcmp(str1,str2) 같은 경우 0을 반환 -> 이 경우만 탈출
    // buf가 '\r\n'이 되는 경우는 모든 줄을 읽고 나서 마지막 줄에 도착한 경우이다.
    // 헤더의 마지막 줄은 비어있다
    while(strcmp(buf, "\r\n")){
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf); //한줄 한줄 읽은 것을 출력한다.(최대 MAXLINE읽기가능)
    }
    return;
}
// Tiny는 정적 컨텐츠를 위한 홈 디렉토리가 자신의 현재 디렉토리이고,
// 실행파일의 홈 디렉토리는 /cgi-bin이라고 가정한다.
int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;
    // strstr: (대상문자열, 검색할문자열) -> 검색된 문자열(대상 문자열) 뒤에 모든 문자열이 나오게 됨
    // uri에서 "cgi-bin"이라는 문자열이 없으면, static content
    if(!strstr(uri, "cgi-bin")){ // Static content
        strcpy(cgiargs, ""); //cgiargs 인자 string을 지운다.
        strcpy(filename, "."); // 상대 리눅스 경로이름으로 변환 ex) '.'
        strcat(filename, uri); // 상대 리눅스 경로이름으로 변환 ex) '.' + '/index.html'
        if (uri[strlen(uri)-1] == '/') // URI가 '/'문자로 끝난다면
            strcat(filename, "home.html"); // 기본 파일 이름인 home.html을 추가한다. -> 11.10과제 adder.html로 변경
        return 1;
    }
    else{ // Dynamic content (cgi-bin이라는 문자열 존재)
        // 모든 CGI 인자들을 추출한다.
        ptr = index(uri, '?');
        if(ptr){
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, "");
        // 나머지 URI 부분을 상대 리눅스 파일이름으로 변환
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}
// static content를 요청하면 서버가 disk에서 파일을 찾아서 메모리 영역으로 복사하고, 복사한 것을 client fd로 복사
void serve_static(int fd, char *filename, int filesize, char *method)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    // Send response headers to client
    get_filetype(filename, filetype); // 5개 중에 무슨 파일형식인지 검사해서 filetype을 채워넣음
    //client에 응답 줄과 헤더를 보낸다.
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);
    sprintf(buf, "%sConnextion: close\r\n",buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); //여기 \r\n 빈줄 하나가 헤더종료표시
    Rio_writen(fd, buf, strlen(buf));// buf에서 strlen(buf) 바이트만큼 fd로 전송한다.
    printf("Response headers: \n");
    printf("%s", buf);

    // @과제11번 수정
    if(strcasecmp(method,"HEAD") == 0)
        return;
    // Send response body to client
    // open(열려고 하는 대상 파일의 이름, 파일을 열 때 적용되는 열기 옵션, 파일 열 때의 접근 권한 설명)
    // return 파일 디스크립터
    // O_RDONLY : 읽기 전용으로 파일 열기
    // 즉, filename의 파일을 읽기 전용으로 열어서 식별자를 받아온다.
    srcfd = Open(filename, O_RDONLY, 0);
    // 요청한 파일을 disk에서 가상메모리 영역으로 mapping한다.
    // mmap을 호출하면 파일 srcfd의 첫 번째 filesize 바이트를 
    // 주소 srcp에서 시작하는 사적 읽기-허용 가상메모리 영역으로 mapping
    
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    // Mmap대신 malloc 사용
    srcp = (char*)malloc(filesize);
    Rio_readn(srcfd,srcp,filesize);

    // 파일을 메모리로 매핑한 후에 더 이상 이 식별자는 필요 없으므로 닫기(치명적인 메모리 누수 방지)
    Close(srcfd);
    // 실제로 파일을 client로 전송
    // rio_writen함수는 주소 srcp에서 시작하는 filesize를 클라이언트의 연결 식별자 fd로 복사
    Rio_writen(fd, srcp, filesize);
    // 매핑된 가상메모리 주소를 반환(치명적인 메모리 누수 방지)
    // Munmap(srcp, filesize);
    free(srcp);
}

// get_filetype - Derive file type from filename, Tiny는 5개의 파일형식만 지원한다.
void get_filetype(char *filename, char *filetype)
{
    if(strstr(filename, ".html")) // filename 문자열 안에 ".html"이 있는지 검사
        strcpy(filetype, "text/html");
    else if(strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if(strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if(strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if(strstr(filename, ".mp4"))
        strcpy(filetype, "video/mp4");
    else    
        strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    // Return first part of HTTP response
    // 클라이언트에 성공을 알려주는 응답 라인을 보내는 것으로 시작
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if(Fork() == 0){ // 새로운 Child process를 fork한다.
        // Real server would set all CGI vars here(실제 서버는 여기서 다른 CGI 환경변수도 설정)
        // QUERY_STRING 환경변수를 요청 URI의 CGI 인자들로 초기화
        setenv("QUERY_STRING", cgiargs, 1);
        // Dup2함수를 통해 표준 출력을 클라이언트와 연계된 연결 식별자로 재지정 
        //-> CGI 프로그램이 표준 출력으로 쓰는 모든것은 클라이언트로 바로 감(부모프로세스의 간섭 없이)
        // @과제11번 수정
        // REQUEST_METHOD 환경변수를 요청 URI의 CGI 인자들로 초기화
        setenv("REQUEST_METHOD", method, 1);
        Dup2(fd, STDOUT_FILENO); // Redirect stdout to client
        Execve(filename, emptylist, environ); // Run CGI program
    } 
    // 부모는 자식이 종료되어 정리되는 것을 기다리기 위해 wait 함수에서 블록된다.
    Wait(NULL); // Parent waits for and reaps child

}