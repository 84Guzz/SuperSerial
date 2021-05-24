#ifndef SuperSerial_h
#define SuperSerial_h

#include "Arduino.h"
#include <FastCRC.h>

struct Telegram {
	unsigned int ID; //Telegram ID
	byte size; //Payload size
	char data[15]; //Payload
};

struct resendMsg {
	bool ack; //Message is acknowledged
	unsigned long time; //[ms] Time at which the message was first sent
	byte count; //Amount of times the message has been resent
	unsigned int ID; //Message ID
	char msg[25]; //The message frame
	unsigned int size; //Number of bytes in frame
};

class SuperSerial {
 public:
	SuperSerial(HardwareSerial &ser, unsigned long interval, byte count); //Constructor
	void send(Telegram tel); //Sends a telegram
	void update(); //Checks for incoming serial data
	unsigned int available(); //Returns number of available telegrams
	Telegram read(); //Returns the oldest available telegram
 private:
	unsigned int _asciiToUint(String str); //Function to convert ascii string to Uint
	HardwareSerial* _ser; //Reference to serial
	FastCRC16 CRC16; //FastCRC library
	unsigned long _resendInterval; //Interval with which messages will be resent 
	byte _resendCount; //Max amount of resend without ack
	resendMsg _rbuf[10]; //Resend buffer
	unsigned int _rindex; //Resend index
	byte _msgID; //Message ID
	unsigned int _available; //Number of available telegrams
	Telegram _abuf[10]; //Available telegram buffer
};

#endif
