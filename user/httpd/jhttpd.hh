#ifndef JOS_HTTPD_JHTTPD_HH
#define JOS_HTTPD_JHTTPD_HH

extern "C" {
#include <inc/error.h>
}
#include <inc/error.hh>
#include <filesum.hh>
#include <db_select.hh>
#include <db_join.hh>

class httpd_get
{
public:
    virtual void process_request(int s, const char *get) = 0;
    virtual ~httpd_get(void) {}
};


class httpd_filesum_get: public httpd_get
{
public:
    httpd_filesum_get(httpd_filesum *app);
    ~httpd_filesum_get(void) {}
   
   void process_request(int s, const char *get);   
private:
   httpd_filesum *app_;
};

class httpd_db_select_get : public httpd_get
{
public:
    httpd_db_select_get(httpd_db_select *app);
    ~httpd_db_select_get(void) {}
   
   void process_request(int s, const char *get);   
private:
	httpd_db_select * app_;
};

class httpd_db_join_get : public httpd_get
{
public:
	httpd_db_join_get(httpd_db_join *app);
	~httpd_db_join_get(void) {}
   
	void process_request(int s, const char *get);   
private:
	httpd_db_join * app_;
};

// Start the httpd server
void jhttpd(uint16_t port, httpd_get *get, void (*cb)(void));

#endif

