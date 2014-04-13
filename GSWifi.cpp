#include "wirish.h"

#include <stdio.h>
#include <string.h>

#include "GSWifi.h"

HardwareSerial &SerialWifi = Serial2;
HardwareSerial &SerialHost = Serial3;

GSWifiStack *GSWifiStack::m_sInstance = NULL;

static void sendReceivePrint(const char *command, const char *expectEnd = NULL){
    
    if(command){
        char str[50];
        sprintf(str,"%s\r",command);
        printf("sent:");
        printf(command);
        printf("\n");
        SerialWifi.print(str);
    }

    char buf[100];
    buf[0] = '\0';
    int i = 0;
    if (expectEnd == NULL) expectEnd = "OK\r\n";

    while(true){
        while(SerialWifi.available()){
            buf[i] = SerialWifi.read();
            buf[i+1] = '\0';
            SerialHost.write(buf[i]);
            i++;
        }

        if (strlen(buf) >= strlen(expectEnd)){
            char *p = buf + (strlen(buf) - strlen(expectEnd));
            if (strcmp(p, expectEnd) == 0){
                break;
            }
        }
    }

}

GSWifiStack* GSWifiStack::instance(){
    if (!m_sInstance){
        m_sInstance = new GSWifiStack();
    }
    return m_sInstance;
}

bool GSWifiStack::kyInitializeStack(){

    sendReceivePrint("AT");         //echo
    sendReceivePrint("AT+RESET","\n\rAPP Reset-APP SW Reset\r\n");   //reset the chip
    sendReceivePrint("ATE0");       //shut echo down
    sendReceivePrint("ATV1");       //set verbose on
    // SerialWifi.print("ATB=38400\r");  //set baud rate
    // SerialWifi.end();
    // SerialWifi.begin(38400);
    // sendReceivePrint(NULL);

    sendReceivePrint("AT&K0");      //disable software flow control
    sendReceivePrint("AT&R0");      //disable hardware flow control
    sendReceivePrint("AT+WAUTH=0");  //for WPA/WPA2, authentication mode should 0:None
    sendReceivePrint("AT+WSEC=0");   //for My HOME :0:Auto security

    // SerialWifi.print("ATB=38400\r");
    // SerialWifi.flush();
    // delay(500);
    // SerialWifi.end();
    // SerialWifi.begin(38400);        //ATB commands response will be sent in new baudrate
    // //sendReceivePrint(NULL);     

    //sendReceivePrint("AT&W0");

    bool doStore = false;
    if (doStore){
        //key calc
        printf("now calculate key. it will take seconds\n");
        sendReceivePrint("AT+WPAPSK=0024A5C24290_G,3n6xn7hyxurup");

        sendReceivePrint("AT&W1");  //save to profile 1
        sendReceivePrint("AT&Y1");  //set default profile to 1
        sendReceivePrint("ATZ1");   //load profile 1
    }else{
        //load key
        printf("Loading profile..\n");
        sendReceivePrint("ATZ1");   //load profile 1
    }

    bool useDHCP = false;
    if (useDHCP){
        sendReceivePrint("AT+NDHCP=1");     //enable DHCP Client

        //DNS will be also configured (even no output)
        //

    }else{
        sendReceivePrint("AT+NSET=192.168.107.200,255.255.255.0,192.168.107.1");
        sendReceivePrint("AT+DNSSET=192.168.107.1");
    }

    //OK, let's Associate/Start with Wireless
    sendReceivePrint("AT+WA=0024A5C24290_G");
    sendReceivePrint("AT+BDATA=1");	//enable bulk
    return true;
}

static const char ESC=0x1B;

