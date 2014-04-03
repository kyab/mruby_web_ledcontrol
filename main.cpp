

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <errno.h>

#include <new>  //for new(std::nothrow)

#include "wirish.h"

#include "mruby.h"
#include "mruby/class.h"
#include "mruby/value.h"
#include "mruby/irep.h"
#include "mruby/string.h"

#include "GSWifi.h"

#define LED_GREEN  Port2Pin('D',12)
#define LED_ORANGE Port2Pin('D',13)
#define LED_RED    Port2Pin('D',14)    
#define LED_BLUE   Port2Pin('D',15)


extern HardwareSerial &SerialWifi;      //Serial2
extern HardwareSerial &SerialHost;      //Serial3

/*
http://aeroquad.com/showthread.php?8371-libmaple-and-stm32f4discovery-HardwareSerial-issue
My understanding for pin availability for Serial is as belows.

// from discovery_f4.h and discoveryf4 manual
#define BOARD_USART1_TX_PIN Port2Pin('A', 9) //USB VBUS
#define BOARD_USART1_RX_PIN Port2Pin('A',10) //USB ID
#define BOARD_USART2_TX_PIN Port2Pin('A', 2) //Free
#define BOARD_USART2_RX_PIN Port2Pin('A', 3) //Free
#define BOARD_USART3_TX_PIN Port2Pin('D', 8) //Free
#define BOARD_USART3_RX_PIN Port2Pin('D', 9) //Free
#define BOARD_UART4_TX_PIN Port2Pin('C',10) //SCLK
#define BOARD_UART4_RX_PIN Port2Pin('C',11) //Free
#define BOARD_UART5_TX_PIN Port2Pin('C',12) //SDIN
#define BOARD_UART5_RX_PIN Port2Pin('D', 2) //Free
*/


// char *const CCM_RAM_BASE = (char *)0x10000000;
// char *g_ccm_heap_next;

mrb_state *mrb;
int ai;
size_t total_size = 0;

extern const uint8_t script[];
mrb_value blinker_obj;
mrb_value server_obj;

static void
p(mrb_state *mrb, mrb_value obj)
{
  obj = mrb_funcall(mrb, obj, "inspect", 0);
  fwrite(RSTRING_PTR(obj), RSTRING_LEN(obj), 1, stdout);
  putc('\n', stdout);
  SerialHost.flush();
}


extern "C"{

    void _exit(int rc){
        SerialHost.println("[newlib stub] _exit called.");
        while(1){
            //delay(100);
        }
    }
    int _getpid(){
        return 1;
    }

    
    int _kill(int pid, int sig){
        errno = EINVAL;
        return -1;
    }

    void abort(void){
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_BLUE, HIGH);
        SerialHost.println("[newlib stub] aborted!");
        for(;;){
            //delay(200);
        }
    }

    //http://todotani.cocolog-nifty.com/blog/2010/05/mbed-gccprintf-.html
    int _write_r(struct _reent *r, int file, const void *ptr, size_t len){
        size_t i;
        unsigned char *p = (unsigned char *)ptr;
        for (i = 0 ; i < len ; i++){
            SerialHost.write(*p++);
        }
        SerialHost.flush();
        return len;
    }

}

void initBlinker(){
    //Get Blinker class and create instance.
    //equivalent to ruby: blinker_obj = Blinker.new(13,1000)
    RClass *blinker_class = mrb_class_get(mrb, "Blinker");
    if (mrb->exc){
        SerialHost.println("failed to load class Blinker");
        return;
    }

    mrb_value args[2];
    args[0] = mrb_fixnum_value(LED_BLUE);     //pin Number
    args[1] = mrb_fixnum_value(300);   //interval
    blinker_obj = mrb_class_new_instance(mrb, 2, args, blinker_class);

    //is exception occure?
    if (mrb->exc){
        SerialHost.println("failed to create Blinker instance");
        return;
    }

    printf("blinker_obj initialized\n");
}

void initServer(){
    RClass *server_class = mrb_class_get(mrb, "WebServer");
    if (mrb->exc){
        SerialHost.println("failed to load class WebServer");
        return;
    }

    server_obj = mrb_class_new_instance(mrb, 0, NULL, server_class);
    if (mrb->exc){
        SerialHost.println("failed to create WebServer instance");
        return ;
    }

    printf("server_obj initialized\n");
}

/* 

custom allocator

*/

#ifdef VERBOSE_MALLOC
#define malloc_putc(x) SerialHost.print(x)
#else
#define malloc_putc(x)
#endif

int failcount = 0;
unsigned int high;
void *myallocfCCM(mrb_state *mrb, void *p, size_t size, void *ud)
{

    static char *CCM_RAM_BASE = (char *)0x10000000;
    static char *g_ccm_heap_next = CCM_RAM_BASE;

    malloc_putc('.');
    if (size == 0){
        if (CCM_RAM_BASE <= p && (unsigned int)p <= (unsigned int)CCM_RAM_BASE + 50*1024){
            malloc_putc('-');
        }else{
            malloc_putc('F');
            free(p);
        }

        return NULL;
    }

    void *ret = NULL;
    if ((unsigned int)g_ccm_heap_next < (unsigned int)CCM_RAM_BASE + 50*1024){ //first 46kb is special ccm heap
        malloc_putc('C');
        ret = g_ccm_heap_next;
        g_ccm_heap_next += size;

        //4 byte alignment
        if ((unsigned int)g_ccm_heap_next & 0x00000003){
            g_ccm_heap_next += 4-((unsigned int)g_ccm_heap_next & 0x00000003);
        }

    }else{
        malloc_putc('N');
        ret = realloc(p, size);
        if (!ret){
            SerialHost.print('!');
            SerialHost.print("\n!!!memory allocation error for size:");
            SerialHost.print(size, DEC);
            SerialHost.print("\n\tcurrent total :");
            SerialHost.print(total_size, DEC);
            SerialHost.print("\n\tfail count : ");
            SerialHost.println(++failcount, DEC);    
            delay(1000);
            return NULL;
        }

        unsigned int point = (unsigned int)ret + size;
        if (point > high){
            high = point;
        }

    }
    total_size += size;
    return ret;
}


