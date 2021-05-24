#include "Arduino.h"
#include "SuperSerial.h"
#include <FastCRC.h>

//Constructor, receives reference to hardware serial object
SuperSerial :: SuperSerial(HardwareSerial &ser, unsigned long interval, byte count) {
	_ser = &ser;
	_ser->begin(9600);
	_resendInterval = interval;
	_resendCount = count;
	_rindex = 0;
	_msgID = 0;
	_available = 0;
	//Initialize resend buffer
	for (byte i = 0; i < 10; i++) {
		_rbuf[i].ack = true;
		_rbuf[i].count = 0;
	}		
}

//Method to compose and send packet
//Adds message ID and checksum
//Adds message to resend buffer
void SuperSerial :: send(Telegram tel) {
	//Initialize the character array
	//Header, checksum and footer are 10 bytes combined
	char myBuf[tel.size + 10];
	
	//Start of text character is '!'
	myBuf[0] = '!';
	
	//convert telegram and message ID
	//Telegram ID starts at byte 1
	char c_telID[4];
	sprintf(c_telID, "%02X", tel.ID);
	strncpy(myBuf + 1, c_telID, 2);	
	
	//Message ID starts at byte 3
	char c_msgID[4];
	sprintf(c_msgID, "%02X", _msgID);
	strncpy(myBuf + 3, c_msgID, 2);			
	
	//Message payload starts at byte 5
	for (byte i = 0; i < tel.size; i++) {
		myBuf[i + 5] = tel.data[i];
	}
	
	//Generate checksum
	uint8_t crcBuf[4 + tel.size];
	for (int i = 0; i < 4 + tel.size; i ++){
		crcBuf[i] = myBuf[i + 1];
	}
	unsigned int checksum_crc = CRC16.ccitt(crcBuf, 4 + tel.size);
	char c_checksum[8];
	sprintf(c_checksum, "%04X", checksum_crc);
	for (int i = 0; i < 4; i++){
		myBuf[i + tel.size + 5] = c_checksum[i];
	}	
	
	//End of text character is NL, ascii code 10
	myBuf[tel.size + 9] = 10;
	
	//Send the message
	_ser->write(myBuf,tel.size + 10);

	
	//Place message in resend buffer
	//Reset retry count and store message ID
	_rbuf[_rindex].ack = false;
	_rbuf[_rindex].count = 0;
	_rbuf[_rindex].ID = _msgID;
	
	//Increase message ID
	_msgID++;
	
	//Store timestamp
	_rbuf[_rindex].time = millis();
	
	//Copy message bytes
	for (byte i = 0; i < tel.size + 10; i++) {
		_rbuf[_rindex].msg[i] = myBuf[i];
	}
	
	//Store size
	_rbuf[_rindex].size = tel.size + 10;

	//Increase buffer index and wrap around if necessary
	_rindex++;
	if (_rindex >= 10) _rindex = 0;	
}

//Checks if any new message has been received
//Check if message needs to be resent
void SuperSerial :: update(){
	
	//Check if incoming data is valid and extract payload
	while (_ser->available()) {
		
		//Reserve a string for the incoming data
		String msg = "";
		msg.reserve(25);
		msg = _ser->readStringUntil('\n');
		
		//Check if start character is present
		if (msg.startsWith("!")) {
		
			//Extract checksum, last 4 bytes of message
			unsigned int rcvChecksum = _asciiToUint(msg.substring(msg.length() - 4, msg.length()));
			
			//Calculate checksum from received data
			//Length of data is message size minus:
			//- Start character (!), 1 byte
			//- Checksum, 4 bytes
			//Finally add one for the char array overhead
			char crcBuf[msg.length() - 4];
			
			//Copy characters to buffer, skip start character
			msg.substring(1, msg.length() - 4).toCharArray(crcBuf, sizeof(crcBuf));
			
			//Calculate checksum of data
			//-1 is needed to compensate for char array overhead
			unsigned int calcChecksum = CRC16.ccitt((uint8_t*)crcBuf, sizeof(crcBuf) - 1);
			
			//Compare checksums
			if (rcvChecksum == calcChecksum) {
				//Extract telegram ID
				//2nd and 3rd character
				unsigned int telID = _asciiToUint(msg.substring(1, 3));
				
				//Extract message ID
				unsigned int msgID = _asciiToUint(msg.substring(3, 5));
				
				//If telegram ID is ack (code 0), find message in ack buffer
				if (telID == 0) {
					
					//Loop through resend buffer
					for (int i = 0; i < 10; i++) {
								
						//Check if message IDs match
						if (!_rbuf[i].ack && msgID == _rbuf[i].ID) {
							
							//Set acknowledged bit
							_rbuf[i].ack = true;
							
							//Jump out of loop
							break;
						}
						
					}
				//If telegram ID is not ack, add message to the available buffer and acknowledge it
				} else {
					
					//Transfer telegram ID
					_abuf[_available].ID = telID;
					
					//Get message payload size, subtract 9 bytes:
					//Start character (1)
					//Telegram and message ID (2 + 2)
					//Checksum (4)
					_abuf[_available].size = msg.length() - 9;					
					
					//Get message payload
					msg.substring(5, msg.length() - 4).toCharArray(_abuf[_available].data, msg.length() - 8);
					
					//Increase available buffer counter
					_available++;
					
					//Send acknowledgment, 10 bytes
					//Prefil start of text (!), telegram ID (0) and newline
					char ackBuf[10] = {'!','0','0','0','0','0','0','0','0','\n'};
					
					//Insert the message ID
					char c_msgID[4];
					sprintf(c_msgID, "%02X", msgID);
					ackBuf[3] = c_msgID[0];
					ackBuf[4] = c_msgID[1];	

					//Generate checksum
					uint8_t crcBuf[5];
					for (int i = 0; i < 4; i ++){
						crcBuf[i] = ackBuf[i + 1];
					}
					unsigned int checksum_crc = CRC16.ccitt(crcBuf, 4);
					char c_checksum[8];
					sprintf(c_checksum, "%04X", checksum_crc);
					for (int i = 0; i < 4; i++){
						ackBuf[i + 5] = c_checksum[i];
					}

					//Send the message
					_ser->write(ackBuf,10);					
				}
			}
		}
	}
	
	//Loop through resend buffer
	for (byte i = 0; i < 10; i++) {
		
		//Check if ack has not yet been received 
		//and resend interval has been exceeded
		if (!_rbuf[i].ack && (millis() - _rbuf[i].time >= _resendInterval)){
			
			//send message again
			_ser->write(_rbuf[i].msg,_rbuf[i].size);
			
			//Reset timestamp
			_rbuf[i].time = millis();
			
			//Increase resend counter
			_rbuf[i].count++;
			
			//Set acknowledge bit if resend count has been exceeded
			_rbuf[i].ack = (_rbuf[i].count > _resendCount - 1);
		}
	}
}

//Returns the number of available data packets
unsigned int SuperSerial :: available(){
	return _available;
}

//Return oldest telegram
Telegram SuperSerial :: read(){
	if (_available > 0) {
		
		//Get local copy of oldest telegram in buffer
		Telegram myTel = _abuf[0];
		
		//Loop through buffer and shift all telegrams down 1 position
		for (int i = 0; i < _available; i++) {
				_abuf[i] = _abuf[i + 1];
		}
		
		//Decrease available counter
		_available--;
		
		//Return the oldest telegram
		return myTel;
	}
}

//Converts ascii string to unsigned integer
unsigned int SuperSerial :: _asciiToUint(String str){
    unsigned int value;
    value = strtol(str .c_str(), NULL, 16);
    return value;
}