void GSWifiStack::processEvents(){

	while(SerialWifi.available()){
		byte b = SerialWifi.read();
		//SerialHost.write(b);
		m_buffer[m_bufferIndex] = b;
		m_bufferIndex++;
		m_buffer[m_bufferIndex] = '\0';

        if (m_bufferIndex == 1){
            continue;
        }
        if (m_bufferIndex == 2){
            if (m_state == STATE_NONE){
                if (m_buffer[0] == ESC && m_buffer[1] == 'Z'){
                    m_state = STATE_RECEIVING;
                }else if (m_buffer[0] == ESC && m_buffer[1] == 'O'){
                    m_state = STATE_NONE;
                    m_bufferIndex = 0;
                    m_buffer[0] = '\0';
                    printf("transmission success\n");
                    /* code */
                }else if (m_buffer[0] == ESC && m_buffer[1] == 'F'){
                    m_state = STATE_NONE;
                    m_bufferIndex = 0;
                    m_buffer[0] = '\0';
                    printf("transmission failed\n");                    
                }
            }
            continue;
        }
        if (m_bufferIndex == 6){
            if (strncmp( (const char *)m_buffer, "\r\nOK\r\n", 6) == 0){
                printf("OK from Wifi. maybe close success\n");
                m_state = STATE_NONE;
                m_bufferIndex = 0;
                m_buffer[0] = '\0';
                continue;
            }
        }
        if (m_bufferIndex < 9){    ///*strlen(\r\nCONNECT))
            continue;
        }

		switch(m_state){
        case STATE_NONE:
            if( strncmp((const char *)m_buffer, "\r\nCONNECT", strlen("\r\nCONNECT")) == 0){
                m_state = STATE_CONNECT;
            }
            else if( strncmp((const char *)m_buffer,"\r\nDISCONNECT", strlen("\r\nDISCONNECT")) == 0){
                m_state = STATE_DISCONNECT;
            }

            break;
        default:
            break;
        }

        size_t receiving_size;

        switch(m_state){
        case STATE_NONE:
            break;
        case STATE_CONNECT:         //new connection from remote peer
            //end with \r\n?
            if (strstr((const char *)(m_buffer+2),"\r\n")){
                int cid_host = 0;
                int cid = 0;
                char address[16];
                unsigned short port;
                sscanf((const char *)m_buffer,"\r\nCONNECT %x %x %s %hu\r\n",&cid_host,
                                                             &cid, address, &port);
                printf("new connection cid=%d, from %s:%d\n", cid, address, port);
                printf((const char *)m_buffer);
                onNewConnection(cid_host, cid, address, port);
                
                m_state = STATE_NONE;
                m_bufferIndex = 0;
                m_buffer[0] = '\0';
            }
            break;

        case STATE_DISCONNECT:
            if (strstr((const char *)(m_buffer+2), "\r\n")){

                int cid = 0;
                sscanf((const char *)m_buffer,"\r\nDISCONNECT %x\r\n", &cid);
                printf("cid:%d disconnected\n",cid);

                onDisconnect(cid);

                m_state = STATE_NONE;
                m_bufferIndex = 0;
                m_buffer[0] = '\0';
            }
            break;
        case STATE_RECEIVING:
            if (m_receiving_cid == -1){
                if (m_bufferIndex > strlen("EZA1234")){
                    sscanf((const char *)m_buffer,"\x1BZ%1x%4d",&m_receiving_cid,&m_receiving_size_togo);
                    printf("new data from cif=%d, size=%d\n",m_receiving_cid, m_receiving_size_togo);
                }else{
                    break;
                }
            }
            receiving_size = m_bufferIndex - 7;//strlen("EZA1234");
            if (receiving_size >= m_receiving_size_togo){
                printf("all data received\n");

                onNewReceive(m_receiving_cid, m_buffer+strlen("EZA1234"), receiving_size);

                m_receiving_cid = -1;
                m_receiving_size_togo = 0;
                m_bufferIndex = 0;
                m_buffer[0] = '\0';
                m_state = STATE_NONE;
            }
            break;
        default:
            break;
        }
        if (m_bufferIndex >= 1400) {
            printf("!!!!!!!!!!!!Overflow!!!!!!!!!!!\n");
            m_bufferIndex = 0;
        }
    }
    
}


int GSWifiStack::openPort(unsigned short port){
    /*
    sent:AT+NSTCP=3000

    CONNECT 0

    OK

    */  

    int cid = -1;
    char command[15];
    sprintf(command, "AT+NSTCP=%u\r",port);
    SerialWifi.print(command);

    char buf[100];
    buf[0] = '\0';
    int i = 0;
    bool got_cid = false;
    while(true){
        while(SerialWifi.available()){
            buf[i] = SerialWifi.read();
            buf[i+1] = '\0';
            SerialHost.write(buf[i]);
            i++;
        }

        if (got_cid){
            if (strstr(buf, "OK\r\n")){
                break;
            }
        }

        if (strlen(buf) >= strlen("\r\nCONNECT A\r\n")){
            if (strstr(buf, "\r\nCONNECT ")){
                sscanf(buf, "\r\nCONNECT %x\r\n", &cid);
                got_cid = true;
            }
        }


    }

    if (cid >= 0){
        List<Incommings *>::iterator *pos = m_acceptList.begin();
        while(pos){
            if (pos->item->cid == cid){
                Incommings *old = pos->item;
                m_acceptList.remove(old);
                delete old;
                break;
            }
            pos = pos->next;
        }

        Incommings *incommings = new Incommings();
        incommings->cid = cid;
        m_acceptList.add(incommings);
        printf("new host cid:%d registered\n",cid);
    }

    return cid;
}



int GSWifiStack::popClient(int host_cid)
{
    int cid = -1;
    List<Incommings *>::iterator *pos = m_acceptList.begin();
    while(pos){
        if (pos->item->cid == host_cid){
            List<int> *clients = &(pos->item->clients);
            if (clients->begin() != NULL){
                cid = clients->begin()->item;
                clients->remove(cid);
            }
            break;
        }
    }
    //printf("GSWifiStack::popClient returns %d\n",cid);
    return cid;
}