TCPServer server;
List<TCPSocket *> sockets;
void setup() {
    SerialHost.begin(38400);
    SerialHost.println("");
    SerialHost.println("--------------------------------------------");
    SerialHost.println("      mruby web led controller    ");
    SerialHost.println("--------------------------------------------");

    pinMode(LED_BLUE, OUTPUT);
    pinMode(LED_ORANGE, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);

    //init Wifi
    // SerialWifi.begin(38400);
    // delay(500);
    // GSWifiStack::instance()->kyInitializeStack();
    // server.listen(80);
    // SerialHost.println("WiFi initialized");


    //init mruby
    printf("mrb_open...\n");
    mrb = mrb_open_allocf(myallocfCCM, NULL);
    if (mrb == NULL){
        SerialHost.println("failed to mrb_open()");
        delay(100);
    }else{
        SerialHost.print("mrb_open done ");
        SerialHost.print(" total allocated size = ");
        SerialHost.print(total_size, DEC);
        SerialHost.println("[bytes]");
    }

    SerialHost.println("loading irep...");
    ai = mrb_gc_arena_save(mrb);
    mrb_load_irep(mrb, script);
    if (mrb->exc){
        p(mrb, mrb_obj_value(mrb->exc));
        mrb->exc = NULL;
        return;
    }
    mrb_gc_arena_restore(mrb, ai);
   
    SerialHost.println("initializing blinker...");
    initBlinker();
    if (mrb->exc){
        p(mrb, mrb_obj_value(mrb->exc));
        mrb->exc = NULL;
        return;
    }
    SerialHost.println("initializing Server...");
    initServer();
    if (mrb->exc){
        p(mrb, mrb_obj_value(mrb->exc));
        mrb->exc = NULL;
        return;
    }
    ai = mrb_gc_arena_save(mrb);

    SerialHost.println("now get into loop");

    return;
}

//return true if responsed.
bool handleReceive(TCPSocket *socket){

    size_t size = socket->available();
    byte *buffer = new byte[size];
    socket->receive(buffer, size);

    bool ret = false;
    
    mrb_funcall(mrb, server_obj, "add_request", 2, mrb_fixnum_value(socket->cid()), mrb_str_new(mrb, (const char *)buffer, size));
    if (mrb->exc){
        printf("failed to call add_request\n");
        goto error;
    }else{
        mrb_value has_response = mrb_funcall(mrb, server_obj, "response?", 1, mrb_fixnum_value(socket->cid()));
        if (mrb->exc){
            printf("failed to call response?\n");
            goto error;
        } 
        if (mrb_bool(has_response)){
            mrb_value response = mrb_funcall(mrb, server_obj, "response", 1, mrb_fixnum_value(socket->cid()));
            if (mrb->exc){
                printf("failed to call response\n");
                goto error;
            }
            printf("now send response:%s\n", RSTRING_PTR(response));
            socket->send( (const byte *)RSTRING_PTR(response), RSTRING_LEN(response) );
            printf("now close socket\n");
            socket->close();    //not good? because send is asynchronous...

            mrb_funcall(mrb,server_obj, "remove_request", 1, mrb_fixnum_value(socket->cid()));
            ret = true;
        }else{
            printf("not response yet\n");
        }
    }

    delete [] buffer;
    return ret;
error:
    p(mrb, mrb_obj_value(mrb->exc));
    mrb->exc = NULL;
    delete [] buffer;
    return false;

}

int count = 0;
void loop(){
    //delay(500);
    int cid;
    if ((cid = server.accept()) >= 0){
        SerialHost.println("new connection");
        sockets.add(new TCPSocket(cid));
    }

    List<TCPSocket *>::iterator *pos = sockets.begin();
    while (pos){
        TCPSocket *socket = pos->item;
        bool incremented = false;
        int size = socket->available();

        if (size < 0){  //socket closed?
            pos = pos->next;
            incremented = true;
            sockets.remove(socket);
            delete socket;
        }else if (size == 0){

        }else{
            // digitalWrite(LED_ORANGE, LOW);
            if (handleReceive(socket)){
                pos = pos->next;
                incremented = true;
                sockets.remove(socket);
                delete socket;
            }
            // digitalWrite(LED_ORANGE, HIGH);
        }
        mrb_gc_arena_restore(mrb, ai);
        if (!incremented) pos = pos->next;
    }

    // mrb_funcall(mrb, blinker_obj, "run", 0);
    // if(mrb->exc){
    //     SerialHost.println("failed to run blinker_obj!");
    //     mrb->exc = 0;
    //     delay(3000);
    // }
    // mrb_gc_arena_restore(mrb, ai);

}

// Force init to be called *first*, i.e. before static object allocation.
// Otherwise, statically allocated objects that need libmaple may fail.
__attribute__((constructor)) void premain() {
    init();
}

int main(void) {
    setup();

    while (true) {
        loop();
        GSWifiStack::instance()->processEvents();
    }
    return 0;
}
