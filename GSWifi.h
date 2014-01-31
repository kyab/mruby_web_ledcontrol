
#ifndef __GSWIFI_H__
#define __GSWIFI_H__

#include <stdio.h>
struct RingBuffer{
	byte *m_buffer;

	size_t m_size;
	byte *m_write_p;
	byte *m_read_p;

	RingBuffer(size_t size){
		m_size = size;
		printf("before allocate mem\n");
		m_buffer = new byte[m_size];
		printf("after allocate mem %p\n",m_buffer);
		m_write_p = m_buffer;
		m_read_p = m_buffer;
	}

	~RingBuffer(){
		delete [] m_buffer;
	}

	size_t available(){
		if (m_write_p >= m_read_p){
			return m_write_p - m_read_p;
		}else{
			return (m_buffer + m_size - m_read_p) + (m_write_p - m_buffer);
		}
	}

	void write(const byte *buffer, size_t size){

		if ((size_t)(m_buffer + m_size - m_write_p)>= size ){
			memcpy(m_write_p, buffer, size);
			m_write_p += size;
		}else{
			size_t tempLen = m_buffer + m_size - m_write_p;
			memcpy(m_write_p, buffer, tempLen);
			size -= tempLen;
			memcpy(m_buffer, buffer + tempLen, size);
			m_write_p = m_buffer + size; 
		}
	}

	void read(byte *buffer, size_t size){
		if ((size_t)(m_buffer + m_size - m_read_p) >= size){
			memcpy(buffer, m_read_p, size);
			m_read_p += size;
		}else{
			size_t tempLen = m_buffer + m_size - m_read_p;
			memcpy(buffer, m_read_p, tempLen);
			size -= tempLen;
			memcpy(buffer+tempLen, m_buffer, size);
			m_read_p = m_buffer + size;
		}
	}
};

template <typename T>
struct List{
	struct iterator{
		T item;
		iterator *next;
	};
private:
	iterator *m_list;

public:
	List(){
		m_list = NULL;
	}
	void add(T entry){
		if (!m_list){
			m_list = new iterator();
			m_list->item = entry;
			m_list->next = NULL;
			return;
		}

		iterator *l = m_list;
		while(l->next) l = l->next;
		l->next = new iterator();
		l->next->item = entry;
		l->next->next = NULL;
	}
	bool remove(T entry){
		if (!m_list) return false;
		iterator *lprev = NULL;
		iterator *l = m_list;
		while(l){
			if (l->item == entry){
				if (lprev == NULL){
					m_list = l->next;
					delete l;
					return true;
				}else{

					lprev->next = l->next;
					delete l;
					return true;
				}
			}
			lprev = l;
			l = l->next;
		}
		return false;
	}
	iterator *begin(){
		return m_list;
	}
};



struct Incommings{
	int cid;	//host cid
	List<int> clients;
};

struct RecvBuffer{
	int cid;	//client cid
	RingBuffer *buffer;	//rx ring buffer
};

class GSWifiStack{
public:
	static GSWifiStack *instance();
	
	bool kyInitializeStack();
	void processEvents();	//should be called from main loop

	//for TCP Server
	int openPort(unsigned short port);
	int popClient(int host_cid);

	//for TCP Socket
	int available(int cid);
	void read(int cid, byte *buffer, size_t size);
	void write(int cid, const byte *buffer, size_t size);
	void close(int cid);

private:
	static GSWifiStack *m_sInstance; 
	GSWifiStack(){
		m_buffer[0] = '\0';
		m_bufferIndex = 0;
		m_receiving_cid = -1;
		m_receiving_size_togo = 0;
		m_state = STATE_NONE;

	}
	
	byte m_buffer[1400];
	size_t m_bufferIndex;
	int m_receiving_cid;
	size_t m_receiving_size_togo;

	List<Incommings *> m_acceptList;
	List<RecvBuffer *> m_bufferList;

	enum GSWIFI_STATE{
		STATE_NONE = 0,
		STATE_CONNECT = 1,
		STATE_DISCONNECT = 2,
		STATE_RECEIVING = 3
	};
	GSWIFI_STATE m_state;

	void onNewConnection(int host_cid, int cid, const char *address, unsigned short port);
	void onNewReceive(int cid, const byte *buffer, size_t size);
	void onDisconnect(int cid);
};

class TCPServer{
public:
	bool listen(unsigned short port);
	int accept();	//-1 for none.

private:
	int m_cid;
};

class TCPSocket{
public:
	TCPSocket(int cid);
	//peek(byte **buffer, size_t *length);

	int available();
	void receive(byte *buffer, size_t size);

	void send(const byte *buffer, size_t size);

	void close();

	int cid() const;

	//virtual onReceive(byte *buffer, size_t length) = 0;
private:
	int m_cid;
};



#endif /*__GSWIFI_H__*/