void GSWifiStack::onNewConnection(int host_cid, int cid, const char *address, unsigned short port)
{
    //save to Incomming lists
    List<Incommings *>::iterator *pos = m_acceptList.begin();
    while(pos){
        if (pos->item->cid == host_cid){
            pos->item->clients.add(cid);
            printf("!!!new incomming added to host:%d, client:%d\n", host_cid, cid);
            break;
        }
        pos = pos->next;
    }

    //prepare new buffer for new cid
    List<RecvBuffer *>::iterator *pos2 = m_bufferList.begin();
    while(pos2){
        if (pos2->item->cid == cid){
            RecvBuffer *old = pos2->item;
            m_bufferList.remove(old);
            delete old->buffer;
            delete old;
            break;
        }
        pos2 = pos2->next;
    }
    
    RecvBuffer *newBuffer = new RecvBuffer();
    newBuffer->cid = cid;
    newBuffer->buffer = new RingBuffer(1400);

    m_bufferList.add(newBuffer);
}

void GSWifiStack::onDisconnect(int cid)
{
    List<RecvBuffer *>::iterator *pos = m_bufferList.begin();
    while(pos){
        if (pos->item->cid == cid){
            RecvBuffer *old = pos->item;
            m_bufferList.remove(old);
            delete old->buffer;
            delete old;
            return;
        }
        pos = pos->next;
    }            

    printf("onDisconnect for unknown cid=%d\n",cid);

}

void GSWifiStack::onNewReceive(int cid, const byte *buffer, size_t size)
{

    List<RecvBuffer *>::iterator *pos = m_bufferList.begin();
    while(pos){
        if (pos->item->cid == cid){
            pos->item->buffer->write(buffer, size);
            printf("onNewReceive written for buffer. cid = %d\n", cid);
            return;
        }
        pos = pos->next;
    }
    printf("can't store received buffer for unknown cid=%d\n",cid);
}

int GSWifiStack::available(int cid){
    List<RecvBuffer *>::iterator *pos = m_bufferList.begin();
    while(pos){
        if (pos->item->cid == cid){
            size_t ret = pos->item->buffer->available();
            if (ret > 0){
                printf("data available for cid=%d\n",cid);
            }
            return (int)ret;
        }
        pos = pos->next;
    }
    printf("can't get available for unknown cid=%d. closed?\n", cid);
    return -1;
}

void GSWifiStack::read(int cid, byte *buffer, size_t size){
    List<RecvBuffer *>::iterator *pos = m_bufferList.begin();
    while(pos){
        if (pos->item->cid == cid){
            pos->item->buffer->read(buffer, size);
            return;
        }
        pos = pos->next;
    }
    printf("can't read buffer from unknown cid=%d\n",cid);
}

//blocking write
void GSWifiStack::write(int cid, const byte *buffer, size_t size){
    char header[10];
    sprintf(header, "\x1BZ%01x%04d", cid, size);
    SerialWifi.print(header);
    for (size_t i = 0 ; i < size; i++){
        SerialWifi.write(buffer[i]);
    }
    SerialWifi.flush();

    printf("GSWifiStack::write(%d,%p,%u) done\n",cid, buffer, size);
    
    //no wait!
    //sendReceivePrint(NULL, "\x1BO");

    //TODO need to wait result??
}

void GSWifiStack::close(int cid){
    //AT+NCLOSE=%01x
    printf("GSWifiStack::close(cid=%d)\n",cid);
    char command[15];
    sprintf(command,"AT+NCLOSE=%01x\r",cid);
    SerialWifi.print(command);
    // unsigned long start = millis();
    // sendReceivePrint(command);
    // printf("take %lu ms to close\n", millis() - start);

    //remove immediately
    List<RecvBuffer *>::iterator *pos = m_bufferList.begin();
    while(pos){
        if (pos->item->cid == cid){
            RecvBuffer *old = pos->item;
            m_bufferList.remove(old);
            delete old->buffer;
            delete old;
            break;
        }
        pos = pos->next;
    }
}


bool TCPServer::listen(unsigned short port)
{
    m_cid = GSWifiStack::instance()->openPort(port);
    printf("TCPServer::listen(%u) got cid:%d\n",port, m_cid);
    if (m_cid >= 0) return true;
    return false;
}

//returns -1  if no clients yet.
int TCPServer::accept()
{
    int cid = GSWifiStack::instance()->popClient(m_cid);
    return cid;
}

TCPSocket::TCPSocket(int cid)
{
    m_cid = cid;
}

int TCPSocket::available()
{
    return GSWifiStack::instance()->available(m_cid);
}

void TCPSocket::receive(byte *buffer, size_t size)
{
    printf("TCPSocket::reveive enter\n");
    GSWifiStack::instance()->read(m_cid, buffer, size);
    printf("TCPSocket::receive leave\n");
}

void TCPSocket::send(const byte *buffer, size_t size)
{
    GSWifiStack::instance()->write(m_cid, buffer, size);
}

void TCPSocket::close()
{
    printf("TCPSocket::close() cid = %d\n",m_cid);
    GSWifiStack::instance()->close(m_cid);
}

int TCPSocket::cid() const
{
    return m_cid;
}


