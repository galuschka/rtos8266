/*
 * Updator.h
 */

#ifndef MAIN_UPDATOR_H_
#define MAIN_UPDATOR_H_

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include <driver/gpio.h>    // gpio_num_t

class WebServer;

class Updator
{
public:
    Updator() {};                   // just used once for s_updator
    static Updator & Instance();    // return s_updator

    bool Go();      // trigger update
    bool Confirm(); // trigger continue: testing -> stable

    bool         SetUri( const char * uri );      // set in vfs and for next download
    const char * GetUri()   const { return mUri; };       // get uri to be used for download
    const char * GetMsg()   const { return mMsg; };       // status message
    uint8_t      Progress() const { return mProgress; };  // to be used for progress bar

    bool Init();
    void AddPage( WebServer & webserver );
    void GetReboot(   struct httpd_req * req );  // HTTP_GET reboot
    void GetUpdate(   struct httpd_req * req );  // HTTP_GET start FW/favicon update or initiate reboot
    void PostUpdate(  struct httpd_req * req );  // HTTP_POST show progress on FW update
    void PostFavicon( struct httpd_req * req );  // HTTP_POST perform favicon update

    void Run();  // internal thread routine, but must be public
private:
    void Update();
    void ReadUri();

    uint8_t           mProgress   {0};   // 0: idle / 1:..94,96..98: progress / 95: confirm / 99: failed / 100: success
    char              mUri[80]    {""};  // http://my.really.long.uri:8888/to/firmware/location/is/67/in/length
    const char      * mMsg        {0};   // status information
    TaskHandle_t      mTaskHandle {0};
    SemaphoreHandle_t mSemaphore  {0};
};

#endif /* MAIN_UPDATOR_H_ */


